# SPDX-License-Identifier: GPL-3.0-or-later
# The examples, one extension per pattern, each its own project too, built against the installed SDK.
cmake_minimum_required(VERSION 3.20)
project(wxl-examples LANGUAGES CXX)

enable_testing() # each example adds a load test in the fake host

add_subdirectory(hello)     # the smallest extension: an override and a handler
add_subdirectory(events)    # overrides and handlers: priority, once, a filter, answers
add_subdirectory(observe)   # observable properties, object handles, the Game facade
add_subdirectory(settings)  # annotated settings and a coroutine
add_subdirectory(service)   # publish a service and use it
add_subdirectory(intercept) # interceptors on client functions
add_subdirectory(hook)      # a raw hook, with low_level
