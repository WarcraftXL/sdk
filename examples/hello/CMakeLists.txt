# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(wxl-hello LANGUAGES CXX)

find_package(WarcraftXL 1.3 CONFIG REQUIRED)

wxl_add_extension(wxl-hello
    SOURCES Hello.cpp
    MANIFEST wxl.json
    LINK WarcraftXL::game)
wxl_add_load_test(wxl-hello ARGS --world 571 --expect-log "wxl-hello: wxl-hello 1.0.0 started"
                                     --expect-log "wxl-hello: map 571 after 3 ticks")
