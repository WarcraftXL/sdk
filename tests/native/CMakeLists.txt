# SPDX-License-Identifier: GPL-3.0-or-later
# ctest: the baseline check and the stock image kit.

find_program(WXL_LUAJIT NAMES luajit luajit-2.1 DOC "LuaJIT, for the settings, ABI and baseline tools")
if(WXL_LUAJIT)
    add_test(NAME baseline-tool COMMAND "${WXL_LUAJIT}" "${PROJECT_SOURCE_DIR}/tools/baseline/test_baseline.lua")
endif()

wxl_add_stock_test(stock-image SOURCES StockImageCheck.cpp)
