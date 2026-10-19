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
#include "hedgr.model/domain/secret.hpp"
#include <openssl/crypto.h>
#include <utility>

namespace hedgr::model::domain {

/**
 * @brief Wipes @p value's characters in place.
 *
 * A function of its own because three places in this file have to do it and
 * none of them may forget: a wipe that is only correct in the destructor is a
 * wipe that is missing wherever a secret was reassigned or cleared.
 *
 * @c OPENSSL_cleanse rather than @c fill, because the compiler is entitled to
 * remove a store to memory it can prove is dead, and a wiped buffer is by
 * definition dead. The call is opaque to the optimiser, so the wipe happens.
 */
namespace {

void wipe(std::string& value) noexcept {
    /*
     * By capacity, not size: a moved-from short string has size zero but keeps
     * the password in its in-object buffer after the first byte.
     */
    OPENSSL_cleanse(value.data(), value.capacity());
}

}

secret_string::secret_string(std::string value) : value_(std::move(value)) {
    wipe(value);
}

secret_string::~secret_string() {
    /*
     * Both the wipe and the release matter, and in this order: releasing first
     * would hand the allocation back to whoever allocates next with the
     * password still in it.
     */
    wipe(value_);
}

secret_string::secret_string(const secret_string& other) : value_(other.value_) {}

secret_string& secret_string::operator=(const secret_string& other) {
    if (this == &other)
        return *this;

    /*
     * The wipe before the copy is not tidiness: without it the bytes this held
     * are released by the assignment with the old secret still in them.
     */
    wipe(value_);
    value_ = other.value_;
    return *this;
}

secret_string::secret_string(secret_string&& other) noexcept
    : value_(std::move(other.value_)) {
    /*
     * The moved-from string is not required to be empty, and for short strings
     * it is not: libstdc++ leaves the bytes in the small-string buffer. It is
     * wiped and released here rather than left to the destructor, because a
     * moved-from secret is the copy that nobody thinks about.
     */
    other.clear();
}

secret_string& secret_string::operator=(secret_string&& other) noexcept {
    if (this == &other)
        return *this;

    wipe(value_);
    value_ = std::move(other.value_);
    other.clear();
    return *this;
}

void secret_string::clear() noexcept {
    wipe(value_);
    value_.clear();
    value_.shrink_to_fit();
}

}
