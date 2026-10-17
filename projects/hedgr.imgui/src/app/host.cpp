/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) 2025 Marco Craveiro <marco.craveiro@gmail.com>
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
#include "hedgr.imgui/app/host.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <boost/exception/diagnostic_information.hpp>
#include "ores.logging/lifecycle_manager.hpp"
#include "ores.utility/streaming/std_vector.hpp" // IWYU pragma: keep.
#include "hedgr.imgui/app/application.hpp"
#include "hedgr.imgui/config/parser.hpp"

namespace hedgr::imgui::app {

using namespace ores::logging;
using hedgr::imgui::config::parser;

int host::execute(const std::vector<std::string>& args,
    std::ostream& std_output, std::ostream& error_output) {
    /*
     * Create the configuration from command line options.
     */
    parser p;
    const auto ocfg(p.parse(args, std_output, error_output));

    /*
     * If we have no configuration, then there is nothing to do. This can only
     * happen if the user requested some valid options such as help or version;
     * any errors at the command line level are treated as exceptions, and all
     * other cases must result in a configuration object.
     */
    if (!ocfg)
        return EXIT_SUCCESS;

    /*
     * Since we have a configuration, we can now attempt to initialise the
     * logging subsystem.
     */
    const auto& cfg(*ocfg);

    /*
     * The file sink does not create its directory, so the directory is made
     * here. A host where it cannot be made still runs, with the console as its
     * only sink, rather than failing to start over a log file.
     */
    auto logging = cfg.logging;
    if (logging && !logging->output_directory.empty()) {
        std::error_code ignored;
        std::filesystem::create_directories(logging->output_directory, ignored);
        if (ignored) {
            logging->filename.clear();
            logging->output_directory.clear();
            logging->output_to_console = true;
        }
    }
    lifecycle_manager lm(logging);

    /*
     * Log the configuration and command line arguments.
     */
    if (logging && !logging->filename.empty()) {
        BOOST_LOG_SEV(lg(), info) << "Logging to "
                                  << (logging->output_directory
                                         / logging->filename);
    }
    BOOST_LOG_SEV(lg(), info) << "Command line arguments: " << args;
    BOOST_LOG_SEV(lg(), debug) << "Configuration: " << cfg;

    /*
     * Execute the application.
     */
    try {
        /*
         * HEDGR_CAPTURE asks for one finished frame to be written and the
         * process to leave. It is the only way to inspect a change without a
         * person watching, so it is read here and passed down.
         */
        std::filesystem::path capture;
        if (const char* path = std::getenv("HEDGR_CAPTURE");
            path != nullptr && *path != '\0')
            capture = path;

        /*
         * Which frame to write is part of the question a capture answers. A
         * surface that arrives from a worker -- anything behind the store's key
         * derivation, which is deliberately slow -- is not on screen by the
         * eighth frame, so a run that inspects one asks for a later frame
         * rather than the capture learning to wait for a condition it cannot
         * name.
         */
        int capture_frame = hedgr::imgui::app::application::default_capture_frame;
        if (const char* late = std::getenv("HEDGR_CAPTURE_FRAME");
            late != nullptr && *late != '\0')
            capture_frame = std::atoi(late);

        hedgr::imgui::app::application app(cfg, std::move(capture), capture_frame);
        app.run();
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        /*
         * Try to dump useful, but less user-friendly information by
         * interrogating the exception. Note that we must catch by
         * std::exception and cast the boost exception; if we were to catch
         * boost exception, we would not have access to the what() method and
         * thus could not provide the exception message to the console.
         */
        const auto *const be(dynamic_cast<const boost::exception* const>(&e));
        if (be == nullptr)
            throw;

        using boost::diagnostic_information;
        BOOST_LOG_SEV(lg(), error) << "Error: " << diagnostic_information(*be);
        BOOST_LOG_SEV(lg(), error) << "Failed to execute application.";
        throw;
    }
}

}
