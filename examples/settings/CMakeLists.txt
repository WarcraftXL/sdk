# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(wxl-greeter LANGUAGES CXX)

find_package(WarcraftXL 1.3 CONFIG REQUIRED)

wxl_add_extension(wxl-greeter
    SOURCES src/Greeter.cpp
    SETTINGS src/GreeterSettings.hpp
    LINK WarcraftXL::game)
wxl_add_load_test(wxl-greeter)
