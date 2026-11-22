# SPDX-FileCopyrightText: 2026 Elis
# SPDX-License-Identifier: GPL-3.0-or-later

function(hinkwise_add_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE hinkwise_domain Qt6::Test)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

# The infrastructure tests open real database files and write real JSON, so they
# link the infrastructure layer rather than the model alone.
function(hinkwise_add_infrastructure_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE hinkwise_infrastructure Qt6::Test)
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
hinkwise_add_test(tst_metrics domain/tst_metrics.cpp)
hinkwise_add_test(tst_rules domain/tst_rules.cpp)
hinkwise_add_test(tst_projection domain/tst_projection.cpp)
hinkwise_add_test(tst_rebalance domain/tst_rebalance.cpp)

# Serialisation belongs to the domain, so this one needs no database.
hinkwise_add_test(tst_serialisation infrastructure/tst_serialisation.cpp)

hinkwise_add_infrastructure_test(tst_backup infrastructure/tst_backup.cpp)
hinkwise_add_infrastructure_test(tst_migrator infrastructure/tst_migrator.cpp)
hinkwise_add_infrastructure_test(tst_repositories infrastructure/tst_repositories.cpp)
