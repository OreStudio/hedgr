# -*- mode: cmake; cmake-tab-width: 4; indent-tabs-mode: nil -*-
#
# Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
set(files
    "account_seed_tests.cpp"
    "catalogue_tests.cpp"
    "connection_options_tests.cpp"
    "deadline_wait_tests.cpp"
    "crm_worker_tests.cpp"
    "environment_seed_tests.cpp"
    "hermetic_environment.cpp"
    "market_feed_tests.cpp"
    "market_stream_tests.cpp"
    "read_service_tests.cpp"
    "reported_error_tests.cpp"
    "runtime_tests.cpp"
    "seed_store_tests.cpp"
    "store_lock_tests.cpp"
    "term_curve_tests.cpp"
)

# The headers are listed for the install and IDE targets.
set(HEADERS
)
