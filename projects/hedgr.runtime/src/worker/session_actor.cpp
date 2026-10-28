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
#include "hedgr.session/transport/session_link.hpp"
#include <chrono>
#include <utility>

namespace hedgr::runtime::worker {

namespace {

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

bool session_actor::take_reconnected() {
    return reconnected_.exchange(false, std::memory_order_acq_rel);
}

bool session_actor::submit_read(read_request request) {
    {
        std::lock_guard lock(mu_);
        if (stopping_)
            return false;
        reads_.push_back(std::move(request));
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

void session_actor::push(model::event::event fact) const {
    if (options_.events != nullptr)
        options_.events->push(std::move(fact));
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
    session::market::market_tick_stream stream(link, options_.token,
        options_.principal, options_.tenant_id, options_.party_id, codec);

    /*
     * The handler owns the sink with this object, so a handler still running
     * when the stream closes writes into memory that is alive.
     */
    const auto sink = sink_;
    const auto on_tick = [sink](const session::market::tick& tick) {
        sink->on_tick(tick);
    };
    bool ever_connected = false;

    while (true) {
        std::vector<command> queued;
        std::optional<std::string> token;
        std::optional<std::vector<watch_target>> watch;
        std::vector<read_request> reads;
        {
            std::unique_lock lock(mu_);
            /*
             * A live stream sleeps until there is something to do: a watch
             * change, a new token, a read or a stop. Only a stream that is not
             * yet connected or subscribed wakes on the retry cadence, because
             * that is the state a connect or a subscribe has to be tried again
             * from. An idle session therefore costs no wakes.
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
            const bool live = stream.connected() && stream.subscribed();
            const auto ready = [this]() {
                return stopping_ || !commands_.empty()
                    || new_watch_.has_value() || new_token_.has_value()
                    || !reads_.empty();
            };
            if (live)
                cv_.wait(lock, ready);
            else
                cv_.wait_for(lock, std::chrono::milliseconds{100}, ready);
            if (stopping_)
                break;
            queued.swap(commands_);
            watch.swap(new_watch_);
            token.swap(new_token_);
            if (stream.connected())
                reads.swap(reads_);
        }

        if (token)
            stream.set_token(std::move(*token));
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

        if (!stream.connected()) {
            if (!stream.connect()) {
                std::vector<read_request> refused;
                {
                    std::lock_guard lock(mu_);
                    refused.swap(reads_);
                }
                fail_reads(refused, stream.last_error());
                wait_for_retry();
                continue;
            }
            if (ever_connected)
                reconnected_.store(true, std::memory_order_release);
            ever_connected = true;
        }

        if (!stream.subscribed() && !stream.subscribe(on_tick))
            wait_for_retry();
    }

    /*
     * A read that is still queued has a caller waiting on its event, so it is
     * answered with its failure when the actor stops rather than left to
     * strand whatever mark the caller set when it submitted it.
     */
    {
        std::vector<read_request> stranded;
        {
            std::lock_guard lock(mu_);
            stranded.swap(reads_);
        }
        fail_reads(stranded, stopped_read_message);
    }

    stream.disconnect();
    finished_.store(true, std::memory_order_release);
}

}
