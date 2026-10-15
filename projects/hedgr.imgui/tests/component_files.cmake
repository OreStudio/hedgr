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
# frame to hedgr.ui. There is no honest unit test for that, because a test which
# asserted the defaults of a config struct would pass with every function
# stubbed out. It is verified by running it: build-target.sh, then
# HEDGR_CAPTURE=<path> run.sh, and read the frame it writes.
set(files
)

# The headers are listed for the install and IDE targets.
set(HEADERS
)
