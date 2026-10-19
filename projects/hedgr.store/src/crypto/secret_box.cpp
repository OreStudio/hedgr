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
#include "hedgr.store/crypto/secret_box.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <vector>

namespace hedgr::store::crypto {

namespace {

/** @brief The failure that means the operator mistyped the password. */
box_error wrong_password() {
    return box_error{.code = box_errc::wrong_master_password,
        .message = "the master password does not open this store"};
}

/**
 * @brief The failure that means the cryptography, not the secret, went wrong.
 *
 * Every message here is a fixed sentence about an OpenSSL step and never
 * contains a password, a key or a plaintext. A diagnostic that quoted its input
 * would be a password in a log file.
 */
box_error crypto_failure(std::string_view what) {
    return box_error{.code = box_errc::crypto_failed,
        .message = std::string{what}};
}

/**
 * @brief An AES-GCM cipher context, freed however the function leaves.
 *
 * A deleter rather than a raw pointer so that the four failure paths below
 * cannot each forget the free. The key and the plaintext buffers are cleansed
 * separately, because this type owns the context and not the key material.
 */
using cipher_context = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

/**
 * @brief Overwrites @p bytes before their allocation is released.
 *
 * Called on the derived key and on every buffer that held plaintext. The key is
 * as dangerous as the password it came from: it opens every credential in the
 * store, and a key left in a freed allocation is a key a later allocation
 * inherits.
 */
void cleanse(std::vector<unsigned char>& bytes) noexcept {
    if (!bytes.empty())
        OPENSSL_cleanse(bytes.data(), bytes.size());
}

/**
 * @brief Derives the AES-256 key from @p password and @p salt.
 *
 * PBKDF2-HMAC-SHA256 through OpenSSL's own implementation. Written by hand it
 * would be a homemade construction guarding every credential in the store,
 * which is the one thing a key derivation must never be.
 */
box_result<std::vector<unsigned char>> derive_key(std::string_view password,
    const std::vector<unsigned char>& salt) {
    std::vector<unsigned char> key(secret_box::key_length);

    /*
     * The password is passed as a pointer and a length rather than as a
     * NUL-terminated string, so a password containing a NUL byte is derived in
     * full instead of being silently truncated at it.
     */
    const auto derived = PKCS5_PBKDF2_HMAC(password.data(),
        static_cast<int>(password.size()), salt.data(),
        static_cast<int>(salt.size()), secret_box::iterations, EVP_sha256(),
        static_cast<int>(key.size()), key.data());

    if (derived != 1) {
        cleanse(key);
        return std::unexpected(crypto_failure("the key derivation failed"));
    }
    return key;
}

/** @brief Base64, standard alphabet with padding, and no line breaks. */
box_result<std::string> base64_encode(const std::vector<unsigned char>& bytes) {
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return std::unexpected(crypto_failure("the sealed value is too large to encode"));

    /*
     * The cast is a conversion, not a truncation: the guard above has already
     * refused anything an int cannot hold, and EVP_EncodeBlock needs one.
     */
    const auto input_length = static_cast<int>(bytes.size());
    std::string encoded(
        static_cast<std::size_t>(4) * ((bytes.size() + 2) / 3), '\0');

    const auto written = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(encoded.data()), bytes.data(),
        input_length);
    if (written < 0)
        return std::unexpected(crypto_failure("the sealed value could not be encoded"));
    encoded.resize(static_cast<std::size_t>(written));
    return encoded;
}

/**
 * @brief Decodes base64, refusing anything that is not this alphabet.
 *
 * Padding is checked by hand rather than left to OpenSSL, because
 * @c EVP_DecodeBlock accepts a padded input and returns a count that includes
 * the padding, so a decoder that trusts it reads one to two bytes of garbage
 * past the end of the real ciphertext. Trimming here is what makes the
 * salt/iv/tag split below land on the right offsets.
 */
box_result<std::vector<unsigned char>> base64_decode(std::string_view text) {
    if (text.empty())
        return std::unexpected(wrong_password());

    if (text.size() % 4 != 0
        || !std::all_of(text.begin(), text.end(), [](char character) {
               return (character >= 'A' && character <= 'Z')
                   || (character >= 'a' && character <= 'z')
                   || (character >= '0' && character <= '9')
                   || character == '+' || character == '/' || character == '=';
           }))
        return std::unexpected(wrong_password());
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return std::unexpected(wrong_password());

    std::size_t padding = 0;
    if (text.ends_with("=="))
        padding = 2;
    else if (text.ends_with("="))
        padding = 1;

    std::string buffer{text};
    std::vector<unsigned char> decoded(buffer.size());
    const auto written = EVP_DecodeBlock(decoded.data(),
        reinterpret_cast<const unsigned char*>(buffer.data()),
        static_cast<int>(buffer.size()));
    if (written < 0)
        return std::unexpected(wrong_password());

    const auto decoded_length = static_cast<std::size_t>(written);
    if (decoded_length < padding)
        return std::unexpected(wrong_password());
    decoded.resize(decoded_length - padding);
    return decoded;
}

/**
 * @brief GCM-encrypts @p plaintext under @p key and @p iv.
 *
 * Returns the tag followed by the ciphertext, in that order, because that is
 * the order they appear in the stored value once the salt and the nonce are in
 * front of them: @c salt(16) @c || @c iv(12) @c || @c tag(16) @c ||
 * @c ciphertext. Producing them the other way round here and reversing them in
 * @c seal would be one place too many for the two to disagree, which is exactly
 * the bug this function's shape now prevents.
 *
 * The tag is what authenticates the ciphertext: a single flipped bit anywhere
 * in the sealed value makes the check fail on open.
 */
box_result<std::vector<unsigned char>> encrypt(const std::vector<unsigned char>& key,
    const std::vector<unsigned char>& iv, std::string_view plaintext) {
    cipher_context context(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!context)
        return std::unexpected(crypto_failure("a cipher context could not be created"));

    if (EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, key.data(),
            iv.data()) != 1)
        return std::unexpected(crypto_failure("the cipher could not be initialised"));

    /*
     * GCM is a stream mode, so the output is never longer than the input plus
     * one final block, and the final call places no bytes in practice.
     */
    std::vector<unsigned char> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    int produced = 0;
    int total = 0;

    if (EVP_EncryptUpdate(context.get(), ciphertext.data(), &produced,
            reinterpret_cast<const unsigned char*>(plaintext.data()),
            static_cast<int>(plaintext.size())) != 1)
        return std::unexpected(crypto_failure("the plaintext could not be encrypted"));
    total = produced;

    if (EVP_EncryptFinal_ex(context.get(), ciphertext.data() + total, &produced) != 1)
        return std::unexpected(crypto_failure("the plaintext could not be encrypted"));
    total += produced;
    ciphertext.resize(static_cast<std::size_t>(total));

    std::array<unsigned char, secret_box::tag_length> tag{};
    if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG,
            static_cast<int>(secret_box::tag_length), tag.data()) != 1)
        return std::unexpected(crypto_failure("the authentication tag could not be read"));

    std::vector<unsigned char> sealed;
    sealed.reserve(ciphertext.size() + tag.size());
    sealed.insert(sealed.end(), tag.begin(), tag.end());
    sealed.insert(sealed.end(), ciphertext.begin(), ciphertext.end());
    cleanse(ciphertext);
    return sealed;
}

/**
 * @brief GCM-decrypts @p ciphertext under @p key and @p iv, checking @p tag.
 *
 * The tag comparison is OpenSSL's, inside @c EVP_DecryptFinal_ex, and is
 * constant time. Comparing the bytes here instead would be a timing oracle on
 * the one secret the whole store rests on, so no comparison is written out.
 */
box_result<std::string> decrypt(const std::vector<unsigned char>& key,
    const std::vector<unsigned char>& iv,
    const std::vector<unsigned char>& ciphertext,
    const std::vector<unsigned char>& tag) {
    cipher_context context(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!context)
        return std::unexpected(crypto_failure("a cipher context could not be created"));

    if (EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, key.data(),
            iv.data()) != 1)
        return std::unexpected(crypto_failure("the cipher could not be initialised"));

    /*
     * The tag is given to OpenSSL before the ciphertext is decrypted, so the
     * final call below verifies it. There is no separate comparison to forget.
     */
    if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG,
            static_cast<int>(secret_box::tag_length),
            const_cast<unsigned char*>(tag.data())) != 1)
        return std::unexpected(crypto_failure("the authentication tag could not be set"));

    std::vector<unsigned char> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int produced = 0;
    int total = 0;

    if (EVP_DecryptUpdate(context.get(), plaintext.data(), &produced,
            ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
        cleanse(plaintext);
        return std::unexpected(crypto_failure("the ciphertext could not be decrypted"));
    }
    total = produced;

    if (EVP_DecryptFinal_ex(context.get(), plaintext.data() + total, &produced) != 1) {
        /*
         * The tag did not verify, which is what a wrong password looks like and
         * what a tampered ciphertext looks like. The two are reported with one
         * code because they are one observation. The buffer may hold the
         * unauthenticated plaintext at this point, so it is wiped before the
         * error is returned rather than left for the allocator.
         */
        cleanse(plaintext);
        return std::unexpected(wrong_password());
    }
    total += produced;

    std::string result(reinterpret_cast<const char*>(plaintext.data()),
        static_cast<std::size_t>(total));
    cleanse(plaintext);
    return result;
}

}

std::string_view to_string(box_errc value) noexcept {
    switch (value) {
    case box_errc::wrong_master_password:
        return "wrong_master_password";
    case box_errc::crypto_failed:
        return "crypto_failed";
    }
    return "unknown";
}

box_result<std::string> secret_box::seal(std::string_view master_password,
    std::string_view plaintext) {
    /*
     * An empty password is refused rather than honoured. Sealing under nothing
     * would produce a file that looks protected and is not, and the interface
     * has a create prompt precisely so an operator is never one empty field
     * away from that.
     */
    if (master_password.empty())
        return std::unexpected(crypto_failure("the master password is empty"));

    std::vector<unsigned char> salt(salt_length);
    std::vector<unsigned char> iv(iv_length);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1
        || RAND_bytes(iv.data(), static_cast<int>(iv.size())) != 1) {
        cleanse(salt);
        cleanse(iv);
        return std::unexpected(crypto_failure("random bytes could not be drawn"));
    }

    auto key = derive_key(master_password, salt);
    if (!key) {
        cleanse(salt);
        cleanse(iv);
        return std::unexpected(key.error());
    }

    auto ciphertext = encrypt(key.value(), iv, plaintext);
    cleanse(key.value());
    if (!ciphertext) {
        cleanse(salt);
        cleanse(iv);
        return std::unexpected(ciphertext.error());
    }

    std::vector<unsigned char> combined;
    combined.reserve(salt.size() + iv.size() + ciphertext.value().size());
    combined.insert(combined.end(), salt.begin(), salt.end());
    combined.insert(combined.end(), iv.begin(), iv.end());
    combined.insert(combined.end(), ciphertext.value().begin(), ciphertext.value().end());

    auto encoded = base64_encode(combined);

    cleanse(salt);
    cleanse(iv);
    cleanse(ciphertext.value());
    cleanse(combined);
    return encoded;
}

box_result<std::string> secret_box::open(std::string_view master_password,
    std::string_view sealed) {
    if (master_password.empty())
        return std::unexpected(wrong_password());

    auto combined = base64_decode(sealed);
    if (!combined) {
        /*
         * A structurally broken value is reported as a wrong password: it is
         * the same observation from here, an operator cannot act on the
         * difference, and a separate code would invite a caller to retry
         * something that will never succeed.
         */
        return std::unexpected(wrong_password());
    }

    const std::size_t header = salt_length + iv_length + tag_length;
    if (combined.value().size() < header)
        return std::unexpected(wrong_password());

    const auto& bytes = combined.value();
    const std::vector<unsigned char> salt(bytes.begin(), bytes.begin() + salt_length);
    const std::vector<unsigned char> iv(
        bytes.begin() + salt_length, bytes.begin() + salt_length + iv_length);
    const std::vector<unsigned char> tag(bytes.begin() + salt_length + iv_length,
        bytes.begin() + header);
    const std::vector<unsigned char> ciphertext(bytes.begin() + header, bytes.end());

    auto key = derive_key(master_password, salt);
    if (!key)
        return std::unexpected(key.error());

    /*
     * The plaintext is wiped on every path out of this function, including the
     * failure one, so the only copy of it that survives is the one the caller
     * asked for.
     */
    auto plaintext = decrypt(key.value(), iv, ciphertext, tag);
    cleanse(key.value());
    return plaintext;
}

}
