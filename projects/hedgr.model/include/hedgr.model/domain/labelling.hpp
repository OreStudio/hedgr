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
#ifndef HEDGR_MODEL_DOMAIN_LABELLING_HPP
#define HEDGR_MODEL_DOMAIN_LABELLING_HPP

#include "hedgr.model/export.hpp"
#include <cstdint>
#include <string>

namespace hedgr::model::domain {

/**
 * @brief What kind of row a labelling attaches a label to.
 *
 * The owner id alone is not enough to resolve the row, because an instance and
 * an account are keyed in different tables. The discriminator is carried in the
 * junction rather than inferred, so a lookup never has to guess.
 */
enum class labelling_owner : std::uint8_t {
    /** @brief The owner id names an instance. */
    instance = 0,
    /** @brief The owner id names an account. */
    account = 1,
};

/**
 * @brief The junction that attaches one label to one owner.
 *
 * A label is an entity and an owner is any instance or account, so the
 * attachment is a pair of keys and nothing else. The pair is the identity: the
 * same label attached to the same owner twice is the same attachment, which is
 * what lets an attach be idempotent.
 */
struct labelling final {
    /** @brief Which table @c owner_id names. */
    labelling_owner owner = labelling_owner::instance;

    /** @brief Key of the instance or account being labelled. */
    std::string owner_id;

    /** @brief Key of the label attached to it. */
    std::string label_id;

    friend bool operator==(const labelling&, const labelling&) = default;
};

}

#endif
