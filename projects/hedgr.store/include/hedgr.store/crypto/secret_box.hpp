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
#ifndef HEDGR_STORE_CRYPTO_SECRET_BOX_HPP
#define HEDGR_STORE_CRYPTO_SECRET_BOX_HPP

#include "hedgr.store/export.hpp"
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace hedgr::store::crypto {

/**
 * @brief The closed set of ways sealing or opening a box can fail.
 *
 * Two codes and no more, because the caller has exactly two questions to answer
 * and they have different consequences: was this the wrong secret, or is this
 * machine's cryptography broken? Folding either into the other would make a
 * failed login page read like a corrupted database, and a corrupted database
 * read like a typo.
 */
enum class box_errc : std::uint8_t {
    /**
     * @brief The password did not open the box.
     *
     * Also what a tampered or truncated ciphertext reports, deliberately. The
     * two are indistinguishable to this code by construction -- AES-GCM
     * authenticates the ciphertext with the same tag that proves the key -- and
     * telling them apart would mean guessing. An operator can act on the first
     * and cannot act on the second, so both are reported as the first and no
     * caller has to invent a story about which it was.
     */
    wrong_master_password,
    /**
     * @brief The cryptography itself could not run.
     *
     * Out of memory, a rejected parameter, a random source that will not
     * produce bytes, OpenSSL declining to build a context. Not the operator's
     * doing and not the secret's fault, which is why it is not folded into the
     * code above.
     */
    crypto_failed,
};

/** @brief Why a seal or an open failed. */
struct box_error final {
    /** @brief The classified failure. */
    box_errc code = box_errc::crypto_failed;

    /** @brief Human-readable detail. Never the secret, never the plaintext. */
    std::string message;
};

/** @brief The outcome of a seal or an open. */
template <typename T>
using box_result = std::expected<T, box_error>;

/** @brief The stable text of a failure code, for logs and test messages. */
[[nodiscard]] HEDGR_STORE_EXPORT std::string_view
to_string(box_errc value) noexcept;

/**
 * @brief Seals and opens a secret under a master password.
 *
 * AES-256-GCM, with the key derived from the password by PBKDF2-HMAC-SHA256 at
 * 600,000 iterations. A fresh 16-byte salt and a fresh 12-byte nonce are drawn
 * per seal, so the same password and the same plaintext produce different
 * ciphertext every time and no nonce is ever reused under one key.
 *
 * The stored form is base64 of @c salt(16) @c || @c iv(12) @c || @c tag(16)
 * @c || @c ciphertext, which is the format OreStudio's @c ores.security
 * @c crypto::encryption wrote before it was deleted. The two are byte for byte
 * compatible in both directions, and that is deliberate: a store sealed here
 * opens against a restored OreStudio class and a store sealed by the old Qt
 * client opens here, which is what makes the format a fact about the data
 * rather than an accident of this file.
 *
 * Hedgr owns this code rather than calling OreStudio's because the class that
 * held it was deleted there on 2026-09-26 as a dead surface, and ADR 6 records
 * that. Hedgr needs it, so it lives in the repo that needs it.
 *
 * The password is the only thing that opens a box. Nothing here stores one,
 * derives a verifier from one, or writes one down.
 */
class HEDGR_STORE_EXPORT secret_box final {
public:
    /** @brief Length of the PBKDF2 salt, in bytes. */
    static constexpr std::size_t salt_length = 16;

    /** @brief Length of the AES-GCM nonce, in bytes. */
    static constexpr std::size_t iv_length = 12;

    /** @brief Length of the AES-GCM authentication tag, in bytes. */
    static constexpr std::size_t tag_length = 16;

    /** @brief Length of the derived AES-256 key, in bytes. */
    static constexpr std::size_t key_length = 32;

    /**
     * @brief The PBKDF2 iteration count.
     *
     * OWASP's figure, and slow on purpose: the derivation is the only thing
     * standing between a copied database file and a dictionary attack on the
     * master password. It is why every call to this class happens off the frame
     * loop.
     */
    static constexpr int iterations = 600000;

    /**
     * @brief Seals @p plaintext under @p master_password.
     *
     * @return The base64 form described on the class, or @c crypto_failed when
     * the cryptography could not run. A password is not verified here and
     * cannot be: sealing always succeeds.
     */
    [[nodiscard]] static box_result<std::string>
    seal(std::string_view master_password, std::string_view plaintext);

    /**
     * @brief Opens a box sealed by @c seal, or by the class this replaces.
     *
     * @return The plaintext, or @c wrong_master_password when the password does
     * not open the box -- which is also what a damaged ciphertext reports, for
     * the reason given on @c box_errc -- or @c crypto_failed when the
     * cryptography could not run at all.
     */
    [[nodiscard]] static box_result<std::string>
    open(std::string_view master_password, std::string_view sealed);
};

}

#endif
