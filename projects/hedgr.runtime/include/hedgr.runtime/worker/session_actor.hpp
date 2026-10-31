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
#ifndef HEDGR_RUNTIME_WORKER_SESSION_ACTOR_HPP
#define HEDGR_RUNTIME_WORKER_SESSION_ACTOR_HPP

#include "hedgr.model/command/channel.hpp"
#include "hedgr.model/domain/secret.hpp"
#include "hedgr.model/domain/session.hpp"
#include "hedgr.model/state/bucket_ring.hpp"
#include "hedgr.runtime/export.hpp"
#include "hedgr.runtime/worker/tick_feed.hpp"
#include "hedgr.runtime/worker/tick_sink.hpp"
#include "hedgr.session/transport/session_error.hpp"
#include "hedgr.session/transport/session_link.hpp"
#include "hedgr.session/transport/session_options.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace hedgr::runtime::worker {

/** @brief What one session's tick stream needs to connect. */
struct session_actor_options final {
    session::transport::session_options options;

    /*
     * The facts a signed-in session runs on. A caller that has already signed
     * in fills them in with what the instance answered, which is the shape the
     * tick feed was first written for. A caller that has not fills in a
     * credential instead, and the actor signs in and fills these from the
     * answer, so a session has one connection from its first byte.
     */
    std::string token;
    std::string principal;
    std::string tenant_id;
    std::string party_id;

    /**
     * @brief The credential to sign in with, when the actor signs itself in.
     *
     * A credential typed at the prompt travels here. It is wiped the moment
     * `authenticate` returns, so it lives for exactly one attempt.
     */
    model::domain::credential credential;

    /**
     * @brief A stored credential, still sealed, for the actor to open.
     *
     * Either this or @c credential is filled, never both. The key derivation
     * behind opening it is half a second of deliberate work, which is the
     * reason it belongs on the actor's thread rather than the frame loop's.
     */
    std::string sealed_credential;

    /**
     * @brief The master password @c sealed_credential may be opened with.
     *
     * Required when @c sealed_credential is set and ignored otherwise, so the
     * two cannot disagree about which secret is being used.
     */
    model::domain::secret_string master_password;

    /**
     * @brief Which attempt at this session this actor is.
     *
     * Every fact it posts carries this number, so the frame loop can tell a
     * stopped actor's last words from the actor that replaced it.
     */
    std::uint64_t attempt = 0;

    /** @brief The session this actor belongs to, which its reads name. */
    model::domain::session_key key;

    /**
     * @brief Where the answers to the reads asked of this actor go.
     *
     * Null only for a test that asks it no reads, because an actor with no
     * queue could only drop an answer.
     */
    model::command::event_queue* events = nullptr;

    /**
     * @brief The connection the actor runs on, when the caller owns one.
     *
     * Empty means the actor opens and owns its own. A test supplies a link it
     * scripts, which is how the loop's connect, its answers and its shutdown
     * are driven without a broker. The link must outlive the actor's thread.
     */
    std::shared_ptr<session::transport::session_link> link;

    /**
     * @brief How long the actor waits between liveness requests.
     *
     * Ten seconds by default (D4). The request is one anonymous read, so a
     * hundred idle sessions cost ten requests a second, and a stopped broker is
     * noticed within one interval plus the request timeout.
     */
    std::chrono::milliseconds ping_interval{10'000};

    /**
     * @brief How many liveness requests may fail before the connection is
     * rebuilt.
     *
     * Thirteen intervals of ten seconds outlasts the window cnats spends
     * reconnecting on its own, which is what makes a broker that is not coming
     * back something this process acts on rather than waits behind.
     */
    std::size_t failures_before_reconnect = 13;

    /**
     * @brief How many reads may wait before the newest is refused.
     *
     * The service admits one read per request id, so the queue is bounded by
     * the windows that read; this is the hard backstop for a caller that gets
     * that wrong, and it holds an answer event for the refusal so the caller's
     * in-flight mark is cleared rather than left set.
     */
    std::size_t max_queued_reads = 256;
};

/**
 * @brief One session's live prices, aggregated and handed to the frame loop.
 *
 * Runs on one connection and its own thread: the connection the caller gave
 * it, or one it opens itself when the caller gave none. The frame thread posts
 * the set of series it wants and drains closed buckets; the NATS dispatch
 * thread decodes ticks and closes buckets; this class never touches the model.
 * One instance exists per authenticated session for as long as the session is
 * live.
 *
 * A tick updates three current buckets, one per resolution. When a bucket's
 * interval passes, the previous bucket is published to the ring the frame loop
 * drains. Tick rate therefore costs the frame loop nothing; only bucket rate
 * crosses the thread boundary.
 *
 * The thread can sit inside a connect for the connect timeout, so the frame
 * thread asks it to stop with @c request_stop and joins it only once
 * @c finished says the connection is closed.
 */
class HEDGR_RUNTIME_EXPORT session_actor final : public tick_feed {
public:
    explicit session_actor(session_actor_options options);

    /** @brief Stops and joins the thread. Blocks for a connect in flight. */
    ~session_actor() override;

    session_actor(const session_actor&) = delete;
    session_actor& operator=(const session_actor&) = delete;

    /** @brief Starts the connection thread. */
    void start();

    /**
     * @brief Asks the thread to close the connection and exit.
     *
     * Returns at once. The thread closes the connection when its current
     * connect attempt, if any, returns, and @c finished then answers true.
     */
    void request_stop() override;

    /** @brief Whether the thread has closed the connection and is exiting. */
    [[nodiscard]] bool finished() const noexcept override {
        return finished_.load(std::memory_order_acquire);
    }

    /**
     * @brief The token the next connect resumes with. Thread safe.
     *
     * A refresh changes the credential and nothing else. The subscription
     * stays, and so do the open buckets.
     */
    void set_token(std::string token) override;

    /**
     * @brief Asks the session to act as another party. Thread safe.
     *
     * Queued as a command rather than applied here, because the transport is
     * the actor thread's: the frame loop leaves the choice and the answer comes
     * back as an event. A request that arrives before the sign-in has finished
     * waits for it, and a newer request replaces one not yet taken.
     */
    void select_party(std::string party_id) override;

    /**
     * @brief Watches one series, keeping its ticks.
     *
     * Thread safe. The series is watched until @c unwatch, whether or not the
     * connection is open at the time.
     */
    void watch(model::state::series_handle series,
        std::string canonical_uri) override;

    /** @brief Stops watching one series. Thread safe. */
    void unwatch(model::state::series_handle series) override;

    /** @brief Makes the given set the whole watch list. Thread safe. */
    void set_watch(std::span<const watch_target> wanted) override;

    /**
     * @brief Copies the closed buckets waiting for the frame loop.
     *
     * Called from the frame thread only.
     */
    std::size_t drain(std::vector<model::state::series_bucket>& out) override {
        return sink_->drain(out);
    }

    /**
     * @brief Queues one read for this session's connection. Thread safe.
     *
     * True when it was accepted, and then an answer event follows: the read's
     * own answer when the connection is signed in, or its failure when the
     * actor stops before running it or the queue is already full. False means
     * the actor is stopping, and then nothing was queued and nothing answered.
     */
    bool submit_read(read_request request) override;

    /** @brief Ticks bucketed since construction. */
    [[nodiscard]] std::size_t ticks() const noexcept {
        return sink_->ticks();
    }

    /** @brief Series currently watched. */
    [[nodiscard]] std::size_t watched() const {
        return sink_->watched();
    }

    /** @brief Buckets dropped because the frame loop was too far behind. */
    [[nodiscard]] std::size_t dropped() const noexcept {
        return sink_->dropped();
    }

private:
    /** @brief A watch change posted from the frame thread. */
    struct command final {
        bool add = true;
        model::state::series_handle series;
        std::string canonical_uri;
    };

    /**
     * @brief What the session runs on once it is signed in.
     *
     * Filled from the caller's options in the pre-authenticated path, and from
     * the instance's answer when the actor signs itself in. The actor thread
     * owns it; nothing else reads it.
     */
    struct facts final {
        std::string token;
        std::string principal;
        std::string tenant_id;
        std::string tenant_name;
        std::string party_id;
        std::string party_name;

        /** @brief How long the token lasts, as the instance stated it. */
        int lifetime_s = 0;
    };

    void run();
    void stop();
    void wait_for_retry();

    /** @brief Connects, signs in, and reports the outcome. False on failure. */
    bool sign_in(session::transport::session_link& link);

    /** @brief Runs @p reads on @p link and pushes each answer. */
    void answer_reads(std::vector<read_request>& reads,
        session::transport::session_link& link);

    /** @brief Pushes a failed answer for each of @p reads, then clears it. */
    void fail_reads(std::vector<read_request>& reads, std::string_view reason);

    /**
     * @brief Answers every read still queued with the stop's failure.
     *
     * A caller that submitted a read marked it in flight, and only an answer
     * clears that mark, so a stop that dropped the queue would leave the
     * caller waiting for an answer that is never coming.
     */
    void answer_stranded_reads();

    /** @brief Pushes @p fact when the actor has a queue to push it to. */
    void push(model::event::event fact) const;

    /** @brief Pushes a fact about this session that is not a read's answer. */
    void emit(model::event::event_kind kind) const;

    /** @brief Reports a move in the session's lifecycle. */
    void push_status(model::domain::session_status status) const;

    /** @brief Reports a refused sign-in, keeping the sentence and cause apart. */
    void push_failure(const session::transport::session_error& error) const;

    /** @brief Reports what the instance answered to a sign-in. */
    void push_authenticated(const session::transport::session_account& account)
        const;

    /** @brief Reports a replaced token. */
    void push_token_refreshed() const;

    /** @brief Reports the party the session now acts as. */
    void push_party_changed() const;

    /** @brief Reports a refused party change. */
    void push_party_refused(const session::transport::session_error& error) const;

    session_actor_options options_;
    std::shared_ptr<tick_sink> sink_;

    /** @brief The facts the connection runs on. The actor thread's. */
    facts facts_;

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<command> commands_;
    std::optional<std::vector<watch_target>> new_watch_;
    std::optional<std::string> new_token_;
    std::optional<std::string> new_party_;
    std::vector<read_request> reads_;
    bool stopping_ = false;

    std::atomic<bool> finished_{false};
};

}

#endif
