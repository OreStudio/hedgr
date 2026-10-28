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
#include "hedgr.runtime/worker/market_stream.hpp"
#include "hedgr.session/market/tick_stream.hpp"
#include <chrono>
#include <utility>

namespace hedgr::runtime::worker {

market_stream::market_stream(market_stream_options options)
    : options_(std::move(options))
    , sink_(std::make_shared<tick_sink>()) {
}

market_stream::~market_stream() {
    stop();
}

void market_stream::start() {
    if (thread_.joinable())
        return;
    thread_ = std::thread([this]() { run(); });
}

void market_stream::request_stop() {
    {
        std::lock_guard lock(mu_);
        stopping_ = true;
    }
    cv_.notify_all();
}

void market_stream::stop() {
    request_stop();
    if (thread_.joinable())
        thread_.join();
}

void market_stream::set_token(std::string token) {
    {
        std::lock_guard lock(mu_);
        new_token_ = std::move(token);
    }
    cv_.notify_all();
}

void market_stream::watch(model::state::series_handle series,
    std::string canonical_uri) {
    {
        std::lock_guard lock(mu_);
        commands_.push_back(command{
            .add = true, .series = series,
            .canonical_uri = std::move(canonical_uri)});
    }
    cv_.notify_all();
}

void market_stream::unwatch(model::state::series_handle series) {
    {
        std::lock_guard lock(mu_);
        commands_.push_back(command{.add = false, .series = series});
    }
    cv_.notify_all();
}

bool market_stream::take_reconnected() {
    return reconnected_.exchange(false, std::memory_order_acq_rel);
}

void market_stream::wait_for_retry() {
    std::unique_lock lock(mu_);
    cv_.wait_for(lock, std::chrono::seconds{1},
        [this]() { return stopping_ || !commands_.empty(); });
}

void market_stream::run() {
    session::market::market_tick_stream stream(options_.options,
        options_.token, options_.principal, options_.tenant_id,
        options_.party_id);

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
        {
            std::unique_lock lock(mu_);
            /*
             * A live stream sleeps until there is something to do: a watch
             * change, a new token or a stop. Only a stream that is not yet
             * connected or subscribed wakes on the retry cadence, because
             * that is the state a connect or a subscribe has to be tried
             * again from. An idle session therefore costs no wakes.
             */
            const bool live = stream.connected() && stream.subscribed();
            const auto ready = [this]() {
                return stopping_ || !commands_.empty()
                    || new_token_.has_value();
            };
            if (live)
                cv_.wait(lock, ready);
            else
                cv_.wait_for(lock, std::chrono::milliseconds{100}, ready);
            if (stopping_)
                break;
            queued.swap(commands_);
            token.swap(new_token_);
        }

        if (token)
            stream.set_token(std::move(*token));
        for (auto& item : queued) {
            if (item.add)
                sink->watch(item.series, std::move(item.canonical_uri));
            else
                sink->unwatch(item.series);
        }

        if (!stream.connected()) {
            if (!stream.connect()) {
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

    stream.disconnect();
    finished_.store(true, std::memory_order_release);
}

}
