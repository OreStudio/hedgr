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
#include "hedgr.model/state/tables.hpp"
#include <utility>

namespace hedgr::model::state {

namespace {

void grow(window_columns& columns, std::size_t count) {
    if (columns.kind.size() >= count)
        return;
    columns.kind.resize(count);
    columns.open.resize(count);
    columns.session_index.resize(count, handle<session_tag>::invalid_index);
    columns.session_generation.resize(count, 0);
    columns.next_refresh_ns.resize(count);
    columns.id.resize(count);
    columns.cadence_ms.resize(count, 1000);
    columns.state.resize(count);
    columns.session_instance.resize(count);
    columns.session_account.resize(count);
    columns.series.resize(count);
}

}

window_handle window_table::insert(const domain::window_spec& spec) {
    const auto index = layout_.allocate();
    grow(columns_, layout_.capacity());

    columns_.kind[index] = static_cast<std::uint8_t>(spec.kind);
    columns_.open[index] = 1;
    columns_.session_index[index] = handle<session_tag>::invalid_index;
    columns_.session_generation[index] = 0;
    columns_.next_refresh_ns[index] = 0;
    columns_.id[index] = spec.id;
    columns_.cadence_ms[index] = spec.cadence.count();
    columns_.state[index] = spec.state;
    columns_.session_instance[index] =
        spec.session ? spec.session->instance_id : std::string{};
    columns_.session_account[index] =
        spec.session ? spec.session->account_id : std::string{};
    columns_.series[index].clear();

    const window_handle h{index, layout_.generation[index]};
    by_id_.emplace(columns_.id[index], h);
    return h;
}

void window_table::remove(window_handle h) {
    if (!contains(h))
        return;
    by_id_.erase(columns_.id[h.index]);
    columns_.id[h.index].clear();
    columns_.session_index[h.index] = handle<session_tag>::invalid_index;
    columns_.session_generation[h.index] = 0;
    columns_.session_instance[h.index].clear();
    columns_.session_account[h.index].clear();
    columns_.series[h.index].clear();
    layout_.release(h.index);
}

std::optional<window_handle> window_table::find(std::string_view id) const {
    const auto it = by_id_.find(std::string(id));
    if (it == by_id_.end())
        return std::nullopt;
    return it->second;
}

bool window_table::is_open(window_handle h) const {
    return columns_.open[h.index] != 0;
}

void window_table::request_focus(std::string_view id) {
    focus_request_ = std::string{id};
}

std::string window_table::take_focus_request() const {
    std::string result = std::move(focus_request_);
    focus_request_.clear();
    return result;
}

void window_table::set_open(window_handle h, bool value) {
    columns_.open[h.index] = value ? 1 : 0;
}

domain::window_kind window_table::kind(window_handle h) const {
    return static_cast<domain::window_kind>(columns_.kind[h.index]);
}

std::string_view window_table::id(window_handle h) const {
    return columns_.id[h.index];
}

const domain::window_state& window_table::state(window_handle h) const {
    return columns_.state[h.index];
}

domain::window_state& window_table::state(window_handle h) {
    return columns_.state[h.index];
}

std::int64_t window_table::next_refresh_ns(window_handle h) const {
    return columns_.next_refresh_ns[h.index];
}

void window_table::set_next_refresh_ns(window_handle h, std::int64_t value) {
    columns_.next_refresh_ns[h.index] = value;
}

void window_table::bind_session(window_handle h, session_handle session) {
    columns_.session_index[h.index] = session.index;
    columns_.session_generation[h.index] = session.generation;
}

void window_table::clear_session(window_handle h) {
    columns_.session_index[h.index] = handle<session_tag>::invalid_index;
    columns_.session_generation[h.index] = 0;
}

std::optional<session_handle> window_table::session_of(window_handle h) const {
    const session_handle session{
        columns_.session_index[h.index],
        columns_.session_generation[h.index],
    };
    if (!session.is_valid())
        return std::nullopt;
    return session;
}

std::span<const series_handle>
window_table::series(window_handle h) const noexcept {
    return columns_.series[h.index];
}

void window_table::set_series(window_handle h,
    std::span<const series_handle> handles) {
    columns_.series[h.index].assign(handles.begin(), handles.end());
}

domain::window_spec
window_table::spec(window_handle h, const session_table& sessions) const {
    domain::window_spec result;
    result.id = columns_.id[h.index];
    result.kind = static_cast<domain::window_kind>(columns_.kind[h.index]);
    result.cadence = std::chrono::milliseconds(columns_.cadence_ms[h.index]);
    result.state = columns_.state[h.index];
    if (const auto session = session_of(h); session && sessions.contains(*session)) {
        result.session = domain::session_key{
            std::string(sessions.instance_id(*session)),
            std::string(sessions.account_id(*session)),
        };
    } else if (!columns_.session_instance[h.index].empty()
        || !columns_.session_account[h.index].empty()) {
        /*
         * The window is unbound, because its session does not exist yet. The
         * pair it was saved against is still the answer to "which sign-in makes
         * this window live", so it is reported rather than dropped.
         */
        result.session = domain::session_key{
            columns_.session_instance[h.index],
            columns_.session_account[h.index],
        };
    }
    return result;
}

std::vector<domain::window_spec>
window_table::specs(const session_table& sessions) const {
    std::vector<domain::window_spec> result;
    result.reserve(layout_.live_count);
    for (std::uint32_t index = 0; index < layout_.alive.size(); ++index) {
        if (layout_.alive[index] == 0)
            continue;
        const window_handle h{index, layout_.generation[index]};
        result.push_back(spec(h, sessions));
    }
    return result;
}

void window_table::clear() noexcept {
    columns_ = {};
    layout_ = {};
    by_id_.clear();
}

}
