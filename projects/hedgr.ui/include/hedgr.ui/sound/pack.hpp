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
#ifndef HEDGR_UI_SOUND_PACK_HPP
#define HEDGR_UI_SOUND_PACK_HPP

#include "hedgr.ui/export.hpp"
#include <optional>
#include <span>
#include <string_view>

namespace hedgr::ui::sound {

/**
 * @brief One name, or name prefix, and the file that answers it.
 *
 * A registration is against the name and not against the raise site, which is
 * what lets a listener be added without touching the code that raises. The name
 * may be a prefix, so a whole family such as every readiness change can share
 * one sound without a listener having to spell out each member.
 */
struct registration final {
    /** @brief The signal name, or a prefix of it, that this sound answers. */
    std::string_view signal;

    /** @brief The file, inside the sound directory, to play. */
    std::string_view file;
};

/**
 * @brief Every registered name to sound pair.
 *
 * The table is the whole sound configuration. Clearing it is how sound is put
 * down, and the player opens no output device when it is empty, so this
 * function is the one place sound is switched on or off.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::span<const registration>
registrations() noexcept;

/**
 * @brief The file for @p signal_name, or nothing when no registration matches.
 *
 * The longest matching prefix wins, so a specific name overrides a broader one
 * registered beside it. An exact name is a prefix of itself and is therefore
 * the ordinary case rather than a special one.
 */
[[nodiscard]] HEDGR_UI_EXPORT std::optional<std::string_view>
sound_for(std::string_view signal_name) noexcept;

}

#endif
