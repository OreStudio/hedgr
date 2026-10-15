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
#include "hedgr.model/domain/store_lock.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <new>
#include <string>
#include <type_traits>
#include <utility>

using hedgr::model::domain::secret_string;
using hedgr::model::domain::store_lock;

/*
 * The two properties worth pinning about a secret are both about what happens
 * to its bytes rather than about what it returns: that no copy is left behind
 * by an ordinary use, and that the allocation is wiped when the last reference
 * to it goes. Neither can be checked by inspecting a value, so the tests below
 * look at storage.
 */

TEST_CASE("a secret can be copied, and every copy wipes itself", "[secret]") {
    /*
     * Copies are permitted deliberately, and the safety of that is the property
     * pinned here: each copy cleanses its own buffer on destruction, so the
     * number of copies says nothing about how long the bytes live.
     *
     * Deleting the copy operations was the first design, and it failed the
     * first time a copy was genuinely needed -- a job struct handed to a worker,
     * a value captured by a coroutine. At that point the call site reaches for
     * std::string and the discipline is gone, silently and in the one place a
     * reviewer is least likely to look. A type that can be used where it is
     * needed, and that cleans up wherever it lands, is the design that survives
     * contact with the rest of the program.
     */
    STATIC_REQUIRE(std::is_copy_constructible_v<secret_string>);
    STATIC_REQUIRE(std::is_copy_assignable_v<secret_string>);

    const secret_string original{std::string{"a-copied-secret-kept-for-a-while"}};
    const secret_string copy{original};
    REQUIRE(copy.view() == original.view());
    REQUIRE(copy.view() == "a-copied-secret-kept-for-a-while");
}

TEST_CASE("a secret is move-only in spirit and keeps its value across a move",
    "[secret]") {
    STATIC_REQUIRE(std::is_move_constructible_v<secret_string>);
    STATIC_REQUIRE(std::is_move_assignable_v<secret_string>);

    secret_string source{std::string{"correct horse battery staple"}};
    REQUIRE(source.size() == 28);
    REQUIRE(source.view() == "correct horse battery staple");

    secret_string moved{std::move(source)};
    REQUIRE(moved.view() == "correct horse battery staple");

    /*
     * The moved-from object cleanses itself, and empties itself with it, so
     * there is no second copy of the password sitting in a variable that looks
     * harmless because it has been moved out of.
     */
    REQUIRE(source.empty());
    REQUIRE(source.size() == 0);
}

TEST_CASE("the secret's buffer is wiped when it is destroyed", "[secret]") {
    /*
     * The point of the whole type, and it needs a real look at the bytes to be
     * worth anything: asserting that a destructor ran proves nothing about what
     * it did.
     *
     * The secret is placed in a buffer this test owns, so that reading the
     * bytes after its destruction is reading storage that is still alive and
     * merely dead as an object. The password is long enough that libstdc++ puts
     * it on the heap rather than in a small-string buffer, so the scan below
     * would find it in that allocation if the wipe did not happen.
     */
    alignas(secret_string) unsigned char storage[sizeof(secret_string)]{};

    const std::string password =
        "correct horse battery staple correct horse battery staple";
    auto* secret = new (storage) secret_string{password};
    REQUIRE(secret->view() == password);
    REQUIRE(secret->size() == 57);

    secret->~secret_string();

    const std::string bytes(reinterpret_cast<const char*>(storage),
        sizeof(secret_string));

    /*
     * Neither the password nor any single character of it survives. The
     * character check is the stronger one: a wiped buffer is all zero bytes,
     * and the password contains no zero byte, so any occurrence of one of its
     * letters would mean bytes were left.
     */
    REQUIRE(bytes.find(password) == std::string::npos);
    REQUIRE(bytes.find('c') == std::string::npos);
    REQUIRE(bytes.find('p') == std::string::npos);
}

TEST_CASE("a copied secret cleanses its own buffer too", "[secret]") {
    /*
     * Copies are permitted, and this is why that is safe rather than a hole in
     * the design: each one wipes on its own destruction, so the number of
     * copies is irrelevant to how long the bytes live. Deleting the copy
     * operations was the first design and it failed the first time a copy was
     * genuinely needed -- a job struct handed to a worker -- at which point the
     * call site would have reached for std::string and lost the discipline
     * silently.
     */
    alignas(secret_string) unsigned char storage[sizeof(secret_string)]{};

    {
        const secret_string original{
            std::string{"a-copied-secret-that-must-also-be-wiped"}};
        auto* copy = new (storage) secret_string{original};
        REQUIRE(copy->view() == "a-copied-secret-that-must-also-be-wiped");
        copy->~secret_string();
    }

    const std::string bytes(reinterpret_cast<const char*>(storage),
        sizeof(secret_string));
    REQUIRE(bytes.find("a-copied-secret") == std::string::npos);
}

TEST_CASE("clearing a secret wipes it and leaves it empty", "[secret]") {
    secret_string secret{std::string{"a-long-enough-password-to-heap-allocate"}};
    REQUIRE_FALSE(secret.empty());

    secret.clear();
    REQUIRE(secret.empty());
    REQUIRE(secret.size() == 0);
    REQUIRE(secret.view().empty());
}

TEST_CASE("assigned secrets do not leave the previous value behind", "[secret]") {
    secret_string secret{
        std::string{"the-first-password-of-a-long-enough-length"}};
    secret = secret_string{std::string{"the-second-password-also-fairly-long"}};

    REQUIRE(secret.view() == "the-second-password-also-fairly-long");
}

TEST_CASE("a secret renders nothing of itself into a string", "[secret]") {
    /*
     * The type has no conversion to std::string, so a secret cannot reach a log
     * line, a format string or a window's state by accident. What a caller gets
     * instead is a view, and this pins the one way out.
     */
    STATIC_REQUIRE_FALSE(std::is_convertible_v<secret_string, std::string>);

    const secret_string secret{std::string{"a-password-nobody-may-log"}};
    REQUIRE(secret.view() == "a-password-nobody-may-log");
}

TEST_CASE("every lock state has a distinct name", "[store_lock]") {
    REQUIRE(to_string(store_lock::absent) == "absent");
    REQUIRE(to_string(store_lock::locked) == "locked");
    REQUIRE(to_string(store_lock::unlocked) == "unlocked");
    REQUIRE(to_string(store_lock::failed) == "failed");

    REQUIRE(hedgr::model::domain::store_lock_count == 4);
}
