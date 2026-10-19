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

std::string join_key(std::string_view instance_id, std::string_view account_id) {
    std::string key;
    key.reserve(instance_id.size() + account_id.size() + 1);
    key.append(instance_id);
    key.push_back('\x1f');
    key.append(account_id);
    return key;
}

void grow(session_columns& columns, std::size_t count) {
    if (columns.status.size() >= count)
        return;
    columns.status.resize(count);
    columns.latency_us.resize(count);
    columns.instance_id.resize(count);
    columns.account_id.resize(count);
    columns.mode.resize(count);
    columns.opened_at_ns.resize(count);
    columns.last_error.resize(count);
    columns.tenant_name.resize(count);
    columns.tenant_id.resize(count);
    columns.party_name.resize(count);
    columns.party_id.resize(count);
    columns.token.resize(count);
    columns.parties.resize(count);
    columns.deployment.resize(count);
}

}

session_handle
session_table::insert(std::string instance_id, std::string account_id) {
    const auto index = layout_.allocate();
    grow(columns_, layout_.capacity());

    columns_.status[index] =
        static_cast<std::uint8_t>(domain::session_status::disconnected);
    columns_.latency_us[index] = 0;
    columns_.opened_at_ns[index] = 0;
    columns_.instance_id[index] = std::move(instance_id);
    columns_.account_id[index] = std::move(account_id);
    columns_.mode[index] =
        static_cast<std::uint8_t>(domain::session_mode::application);
    columns_.last_error[index].clear();
    columns_.tenant_name[index].clear();
    columns_.tenant_id[index].clear();
    columns_.party_name[index].clear();
    columns_.party_id[index].clear();
    columns_.token[index].clear();
    columns_.parties[index].clear();
    columns_.deployment[index] = domain::deployment_snapshot{};

    const session_handle h{index, layout_.generation[index]};
    by_key_.emplace(join_key(columns_.instance_id[index], columns_.account_id[index]), h);
    return h;
}

void session_table::remove(session_handle h) {
    if (!contains(h))
        return;
    by_key_.erase(join_key(columns_.instance_id[h.index], columns_.account_id[h.index]));

    // The token is a secret: drop it now rather than leave it in a free slot.
    columns_.token[h.index].clear();
    columns_.parties[h.index].clear();
    columns_.deployment[h.index] = domain::deployment_snapshot{};
    columns_.mode[h.index] =
        static_cast<std::uint8_t>(domain::session_mode::application);
    columns_.last_error[h.index].clear();
    columns_.tenant_name[h.index].clear();
    columns_.tenant_id[h.index].clear();
    columns_.party_name[h.index].clear();
    columns_.party_id[h.index].clear();
    columns_.instance_id[h.index].clear();
    columns_.account_id[h.index].clear();

    layout_.release(h.index);
}

std::optional<session_handle>
session_table::find(std::string_view instance_id, std::string_view account_id) const {
    const auto it = by_key_.find(join_key(instance_id, account_id));
    if (it == by_key_.end())
        return std::nullopt;
    return it->second;
}

domain::session_status session_table::status(session_handle h) const {
    return static_cast<domain::session_status>(columns_.status[h.index]);
}

void session_table::set_status(session_handle h, domain::session_status value) {
    columns_.status[h.index] = static_cast<std::uint8_t>(value);
}

std::int64_t session_table::latency_us(session_handle h) const {
    return columns_.latency_us[h.index];
}

void session_table::set_latency_us(session_handle h, std::int64_t value) {
    columns_.latency_us[h.index] = value;
}

std::int64_t session_table::opened_at_ns(session_handle h) const {
    return columns_.opened_at_ns[h.index];
}

void session_table::set_opened_at_ns(session_handle h, std::int64_t value) {
    columns_.opened_at_ns[h.index] = value;
}

std::string_view session_table::last_error(session_handle h) const {
    return columns_.last_error[h.index];
}

void session_table::set_last_error(session_handle h, std::string value) {
    columns_.last_error[h.index] = std::move(value);
}

void session_table::set_account_result(session_handle h, std::string tenant_name,
                                        std::string party_name,
                                        std::string party_id, std::string token) {
    columns_.tenant_name[h.index] = std::move(tenant_name);
    columns_.party_name[h.index] = std::move(party_name);
    columns_.party_id[h.index] = std::move(party_id);
    columns_.token[h.index] = std::move(token);
}

void session_table::set_parties(session_handle h, std::string parties) {
    columns_.parties[h.index] = std::move(parties);
}

std::string_view session_table::parties(session_handle h) const {
    return columns_.parties[h.index];
}

domain::session_mode session_table::mode(session_handle h) const {
    return static_cast<domain::session_mode>(columns_.mode[h.index]);
}

void session_table::set_mode(session_handle h, domain::session_mode value) {
    columns_.mode[h.index] = static_cast<std::uint8_t>(value);
}

std::string_view session_table::token(session_handle h) const {
    return columns_.token[h.index];
}

void session_table::set_token(session_handle h, std::string token) {
    columns_.token[h.index] = std::move(token);
}

const domain::deployment_snapshot&
session_table::deployment(session_handle h) const {
    return columns_.deployment[h.index];
}

void session_table::set_deployment(session_handle h,
    domain::deployment_snapshot value) {
    columns_.deployment[h.index] = std::move(value);
}

std::string_view session_table::instance_id(session_handle h) const {
    return columns_.instance_id[h.index];
}

std::string_view session_table::account_id(session_handle h) const {
    return columns_.account_id[h.index];
}

void session_table::set_tenant_id(session_handle h, std::string tenant_id) {
    columns_.tenant_id[h.index] = std::move(tenant_id);
}

std::string_view session_table::tenant_id(session_handle h) const {
    return columns_.tenant_id[h.index];
}

std::string_view session_table::tenant_name(session_handle h) const {
    return columns_.tenant_name[h.index];
}

std::string_view session_table::party_name(session_handle h) const {
    return columns_.party_name[h.index];
}

std::string_view session_table::party_id(session_handle h) const {
    return columns_.party_id[h.index];
}

void session_table::clear() noexcept {
    columns_ = {};
    layout_ = {};
    by_key_.clear();
}

}
