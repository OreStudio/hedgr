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
#include "ores.platform/environment/environment.hpp"
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {

/**
 * @brief Points the seeder's checkout scan at nowhere before any case runs.
 *
 * The scan reads a directory of OreStudio checkouts, which is a fact about the
 * host and not about this program: left on, the suite would seed and probe
 * whatever checkouts sit beside the repository and pass or fail with them. Set
 * here rather than by the test runner so a binary run by hand sees the same
 * deployed-package case, one instance from the process's own options.
 *
 * The cases that are about seeding a list of environments point the variable
 * at a fixture themselves.
 */
class hermetic_environment final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override {
        ores::platform::environment::environment::set_value(
            "HEDGR_ORESTUDIO_ROOT", "");
    }
};

}

CATCH_REGISTER_LISTENER(hermetic_environment)
