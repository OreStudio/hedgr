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
/*
 * miniaudio is a single header. The implementation is compiled here, in the one
 * translation unit that owns the device, and nowhere else, because the macro
 * expects exactly one definition in the program. This is the only file that
 * sees miniaudio at all: the header above keeps it out of every consumer of
 * hedgr.ui.
 */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "hedgr.ui/sound/pack.hpp"
#include "hedgr.ui/sound/player.hpp"
#include "ores.logging/make_logger.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

/** @brief This player's logger. A silent host is a warning, not an error. */
[[nodiscard]] auto& lg() {
    using namespace ores::logging;
    static auto instance = make_logger("hedgr.ui.sound.player");
    return instance;
}

/**
 * @brief The directory holding the running executable.
 *
 * Read from /proc/self/exe so the lookup works whatever directory the process
 * was started from, the way the icon and font lookups do. An empty path means
 * every candidate below is skipped.
 */
std::filesystem::path executable_directory() {
    std::error_code ec;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec)
        return {};
    return self.parent_path();
}

/**
 * @brief The first candidate that exists, or an empty path.
 */
std::filesystem::path first_existing(
    const std::vector<std::filesystem::path>& candidates) {
    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (candidate.empty())
            continue;
        if (std::filesystem::exists(candidate, ec) && !ec)
            return candidate;
    }
    return {};
}

/**
 * @brief Where the curated sound files live at run time.
 *
 * Installed beside the library under share/hedgr.ui, or, for a run straight out
 * of the build tree, in the source tree's assets directory. The same three
 * candidates the icon sheet uses, so a deployed bundle and a developer's run
 * resolve their assets the same way.
 */
std::filesystem::path sound_directory_path() {
    const auto exe_dir = executable_directory();
    std::vector<std::filesystem::path> candidates;
    if (!exe_dir.empty()) {
        candidates.push_back(
            exe_dir.parent_path() / "share" / "hedgr.ui" / "sounds");
        candidates.push_back(exe_dir / "sounds");
    }
    candidates.push_back("assets/sounds");
    return first_existing(candidates);
}

/**
 * @brief The miniaudio engine, opened once and closed with the player.
 *
 * Opening the device is the only thing here that can fail in an ordinary way,
 * and it does fail on a host with no sound card or no audio server. That is not
 * an error the application has to act on: the player simply never starts a
 * worker, so every later submission is dropped and nothing retries.
 */
class output_device final {
public:
    output_device() = default;

    ~output_device() {
        if (ready_)
            ma_engine_uninit(&engine_);
    }

    output_device(const output_device&) = delete;
    output_device& operator=(const output_device&) = delete;

    /** @brief Opens the default output, reporting whether there was one. */
    [[nodiscard]] bool open() {
        if (ready_)
            return true;
        ma_engine_config config = ma_engine_config_init();
        if (ma_engine_init(&config, &engine_) != MA_SUCCESS) {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "No audio output device; sounds will be silent";
            return false;
        }
        ready_ = true;
        BOOST_LOG_SEV(lg(), ores::logging::info)
            << "Audio output opened";
        return true;
    }

    /**
     * @brief Decodes and mixes @p file, returning at once.
     *
     * Reading and decoding happen on this call, which is why the caller is the
     * player's own thread and never the frame loop. The mixing itself is
     * miniaudio's audio thread.
     */
    void play(const std::filesystem::path& file) {
        if (!ready_)
            return;
        const std::string text = file.string();
        BOOST_LOG_SEV(lg(), ores::logging::debug) << "Playing " << text;
        if (ma_engine_play_sound(&engine_, text.c_str(), nullptr)
            != MA_SUCCESS) {
            BOOST_LOG_SEV(lg(), ores::logging::warn)
                << "Could not play " << text;
        }
    }

private:
    ma_engine engine_{};
    bool ready_ = false;
};

}

namespace hedgr::ui::sound {

/**
 * @brief The player's thread, queue and device.
 *
 * Everything that can block lives here rather than in the caller. The queue
 * carries a sound's file name and nothing else, which is the whole of what ADR
 * 8 allows to cross.
 */
struct player::impl final {
    sink_fn sink;
    std::unique_ptr<output_device> device;
    std::filesystem::path directory;

    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> pending;
    bool stop_requested = false;
    bool running = false;
    std::thread worker;

    /**
     * @brief Takes queued files until told to stop.
     *
     * The queue is drained before the stop is honoured, so a sound submitted on
     * the last frame before shutdown still plays. A stop with an empty queue
     * ends the thread at once.
     */
    void run() {
        while (true) {
            std::string file;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock,
                    [this] { return stop_requested || !pending.empty(); });
                if (pending.empty()) {
                    if (stop_requested)
                        return;
                    continue;
                }
                file = std::move(pending.front());
                pending.pop_front();
            }
            sink(file);
        }
    }
};

player::player() : impl_(std::make_unique<impl>()) {
    impl_->device = std::make_unique<output_device>();
    impl_->directory = sound_directory_path();

    /*
     * The default sink resolves the name against the directory the application
     * is running from. It holds the impl by pointer rather than a copy of the
     * device, because the device outlives every call and is closed only after
     * the worker has been joined.
     */
    auto* self = impl_.get();
    impl_->sink = [self](std::string_view file) {
        self->device->play(self->directory / std::filesystem::path(file));
    };
}

player::player(sink_fn sink) : impl_(std::make_unique<impl>()) {
    impl_->sink = std::move(sink);
}

player::~player() {
    stop();
}

void player::start() {
    auto& self = *impl_;
    if (self.running)
        return;

    /*
     * A device with no output leaves the player silent and no worker running,
     * which is a state and not a failure. Only the default sink has a device to
     * open; an injected sink is taken to be its own output.
     */
    if (self.device && !self.device->open())
        return;

    self.running = true;
    self.worker = std::thread([&self] { self.run(); });
}

std::size_t
player::submit(std::span<const hedgr::model::signal::signal> signals) {
    auto& self = *impl_;
    if (!self.running)
        return 0;

    std::size_t queued = 0;
    {
        std::lock_guard lock(self.mutex);
        for (const auto& value : signals) {
            const auto file = sound_for(value.name);
            if (!file)
                continue;
            self.pending.emplace_back(*file);
            ++queued;
        }
    }

    /*
     * The notify is outside the lock, so a worker that has just woken does not
     * wait on the caller to release it. Nothing here opens a file or touches the
     * device, which is what keeps a sound off the frame budget.
     */
    if (queued != 0)
        self.cv.notify_one();
    return queued;
}

void player::stop() {
    auto& self = *impl_;
    if (!self.running)
        return;

    {
        std::lock_guard lock(self.mutex);
        self.stop_requested = true;
    }
    self.cv.notify_all();
    if (self.worker.joinable())
        self.worker.join();
    self.running = false;
}

bool player::started() const noexcept {
    return impl_->running;
}

}
