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
#ifndef HEDGR_UI_SOUND_PLAYER_HPP
#define HEDGR_UI_SOUND_PLAYER_HPP

#include "hedgr.model/signal/signal.hpp"
#include "hedgr.ui/export.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace hedgr::ui::sound {

/**
 * @brief Plays a short file for a registered signal, on its own thread.
 *
 * The player owns the thread and the output device, and the only thing that
 * crosses from the frame loop is the name of a sound to start. Mixing audio on
 * the frame loop was rejected because a long sound or a slow disk shows up as a
 * stutter rather than as a missing sound, and the queue is how that is avoided:
 * submitting locks a queue and returns, and every blocking part, opening the
 * file, decoding it and mixing it, happens on the worker.
 *
 * The sink is injectable so a test can prove a name arrives and arrives off the
 * caller's thread without a sound card. The default sink is miniaudio, which is
 * the backend ADR 8 names.
 */
class HEDGR_UI_EXPORT player final {
public:
    /** @brief What plays one file. Called on the worker thread. */
    using sink_fn = std::function<void(std::string_view file)>;

    /** @brief Builds a player whose sink is miniaudio. */
    player();

    /**
     * @brief Builds a player that hands each file to @p sink.
     *
     * For a test, and for any future run that wants to route a sound somewhere
     * other than the default output device.
     */
    explicit player(sink_fn sink);

    ~player();

    player(const player&) = delete;
    player& operator=(const player&) = delete;
    player(player&&) = delete;
    player& operator=(player&&) = delete;

    /**
     * @brief Starts the worker, and the output device when there is one.
     *
     * Returns with @c started false and no worker when the device could not be
     * opened, which is the ordinary outcome on a host with no sound card. A
     * silent player is not an error: the application runs, every later
     * submission is dropped, and nothing retries the device.
     */
    void start();

    /**
     * @brief Queues every registered name in @p signals.
     *
     * Returns how many sounds were queued. A signal with no registration is
     * skipped, which is legal and costs nothing. This returns without touching
     * the device or the filesystem, so calling it from the frame loop cannot
     * cost a frame.
     */
    [[nodiscard]] std::size_t
    submit(std::span<const hedgr::model::signal::signal> signals);

    /**
     * @brief Stops the worker and closes the device, waiting for both.
     *
     * Idempotent, and called by the destructor, so a player that is destroyed
     * without an explicit stop still joins its thread before the device goes.
     */
    void stop();

    /** @brief Whether a worker is running and sounds can be queued. */
    [[nodiscard]] bool started() const noexcept;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

}

#endif
