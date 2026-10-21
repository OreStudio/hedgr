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
#ifndef HEDGR_RUNTIME_SERVICE_PENDING_READS_HPP
#define HEDGR_RUNTIME_SERVICE_PENDING_READS_HPP

#include "hedgr.model/event/event.hpp"
#include "hedgr.model/state/tenant_key.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace hedgr::runtime::service {

/** @brief The reads that are marked while they run and have no table row to say so. */
enum class request_kind : std::uint8_t {
    catalogue,
    points,
    deployment,
    image,
    currency_image_map,
    history,
};

/**
 * @brief One read in flight, by what it is and what it is about.
 *
 * The subject is built by the factory of its kind and nowhere else, so two
 * starters cannot spell one session or one image two ways and mark it twice.
 * The answer to a read names the same id, so the mark is cleared from the
 * answer and not by a handler that has to remember to.
 */
struct request_id final {
    request_kind kind;
    std::string subject;

    [[nodiscard]] static request_id catalogue(std::string_view instance_id,
        std::string_view account_id) {
        return {request_kind::catalogue, pair(instance_id, account_id)};
    }

    [[nodiscard]] static request_id points(std::string_view instance_id,
        std::string_view account_id, std::string_view series_id) {
        return {request_kind::points,
            model::state::make_tenant_key(pair(instance_id, account_id), series_id)};
    }

    [[nodiscard]] static request_id history(std::string_view instance_id,
        std::string_view account_id, std::string_view series_id) {
        return {request_kind::history,
            model::state::make_tenant_key(pair(instance_id, account_id), series_id)};
    }

    [[nodiscard]] static request_id deployment(std::string_view instance_id,
        std::string_view account_id) {
        return {request_kind::deployment, pair(instance_id, account_id)};
    }

    [[nodiscard]] static request_id image(std::string_view tenant_id,
        std::string_view image_id) {
        return {request_kind::image,
            model::state::make_tenant_key(tenant_id, image_id)};
    }

    [[nodiscard]] static request_id currency_image_map(std::string_view tenant_id) {
        return {request_kind::currency_image_map, std::string{tenant_id}};
    }

    /**
     * @brief The read an event answers, or nothing when it answers none.
     *
     * Every event that ends one of the reads above, whether it carries the
     * result or the reason it failed, names the read it ends.
     */
    [[nodiscard]] static std::optional<request_id>
    answered_by(const model::event::event& ev) {
        using kind = model::event::event_kind;
        switch (ev.kind) {
        case kind::series_catalogue:
            return catalogue(ev.instance_id, ev.account_id);
        case kind::series_points:
            return points(ev.instance_id, ev.account_id, ev.series_id);
        case kind::series_history:
            return history(ev.instance_id, ev.account_id, ev.series_id);
        case kind::deployment_snapshot:
            return deployment(ev.instance_id, ev.account_id);
        case kind::image_loaded:
            return image(ev.tenant_id, ev.image_id);
        case kind::currency_image_map:
            return currency_image_map(ev.tenant_id);
        default:
            return std::nullopt;
        }
    }

private:
    [[nodiscard]] static std::string pair(std::string_view instance_id,
        std::string_view account_id) {
        return model::state::make_tenant_key(instance_id, account_id);
    }
};

/**
 * @brief Every read in flight, in one place.
 *
 * A read is marked when it starts and unmarked when its answer is applied, and
 * dropping the answer for any reason would leave the read refused for the rest
 * of the process. One registry means one place that clears, and a kind added to
 * @c request_kind is cleared with the rest.
 */
class pending_reads final {
public:
    /** @brief Marks @p id and returns whether it was free. */
    bool mark(const request_id& id) {
        return marks_[index(id.kind)].insert(id.subject).second;
    }

    [[nodiscard]] bool contains(const request_id& id) const {
        return marks_[index(id.kind)].contains(id.subject);
    }

    void clear(const request_id& id) {
        marks_[index(id.kind)].erase(id.subject);
    }

    void clear_all() {
        for (auto& subjects : marks_)
            subjects.clear();
    }

    /** @brief The subjects marked under @p kind, for a screen that draws them. */
    [[nodiscard]] const std::unordered_set<std::string>&
    subjects(request_kind kind) const noexcept {
        return marks_[index(kind)];
    }

    [[nodiscard]] bool empty() const noexcept {
        for (const auto& subjects : marks_) {
            if (!subjects.empty())
                return false;
        }
        return true;
    }

private:
    [[nodiscard]] static constexpr std::size_t index(request_kind kind) noexcept {
        return static_cast<std::size_t>(kind);
    }

    std::array<std::unordered_set<std::string>, 6> marks_;
};

}

#endif
