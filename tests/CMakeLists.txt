# SPDX-FileCopyrightText: 2026 Elis
# SPDX-License-Identifier: GPL-3.0-or-later

function(hinkwise_add_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE hinkwise_domain Qt6::Test)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

hinkwise_add_test(tst_money domain/tst_money.cpp)
hinkwise_add_test(tst_percentage domain/tst_percentage.cpp)
hinkwise_add_test(tst_year_month domain/tst_year_month.cpp)
hinkwise_add_test(tst_bucket domain/tst_bucket.cpp)
hinkwise_add_test(tst_targets domain/tst_targets.cpp)
hinkwise_add_test(tst_asset domain/tst_asset.cpp)
hinkwise_add_test(tst_liability domain/tst_liability.cpp)
hinkwise_add_test(tst_balance_sheet domain/tst_balance_sheet.cpp)
