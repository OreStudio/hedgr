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
#include "hedgr.imgui/config/parser.hpp"

#include <format>
#include <ostream>
#include <boost/program_options.hpp>
#include <boost/throw_exception.hpp>
#include "hedgr.imgui/config/parser_exception.hpp"
#include "ores.logging/logging_configuration.hpp"
#include "ores.utility/program_options/environment_mapper_factory.hpp"

namespace {

const std::string more_information("Try --help' for more information.");
const std::string product_version("Hedgr ImGui Client v0.0.1");
const std::string usage_error_msg("Usage error: ");

const std::string help_arg("help");
const std::string version_arg("version");

const std::string connect_host_arg("connect-host");
const std::string connect_port_arg("connect-port");
const std::string connect_identifier_arg("connect-identifier");

using boost::program_options::value;
using boost::program_options::variables_map;
using boost::program_options::options_description;

using ores::comms::net::client_options;
using hedgr::imgui::config::options;
using hedgr::imgui::config::parser_exception;

/**
 * @brief Creates the option descriptions.
 */
options_description make_options_description() {
    using ores::logging::logging_configuration;

    options_description god("General");
    god.add_options()
        ("help,h", "Display usage and exit.")
        ("version,v", "Output version information and exit.");

    const auto lod(logging_configuration::make_options_description(
            "hedgr.imgui.log"));

    options_description cod("Connection");
    cod.add_options()
        (connect_host_arg.c_str(), value<std::string>(),
            "Host to connect to (e.g., localhost)")
        (connect_port_arg.c_str(), value<std::string>(),
            "Port to connect to (e.g., 55555)")
        (connect_identifier_arg.c_str(), value<std::string>(),
            "Client identifier to use when connecting");

    options_description r;
    r.add(god).add(lod).add(cod);
    return r;
}

/**
 * @brief Print help text.
 */
void print_help(const options_description& od, std::ostream& info) {
    info << "Hedgr ImGui is a remote GUI client for ORE Studio."
         << std::endl
         << "It provides an ImGui-based interface for connecting to and interacting with ores.comms.service."
         << std::endl << std::endl
         << "Usage: hedgr.imgui [options]"
         << std::endl << std::endl
         << od << std::endl;
}

/**
 * @brief Print the program's version details.
 */
void version(std::ostream& info) {
    info << product_version << std::endl
         << "Copyright (C) 2025 Marco Craveiro." << std::endl
         << "License GPLv3: GNU GPL version 3 or later "
         << "<http://gnu.org/licenses/gpl.html>." << std::endl
         << "This is free software: you are free to change and redistribute it."
         << std::endl << "There is NO WARRANTY, to the extent permitted by law."
         << std::endl;
}

/**
 * @brief Reads the connection configuration from the variables map.
 */
std::optional<client_options>
read_connection_configuration(const variables_map& vm) {
    const bool has_host = vm.count(connect_host_arg) != 0;
    const bool has_port = vm.count(connect_port_arg) != 0;
    const bool has_identifier = vm.count(connect_identifier_arg) != 0;

    if (!has_host && !has_port && !has_identifier)
        return {};

    client_options r;
    r.host = has_host ? vm[connect_host_arg].as<std::string>() : "localhost";
    r.port = has_port ? static_cast<std::uint16_t>(std::stoi(vm[connect_port_arg].as<std::string>())) : 55555;
    r.client_identifier = has_identifier ? vm[connect_identifier_arg].as<std::string>() : "hedgr-imgui";
    r.verify_certificate = false; // FIXME: for now

    if (r.port < 1 || r.port > 65535) {
        BOOST_THROW_EXCEPTION(parser_exception(
                std::format("Port number {} is out of valid range (1-65535)!", r.port)));
    }

    return r;
}

/**
 * @brief Parses the arguments supplied in the command line and converts them
 * into a configuration object.
 */
std::optional<options>
parse_arguments(const std::vector<std::string>& arguments, std::ostream& info) {
    using ores::logging::logging_configuration;
    using ores::utility::program_options::environment_mapper_factory;

    const auto od(make_options_description());
    const auto name_mapper(environment_mapper_factory::make_mapper("HEDGR"));

    variables_map vm;
    boost::program_options::store(
        boost::program_options::command_line_parser(arguments).
        options(od).run(), vm);
    boost::program_options::store(
        boost::program_options::parse_environment(od, name_mapper), vm);

    const bool has_version(vm.count(version_arg) != 0);
    const bool has_help(vm.count(help_arg) != 0);

    if (has_help) {
        print_help(od, info);
        return {};
    }

    if (has_version) {
        version(info);
        return {};
    }

    options r;
    r.logging = logging_configuration::read_options(vm);
    r.connection = read_connection_configuration(vm);
    return r;
}

}

namespace hedgr::imgui::config {

std::optional<options>
parser::parse(const std::vector<std::string>& arguments,
    std::ostream& info, std::ostream& err) const {

    try {
        return parse_arguments(arguments, info);
    } catch(const parser_exception& e) {
        err << usage_error_msg << e.what() << std::endl
            << more_information << std::endl;
        BOOST_THROW_EXCEPTION(e);
    } catch (const boost::program_options::error& e) {
        err << usage_error_msg << e.what() << std::endl
            << more_information << std::endl;
        BOOST_THROW_EXCEPTION(parser_exception(e.what()));
    }
}

}
