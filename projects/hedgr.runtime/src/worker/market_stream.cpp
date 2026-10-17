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
#include "ores.logging/make_logger.hpp"
#include <array>
#include <chrono>

namespace {

[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.runtime.worker.market_stream");
    return instance;
}

constexpr std::array<hedgr::model::domain::market_level, 3> levels{
    hedgr::model::domain::market_level::fine,
    hedgr::model::domain::market_level::medium,
    hedgr::model::domain::market_level::coarse};

}

namespace hedgr::runtime::worker {

/**
 * @brief One watched series' current buckets.
 *
 * Shared with the subscription's callback so a watch that is removed while a
 * tick is in flight keeps its state alive until the callback returns. Only the
 * callback thread writes the buckets, under the mutex.
 */
struct market_stream::series_state final {
    series_state(std::string id, model::state::bucket_ring& ring,
        std::atomic<std::size_t>& ticks)
        : series_id(std::move(id))
        , ring(ring)
        , ticks(ticks) {
    }

    void on_tick(const session::market::tick& tick) {
        std::lock_guard lock(mu);
        for (std::size_t i = 0; i < levels.size(); ++i) {
            const auto level = levels[i];
            const auto interval = model::domain::market_level_ns(level);
            const auto bucket = tick.at_ns / interval;
            /*
             * A tick older than what this level already holds is late, and a
             * ring runs forward in time. It is counted and dropped rather than
             * rewriting a bucket the frame loop may already have drawn.
             */
            if (current[i].at_ns != 0 && tick.at_ns < current[i].at_ns)
                continue;
            if (bucket != bucket_start[i]) {
                if (current[i].at_ns != 0) {
                    ring.try_push(model::state::market_bucket{
                        .series_id = series_id,
                        .level = level,
                        .point = current[i]});
                }
                bucket_start[i] = bucket;
            }
            current[i] = model::domain::market_point{
                .at_ns = tick.at_ns, .value = tick.value};
        }
        ticks.fetch_add(1, std::memory_order_relaxed);
    }

    std::string series_id;
    model::state::bucket_ring& ring;
    std::atomic<std::size_t>& ticks;

    std::mutex mu;
    std::array<std::int64_t, 3> bucket_start{-1, -1, -1};
    std::array<model::domain::market_point, 3> current{};
};

market_stream::market_stream(market_stream_options options)
    : options_(std::move(options)) {
}

market_stream::~market_stream() {
    stop();
}

void market_stream::start() {
    if (thread_.joinable())
        return;
    thread_ = std::thread([this]() { run(); });
}

void market_stream::stop() {
    {
        std::lock_guard lock(mu_);
        if (stopping_)
            return;
        stopping_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void market_stream::watch(std::string series_id, std::string uri) {
    {
        std::lock_guard lock(mu_);
        commands_.push_back(command{
            .add = true, .series_id = std::move(series_id),
            .uri = std::move(uri)});
    }
    cv_.notify_all();
}

void market_stream::unwatch(std::string_view series_id) {
    {
        std::lock_guard lock(mu_);
        commands_.push_back(command{
            .add = false, .series_id = std::string{series_id}, .uri = {}});
    }
    cv_.notify_all();
}

std::size_t market_stream::drain(
    std::vector<model::state::market_bucket>& out) {
    return ring_.drain(out);
}

bool market_stream::take_reconnected() {
    return reconnected_.exchange(false, std::memory_order_acq_rel);
}

void market_stream::apply_commands(
    std::unordered_map<std::string, std::string>& desired, bool& changed) {
    std::vector<command> queued;
    {
        std::lock_guard lock(mu_);
        queued.swap(commands_);
    }
    for (auto& item : queued) {
        if (item.add) {
            const auto found = desired.find(item.series_id);
            if (found == desired.end() || found->second != item.uri) {
                desired.insert_or_assign(std::move(item.series_id),
                    std::move(item.uri));
                changed = true;
            }
        } else {
            if (desired.erase(item.series_id) > 0)
                changed = true;
        }
    }
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

    std::unordered_map<std::string, std::string> desired;
    std::unordered_map<std::string, std::shared_ptr<series_state>> series;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        retry_at;
    bool ever_connected = false;

    auto reconcile = [&]() {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = desired.begin(); it != desired.end(); ++it) {
            if (series.contains(it->first))
                continue;
            if (const auto retry = retry_at.find(it->first);
                retry != retry_at.end() && retry->second > now) {
                continue;
            }
            auto state = std::make_shared<series_state>(it->first, ring_,
                ticks_);
            if (stream.subscribe(it->first, it->second,
                    [state](const session::market::tick& tick) {
                        state->on_tick(tick);
                    })) {
                series.insert_or_assign(it->first, std::move(state));
            } else {
                retry_at[it->first] = now + std::chrono::seconds{10};
                BOOST_LOG_SEV(lg(), ores::logging::warn)
                    << "Could not watch series '" << it->first
                    << "' in party '" << options_.party_id << "': "
                    << stream.last_error();
            }
        }
        for (auto it = series.begin(); it != series.end();) {
            if (desired.contains(it->first)) {
                ++it;
                continue;
            }
            stream.unsubscribe(it->first);
            it = series.erase(it);
        }
        subscriptions_.store(stream.watched(), std::memory_order_relaxed);
    };

    while (true) {
        {
            std::unique_lock lock(mu_);
            cv_.wait_for(lock, std::chrono::milliseconds{100}, [this]() {
                return stopping_ || !commands_.empty();
            });
            if (stopping_)
                break;
        }

        bool changed = false;
        apply_commands(desired, changed);
        static_cast<void>(changed);

        if (!stream.connected()) {
            series.clear();
            subscriptions_.store(0, std::memory_order_relaxed);
            if (!stream.connect()) {
                wait_for_retry();
                continue;
            }
            if (ever_connected)
                reconnected_.store(true, std::memory_order_release);
            ever_connected = true;
        }

        reconcile();
    }

    stream.disconnect();
}

}
