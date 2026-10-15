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
#include "hedgr.model/domain/label.hpp"
#include <format>

namespace hedgr::model::domain {

std::string_view to_string(label_colour value) noexcept {
    switch (value) {
    case label_colour::grey:
        return "grey";
    case label_colour::blue:
        return "blue";
    case label_colour::green:
        return "green";
    case label_colour::amber:
        return "amber";
    case label_colour::red:
        return "red";
    case label_colour::purple:
        return "purple";
    }
    return "unknown";
}

std::expected<label_colour, std::string>
label_colour_from_string(std::string_view text) {
    if (text == "grey")
        return label_colour::grey;
    if (text == "blue")
        return label_colour::blue;
    if (text == "green")
        return label_colour::green;
    if (text == "amber")
        return label_colour::amber;
    if (text == "red")
        return label_colour::red;
    if (text == "purple")
        return label_colour::purple;
    return std::unexpected(std::format("unknown label colour '{}'", text));
}

std::string_view to_string(label_severity value) noexcept {
    switch (value) {
    case label_severity::none:
        return "none";
    case label_severity::caution:
        return "caution";
    case label_severity::danger:
        return "danger";
    }
    return "unknown";
}

std::expected<label_severity, std::string>
label_severity_from_string(std::string_view text) {
    if (text == "none")
        return label_severity::none;
    if (text == "caution")
        return label_severity::caution;
    if (text == "danger")
        return label_severity::danger;
    return std::unexpected(std::format("unknown label severity '{}'", text));
}

label_severity combine(label_severity left, label_severity right) noexcept {
    /*
     * The enum is ordered by seriousness and the storage type is unsigned, so
     * the comparison is the whole implementation. Spelling the ordering out as
     * a table instead would be a second place for it to be wrong.
     */
    return static_cast<std::uint8_t>(left) >= static_cast<std::uint8_t>(right)
        ? left
        : right;
}

}
