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
#include "hedgr.ui/sound/pack.hpp"
#include "hedgr.model/signal/names.hpp"
#include <array>
#include <cstddef>

namespace {

namespace names = hedgr::model::signal::names;
using hedgr::ui::sound::registration;

/*
 * The mapping, and the whole of it.
 *
 * Ten entries, one per transition the application can raise, one for the
 * launch, one for the intro, one for the pointer moving along the bar, one for a window closing
 * and one per click, because those are the facts that exist rather than because
 * ten is a round number. Every one of them is raised on a change and never on
 * a poll, so a deployment that has been unreachable for an hour has raised its
 * lost signal once; the noise a sound makes is bounded by how often the estate
 * flaps and not by how often it is probed. That bound is the reason the table
 * is small: a sound is heard by everyone in the room, so a new entry has to be
 * worth a noise nobody asked to hear again.
 *
 * The files are a curated subset of Kenney's CC0 packs, committed under
 * assets/sounds with the licence text beside them. The names are Kenney's own
 * rather than renamed, so a file in the checkout can be traced to the pack it
 * came from without a second list.
 *
 * The launch and the close both reuse the one low bell. It is the only file
 * that reads as a thing beginning or ending rather than as a thing going
 * wrong, and at 132ms it is short enough that a burst of closes does not queue
 * a tail. The hover tick reuses the click, which is the pack's shortest file at
 * 103ms and the right shape for a pointer passing over an entry; a bell or a
 * chime on every entry would be heavier than the movement it marks, and a gain
 * column in this table would put mixing back into the mapping, which is exactly
 * where ADR 8 keeps it out.
 *
 * The intro is the one long file, 6s, because it is the launch's brand moment
 * and is raised once. It is a swell of Kenney's spaceEngineLow_000
 * with lowFrequency_explosion_000 as the impact (tools/make_intro_sound.sh),
 * and is loudest 3.023s in, which is when the intro's logo is brightest.
 */
constexpr std::array<registration, 10> table{{
    {names::application_started, "bong_001.wav"},
    {names::application_intro, "intro_swell.wav"},
    {names::instance_readiness_degraded, "error_001.wav"},
    {names::instance_readiness_recovered, "confirmation_001.wav"},
    {names::instance_readiness_bootstrapping, "bong_001.wav"},
    {names::instance_reachability_lost, "error_002.wav"},
    {names::instance_reachability_restored, "confirmation_002.wav"},
    {names::shortcut_activated, "click1.wav"},
    {names::shortcut_hovered, "click1.wav"},
    {names::window_closed, "bong_001.wav"},
}};

}

namespace hedgr::ui::sound {

std::span<const registration> registrations() noexcept {
    return table;
}

std::optional<std::string_view> sound_for(std::string_view signal_name) noexcept {
    std::optional<std::string_view> found;
    std::size_t matched = 0;

    /*
     * Longest prefix wins. A broad entry such as "instance.readiness" may sit
     * beside the specific names it covers, and the specific name has to take
     * the click, so the walk keeps the longest match rather than the first.
     */
    for (const auto& row : table) {
        if (row.signal.size() < matched)
            continue;
        if (signal_name.size() < row.signal.size())
            continue;
        if (signal_name.substr(0, row.signal.size()) != row.signal)
            continue;
        found = row.file;
        matched = row.signal.size();
    }

    return found;
}

}
