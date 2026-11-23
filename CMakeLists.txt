cmake_minimum_required(VERSION 3.21)

project(hinkwise
    VERSION 0.1.0
    DESCRIPTION "Personal finance planning around the four-bucket model"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Sql Test)

qt_standard_project_setup(REQUIRES 6.8)

# Domain layer: plain C++20, no Qt dependency, so it stays fast to test and
# impossible to entangle with the UI.
add_library(hinkwise_domain STATIC
    src/domain/asset.cpp
    src/domain/balance_sheet.cpp
    src/domain/bucket.cpp
    src/domain/household.cpp
    src/domain/insight.cpp
    src/domain/jurisdiction.cpp
    src/domain/liability.cpp
    src/domain/metrics.cpp
    src/domain/phase.cpp
    src/domain/projection.cpp
    src/domain/rebalance.cpp
    src/domain/rules.cpp
    src/domain/serialisation.cpp
    src/domain/targets.cpp
    src/domain/year_month.cpp
)
target_include_directories(hinkwise_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(hinkwise_domain PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hinkwise_domain PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Application layer: the use cases and the dashboard snapshot. Qt-free like the
# domain, so the calculations stay testable without a GUI or a database.
add_library(hinkwise_application STATIC
    src/application/dashboard.cpp
    src/application/use_cases.cpp
)
target_link_libraries(hinkwise_application PUBLIC hinkwise_domain)
target_compile_features(hinkwise_application PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hinkwise_application PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Infrastructure layer: the outside world. SQLite and the JSON backup format
# live here, with Qt keeping its distance from the model.
add_library(hinkwise_infrastructure STATIC
    src/infrastructure/json/backup.cpp
    src/infrastructure/sqlite/database.cpp
    src/infrastructure/sqlite/migrator.cpp
    src/infrastructure/sqlite/repositories.cpp
)
target_link_libraries(hinkwise_infrastructure PUBLIC
    hinkwise_domain
    Qt6::Core
    Qt6::Sql
)
target_compile_features(hinkwise_infrastructure PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hinkwise_infrastructure PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Presentation helpers with no QML or widget dependency: the money-in, text-out
# edge and the series the charts draw, which both the view models and their unit
# tests compile against.
add_library(hinkwise_presentation STATIC
    src/presentation/cashflow_series.cpp
    src/presentation/catalog.cpp
    src/presentation/fi_track.cpp
    src/presentation/input_text.cpp
    src/presentation/projection_series.cpp
)
target_include_directories(hinkwise_presentation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# The series helpers read the month flows the application layer hands them, so
# the application layer is a dependency of the text edge and not the other way
# round.
target_link_libraries(hinkwise_presentation PUBLIC hinkwise_application Qt6::Core)
target_compile_features(hinkwise_presentation PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hinkwise_presentation PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(hinkwise
    src/main.cpp
)

# The generated QML type registration includes the presentation headers by
# name, so their directory has to be on the compiler's include path.
target_include_directories(hinkwise PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/presentation)

# Theme is a QML singleton; this must be set before qt_add_qml_module().
set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(hinkwise
    URI Hinkwise
    VERSION ${PROJECT_VERSION}
    SOURCES
        src/presentation/accounts_view_model.cpp
        src/presentation/accounts_view_model.h
        src/presentation/app_controller.cpp
        src/presentation/app_controller.h
        src/presentation/cashflow_view_model.cpp
        src/presentation/cashflow_view_model.h
        src/presentation/dashboard_view_model.cpp
        src/presentation/dashboard_view_model.h
        src/presentation/plan_view_model.cpp
        src/presentation/plan_view_model.h
        src/presentation/review_view_model.cpp
        src/presentation/review_view_model.h
        src/presentation/settings_view_model.cpp
        src/presentation/settings_view_model.h
        src/presentation/wizard_view_model.cpp
        src/presentation/wizard_view_model.h
    QML_FILES
        qml/Main.qml
        qml/theme/Theme.qml
        qml/components/BucketCard.qml
        qml/components/BucketGauge.qml
        qml/components/ChoiceField.qml
        qml/components/ConfirmDialog.qml
        qml/components/FormRow.qml
        qml/components/InsightCard.qml
        qml/components/MoneyField.qml
        qml/components/NavRail.qml
        qml/components/PrimaryButton.qml
        qml/components/SectionHeader.qml
        qml/components/StatBlock.qml
        qml/pages/BucketsPage.qml
        qml/pages/CashflowPage.qml
        qml/pages/DashboardPage.qml
        qml/pages/GuidePage.qml
        qml/pages/OnboardingPage.qml
        qml/pages/PlanPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/YearEndReviewPage.qml
)

target_link_libraries(hinkwise PRIVATE
    hinkwise_application
    hinkwise_domain
    hinkwise_infrastructure
    hinkwise_presentation
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hinkwise PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS hinkwise
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Desktop integration. Without these the binary installs but never appears in a
# launcher, and the Wayland app_id has nothing to match against.
install(FILES packaging/io.github.elisohman.hinkwise.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES packaging/io.github.elisohman.hinkwise.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)
install(FILES packaging/io.github.elisohman.hinkwise.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)

enable_testing()
add_subdirectory(tests)
