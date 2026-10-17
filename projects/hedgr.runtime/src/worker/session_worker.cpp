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
#include "hedgr.runtime/worker/session_worker.hpp"
#include "hedgr.model/event/event.hpp"
#include "hedgr.session/transport/session_client.hpp"
#include "hedgr.session/transport/session_error.hpp"
#include "hedgr.store/crypto/secret_box.hpp"
#include "ores.logging/make_logger.hpp"
#include <algorithm>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#include <chrono>
#include <format>
#include <memory>
#include <stop_token>
#include <string>
#include <utility>

namespace hedgr::runtime::worker {

namespace {

namespace domain = hedgr::model::domain;
namespace event = hedgr::model::event;
namespace transport = hedgr::session::transport;

using hedgr::model::command::event_queue;

/** @brief This worker's logger. No credential or token is ever written. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.runtime.worker.session_worker");
    return instance;
}

/**
 * @brief A fact ready to be tagged with the pair it belongs to.
 *
 * Every event a worker sends is about one session, so the tagging is done once
 * here rather than repeated at each push site.
 */
[[nodiscard]] event::event for_key(const domain::session_key& key,
    std::uint64_t attempt) {
    event::event result;
    result.instance_id = key.instance_id;
    result.account_id = key.account_id;
    result.attempt = attempt;
    return result;
}

void push_status(event_queue& events, const domain::session_key& key,
                 std::uint64_t attempt, domain::session_status status) {
    auto fact = for_key(key, attempt);
    fact.kind = event::event_kind::session_status_changed;
    fact.status = status;
    events.push(std::move(fact));
}

/**
 * @brief Reports a failed attempt, keeping the sentence and the cause apart.
 *
 * @c message is what a person reads and @c payload is the low-level cause that
 * belongs in a log or a dimmed second line; both survive into the frame loop,
 * which is why they travel in separate fields.
 */
void push_failure(event_queue& events, const domain::session_key& key,
                  std::uint64_t attempt, const transport::session_error& error) {
    auto fact = for_key(key, attempt);
    fact.kind = event::event_kind::session_failed;
    fact.code = std::string(transport::to_string(error.code));
    fact.message = error.message;
    fact.payload = error.detail;
    events.push(std::move(fact));
}

/**
 * @brief The deployment-relative subject the placeholder refresh reads.
 */
constexpr std::string_view placeholder_subject = "iam.v1.ops.bootstrap_status";

/**
 * @brief Connects, signs in, then refreshes until asked to stop.
 *
 * The whole lifecycle of one session, and the only place a credential is ever
 * held. Nothing here reads or writes a state table: every outcome leaves as an
 * event.
 */
boost::asio::awaitable<void> run_session(session_job job, event_queue& events,
    std::shared_ptr<std::stop_source> stop) {
    const auto key = job.key;
    transport::session_client client(job.options);
    auto status = domain::session_status::connecting;

    push_status(events, key, job.attempt, status);

    /*
     * The spawn that reaches this coroutine may have come from the frame loop,
     * so the first thing the session does is hand itself to the pool. Every
     * call below blocks, and blocking the frame loop is the one thing the
     * concurrency contract forbids.
     */
    co_await boost::asio::post(co_await boost::asio::this_coro::executor,
        boost::asio::use_awaitable);

    BOOST_LOG_SEV(lg(), ores::logging::info)
        << "Session worker starting for '" << key.instance_id << "' / '"
        << key.account_id << "'";

    if (const auto connected = client.connect(); !connected) {
        /*
         * No retry. A connect failure is a broker that is not answering, a
         * certificate that is not trusted or a prefix that is wrong, and none
         * of those is fixed by trying again: the operator decides what to do
         * next, and a retry loop would only hide the decision behind a storm
         * of identical failures.
         */
        BOOST_LOG_SEV(lg(), ores::logging::warn)
            << "Session '" << key.instance_id << "' / '" << key.account_id
            << "' could not connect: " << connected.error().message;
        push_failure(events, key, job.attempt, connected.error());
        status = domain::session_status::failed;
    } else {
        /*
         * A stored credential is opened here, on a pool thread, and not by the
         * frame loop that handed it over. The key derivation behind it is half
         * a second of deliberate work, and running that where the interface
         * draws would stall every window for its duration.
         *
         * The plaintext that comes out is written straight into the job's
         * credential and never named again, so it lives for exactly this
         * attempt: it is not returned, not logged, and not placed on an event,
         * and the wipe below is the only thing that ends it.
         */
        if (!job.sealed_credential.empty()) {
            auto opened = store::crypto::secret_box::open(
                job.master_password.view(), job.sealed_credential);

            /*
             * The sealed copy is no longer needed whatever happened, and it is
             * still a secret: it opens with the master password, so leaving it
             * in the freed allocation is leaving a credential behind for one
             * attacker with the password.
             */
            std::fill(job.sealed_credential.begin(), job.sealed_credential.end(), '\0');
            job.sealed_credential.clear();
            job.sealed_credential.shrink_to_fit();

            /*
             * The master password is dropped as soon as it has done its one
             * job. This coroutine runs for the whole life of the session, which
             * is hours, and a password held in a long-lived frame is a password
             * held for no reason: nothing after this point can need it.
             */
            job.master_password.clear();

            if (!opened) {
                /*
                 * A stored credential that will not open is a failure to sign
                 * in with the same code a refused password gets, because that
                 * is what it is from the operator's side. The cause is spelled
                 * with the box code, which distinguishes a wrong master password
                 * from a broken crypto step without either reaching a log as a
                 * secret.
                 */
                BOOST_LOG_SEV(lg(), ores::logging::error)
                    << "The stored credential for '" << key.account_id
                    << "' could not be opened";
                push_failure(events, key, job.attempt, transport::session_error{
                    .code = transport::session_errc::authentication_rejected,
                    .message = "the stored credential could not be opened",
                    .detail = std::format("secret_box: {}",
                        store::crypto::to_string(opened.error().code)),
                });
                status = domain::session_status::failed;
            } else {
                job.credential.password = std::move(opened).value();
            }
        }

        if (status != domain::session_status::failed) {
            auto answer = client.authenticate(job.credential.principal,
                job.credential.password);

            /*
             * The secret dies here, before anything else looks at the answer. It
             * was copied into this job and nowhere else, so wiping the buffer now
             * is what makes "the credential exists for the lifetime of one attempt"
             * true rather than aspirational: clear() alone would leave the bytes in
             * the string's allocation.
             */
            std::fill(job.credential.password.begin(), job.credential.password.end(), '\0');
            job.credential.password.clear();
            job.credential.password.shrink_to_fit();

            if (!answer) {
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "Sign-in for '" << key.account_id << "' on '"
                    << key.instance_id << "' failed: "
                    << answer.error().message << " ["
                    << transport::to_string(answer.error().code) << "]";
                push_failure(events, key, job.attempt, answer.error());
                status = domain::session_status::failed;
            } else {
                const auto& account = answer.value();
                auto authenticated = for_key(key, job.attempt);
                authenticated.kind = event::event_kind::session_authenticated;
                authenticated.token = account.token;
                authenticated.tenant_name = account.tenant_name;
                authenticated.tenant_id = account.tenant_id;
                authenticated.party_name = account.party_name;
                authenticated.party_id = account.party_id;
                for (const auto& party : account.parties) {
                    if (!authenticated.parties.empty())
                        authenticated.parties.push_back('\x1e');
                    authenticated.parties += party.id;
                    authenticated.parties.push_back('\x1f');
                    authenticated.parties += party.name;
                }

                /*
                 * The deployment's own rule, applied once here: the tenant IAM
                 * resolved decides whether this session acts on the deployment
                 * or inside it. Everything downstream reads the answer rather
                 * than holding the rule.
                 */
                authenticated.mode =
                    domain::session_mode_for(account.tenant_id);

                /*
                 * The event has no principal field because the session key already
                 * names the account. What the instance answered with is still
                 * worth carrying, and payload is the one free-form field, so it
                 * travels there for the log rather than being dropped.
                 */
                authenticated.payload = account.principal;
                events.push(std::move(authenticated));

                status = domain::session_status::authenticated;
                BOOST_LOG_SEV(lg(), ores::logging::info)
                    << "Session '" << key.instance_id << "' / '"
                    << key.account_id << "' is authenticated";
                push_status(events, key, job.attempt, status);

                boost::asio::steady_timer timer(
                    co_await boost::asio::this_coro::executor);

                /*
                 * A disconnect is noticed at the next tick rather than interrupting
                 * the wait: the timer is not cancelled from the frame loop, because
                 * cancel() on another thread is not something Asio promises, and
                 * one refresh interval of latency on a disconnect is not worth a
                 * cross-thread cancellation protocol.
                 */
                BOOST_LOG_SEV(lg(), ores::logging::debug)
                    << "Session '" << key.instance_id << "' / '"
                    << key.account_id << "' refreshes every "
                    << job.refresh_interval.count() << " ms";

                while (!stop->stop_requested()) {
                    timer.expires_after(job.refresh_interval);
                    const auto started = std::chrono::steady_clock::now();

                    boost::system::error_code ignored;
                    co_await timer.async_wait(
                        boost::asio::redirect_error(boost::asio::use_awaitable, ignored));

                    const auto elapsed = std::chrono::steady_clock::now() - started;
                    if (stop->stop_requested())
                        break;

                    /*
                     * Placeholder data path. There is no live data protocol in
                     * Hedgr yet, and inventing one here would put a fiction on the
                     * wire that the first real protocol would have to unpick. So
                     * the tick is reported as a tick: a fixed deployment-relative
                     * subject, a note saying what it is, and the elapsed wait as
                     * the latency, which is the one honest measurement available.
                     */
                    auto tick = for_key(key, job.attempt);
                    tick.kind = event::event_kind::session_data;
                    tick.subject = std::string(placeholder_subject);
                    tick.payload = "refresh tick; no live data path is wired up yet";
                    tick.latency_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        elapsed)
                                          .count();
                    events.push(std::move(tick));
                }
            }
        }
    }

    /*
     * Only a session that got as far as holding a connection reports the move
     * to disconnected. A failed attempt has already said why it stopped, and
     * overwriting that with "disconnected" would erase the sentence the
     * operator needs.
     */
    BOOST_LOG_SEV(lg(), ores::logging::debug)
        << "Session worker finishing for '" << key.instance_id << "' / '"
        << key.account_id << "' with status " << domain::to_string(status);

    if (status != domain::session_status::failed)
        push_status(events, key, job.attempt, domain::session_status::disconnected);
}

}

void spawn_session(boost::asio::io_context& context, session_job job,
    event_queue& events, std::shared_ptr<std::stop_source> stop) {
    boost::asio::co_spawn(context,
        run_session(std::move(job), events, std::move(stop)),
        boost::asio::detached);
}

}
