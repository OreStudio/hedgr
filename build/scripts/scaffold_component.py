#!/usr/bin/env python3
"""Generate the ORE Studio component skeleton for a Hedgr component.

A Hedgr component follows the single-module component layout used by
ores.utility: cmake root, include/<component>/, src/, tests/ and modeling/
directly under projects/<component>/. This script writes that skeleton so
the boilerplate is generated rather than hand-copied.

Usage:
    scaffold_component.py <component> [--subfacet NAME ...] [--link TARGET ...]
                          [--exe] [--no-tests]
"""

import argparse
import datetime
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
PROJECTS = REPO / "projects"
YEAR = datetime.date.today().year
HOLDER = "Marco Craveiro <marco.craveiro@gmail.com>"

CPP_HEADER = f"""/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * Copyright (C) {YEAR} {HOLDER}
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
"""

CMAKE_HEADER = f"""# -*- mode: cmake; cmake-tab-width: 4; indent-tabs-mode: nil -*-
#
# Copyright (C) {YEAR} {HOLDER}
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 3 of the License, or (at your option) any later
# version.
#
"""


def guard_for(component: str, relative: str) -> str:
    """Include guard for a header, matching the ORE Studio pattern.

    The component's dotted name plus the header's path below include/<component>/
    becomes ORES_COMPONENT_FACET_FILE_HPP. A file whose stem is the component or
    a component.subfacet name contributes no extra part, so the namespace
    documentation headers collapse to the names they document.
    """
    parts = component.split(".")
    rel = pathlib.PurePosixPath(relative)
    if rel.stem == component or rel.stem.startswith(component + "."):
        parts += list(rel.parent.parts)
    else:
        parts += list(rel.parent.parts) + [rel.stem]
    return re.sub(r"[^A-Z0-9]", "_", "_".join(parts).upper()) + "_HPP"


def macro_for(component: str) -> str:
    return re.sub(r"[^A-Z0-9]", "_", component.upper())


def write(path: pathlib.Path, content: str, force: bool = False) -> None:
    if path.exists() and not force:
        print(f"  keep   {path.relative_to(REPO)}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    existed = path.exists()
    path.write_text(content, encoding="utf-8")
    verb = "update" if existed else "create"
    print(f"  {verb} {path.relative_to(REPO)}")


def export_header(component: str) -> str:
    macro = macro_for(component)
    guard = guard_for(component, "export.hpp")
    return f"""{CPP_HEADER}#ifndef {guard}
#define {guard}

#include <boost/config.hpp>

#ifdef {macro}_LIBRARY
#    define {macro}_EXPORT BOOST_SYMBOL_EXPORT
#else
#    define {macro}_EXPORT BOOST_SYMBOL_IMPORT
#endif

#endif
"""


def namespace_header(component: str, subfacet: str | None, description: str) -> str:
    ns = "::".join(component.split("."))
    if subfacet:
        ns = f"{ns}::{subfacet}"
    relative = f"{subfacet}/{component}.{subfacet}.hpp" if subfacet else f"{component}.hpp"
    guard = guard_for(component, relative)
    return f"""{CPP_HEADER}#ifndef {guard}
#define {guard}

/**
 * @brief {description}
 */
namespace {ns} {{}}

#endif
"""


def root_cmake(component: str) -> str:
    return f"""{CMAKE_HEADER}add_subdirectory(${{CMAKE_CURRENT_SOURCE_DIR}}/src)
add_subdirectory(${{CMAKE_CURRENT_SOURCE_DIR}}/tests)
add_subdirectory(${{CMAKE_CURRENT_SOURCE_DIR}}/modeling)
"""


def src_cmake(component: str, links: list[str], is_exe: bool) -> str:
    macro = macro_for(component)
    if is_exe:
        return f"""{CMAKE_HEADER}set(name "{component}")
set(exe_binary_name ${{name}})
set(exe_target_name ${{name}}.exe)

include(${{CMAKE_CURRENT_SOURCE_DIR}}/component_files.cmake)

add_executable(${{exe_target_name}} ${{files}})
set_target_properties(${{exe_target_name}} PROPERTIES
    OUTPUT_NAME ${{exe_binary_name}})

target_include_directories(${{exe_target_name}} PRIVATE
    ${{CMAKE_SOURCE_DIR}}/projects/{component}/include)

target_link_libraries(${{exe_target_name}}
    PRIVATE
{''.join(f'        {link}\n' for link in links)})

install(TARGETS ${{exe_target_name}} RUNTIME DESTINATION bin)
"""
    return f"""{CMAKE_HEADER}set(name "{component}")
set(lib_binary_name ${{name}})
set(lib_target_name ${{name}}.lib)

include(${{CMAKE_CURRENT_SOURCE_DIR}}/component_files.cmake)

# A component with no sources yet must still configure.
if(NOT files)
    message(STATUS "{component}: no sources yet")
    return()
endif()

add_library(${{lib_target_name}} ${{files}})
target_compile_definitions(${{lib_target_name}} PRIVATE {macro}_LIBRARY)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${{lib_target_name}} PRIVATE
        -fvisibility=hidden -fvisibility-inlines-hidden)
endif()
set_target_properties(${{lib_target_name}} PROPERTIES
    OUTPUT_NAME ${{lib_binary_name}})
set_target_properties(${{lib_target_name}}
    PROPERTIES VERSION ${{PROJECT_VERSION}} SOVERSION ${{PROJECT_VERSION_MAJOR}})

target_include_directories(${{lib_target_name}} PUBLIC
    ${{CMAKE_SOURCE_DIR}}/projects/{component}/include)

target_link_libraries(${{lib_target_name}}
    PUBLIC
{''.join(f'        {link}\n' for link in links)})

install(TARGETS ${{lib_target_name}} LIBRARY DESTINATION lib)
"""


def component_files(component: str, sources: list[str], headers: list[str]) -> str:
    def lines(items, prefix=""):
        return "".join(f'    "{prefix}{item}"\n' for item in items)

    return f"""{CMAKE_HEADER}set(files
{lines(sources)})

# The headers are listed for the install and IDE targets.
set(HEADERS
{lines(headers, f"${{CMAKE_CURRENT_SOURCE_DIR}}/../include/")})
"""


def tests_cmake(component: str, links: list[str]) -> str:
    return f"""{CMAKE_HEADER}include(${{CMAKE_CURRENT_SOURCE_DIR}}/component_files.cmake)

# A component with no tests yet must still configure. Add a source to
# component_files.cmake to switch the test target on.
if(NOT files)
    message(STATUS "{component}: no tests defined")
    return()
endif()

set(name "{component}")

add_executable(${{name}}.tests ${{files}})

target_link_libraries(${{name}}.tests
    PRIVATE
{''.join(f'        {link}\n' for link in links)})

install(TARGETS ${{name}}.tests RUNTIME DESTINATION bin)

add_test(NAME ${{name}}.tests
    COMMAND ${{name}}.tests
    WORKING_DIRECTORY ${{CMAKE_RUNTIME_OUTPUT_DIRECTORY}})
"""


def modeling_cmake(component: str) -> str:
    return f"""{CMAKE_HEADER}# PlantUML is optional: the diagram target is only defined when the tool is
# present, so a machine without it still configures and builds.
find_program(PLANTUML_EXECUTABLE plantuml)
if(PLANTUML_EXECUTABLE)
    add_custom_target(generate_{component}_diagram
        COMMENT "Generating PlantUML diagram for {component}" VERBATIM
        COMMAND ${{PLANTUML_EXECUTABLE}} {component}.puml
        WORKING_DIRECTORY ${{CMAKE_CURRENT_SOURCE_DIR}}/)
endif()
"""


def overview(component: str, description: str) -> str:
    return f""":PROPERTIES:
:ID: {component}
:END:
#+title: {component}
#+description: {description}

* Summary

{description}

* Inputs

None yet.

* Outputs

None yet.

* Entry points

None yet.

* Dependencies

None yet.

* See also

- [[file:{component}.puml][Class diagram source]]
"""


def puml(component: str) -> str:
    return f"""@startuml {component}
' Component skeleton. Fill in as the component grows.
title {component}
@enduml
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("component", help="e.g. hedgr.model")
    parser.add_argument("--subfacet", action="append", default=[],
                        help="namespace subfolder, repeatable")
    parser.add_argument("--link", action="append", default=[],
                        help="CMake link target, repeatable")
    parser.add_argument("--exe", action="store_true", help="generate an executable, not a library")
    parser.add_argument("--description", default="", help="one-line namespace description")
    parser.add_argument("--no-tests", action="store_true")
    parser.add_argument("--refresh", action="store_true",
                        help="rewrite only the component_files.cmake lists from the tree")
    args = parser.parse_args()

    component = args.component
    if not component.startswith("hedgr."):
        print(f"error: component '{component}' does not start with 'hedgr.'", file=sys.stderr)
        return 1

    root = PROJECTS / component
    refresh = args.refresh
    if not refresh and root.exists() and any(root.iterdir()):
        print(f"error: {root.relative_to(REPO)} already exists and is not empty", file=sys.stderr)
        return 1
    if refresh and not root.exists():
        print(f"error: {root.relative_to(REPO)} does not exist", file=sys.stderr)
        return 1

    print(f"{'refreshing' if refresh else 'scaffolding'} {component}")
    src_files = sorted(
        str(p.relative_to(root / "src"))
        for p in (root / "src").rglob("*.cpp")
    ) if (root / "src").exists() else []
    headers = sorted(
        str(p.relative_to(root / "include"))
        for p in (root / "include").rglob("*.hpp")
    ) if (root / "include").exists() else []
    test_files = sorted(
        str(p.relative_to(root / "tests"))
        for p in (root / "tests").rglob("*.cpp")
    ) if (root / "tests").exists() else []

    if refresh:
        write(root / "src" / "component_files.cmake",
              component_files(component, src_files, headers), force=True)
        write(root / "tests" / "component_files.cmake",
              component_files(component, test_files, []), force=True)
        return 0

    include_root = root / "include" / component
    write(root / "CMakeLists.txt", root_cmake(component))
    write(include_root / "export.hpp", export_header(component))
    write(include_root / f"{component}.hpp",
          namespace_header(component, None, args.description or f"{component} namespace."))
    for subfacet in args.subfacet:
        write(include_root / subfacet / f"{component}.{subfacet}.hpp",
              namespace_header(component, subfacet,
                               f"{args.description or component} :: {subfacet}."))

    write(root / "src" / "CMakeLists.txt", src_cmake(component, args.link, args.exe))
    write(root / "src" / "component_files.cmake", component_files(component, src_files, headers))

    write(root / "modeling" / "CMakeLists.txt", modeling_cmake(component))
    write(root / "modeling" / "component_overview.org",
          overview(component, args.description or f"{component} component."))
    write(root / "modeling" / f"{component}.puml", puml(component))

    if not args.no_tests:
        test_links = list(args.link)
        if not args.exe:
            test_links = [f"{component}.lib"] + test_links
        write(root / "tests" / "component_files.cmake", component_files(component, [], []))
        write(root / "tests" / "CMakeLists.txt",
              tests_cmake(component,
                          test_links + ["Catch2::Catch2WithMain", "Threads::Threads"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
