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
#ifndef HEDGR_UI_ICON_ICONS_HPP
#define HEDGR_UI_ICON_ICONS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>

/**
 * @brief The whole icon set, as semantic names.
 *
 * This is the only translation unit boundary the icon set has. No other file
 * in the component names a glyph, and no other file knows which catalogue the
 * artwork comes from. The set is therefore swappable by editing this table
 * alone, because the rest of the interface only ever says "the connect icon"
 * rather than "the plug icon".
 *
 * The artwork is Microsoft Fluent UI System Icons, the set ORE Studio uses. The
 * concept column names an ORE catalogue entry and the file it selects; the file
 * is ic_fluent_<concept>_regular.svg, and the concept carries the catalogue's
 * own size suffix, so the file is chosen at the size the catalogue asks for.
 * The guidelines document is
 * doc/knowledge/ui/icon_guidelines.org in the OreStudio tree, and every concept
 * below is one of its entries unless the comment says otherwise.
 *
 * The code points are in the Unicode private use area, starting at U+E000.
 * Nothing outside this file depends on the particular value. It is a key that
 * ties a constant to the matching cell of the rasterised sheet, and the sheet
 * is generated from this same table, so the two cannot drift.
 *
 * The string literals have static storage duration, which is what ImGui
 * requires of a glyph string. ImGui stores the pointer rather than copying the
 * text.
 */

namespace hedgr::ui::icon::detail {

/**
 * @brief Encodes a code point as a NUL terminated UTF-8 string.
 *
 * ImGui hands text to the platform layer as bytes while a font atlas is keyed
 * by code point, so the two have to agree on an encoding. Every icon string is
 * therefore stored as UTF-8 and this is the one place that turns a code point
 * into one.
 */
constexpr std::array<char, 5> utf8(char32_t codepoint) {
    std::array<char, 5> bytes{};
    if (codepoint < 0x80) {
        bytes[0] = static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        bytes[0] = static_cast<char>(0xc0 | (codepoint >> 6));
        bytes[1] = static_cast<char>(0x80 | (codepoint & 0x3f));
    } else if (codepoint < 0x10000) {
        bytes[0] = static_cast<char>(0xe0 | (codepoint >> 12));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        bytes[2] = static_cast<char>(0x80 | (codepoint & 0x3f));
    } else {
        bytes[0] = static_cast<char>(0xf0 | (codepoint >> 18));
        bytes[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
        bytes[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
        bytes[3] = static_cast<char>(0x80 | (codepoint & 0x3f));
    }
    return bytes;
}

/**
 * @brief One glyph's bytes, as a type that can be named by code point.
 *
 * A helper function cannot return a pointer with static storage duration, so
 * the storage is a class template's static member and each code point gets its
 * own instantiation.
 */
template <char32_t Codepoint>
struct glyph_storage final {
    static constexpr std::array<char, 5> bytes = utf8(Codepoint);
};

}

namespace hedgr::ui::icon {

/**
 * @brief The pixel size every glyph is drawn at in the atlas.
 *
 * A font bakes its text at one size and a rasterised glyph has no outline to
 * rescale, so the sheet is rasterised at one size as well. The catalogue's own
 * per-concept size still selects the asset file, which is what the size suffix
 * in the concept column carries.
 */
inline constexpr int glyph_pixel_size = 16;

/**
 * @brief One icon's place in the set, its glyph key and its source file.
 */
struct icon_source final {
    char32_t codepoint;
    // The catalogue's own name for the drawing, which is also the stem of the
    // asset file. Named for the catalogue rather than for the table macro
    // because concept is a keyword.
    const char* catalogue_name;
};

/*
 * The set, one row per semantic name. Each row is
 *
 *     X(hedgr name, code point, ORE catalogue concept)
 *
 * The catalogue concept is also the artwork file name, so a reader can check a
 * row against the asset directory and against the guidelines document. Where
 * Hedgr has a concept the catalogue does not carry, the comment says so and the
 * row keeps the nearest catalogue concept rather than inventing artwork.
 */
#define HEDGR_UI_ICON_TABLE(X)                                                 \
    /* A deployment Hedgr can talk to. The catalogue entry is Server Link. */          \
    X(instance, 0xE000, server_link_20)                                        \
    /* A principal that may be presented to an instance. The catalogue entry is Person. */       \
    X(account, 0xE001, person_20)                                              \
    /* One account presented to one instance. The catalogue entry is Plug Connected    \
     * Checkmark, whose usage is an authenticated session. */                  \
    X(session, 0xE002, plug_connected_checkmark_20)                            \
    /* A connection attempt still in flight. The catalogue has no pending      \
     * concept; Sync is the nearest, a transfer in progress. */                \
    X(session_pending, 0xE003, arrow_sync_20)                                  \
    /* A connection attempt that failed. The catalogue has no failed-session   \
     * concept; Plug Disconnected is the nearest connection state. */          \
    X(session_failed, 0xE004, plug_disconnected_20)                            \
    /* Open a session. The catalogue entry is Plug Connected. */                       \
    X(connect, 0xE005, plug_connected_20)                                      \
    /* Close a session. The catalogue entry is Plug Disconnected. */                   \
    X(disconnect, 0xE006, plug_disconnected_20)                                \
    /* Add a row. The catalogue entry is Add. */                                       \
    X(add, 0xE007, add_20)                                                     \
    /* Remove a row, the destructive action. The catalogue entry is Delete. */         \
    X(remove, 0xE008, delete_20)                                               \
    /* Edit a row in place. The catalogue entry is Edit. */                            \
    X(edit, 0xE009, edit_20)                                                   \
    /* Confirm and persist an edit. The catalogue entry is Save. */                    \
    X(save, 0xE00A, save_20)                                                   \
    /* Abandon an edit, the X of a form. The catalogue entry is Dismiss. */            \
    X(cancel, 0xE00B, dismiss_20)                                              \
    /* A timeline of one numeric series. The catalogue has no single-series    \
     * chart; Chart Multiple is the nearest data-visualisation concept. */     \
    X(chart, 0xE00C, chart_multiple_20)                                        \
    /* A paged table of one resource. The catalogue entry is Table. */                 \
    X(grid, 0xE00D, table_20)                                                  \
    /* A message for a person to read. The catalogue entry is Alert, the bell the      \
     * live asset set carries though the inventory does not list it. */        \
    X(notice, 0xE00E, alert_20)                                                \
    /* Fetch the current data again. The catalogue entry is Sync. */                   \
    X(refresh, 0xE00F, arrow_sync_20)                                          \
    /* Settings. The catalogue entry is Settings. */                                   \
    X(settings, 0xE010, settings_20)                                           \
    /* Something wants attention. The catalogue entry is Warning. */                   \
    X(warning, 0xE011, warning_20)                                             \
    /* Something did not work. The catalogue entry is Error Circle. */                 \
    X(error, 0xE012, error_circle_20)                                          \
    /* Something worth knowing, no urgency. The catalogue entry is Info. */            \
    X(information, 0xE013, info_20)                                            \
    /* Reveal a masked secret. The catalogue has no visibility concept; Lock   \
     * Open is the nearest, access granted. */                                 \
    X(eye, 0xE014, lock_open_20)                                               \
    /* Mask a revealed secret. The catalogue has no visibility concept; Lock   \
     * Closed is the nearest, access withdrawn. */                             \
    X(eye_off, 0xE015, lock_closed_20)                                         \
    /* Filter a list to what matches. The catalogue entry is Search. */                \
    X(search, 0xE016, search_20)                                               \
    /* The Instance Manager's overview. The catalogue has no dashboard         \
     * concept; Apps is the nearest, a grid of tiles. */                       \
    X(dashboard, 0xE017, apps_20)                                              \
    /* A point in time. The catalogue entry is Clock, the one 16px entry. */           \
    X(clock, 0xE018, clock_16)                                                 \
    /* The store is unlocked and the master password is held. The catalogue entry is   \
     * Lock Open. */                                                           \
    X(lock_open, 0xE019, lock_open_20)                                         \
    /* The store is locked. The catalogue entry is Lock Closed. */                     \
    X(lock_closed, 0xE01A, lock_closed_20)                                     \
    /* There is no master password yet. The catalogue has no first-run         \
     * concept; Warning is the nearest, an action wanted. */                   \
    X(lock_absent, 0xE01B, warning_20)                                         \
    /* Lock the store, dropping the held password. The catalogue entry is Lock         \
     * Closed. */                                                              \
    X(lock, 0xE01C, lock_closed_20)                                            \
    /* Enter the master password. The catalogue entry is Key Multiple, the key         \
     * concept the catalogue carries for password operations. */               \
    X(unlock, 0xE01D, key_multiple_20)                                         \
    /* A connection between two things. The catalogue has no standalone link   \
     * concept; Plug Connected is the nearest, a link established. */          \
    X(link, 0xE01E, plug_connected_20)                                         \
    /* Asking every instance whether it answers. The catalogue has no probe    \
     * concept; Sync is the nearest, a server round trip. */                   \
    X(probe, 0xE01F, arrow_sync_20)                                            \
    /* Readiness, the one good answer. The catalogue entry is Checkmark Circle. */     \
    X(ready, 0xE020, checkmark_circle_20)                                      \
    /* A folder in a tree. The catalogue entry is Folder. */                           \
    X(folder, 0xE021, folder_20)                                               \
    /* The instances tree's root. The catalogue has no open-folder asset, so   \
     * the closed Folder carries the root as well. */                          \
    X(folder_root, 0xE022, folder_20)                                          \
    /* Move a row into another folder. The catalogue has no move concept;      \
     * Arrow Upload is the nearest, sending something to a target. */          \
    X(move, 0xE023, arrow_upload_20)                                           \
    /* Create a second copy of a row. The catalogue entry is Copy. */                  \
    X(duplicate, 0xE024, copy_20)                                              \
    /* Sign out of a session. The catalogue has no sign-out concept; Plug      \
     * Disconnected is the nearest, a session ended. */                        \
    X(sign_out, 0xE025, plug_disconnected_20)                                  \
    /* Sign in to an instance. The catalogue has no sign-in concept; Plug      \
     * Connected Checkmark is the nearest, an authenticated session. */        \
    X(sign_in, 0xE026, plug_connected_checkmark_20)                            \
    /* A fact about a session. The catalogue has no fact concept; Info is the  \
     * nearest, an informational datum. */                                     \
    X(fact, 0xE027, info_20)                                                   \
    /* A panel the account's permissions decide. The catalogue has no panel    \
     * concept; Column Triple is the nearest, a layout. */                     \
    X(panel, 0xE028, column_triple_20)                                         \
    /* Jump to the first page. The catalogue has no paging concept; Arrow      \
     * Rotate Counterclockwise is the nearest, back to the start. */           \
    X(page_first, 0xE029, arrow_rotate_counterclockwise_20)                    \
    /* Go back one page. The catalogue entry is Arrow Left. */                         \
    X(page_previous, 0xE02A, arrow_left_20)                                    \
    /* Go forward one page. The catalogue has no paging concept; Arrow         \
     * Clockwise is the nearest, advance. */                                   \
    X(page_next, 0xE02B, arrow_clockwise_20)                                   \
    /* Jump to the last page. The catalogue has no paging concept, so this is  \
     * the same advance as Next; there is no distinct jump-to-end artwork. */  \
    X(page_last, 0xE02C, arrow_clockwise_20)                                   \
    /* A label attached to a record. The catalogue entry is Tag, used there     \
     * for badge definitions and version labels. */                            \
    X(label, 0xE02D, tag_20)

#define HEDGR_UI_ICON_DECLARE(name, codepoint, concept)                        \
    inline constexpr const char* name =                                        \
        detail::glyph_storage<codepoint>::bytes.data();

HEDGR_UI_ICON_TABLE(HEDGR_UI_ICON_DECLARE)

#undef HEDGR_UI_ICON_DECLARE

#define HEDGR_UI_ICON_SOURCE(name, codepoint, concept)                         \
    {codepoint, #concept},

/**
 * @brief The set in table order, for the code that registers the glyphs.
 *
 * The position of a row here is the cell the generator wrote for it, so this
 * array is also the mapping from a code point to a cell of the rasterised
 * sheet. The static assertion below pins the two halves together, because a
 * reordered or renumbered table that forgot the sheet cannot compile.
 */
inline constexpr icon_source icon_sources[] = {
    HEDGR_UI_ICON_TABLE(HEDGR_UI_ICON_SOURCE)
};

#undef HEDGR_UI_ICON_SOURCE

/** @brief How many icons the set carries. */
inline constexpr std::size_t icon_count = std::size(icon_sources);

namespace detail {

/**
 * @brief Whether the code points run consecutively from the first one.
 *
 * The generator writes one sheet cell per row and the loader finds cell n by
 * subtracting the first code point, so the two are only in step while this
 * holds. It is checked at compile time rather than at run time because the
 * failure it prevents is a sheet that is one cell out of step, which would
 * show as every icon wearing its neighbour's face.
 */
constexpr bool codepoints_are_consecutive() {
    char32_t expected = 0xE000;
    for (const auto& source : icon_sources) {
        if (source.codepoint != expected)
            return false;
        ++expected;
    }
    return true;
}

}

static_assert(detail::codepoints_are_consecutive(),
    "the icon table must number its code points consecutively from 0xE000, "
    "because the loader finds a glyph's sheet cell by subtracting the first "
    "code point");

}

#endif
