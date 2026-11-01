/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "hedgr.runtime/service/session_service.hpp"
#include "hedgr.runtime/manager/connection_options.hpp"
#include "hedgr.runtime/worker/session_actor.hpp"
#include "ores.logging/boost_severity.hpp"
#include "support.hpp"
#include <algorithm>
#include <boost/log/sources/severity_feature.hpp>
#include <format>
#include <utility>

namespace hedgr::runtime::service {

namespace {

namespace domain = hedgr::model::domain;
namespace manager = hedgr::runtime::manager;
namespace worker = hedgr::runtime::worker;

using support::lg;
using support::running_key;

/** @brief Every live session row, holes skipped, for a whole-table walk. */
[[nodiscard]] std::vector<model::state::session_handle>
live_sessions(const model::state::session_table& sessions) {
    std::vector<model::state::session_handle> result;
    const auto& layout = sessions.layout();
    for (std::uint32_t index = 0; index < layout.alive.size(); ++index) {
        if (layout.alive[index] == 0)
            continue;
        result.push_back(
            model::state::session_handle{index, layout.generation[index]});
    }
    return result;
}

/** @brief The actor a process runs when no test has stood one in. */
[[nodiscard]] worker::tick_feed_factory real_feeds() {
    return [](worker::session_actor_options options)
        -> std::unique_ptr<worker::tick_feed> {
        auto actor =
            std::make_unique<worker::session_actor>(std::move(options));
        actor->start();
        return actor;
    };
}

}

session_service::session_service(model::command::event_queue& events,
    worker::tick_feed_factory feeds)
    : events_(events), feeds_(std::move(feeds)) {
    if (!feeds_)
        feeds_ = real_feeds();
}
session_service::session_service(model::command::event_queue& events,
    session_dependencies dependencies, worker::tick_feed_factory feeds)
    : events_(events), deps_(std::move(dependencies)),
      feeds_(std::move(feeds)) {
    if (!feeds_)
        feeds_ = real_feeds();
}

void session_service::set_factory(worker::tick_feed_factory feeds) {
    feeds_ = std::move(feeds);
    if (!feeds_)
        feeds_ = real_feeds();
}

const model::domain::instance* session_service::wire_format_holder(
    const model::domain::instance& row) const {
    for (const auto session : live_sessions(deps_->sessions)) {
        const auto status = deps_->sessions.status(session);
        if (status != model::domain::session_status::connecting
            && status != model::domain::session_status::authenticated)
            continue;
        const auto instance_id = deps_->sessions.instance_id(session);
        const auto held = deps_->instances.find(instance_id);
        if (!held)
            continue;
        const auto* other = deps_->instances.get(*held);
        if (other->wire_format != row.wire_format)
            return other;
    }
    return nullptr;
}

void session_service::start(const model::command::command& cmd,
    model::domain::credential credential, std::string sealed_credential,
    session_effect& effect) {
    const auto instance = deps_->instances.find(cmd.instance_id);
    if (!instance)
        return;
    const auto* instance_row = deps_->instances.get(*instance);

    /*
     * Reconnecting a pair resumes the same session rather than making a second
     * one, which is the whole reason the table is keyed by the pair.
     */
    auto handle = deps_->sessions.find(cmd.instance_id, cmd.account_id);
    if (!handle)
        handle = deps_->sessions.insert(cmd.instance_id, cmd.account_id);
    deps_->sessions.set_status(*handle,
        model::domain::session_status::connecting);

    /*
     * The actor is created here, at the connect command, and owns the session
     * from its first byte: the connection, the sign-in, the token and its
     * refresh. The credential travels in its options and exists for exactly one
     * attempt, because the actor wipes it the moment the sign-in resolves.
     */
    worker::session_actor_options options;
    options.options = manager::connection_options_for(*instance_row,
        deps_->seed, deps_->database_path ? deps_->database_path()
                                        : std::filesystem::path{});
    options.key = model::domain::session_key{cmd.instance_id, cmd.account_id};
    options.credential = std::move(credential);
    options.sealed_credential = std::move(sealed_credential);

    /*
     * A stored credential travels sealed, with the password that opens it, and
     * the actor opens it on its own thread: the derivation is half a second of
     * deliberate work that must not happen on the frame loop. The password is
     * copied out of the held secret here, so the actor's copy dies with the
     * attempt and the held copy survives for the next one.
     */
    if (!options.sealed_credential.empty() && deps_->master_password)
        options.master_password = deps_->master_password();

    const auto attempt = ++next_attempt_;
    options.attempt = attempt;
    effect.attempted = session_effect::attempt{cmd.instance_id,
        cmd.account_id, attempt};

    /*
     * A second connect for a pair asks for a fresh attempt; opening stops the
     * actor already held for the pair, so the events of the one being replaced
     * cannot arrive for the connection this call is about to open.
     */
    static_cast<void>(open(std::move(options)));
}

bool session_service::wire_format_allows(
    const model::domain::instance& row) const {
    if (!deps_)
        return true;
    return wire_format_holder(row) == nullptr;
}

session_effect session_service::connect(const model::command::command& cmd) {
    session_effect effect;
    if (!deps_) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::session, cmd.account_id},
            "cannot connect: this session service holds no catalogue"});
        return effect;
    }

    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Sign-in requested for '" << cmd.account_id << "' on '"
        << cmd.instance_id << "'";

    /*
     * Both ids arrive as text from the interface, so both are resolved here:
     * the instance's row is read for the connection settings, and the account
     * has to exist, because its identity is also what a stored credential is
     * looked up by.
     */
    const auto instance = deps_->instances.find(cmd.instance_id);
    if (!instance) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::session, cmd.account_id},
            std::format("cannot connect: no instance {}", cmd.instance_id)});
        return effect;
    }
    const auto* instance_row = deps_->instances.get(*instance);

    if (!deps_->accounts.find(cmd.account_id)) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::session, cmd.account_id},
            std::format("cannot connect: no account {}", cmd.account_id)});
        return effect;
    }

    /*
     * Before the password is remembered or a derivation is started: a refusal
     * starts nothing, and an operator who is told to disconnect another session
     * first should not also find a credential saved for an attempt that never
     * ran.
     */
    if (const auto* holder = wire_format_holder(*instance_row)) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::session, cmd.account_id},
            std::format(
                "cannot connect to {}: it uses the {} wire format, but {} is "
                "signed in using {}, and this process speaks one wire format at "
                "a time; disconnect {} first",
                instance_row->name, instance_row->wire_format, holder->name,
                holder->wire_format, holder->name)});
        return effect;
    }

    /*
     * A credential typed for this attempt wins over a stored one. The operator
     * who has just filled in a password prompt is asking for that password to
     * be used, and quietly preferring the remembered one would make the prompt
     * a lie.
     */
    if (!cmd.password.empty()) {
        if (cmd.remember && deps_->store_lock
            && deps_->store_lock() == model::domain::store_lock::unlocked) {
            if (deps_->remember)
                deps_->remember(cmd.account_id,
                    std::string{cmd.password.view()});
        } else if (cmd.remember) {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Not remembering the password for '" << cmd.account_id
                << "': the store is not unlocked";
        }
        start(cmd,
            model::domain::credential{cmd.principal,
                std::string{cmd.password.view()}},
            {}, effect);
        return effect;
    }

    /*
     * No password and a closed lock is a refusal, and the sentence says which
     * of the two things went wrong. "The password was wrong" would be wrong:
     * nothing was tried, and an operator who is told their password is bad will
     * retype a password that was never the problem.
     */
    if (!deps_->store_lock
        || deps_->store_lock() != model::domain::store_lock::unlocked) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::store_lock, cmd.account_id},
            std::format(
                "cannot connect to {}: the store is {}, so no password is "
                "available and none was supplied",
                instance_row->name,
                domain::to_string(deps_->store_lock ? deps_->store_lock()
                                                  : domain::store_lock{}))});
        return effect;
    }

    if (!deps_->sealed_credential) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::credential, cmd.account_id},
            "cannot read the stored credential"});
        return effect;
    }
    auto sealed = deps_->sealed_credential(cmd.account_id);
    if (!sealed) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::credential, cmd.account_id},
            support::store_failure("cannot read the stored credential",
                sealed.error())});
        return effect;
    }
    if (!sealed.value()) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::credential, cmd.account_id},
            std::format(
                "cannot connect to {}: the account has no stored credential and "
                "no password was supplied",
                instance_row->name)});
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Cannot sign in to '" << instance_row->name
            << "': account '" << cmd.account_id
            << "' has no stored credential and no password was supplied";
        return effect;
    }

    auto blob = std::move(sealed).value();
    start(cmd, {}, std::move(blob.value()), effect);
    return effect;
}

session_effect session_service::disconnect(const model::command::command& cmd) {
    session_effect effect;
    if (!deps_)
        return effect;

    /*
     * A sign-out is the one stop that says goodbye: the instance is asked to
     * drop the token, best effort, because a token nobody will use again is
     * still a credential (SR-24).
     */
    sign_out(cmd.instance_id, cmd.account_id);
    effect.detach.push_back(
        session_effect::watched{cmd.instance_id, cmd.account_id});

    const auto handle = deps_->sessions.find(cmd.instance_id, cmd.account_id);
    if (!handle)
        return effect;

    /*
     * The windows are unbound before the row is released, because release bumps
     * the generation: after it, the handle a window holds no longer compares
     * equal to the row it was bound to, and the unlink would silently miss.
     */
    effect.unbind.push_back(*handle);
    deps_->sessions.remove(*handle);
    effect.save_windows = true;
    return effect;
}

session_effect session_service::select_party(
    const model::command::command& cmd) {
    session_effect effect;
    if (!deps_) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::session, cmd.account_id},
            "acting as another party needs a session service with a catalogue"});
        return effect;
    }
    if (cmd.party_id.empty()) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::session, cmd.account_id},
            "no party was named"});
        return effect;
    }

    const auto handle = deps_->sessions.find(cmd.instance_id, cmd.account_id);
    if (!handle
        || deps_->sessions.status(*handle)
            != model::domain::session_status::authenticated) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::session, cmd.account_id},
            "acting as another party needs a signed-in session"});
        return effect;
    }

    /*
     * The choice is handed to the session's own actor, which owns the
     * transport. A session with no actor left has nothing to ask, and saying so
     * is better than leaving the control changed and nothing happening.
     */
    const auto actor = feed(cmd.instance_id, cmd.account_id);
    if (actor == nullptr) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::session, cmd.account_id},
            "the session has no connection to act as another party"});
        return effect;
    }
    actor->select_party(cmd.party_id);
    return effect;
}

std::shared_ptr<worker::tick_feed>
session_service::open(worker::session_actor_options options) {
    options.events = &events_;
    const auto key =
        running_key(options.key.instance_id, options.key.account_id);
    if (const auto found = actors_.find(key); found != actors_.end()) {
        found->second->request_stop();
        retiring_.push_back(std::move(found->second));
        actors_.erase(found);
    }

    std::shared_ptr<worker::tick_feed> actor(feeds_(std::move(options)));
    if (!actor)
        return actor;

    actors_.insert_or_assign(key, actor);
    return actor;
}

std::shared_ptr<worker::tick_feed> session_service::feed(
    std::string_view instance_id, std::string_view account_id) const {
    const auto found = actors_.find(running_key(instance_id, account_id));
    return found == actors_.end() ? nullptr : found->second;
}

bool session_service::submit_read(std::string_view instance_id,
    std::string_view account_id, worker::read_request request) {
    const auto actor = feed(instance_id, account_id);
    return actor != nullptr && actor->submit_read(std::move(request));
}

void session_service::retire(std::string_view instance_id,
    std::string_view account_id) {
    const auto found = actors_.find(running_key(instance_id, account_id));
    if (found == actors_.end())
        return;
    found->second->request_stop();
    retiring_.push_back(std::move(found->second));
    actors_.erase(found);
}

void session_service::sign_out(std::string_view instance_id,
    std::string_view account_id) {
    const auto found = actors_.find(running_key(instance_id, account_id));
    if (found == actors_.end())
        return;
    found->second->request_logout();
    retiring_.push_back(std::move(found->second));
    actors_.erase(found);
}

void session_service::cancel() {
    for (auto& [key, actor] : actors_) {
        static_cast<void>(key);
        actor->request_stop();
    }
    actors_.clear();

    /*
     * Dropping the last reference to an actor joins its thread, which is why
     * every one of them is asked to stop before any is destroyed.
     */
    for (auto& actor : retiring_)
        actor->request_stop();
    retiring_.clear();
}

void session_service::reap() {
    std::erase_if(retiring_,
        [](const auto& actor) { return actor->finished(); });
}

}
