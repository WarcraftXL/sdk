# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(wxl-file-sizes LANGUAGES CXX)

find_package(WarcraftXL 1.3 CONFIG REQUIRED)

wxl_add_extension(wxl-file-sizes
    SOURCES src/FileSizes.cpp
    MANIFEST wxl.json
    LINK WarcraftXL::offsets)
wxl_add_load_test(wxl-file-sizes ARGS --expect-log "wxl-file-sizes: hooked SFileGetFileSize")
