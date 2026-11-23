# SPDX-FileCopyrightText: 2026 Elis
# SPDX-License-Identifier: GPL-3.0-or-later

function(hinkwise_add_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE hinkwise_domain Qt6::Test)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

# Application use cases and the dashboard snapshot link the application layer
# alone: no database, no Qt beyond the test framework itself.
function(hinkwise_add_application_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE hinkwise_application Qt6::Test)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

# Presentation tests compile the money-in, text-out helpers that the view
# models call. No QML engine is needed: the parsing is plain C++.
function(hinkwise_add_presentation_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE hinkwise_presentation Qt6::Test)
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
hinkwise_add_test(tst_household domain/tst_household.cpp)
hinkwise_add_test(tst_metrics domain/tst_metrics.cpp)
hinkwise_add_test(tst_rules domain/tst_rules.cpp)
hinkwise_add_test(tst_projection domain/tst_projection.cpp)
hinkwise_add_test(tst_rebalance domain/tst_rebalance.cpp)

hinkwise_add_application_test(tst_dashboard application/tst_dashboard.cpp)
hinkwise_add_application_test(tst_use_cases application/tst_use_cases.cpp)

hinkwise_add_presentation_test(tst_input_text presentation/tst_input_text.cpp)
hinkwise_add_presentation_test(tst_cashflow_series presentation/tst_cashflow_series.cpp)
hinkwise_add_presentation_test(tst_projection_series presentation/tst_projection_series.cpp)
hinkwise_add_presentation_test(tst_fi_track presentation/tst_fi_track.cpp)

# The setup wizard and the pages that read or write the household are tested
# together against a real database in a temporary directory: creating a
# household, recording a month and acting on the plan are the flows that have to
# work end to end for the application to be usable at all. The view models live
# in the executable, so the sources are compiled in here too.
add_executable(tst_setup_flow
    presentation/tst_setup_flow.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/accounts_view_model.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/app_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/cashflow_series.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/cashflow_view_model.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/dashboard_view_model.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/fi_track.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/input_text.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/plan_view_model.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/projection_series.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/review_view_model.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/wizard_view_model.cpp
)
target_link_libraries(tst_setup_flow PRIVATE
    hinkwise_application
    hinkwise_domain
    hinkwise_infrastructure
    Qt6::Core
    Qt6::Qml
    Qt6::Sql
    Qt6::Test
)
target_include_directories(tst_setup_flow PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/presentation
)
add_test(NAME tst_setup_flow COMMAND tst_setup_flow)

# The settings page changes the profile rather than the accounts, and writes the
# backup that is the household's only copy outside the database. It is tested
# against the same real database the setup flow uses.
add_executable(tst_settings_flow
    presentation/tst_settings_flow.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/app_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/input_text.cpp
    ${CMAKE_SOURCE_DIR}/src/presentation/settings_view_model.cpp
)
target_link_libraries(tst_settings_flow PRIVATE
    hinkwise_application
    hinkwise_domain
    hinkwise_infrastructure
    Qt6::Core
    Qt6::Qml
    Qt6::Sql
    Qt6::Test
)
target_include_directories(tst_settings_flow PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/presentation
)
add_test(NAME tst_settings_flow COMMAND tst_settings_flow)

# Serialisation belongs to the domain, so this one needs no database.
hinkwise_add_test(tst_serialisation infrastructure/tst_serialisation.cpp)

hinkwise_add_infrastructure_test(tst_backup infrastructure/tst_backup.cpp)
hinkwise_add_infrastructure_test(tst_migrator infrastructure/tst_migrator.cpp)
hinkwise_add_infrastructure_test(tst_repositories infrastructure/tst_repositories.cpp)
