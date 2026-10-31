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
#include "hedgr.runtime/worker/session_actor.hpp"
#include "hedgr.session/market/tick_stream.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "hedgr.session/transport/session_error.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include "hedgr.store/crypto/secret_box.hpp"
#include "ores.logging/make_logger.hpp"
#include <algorithm>
#include <chrono>
#include <format>
#include <utility>

namespace hedgr::runtime::worker {

namespace {

namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace transport = hedgr::session::transport;

/** @brief This actor's logger. No credential or token is ever written. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.runtime.worker.session_actor");
    return instance;
}

/**
 * @brief How long a session waits before its next attempt at a refresh.
 *
 * Four fifths of the token's life, the point ores.qt uses and the point the
 * deployment's own iam.token.refresh_threshold_pct names. The instance states
 * the lifetime, so a deployment that shortens it is followed without a client
 * rebuild.
 */
[[nodiscard]] std::chrono::milliseconds refresh_delay_for(int seconds) {
    const auto life = std::chrono::seconds{seconds > 0 ? seconds : 1800};
    return std::chrono::duration_cast<std::chrono::milliseconds>(life * 4 / 5);
}

/**
 * @brief How long a session waits before trying a failed refresh again.
 *
 * A refresh that fails while the token is still valid is not a session that
 * has ended: the instance may be briefly unable to answer. The retry starts at
 * a second and doubles, so a deployment that is down for minutes costs a
 * handful of requests rather than one a second.
 */
[[nodiscard]] std::chrono::milliseconds refresh_retry_delay(std::size_t failures) {
    const auto doublings = std::min<std::size_t>(failures, 6);
    return std::chrono::milliseconds{1000} * (1u << doublings);
}

/**
 * @brief A fact about this session, tagged with the pair and the attempt.
 *
 * Every event a session sends is about one attempt at one pair, so the tagging
 * is done once here rather than at each push site. The attempt is what lets the
 * frame loop tell a stopped actor's last words from the one that replaced it.
 */
[[nodiscard]] event::event for_session(const session_actor_options& options) {
    event::event fact;
    fact.instance_id = options.key.instance_id;
    fact.account_id = options.key.account_id;
    fact.attempt = options.attempt;
    return fact;
}

/**
 * @brief The answer a read gets when the actor stops before running it.
 *
 * A stopped actor still answers every read it accepted, because the caller
 * marked the read in flight when it submitted it and only an answer clears
 * that mark.
 */
constexpr std::string_view stopped_read_message =
    "the session stopped before the read ran";

/**
 * @brief The answer a read gets when the failure came with no sentence.
 *
 * An empty message would read as a successful answer to every handler that
 * asks whether the read failed, so a reason that did not say anything is
 * replaced with one that does.
 */
constexpr std::string_view unknown_read_failure_message =
    "the read did not run: the session's connection is not open";

/**
 * @brief The answer a read gets when the queue is already full.
 *
 * The caller is told the read did not run rather than left waiting for an
 * answer that was never queued, and the answer is pushed here so the mark the
 * caller sets when it submits is cleared by the frame loop as usual.
 */
constexpr std::string_view full_queue_message =
    "the read did not run: the session has more reads waiting than it will "
    "hold";

}

session_actor::session_actor(session_actor_options options)
    : options_(std::move(options))
    , sink_(std::make_shared<tick_sink>()) {
}

session_actor::~session_actor() {
    stop();
}

void session_actor::start() {
    if (thread_.joinable())
        return;
    thread_ = std::thread([this]() { run(); });
}

void session_actor::request_stop() {
    {
        std::lock_guard lock(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
}

void session_actor::request_logout() {
    logout_on_stop_.store(true, std::memory_order_release);
    request_stop();
}

void session_actor::stop() {
    request_stop();
    if (thread_.joinable())
        thread_.join();
}

void session_actor::set_token(std::string token) {
    {
        std::lock_guard lock(mu_);
        new_token_ = std::move(token);
    }
    cv_.notify_all();
}

void session_actor::select_party(std::string party_id) {
    {
        std::lock_guard lock(mu_);
        new_party_ = std::move(party_id);
    }
    cv_.notify_all();
}

void session_actor::watch(model::state::series_handle series,
    std::string canonical_uri) {
    {
        std::lock_guard lock(mu_);
        commands_.push_back(command{
            .add = true, .series = series,
            .canonical_uri = std::move(canonical_uri)});
    }
    cv_.notify_all();
}

void session_actor::unwatch(model::state::series_handle series) {
    {
        std::lock_guard lock(mu_);
        commands_.push_back(command{.add = false, .series = series});
    }
    cv_.notify_all();
}

void session_actor::set_watch(std::span<const watch_target> wanted) {
    {
        std::lock_guard lock(mu_);
        new_watch_ = std::vector<watch_target>(wanted.begin(), wanted.end());
    }
    cv_.notify_all();
}

bool session_actor::submit_read(read_request request) {
    bool full = false;
    {
        std::lock_guard lock(mu_);
        if (stopping_)
            return false;
        full = reads_.size() >= options_.max_queued_reads;
        if (!full)
            reads_.push_back(std::move(request));
    }

    /*
     * A refused read is still answered, because the caller marks it in flight
     * the moment this says it was accepted and only an answer clears the mark.
     * It is not queued, so the answer is the one built here.
     */
    if (full) {
        push(read_failed(request, options_.key, options_.tenant_id,
            full_queue_message));
        return true;
    }
    cv_.notify_all();
    return true;
}

void session_actor::answer_reads(std::vector<read_request>& reads,
    session::transport::session_link& link) {
    for (const auto& request : reads) {
        auto fact = read_event(request, options_.key, options_.tenant_id);
        run_read(link, request, fact);
        push(std::move(fact));
    }
    reads.clear();
}

void session_actor::fail_reads(std::vector<read_request>& reads,
    std::string_view reason) {
    const auto sentence = reason.empty() ? unknown_read_failure_message
                                         : reason;
    for (const auto& request : reads) {
        auto fact = read_failed(request, options_.key, options_.tenant_id,
            sentence);
        push(std::move(fact));
    }
    reads.clear();
}

void session_actor::answer_stranded_reads() {
    std::vector<read_request> stranded;
    {
        std::lock_guard lock(mu_);
        stranded.swap(reads_);
    }
    fail_reads(stranded, stopped_read_message);
}

void session_actor::push(model::event::event fact) const {
    if (options_.events != nullptr)
        options_.events->push(std::move(fact));
}

void session_actor::emit(model::event::event_kind kind) const {
    auto fact = for_session(options_);
    fact.kind = kind;
    push(std::move(fact));
}

void session_actor::push_status(domain::session_status status) const {
    auto fact = for_session(options_);
    fact.kind = event::event_kind::session_status_changed;
    fact.status = status;
    push(std::move(fact));
}

void session_actor::push_failure(const transport::session_error& error) const {
    /*
     * The sentence a person reads and the low-level cause stay apart: @c message
     * is the one the tooltip shows first and @c payload is what belongs in a
     * log or a dimmed second line.
     */
    auto fact = for_session(options_);
    fact.kind = event::event_kind::session_failed;
    fact.code = std::string(transport::to_string(error.code));
    fact.message = error.message;
    fact.payload = error.detail;
    push(std::move(fact));
}

void session_actor::push_authenticated(
    const transport::session_account& account) const {
    auto fact = for_session(options_);
    fact.kind = event::event_kind::session_authenticated;
    fact.token = domain::secret_string{account.token};
    fact.tenant_name = account.tenant_name;
    fact.tenant_id = account.tenant_id;
    fact.party_name = account.party_name;
    fact.party_id = account.party_id;
    for (const auto& party : account.parties) {
        if (!fact.parties.empty())
            fact.parties.push_back('\x1e');
        fact.parties += party.id;
        fact.parties.push_back('\x1f');
        fact.parties += party.name;
    }

    /*
     * The deployment's own rule, applied once here: the tenant IAM resolved
     * decides whether this session acts on the deployment or inside it.
     * Everything downstream reads the answer rather than holding the rule.
     */
    fact.mode = domain::session_mode_for(account.tenant_id);

    /*
     * The event has no principal field because the session key already names
     * the account. What the instance answered with is worth carrying, and
     * payload is the one free-form field, so it travels there for the log.
     */
    fact.payload = account.principal;
    push(std::move(fact));
}

void session_actor::push_token_refreshed() const {
    auto fact = for_session(options_);
    fact.kind = event::event_kind::session_token_refreshed;
    fact.token = domain::secret_string{facts_.token};
    fact.tenant_id = facts_.tenant_id;
    fact.party_id = facts_.party_id;
    fact.payload = facts_.principal;
    push(std::move(fact));
}

void session_actor::push_party_changed() const {
    auto fact = for_session(options_);
    fact.kind = event::event_kind::session_party_changed;
    fact.token = domain::secret_string{facts_.token};
    /*
     * The tenant does not change with the party; the instance's answer carries
     * a tenant name and no key, so the session's own pair is reported.
     */
    fact.tenant_id = facts_.tenant_id;
    fact.tenant_name = facts_.tenant_name;
    fact.party_id = facts_.party_id;
    fact.party_name = facts_.party_name;
    fact.payload = facts_.principal;
    push(std::move(fact));
}

void session_actor::push_party_refused(const transport::session_error& error) const {
    auto fact = for_session(options_);
    fact.kind = event::event_kind::session_party_refused;
    fact.code = std::string(transport::to_string(error.code));
    fact.message = error.message;
    push(std::move(fact));
}

bool session_actor::sign_in(transport::session_link& link) {
    const auto key = options_.key;

    push_status(domain::session_status::connecting);

    if (const auto connected = link.connect(); !connected) {
        /*
         * No retry. A connect failure is a broker that is not answering, a
         * certificate that is not trusted or a prefix that is wrong, and none
         * of those is fixed by trying again: the operator decides what to do
         * next, and a retry loop would only hide the decision behind a storm of
         * identical failures.
         */
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Session '" << key.instance_id << "' / '" << key.account_id
            << "' could not connect: " << connected.error().message;
        push_failure(connected.error());
        return false;
    }

    /*
     * A stored credential is opened here, on the actor's own thread. The key
     * derivation behind it is half a second of deliberate work, and running
     * that where the interface draws would stall every window for its duration.
     *
     * The plaintext that comes out is written into the credential and never
     * named again, so it lives for exactly this attempt: it is not returned,
     * not logged and not placed on an event, and the wipe after the sign-in is
     * the only thing that ends it.
     */
    if (!options_.sealed_credential.empty()) {
        auto opened = store::crypto::secret_box::open(
            options_.master_password.view(), options_.sealed_credential);

        /*
         * The sealed copy is no longer needed whatever happened, and it is
         * still a secret: it opens with the master password, so leaving it in
         * the freed allocation leaves a credential behind for one attacker
         * with the password.
         */
        std::fill(options_.sealed_credential.begin(),
            options_.sealed_credential.end(), '\0');
        options_.sealed_credential.clear();
        options_.sealed_credential.shrink_to_fit();

        /*
         * The master password is dropped as soon as it has done its one job.
         * This thread runs for the whole life of the session, which is hours,
         * and nothing after this point can need it.
         */
        options_.master_password.clear();

        if (!opened) {
            /*
             * A stored credential that will not open is a failure to sign in
             * with the code a refused password gets, because that is what it is
             * from the operator's side. The cause is spelled with the box code,
             * which tells a wrong master password from a broken crypto step
             * without either reaching a log as a secret.
             */
            BOOST_LOG_SEV(lg(), ores::logging::error)
                << "The stored credential for '" << key.account_id
                << "' could not be opened";
            push_failure(transport::session_error{
                .code = transport::session_errc::authentication_rejected,
                .message = "the stored credential could not be opened",
                .detail = std::format("secret_box: {}",
                    store::crypto::to_string(opened.error().code)),
            });
            return false;
        }
        options_.credential.password = std::move(opened).value();
    }

    auto answer = link.authenticate(options_.credential.principal,
        options_.credential.password);

    /*
     * The secret dies here, before anything else looks at the answer. It was
     * copied into this options object and nowhere else, so wiping the buffer
     * now is what makes "the credential exists for one attempt" true rather
     * than aspirational: clear() alone would leave the bytes in the string's
     * allocation.
     */
    std::fill(options_.credential.password.begin(),
        options_.credential.password.end(), '\0');
    options_.credential.password.clear();
    options_.credential.password.shrink_to_fit();

    if (!answer) {
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Sign-in for '" << key.account_id << "' on '" << key.instance_id
            << "' failed: " << answer.error().message << " ["
            << transport::to_string(answer.error().code) << "]";
        push_failure(answer.error());
        return false;
    }

    const auto& account = answer.value();
    facts_.token = account.token;
    facts_.principal = account.principal;
    facts_.tenant_id = account.tenant_id;
    facts_.tenant_name = account.tenant_name;
    facts_.party_id = account.party_id;
    facts_.party_name = account.party_name;
    facts_.lifetime_s = account.access_lifetime_s;

    push_authenticated(account);
    push_status(domain::session_status::authenticated);

    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Session '" << key.instance_id << "' / '" << key.account_id
        << "' is authenticated as party '" << facts_.party_id << "'";
    return true;
}

void session_actor::wait_for_retry() {
    std::unique_lock lock(mu_);
    cv_.wait_for(lock, std::chrono::seconds{1},
        [this]() {
            return stopping_ || !commands_.empty() || !reads_.empty();
        });
}

void session_actor::run() {
    /*
     * The stream runs on one connection: the caller's when it gave one, which
     * is how a test scripts the answers, and otherwise one this thread owns.
     * The tick stream only names what to do on it.
     */
    std::unique_ptr<session::transport::session_client> client;
    std::shared_ptr<session::transport::session_link> connection = options_.link;
    if (connection == nullptr) {
        client = std::make_unique<session::transport::session_client>(
            options_.options);
        connection =
            std::make_shared<session::transport::session_client_link>(*client);
    }
    auto& link = *connection;

    const ores::nats::wire_codec codec(options_.options.wire_format == "json"
            ? ores::nats::wire_format::json
            : ores::nats::wire_format::msgpack);
    /*
     * Whether this actor signs itself in. A caller that has already signed in
     * hands over the facts - which is the shape the tick feed was first built
     * for, and how a test scripts a live session - and the actor runs them
     * unchanged. A caller that has not hands over a credential, and the actor
     * owns the whole of the session from its first byte.
     */
    const bool signs_in = !options_.credential.password.empty()
        || !options_.sealed_credential.empty();

    /*
     * The handler owns the sink with this object, so a handler still running
     * when the stream closes writes into memory that is alive.
     */
    const auto sink = sink_;
    const auto on_tick = [sink](const session::market::tick& tick) {
        sink->on_tick(tick);
    };

    std::unique_ptr<session::market::market_tick_stream> stream;
    const auto make_stream = [&]() {
        return std::make_unique<session::market::market_tick_stream>(link,
            facts_.token, facts_.principal, facts_.tenant_id, facts_.party_id,
            codec);
    };

    if (signs_in) {
        if (!sign_in(link)) {
            /*
             * A failed sign-in has already reported why. A caller that
             * submitted a read before the actor stopped still has its event
             * coming, because it marked the read in flight when it submitted
             * it, and only an answer clears that mark.
             */
            answer_stranded_reads();
            finished_.store(true, std::memory_order_release);
            return;
        }
    } else {
        facts_.token = options_.token;
        facts_.principal = options_.principal;
        facts_.tenant_id = options_.tenant_id;
        facts_.party_id = options_.party_id;
    }

    stream = make_stream();
    bool session_failed = false;
    {
        /*
         * The session's own scope. The declarations below belong to the loop
         * and end with it, which is what keeps them from reading as state of
         * the whole thread.
         */

        /*
         * Liveness. The deployment answers a liveness request while it is
         * there, and a request that fails is the only sign of an outage this
         * process has: cnats reconnects underneath, and its own flag stays
         * true. The first failure is reported once, the first success after it
         * once, and a run of failures long enough to outlast cnats rebuilds the
         * connection here.
         */
        const auto ping_interval = options_.ping_interval.count() > 0
            ? options_.ping_interval
            : std::chrono::milliseconds{10'000};
        auto next_ping = std::chrono::steady_clock::now();
        bool lost = false;
        std::size_t failures = 0;

        /*
         * The refresh deadline, armed from the life the instance stated. Only
         * an actor that signed in refreshes: a session handed a token is
         * refreshed by whoever obtained it.
         *
         * A refresh that fails while the token is still valid is retried with
         * a backoff rather than ending the session, because a deployment that
         * is briefly unable to answer has not ended anything. The session ends
         * on a failed refresh only once the token has really expired (SR-15).
         */
        std::size_t refresh_failures = 0;
        auto next_refresh = std::chrono::steady_clock::time_point::max();
        auto token_expiry = std::chrono::steady_clock::time_point::max();
        const auto arm_refresh = [&](std::chrono::steady_clock::time_point from) {
            const auto life = std::chrono::seconds{
                facts_.lifetime_s > 0 ? facts_.lifetime_s : 1800};
            token_expiry = from + life;
            next_refresh = from + refresh_delay_for(facts_.lifetime_s);
            refresh_failures = 0;
        };
        if (signs_in)
            arm_refresh(std::chrono::steady_clock::now());

    while (true) {
        std::vector<command> queued;
        std::optional<std::string> token;
        std::optional<std::string> party;
        std::optional<std::vector<watch_target>> watch;
        std::vector<read_request> reads;
        {
            std::unique_lock lock(mu_);
            /*
             * A live stream sleeps until there is something to do: a watch
             * change, a new token, a read, a liveness request or a stop. Only
             * a stream that is not yet connected or subscribed wakes on the
             * retry cadence, because that is the state a connect or a
             * subscribe has to be tried again from. An idle session therefore
             * costs no wakes beyond the liveness interval.
             *
             * A read waits for the connection rather than being refused by it:
             * the session is authenticated when the read is asked for, so the
             * stream is about to connect, and a read refused merely because
             * the connect had not finished is a read the caller cannot retry.
             * A queued read therefore wakes the actor, which is also the
             * reason to try a connect before the retry cadence says so. A
             * connect that fails answers every queued read with its reason,
             * because waiting for a deployment that refused the connection
             * would leave the caller waiting for a read that cannot run.
             *
             * A read needs the connection and not the subscription: a session
             * whose tick subscription cannot be opened can still answer the
             * reads its windows ask for.
             */
            const bool live = stream->connected() && stream->subscribed();
            const bool due = live && std::chrono::steady_clock::now()
                    >= std::min(next_ping, next_refresh);
            const auto ready = [this]() {
                return stopping_ || !commands_.empty()
                    || new_watch_.has_value() || new_token_.has_value()
                    || new_party_.has_value() || !reads_.empty();
            };
            if (live && !due)
                cv_.wait_until(lock, std::min(next_ping, next_refresh), ready);
            else if (!live)
                cv_.wait_for(lock, std::chrono::milliseconds{100}, ready);
            if (stopping_)
                break;
            queued.swap(commands_);
            watch.swap(new_watch_);
            token.swap(new_token_);
            party.swap(new_party_);
            if (stream->connected())
                reads.swap(reads_);
        }

        if (token)
            stream->set_token(std::move(*token));

        /*
         * A party change comes before the reads, because the token IAM answers
         * with is the one every read from here on must carry. The tick subject
         * is party scoped, so the subscription is rebuilt for the new party,
         * while the sink keeps its watch set: which series are worth watching
         * is the frame loop's answer, and it changes when the catalogue for
         * the new party arrives.
         */
        if (party && *party != facts_.party_id) {
            const auto selected = link.switch_party(*party);
            if (!selected) {
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "Acting as party '" << *party << "' for '"
                    << options_.key.account_id << "' was refused: "
                    << selected.error().message << " ["
                    << transport::to_string(selected.error().code) << "]";
                push_party_refused(selected.error());
            } else {
                const auto& current = link.account();
                facts_.token = current.token;
                facts_.principal = current.principal;
                facts_.party_id = current.party_id;
                facts_.party_name = current.party_name;
                if (current.access_lifetime_s > 0)
                    facts_.lifetime_s = current.access_lifetime_s;
                if (signs_in)
                    arm_refresh(std::chrono::steady_clock::now());
                push_party_changed();
                BOOST_LOG_SEV(lg(), ores::logging::info)
                    << "Session '" << options_.key.account_id << "' on '"
                    << options_.key.instance_id << "' now acts as party '"
                    << facts_.party_id << "'";

                stream->disconnect();
                stream = make_stream();
            }
        }

        for (auto& item : queued) {
            if (item.add)
                sink->watch(item.series, std::move(item.canonical_uri));
            else
                sink->unwatch(item.series);
        }
        if (watch)
            sink->set_watch(*watch);
        if (!reads.empty())
            answer_reads(reads, link);

        if (stream->connected()
            && std::chrono::steady_clock::now() >= next_ping) {
            const auto sent = std::chrono::steady_clock::now();
            const auto answer = link.ping();
            next_ping = sent + ping_interval;
            if (!answer) {
                ++failures;
                if (!lost) {
                    lost = true;
                    emit(model::event::event_kind::link_lost);
                }
                if (options_.failures_before_reconnect != 0
                    && failures >= options_.failures_before_reconnect) {
                    /*
                     * cnats has had its own turn at reconnecting. Building the
                     * connection again is what lets a broker that went away and
                     * came back be picked up on this attempt rather than on a
                     * refresh an hour from now.
                     */
                    stream->disconnect();
                    failures = 0;
                }
            } else {
                failures = 0;
                if (lost) {
                    lost = false;
                    emit(model::event::event_kind::link_restored);
                }
            }
        }

        if (signs_in && std::chrono::steady_clock::now() >= next_refresh) {
            const auto sent = std::chrono::steady_clock::now();
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Proactive token refresh triggered for '"
                << options_.key.account_id << "' on '" << options_.key.instance_id
                << "'";
            const auto refreshed = link.refresh();
            if (!refreshed) {
                ++refresh_failures;
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "Token refresh for '" << options_.key.account_id
                    << "' on '" << options_.key.instance_id << "' failed ("
                    << refresh_failures << " failed attempts): "
                    << refreshed.error().message << " ["
                    << transport::to_string(refreshed.error().code) << "]";
                if (std::chrono::steady_clock::now() >= token_expiry) {
                    /*
                     * The token has run out, so nothing this session asks for
                     * can be answered any more. The sentence a person reads is
                     * the one the instance gave.
                     */
                    push_failure(refreshed.error());
                    session_failed = true;
                    break;
                }
                next_refresh = sent
                    + refresh_retry_delay(refresh_failures - 1);
                continue;
            }

            facts_.lifetime_s = *refreshed;
            facts_.token = link.account().token;
            push_token_refreshed();
            if (facts_.lifetime_s > 0) {
                arm_refresh(sent);
            } else {
                next_refresh = std::chrono::steady_clock::time_point::max();
            }
            BOOST_LOG_SEV(lg(), ores::logging::info)
                << "Proactive token refresh succeeded for '"
                << options_.key.account_id << "' on '" << options_.key.instance_id
                << "'; next refresh in "
                << std::chrono::duration_cast<std::chrono::seconds>(
                       next_refresh - sent)
                       .count()
                << " s";
        }

        if (!stream->connected()) {
            if (!stream->connect()) {
                std::vector<read_request> refused;
                {
                    std::lock_guard lock(mu_);
                    refused.swap(reads_);
                }
                fail_reads(refused, stream->last_error());
                wait_for_retry();
                continue;
            }
        }

        if (!stream->subscribed() && !stream->subscribe(on_tick))
            wait_for_retry();
    }
    }

    /*
     * A read that is still queued has a caller waiting on its event, so it is
     * answered with its failure when the actor stops rather than left to
     * strand whatever mark the caller set when it submitted it.
     */
    answer_stranded_reads();

    /*
     * A sign-out is the operator's, so the instance is told to drop the token
     * before the connection goes, best effort. Nothing else that stops an actor
     * says goodbye: a teardown, a failure and the process going away all leave
     * the instance to expire the token on its own.
     */
    if (logout_on_stop_.load(std::memory_order_acquire) && !session_failed
        && stream && stream->connected()) {
        if (const auto signed_out = link.logout(); !signed_out) {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Signing out of '" << options_.key.instance_id << "' / '"
                << options_.key.account_id << "' was refused: "
                << signed_out.error().message;
        }
    }

    if (stream)
        stream->disconnect();

    /*
     * A session that got as far as holding a connection reports the move to
     * disconnected. A failed attempt has already said why it stopped, and
     * overwriting that with "disconnected" would erase the sentence the
     * operator needs.
     */
    if (signs_in && !session_failed)
        push_status(domain::session_status::disconnected);

    finished_.store(true, std::memory_order_release);
}

}
