# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(wxl-tally LANGUAGES CXX)

find_package(WarcraftXL 1.3 CONFIG REQUIRED)

wxl_add_extension(wxl-tally SOURCES src/Publish.cpp src/Use.cpp)
target_include_directories(wxl-tally PRIVATE include)
wxl_add_load_test(wxl-tally ARGS --expect-log "wxl-tally: total 5")
