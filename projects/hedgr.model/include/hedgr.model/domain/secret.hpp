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
#ifndef HEDGR_MODEL_DOMAIN_SECRET_HPP
#define HEDGR_MODEL_DOMAIN_SECRET_HPP

#include "hedgr.model/export.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace hedgr::model::domain {

/**
 * @brief A password, in a container that keeps it from ending up anywhere else.
 *
 * The class exists for one reason: a password must not be copied into a place
 * that outlives the operation that needed it. A plain @c std::string invites
 * exactly that, because every copy is cheap and none of them is visible at the
 * call site, and a stray copy left in an allocator's free list is as readable
 * as one that was never wiped.
 *
 * So every buffer it owns is wiped with @c OPENSSL_cleanse before it is
 * released, on destruction and on assignment alike. Cleansing is not the same
 * as clearing: @c clear() leaves the bytes in the allocation for whoever reads
 * that memory next, which is the whole problem.
 *
 * Copies are permitted, and each one wipes itself. Deleting them was the first
 * design and it was wrong: a design that forbids copying is only as good as the
 * places the compiler can see, and the moment one is needed -- a job struct
 * handed to a worker, a struct captured by a coroutine -- the call site
 * reaches for @c std::string and the discipline is gone, silently and at the
 * worst possible place. A copy that cleanses itself is safe wherever it lands,
 * and it is safe for a reason the reader can check locally.
 *
 * There is deliberately no conversion to @c std::string, no stream inserter and
 * no formatting support, so a secret cannot reach a log line by accident. The
 * only way out is @c view(), which is short-lived by construction: it is a
 * @c std::string_view and it dies with the statement that produced it unless
 * the caller goes out of its way to keep it, which is visible in the diff.
 */
class HEDGR_MODEL_EXPORT secret_string final {
public:
    /** @brief An empty secret. */
    secret_string() = default;

    /** @brief Takes ownership of @p value, which is copied once and then wiped. */
    explicit secret_string(std::string value);

    /**
     * @brief Wipes the buffer, so the bytes do not outlive this object.
     *
     * The single most important line in the class. Everything else here is
     * bookkeeping around it.
     */
    ~secret_string();

    /** @brief Duplicates the secret, and the duplicate wipes itself too. */
    secret_string(const secret_string& other);

    /** @brief Wipes what this held, then duplicates @p other. */
    secret_string& operator=(const secret_string& other);

    /** @brief Steals @p other's buffer, whose now-empty allocation it wipes. */
    secret_string(secret_string&& other) noexcept;

    /** @brief Wipes this buffer, then steals @p other's. */
    secret_string& operator=(secret_string&& other) noexcept;

    /**
     * @brief The secret as borrowed characters.
     *
     * The name says what the caller is doing: looking, not taking. The result
     * must not outlive this object, and it must not be stored.
     */
    [[nodiscard]] std::string_view view() const noexcept {
        return std::string_view{value_};
    }

    /** @brief How many characters the secret has. */
    [[nodiscard]] std::size_t size() const noexcept { return value_.size(); }

    /** @brief Whether there is no secret at all. */
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

    /** @brief Wipes and releases the buffer, leaving an empty secret. */
    void clear() noexcept;

private:
    /** @brief The characters, wiped by whoever releases the allocation. */
    std::string value_;
};

}

#endif
