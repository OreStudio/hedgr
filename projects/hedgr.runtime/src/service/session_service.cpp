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

/** @brief The account row @p account_id names, or nullptr. */
[[nodiscard]] const domain::account* account_row(
    const model::state::account_table& accounts, std::string_view account_id) {
    const auto handle = accounts.find(account_id);
    return handle ? accounts.get(*handle) : nullptr;
}

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
    model::domain::credential credential, std::string sealed_credential) {
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
    attempts_[running_key(cmd.instance_id, cmd.account_id)] = attempt;

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
     * The principal is the name IAM is asked about, and it comes from the
     * account row. An account whose principal was left blank would otherwise
     * reach the instance as an empty username, and the sentence it answers with
     * ("Username cannot be empty") names neither the account nor the field the
     * operator has to fill in.
     */
    if (cmd.principal.empty()) {
        effect.refusals.push_back(refusal{
            {model::state::condition_source::session, cmd.account_id},
            std::format("cannot connect to {}: account '{}' has no principal, "
                        "which is the name the instance knows it by",
                instance_row->name, cmd.account_id)});
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
            {});
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
    /*
     * The principal travels with the sealed credential whether or not a
     * password was typed. The box holds the password and nothing else, so a
     * sign-in that means to use it still has to name the account IAM will be
     * asked about: dropping the name here is what makes a remembered password
     * sign in as nobody.
     */
    start(cmd, model::domain::credential{cmd.principal, {}},
        std::move(blob.value()));
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
     * A choice of the party the session already acts as is a confirmation, not a
     * request: the actor would find nothing to change and answer nothing, so the
     * choice would never reach the account. Remembering it here is what makes
     * "this is my party" stick when it happens to be the deployment's default.
     */
    if (cmd.party_id == deps_->sessions.party_id(*handle)) {
        if (deps_->remember_party)
            deps_->remember_party(cmd.account_id, cmd.party_id);
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

std::optional<session_effect> session_service::apply(
    const model::event::event& ev) {
    /*
     * Without a catalogue there are no sessions to apply anything to, and the
     * events a test drives its reads with are not about a session at all.
     */
    if (!deps_)
        return std::nullopt;

    /*
     * A fact from a sign-in attempt that has since been replaced is not news
     * about the session that is live now. The actor of the old attempt may
     * still be finishing, and its disconnect would otherwise land on top of the
     * newer sign-in.
     */
    if (ev.attempt != 0) {
        const auto current = attempts_.find(
            running_key(ev.instance_id, ev.account_id));
        if (current == attempts_.end() || current->second != ev.attempt)
            return std::nullopt;
    }

    const auto handle = deps_->sessions.find(ev.instance_id, ev.account_id);

    /*
     * A session that is gone is not an error: an actor that was asked to stop
     * may still have one event in flight, and applying it to a row that no
     * longer exists would address whoever occupies the slot next.
     */
    if (!handle)
        return std::nullopt;

    session_effect effect;
    switch (ev.kind) {
    case model::event::event_kind::session_status_changed:
        deps_->sessions.set_status(*handle, ev.status);

        /*
         * A session the actor says has ended has no actor worth keeping. The
         * actor announces its own disconnect and failure, so it is retired
         * here; the feed stops watching either way, because a chart keeps its
         * series and the next connect gives the session a new actor to watch
         * them on.
         */
        if (ev.status == model::domain::session_status::disconnected
            || ev.status == model::domain::session_status::failed) {
            effect.detach.push_back({ev.instance_id, ev.account_id});
            retire(ev.instance_id, ev.account_id);
        }
        break;
    case model::event::event_kind::session_authenticated:
        deps_->sessions.set_account_result(*handle, ev.tenant_name,
            ev.party_name, ev.party_id, ev.token);
        deps_->sessions.set_tenant_id(*handle, ev.tenant_id);
        deps_->sessions.set_parties(*handle, ev.parties);
        deps_->sessions.set_mode(*handle, ev.mode);
        deps_->sessions.set_status(*handle,
            model::domain::session_status::authenticated);

        /*
         * The feed starts watching here and not at the connect: a watch needs
         * the session's party, and until the sign-in answers there is none. The
         * actor itself exists already, and has since the connect command.
         */
        effect.attach.push_back({ev.instance_id, ev.account_id});

        /*
         * The flags fall back to the drawn ones until the tenant's map answers,
         * which is why the read is asked for as soon as there is a tenant.
         */
        effect.reads.push_back(
            {session_effect::read_start::kind::currency_image_map,
                ev.instance_id, ev.account_id, ev.tenant_id});

        /*
         * Stamped here rather than by the actor, so the time says when the
         * frame loop accepted the sign-in and every session's clock reading
         * comes from one clock. A re-authentication of a pair that already had
         * a session restamps it, which is what makes the workspace show the age
         * of the connection it is actually looking at.
         */
        deps_->sessions.set_opened_at_ns(*handle, support::steady_now_ns());

        /*
         * The landing surface is decided by the fact and not by the click that
         * asked for it: a super administrator's session belongs to the system
         * tenant, and that is known only once IAM has answered. Opening it here
         * also means a refused sign-in leaves no window behind.
         */
        effect.open.push_back({ev.instance_id, ev.account_id,
            deps_->sessions.mode(*handle)
                    == model::domain::session_mode::system_administration
                ? model::domain::window_kind::command_centre
                : model::domain::window_kind::workspace});

        /*
         * Every other window the wall restored was saved against this same
         * pair and is still unbound. Binding them here is what makes a restored
         * series browser or chart live on the sign-in that owns it.
         */
        effect.rebind.push_back({ev.instance_id, ev.account_id, *handle});

        /*
         * The party this account acted as last time wins over the one the
         * deployment signed the login in as. A remembered party the login no
         * longer holds is not asked for: the deployment answers such a request
         * with a refusal, and a refusal on every sign-in for a party that has
         * been removed is noise about a choice the operator has to make again.
         */
        if (const auto* row = account_row(deps_->accounts, ev.account_id);
            row != nullptr && !row->party_id.empty()
            && row->party_id != ev.party_id) {
            if (model::event::holds_party(ev.parties, row->party_id)) {
                BOOST_LOG_SEV(lg(), ores::logging::info)
                    << "Session '" << ev.instance_id << "' / '"
                    << ev.account_id << "' signed in as party '" << ev.party_id
                    << "' and will act as the remembered party '"
                    << row->party_id << "'";
                effect.party = session_effect::party_switch{ev.instance_id,
                    ev.account_id, row->party_id};
            } else {
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "Session '" << ev.instance_id << "' / '"
                    << ev.account_id << "' remembers party '" << row->party_id
                    << "', which this login may no longer act as; it stays on '"
                    << ev.party_id << "' and the next choice replaces it";
            }
        }
        break;
    case model::event::event_kind::session_token_refreshed:
        /*
         * A refresh replaces the credential, not the session. Reads that start
         * from here on take the new token, and the actor has already handed it
         * to its own subscription, so nothing here touches the feed.
         */
        deps_->sessions.set_token(*handle, ev.token);
        break;
    case model::event::event_kind::session_party_changed:
        /*
         * The session now acts as another party, so the token IAM just issued
         * is the one every read must carry. The actor has reopened its own
         * subscription for the new party; the catalogue is party-scoped, so it
         * is read again rather than filtered.
         */
        deps_->sessions.set_account_result(*handle, ev.tenant_name,
            ev.party_name, ev.party_id, ev.token);
        deps_->sessions.set_tenant_id(*handle, ev.tenant_id);
        effect.refresh_watches.push_back({ev.instance_id, ev.account_id});
        effect.reads.push_back(
            {session_effect::read_start::kind::market_catalogue, ev.instance_id,
                ev.account_id, {}});
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Session '" << ev.instance_id << "' / '" << ev.account_id
            << "' now acts as party '" << ev.party_id << "'";

        /*
         * The choice is remembered against the account, so the next sign-in
         * starts in it. Written on the confirmation and not on the request: a
         * party the instance refuses must not become the remembered one.
         */
        if (deps_->remember_party)
            deps_->remember_party(ev.account_id, ev.party_id);
        break;
    case model::event::event_kind::session_party_refused:
        /*
         * The session keeps its old party and token; the control that asked
         * goes back to what the session says, and the bar carries the reason.
         */
        effect.refusals.push_back(refusal{
            {model::state::condition_source::session, ev.account_id},
            ev.message.empty() ? "the instance refused that party"
                               : ev.message});
        break;
    case model::event::event_kind::link_lost: {
        /*
         * The session stays signed in: the actor keeps asking and rebuilds the
         * connection itself. What the operator needs is the line in the bar,
         * because an outage that is only in the log is invisible on a desk that
         * draws its last numbers whether or not anything is still arriving.
         */
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Session '" << ev.instance_id << "' / '" << ev.account_id
            << "' lost its connection";
        effect.conditions.push_back({
            {model::state::condition_source::session,
                running_key(ev.instance_id, ev.account_id)},
            false, model::state::condition_severity::warning,
            "the connection to the instance was lost; the session is "
            "reconnecting"});
        break;
    }
    case model::event::event_kind::link_restored:
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Session '" << ev.instance_id << "' / '" << ev.account_id
            << "' answered again";
        effect.conditions.push_back({
            {model::state::condition_source::session,
                running_key(ev.instance_id, ev.account_id)},
            true, model::state::condition_severity::warning, {}});
        effect.link_restored =
            session_effect::watched{ev.instance_id, ev.account_id};
        break;
    case model::event::event_kind::deployment_snapshot:
        deps_->sessions.set_deployment(*handle, ev.deployment);
        BOOST_LOG_SEV(lg(), ores::logging::debug)
            << "Applied a deployment snapshot for '" << ev.instance_id
            << "' / '" << ev.account_id << "'";
        break;
    case model::event::event_kind::session_failed:
        deps_->sessions.set_status(*handle,
            model::domain::session_status::failed);

        /*
         * The actor has reported its own end, so the feed stops watching it and
         * the actor itself is left to finish: its thread exits on its own, and
         * the service reaps it.
         */
        effect.detach.push_back({ev.instance_id, ev.account_id});
        retire(ev.instance_id, ev.account_id);

        /*
         * The sentence a person reads stays first and the code follows it in
         * brackets, so the tooltip carries both: the sentence says who to ask,
         * the code says what to tell them.
         */
        deps_->sessions.set_last_error(*handle,
            ev.code.empty() ? ev.message
                            : std::format("{} ({})", ev.message, ev.code));
        break;
    case model::event::event_kind::probe_result:
    case model::event::event_kind::store_unlocked:
    case model::event::event_kind::master_sealed:
    case model::event::event_kind::store_lock_failed:
    case model::event::event_kind::password_policy_fetched:
    case model::event::event_kind::series_catalogue:
    case model::event::event_kind::series_points:
    case model::event::event_kind::series_history:
    case model::event::event_kind::currency_image_map:
    case model::event::event_kind::image_loaded:
    case model::event::event_kind::crm_rates:
    case model::event::event_kind::curve_sets:
    case model::event::event_kind::crm_names:
        /*
         * Each is about the deployment or a window rather than about a session,
         * and is applied by the service that owns it before this one is asked.
         * The cases are listed so that a new kind is a compile error here
         * rather than a silent drop.
         */
        return std::nullopt;
    }
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
    /*
     * Nothing is left to tell apart once every actor is going: a fact that was
     * already in flight is about a session that is being stopped anyway, and
     * the attempts are forgotten with the connections they numbered.
     */
    attempts_.clear();

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
