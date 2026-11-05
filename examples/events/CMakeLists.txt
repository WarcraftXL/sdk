# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(wxl-events LANGUAGES CXX)

find_package(WarcraftXL 1.3 CONFIG REQUIRED)

wxl_add_extension(wxl-events SOURCES src/Events.cpp)
wxl_add_load_test(wxl-events ARGS --world 571
    --expect-log "wxl-events: first frame at 100 ms"
    --expect-log "wxl-events: entered map 571")
