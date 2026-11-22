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
    src/domain/liability.cpp
    src/domain/phase.cpp
    src/domain/targets.cpp
    src/domain/year_month.cpp
)
target_include_directories(hinkwise_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(hinkwise_domain PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hinkwise_domain PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(hinkwise
    src/main.cpp
)

# Theme is a QML singleton; this must be set before qt_add_qml_module().
set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(hinkwise
    URI Hinkwise
    VERSION ${PROJECT_VERSION}
    QML_FILES
        qml/Main.qml
        qml/theme/Theme.qml
        qml/components/BucketCard.qml
)

target_link_libraries(hinkwise PRIVATE
    hinkwise_domain
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
