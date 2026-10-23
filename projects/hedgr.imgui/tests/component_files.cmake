# -*- mode: cmake; cmake-tab-width: 4; indent-tabs-mode: nil -*-
#
# Copyright (C) 2026 Marco Craveiro <marco.craveiro@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
# The executable is glue: it opens a GL context, calls the runtime and hands the
# frame to hedgr.ui, and that is verified by running it: build-target.sh, then
# HEDGR_CAPTURE=<path> run.sh, and read the frame it writes. What is tested here
# are the pure choices the glue makes, such as where the layout file lives.
set(files
    "layout_file_tests.cpp"
    "log_file_tests.cpp"
    "minimised_frames_tests.cpp"
)

# The headers are listed for the install and IDE targets.
set(HEADERS
)
