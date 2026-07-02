cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.cmake)

project(AetherBus
    VERSION ${AETHER_VERSION}
    DESCRIPTION "Modern lightweight serial-port interceptor and protocol sniffer GUI"
    LANGUAGES CXX)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
    ${CMAKE_BINARY_DIR}/generated/aether/version.h
    @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_COVERAGE "Enable coverage instrumentation" OFF)
if(BUILD_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(--coverage)
        add_link_options(--coverage)
    endif()
endif()

# Sanitizer instrumentation. Accepts: address, undefined, address+undefined,
# or thread. Applied to all targets, so configure must run before any target.
set(AETHER_SANITIZER "" CACHE STRING
    "Sanitizer(s) to enable: address, undefined, address+undefined, thread")
if(AETHER_SANITIZER)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR
       CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        string(REPLACE "+" "," _aether_san "${AETHER_SANITIZER}")
        add_compile_options(-fsanitize=${_aether_san} -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=${_aether_san})
        message(STATUS "AetherBus sanitizer enabled: ${_aether_san}")
    else()
        message(WARNING "AETHER_SANITIZER ignored: unsupported compiler")
    endif()
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

# Prebuilt Qt 6 for macOS AGL framework fix
if(APPLE)
    foreach(_qt_target Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network WrapOpenGL::WrapOpenGL Qt6::OpenGL)
        if(TARGET ${_qt_target})
            get_target_property(_qt_link_libs ${_qt_target} INTERFACE_LINK_LIBRARIES)
            if(_qt_link_libs)
                list(TRANSFORM _qt_link_libs REPLACE "^AGL$" "-framework OpenGL")
                list(TRANSFORM _qt_link_libs REPLACE "^.*[/ ]AGL(\\.framework)?$" "-framework OpenGL")
                set_target_properties(${_qt_target} PROPERTIES INTERFACE_LINK_LIBRARIES "${_qt_link_libs}")
            endif()
        endif()
    endforeach()

    set(_aether_compat_fw "${CMAKE_BINARY_DIR}/compat-frameworks")
    set(_aether_opengl_id "/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL")
    if(NOT EXISTS "${_aether_compat_fw}/AGL.framework/AGL")
        file(MAKE_DIRECTORY "${_aether_compat_fw}/AGL.framework")
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} -dynamiclib -x c /dev/null
                    -install_name "${_aether_opengl_id}"
                    -o "${_aether_compat_fw}/AGL.framework/AGL"
            RESULT_VARIABLE _aether_agl_rc OUTPUT_QUIET ERROR_QUIET)
    endif()
    if(EXISTS "${_aether_compat_fw}/AGL.framework/AGL")
        add_link_options("-F${_aether_compat_fw}")
        message(STATUS "AetherBus: AGL link-compat stub at ${_aether_compat_fw}")
    endif()
endif()

# Core interception backend. The POSIX PTY proxy is UNIX-only; on other
# platforms the library still builds with the pure codec + value types.
set(AETHER_CORE_SOURCES
    src/core/serial/serial_types.hpp
    src/core/can/can_types.hpp
    src/core/common/i_bus_backend.hpp
    src/core/common/format_codec.cpp
    src/core/common/format_codec.hpp
    src/core/common/stats_calculator.cpp
    src/core/common/stats_calculator.hpp
    src/core/common/capture_replay.cpp
    src/core/common/capture_replay.hpp
    src/core/common/pcap_writer.cpp
    src/core/common/pcap_writer.hpp
    src/core/common/signal_cleanup.cpp
    src/core/common/signal_cleanup.hpp
    src/core/serial/pty_proxy.cpp
    src/core/serial/pty_proxy.hpp
    src/core/serial/pty_proxy_impl.hpp
    src/core/can/can_backend.cpp
    src/core/can/can_backend.hpp
    src/core/can/dbc_parser.cpp
    src/core/can/dbc_parser.hpp
)
if(WIN32)
    list(APPEND AETHER_CORE_SOURCES
        src/core/serial/win/win_pty_proxy.cpp
        src/core/serial/win/win_pty_proxy.hpp
    )
elseif(APPLE)
    list(APPEND AETHER_CORE_SOURCES
        src/core/serial/posix/posix_pty_proxy.cpp
        src/core/serial/posix/posix_pty_proxy.hpp
        src/core/serial/mac/mac_pty_proxy.cpp
        src/core/serial/mac/mac_pty_proxy.hpp
        src/core/serial/mac/mac_baud.cpp
        src/core/serial/mac/mac_baud.hpp
    )
else() # Default to Linux
    list(APPEND AETHER_CORE_SOURCES
        src/core/serial/posix/posix_pty_proxy.cpp
        src/core/serial/posix/posix_pty_proxy.hpp
        src/core/serial/linux/linux_pty_proxy.cpp
        src/core/serial/linux/linux_pty_proxy.hpp
        src/core/serial/linux_baud.cpp
        src/core/serial/linux_baud.hpp
    )
endif()
add_library(aether_core STATIC ${AETHER_CORE_SOURCES})
target_link_libraries(aether_core PUBLIC Qt6::Core)
# macOS custom-baud support (IOSSIOSPEED) lives in <IOKit/serial/ioss.h>.
if(APPLE)
    target_link_libraries(aether_core PUBLIC "-framework IOKit" "-framework CoreFoundation")
endif()
target_include_directories(aether_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated)

add_library(aether_gui STATIC
    src/gui/mainwindow.cpp
    src/gui/mainwindow.hpp
    src/gui/sessions/session_view.hpp
    src/gui/sessions/session_widget.cpp
    src/gui/sessions/session_widget.hpp
    src/gui/sessions/can_session_widget.cpp
    src/gui/sessions/can_session_widget.hpp
    src/gui/panels/can_decoder_panel.cpp
    src/gui/panels/can_decoder_panel.hpp
    src/gui/panels/config_panel.cpp
    src/gui/panels/config_panel.hpp
    src/gui/panels/can_config_panel.cpp
    src/gui/panels/can_config_panel.hpp
    src/gui/panels/signal_panel.cpp
    src/gui/panels/signal_panel.hpp
    src/gui/panels/injection_panel.cpp
    src/gui/panels/injection_panel.hpp
    src/gui/widgets/consoleview.cpp
    src/gui/widgets/consoleview.hpp
    src/gui/widgets/console_panel.cpp
    src/gui/widgets/console_panel.hpp
    src/gui/widgets/byte_inspector_panel.cpp
    src/gui/widgets/byte_inspector_panel.hpp
    src/gui/widgets/consoleview_paint.cpp
    src/gui/widgets/consoleview_search.cpp
    src/gui/widgets/macrobar.cpp
    src/gui/widgets/macrobar.hpp
    src/gui/common/theme_controller.cpp
    src/gui/common/theme_controller.hpp
    src/gui/widgets/throughput_chart.cpp
    src/gui/widgets/throughput_chart.hpp
    src/gui/widgets/statspanel.cpp
    src/gui/widgets/statspanel.hpp
    src/gui/widgets/can_sniffer_widget.cpp
    src/gui/widgets/can_sniffer_widget.hpp
    src/gui/resources.qrc
)
target_link_libraries(aether_gui PUBLIC aether_core Qt6::Widgets)

if(APPLE)
    add_executable(aetherbus MACOSX_BUNDLE src/main.cpp)
else()
    add_executable(aetherbus src/main.cpp)
endif()
target_link_libraries(aetherbus PRIVATE aether_gui)

# On Windows, build as a GUI-subsystem app so launching spawns no console window.
if(WIN32)
    set_target_properties(aetherbus PROPERTIES WIN32_EXECUTABLE ON)
endif()

# Copy assets to build directory for local execution reference
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(bus_test
        tests/bus_test.hpp
        tests/bus_test.cpp
        tests/bus_test_pty.cpp
        tests/bus_test_gui.cpp
        tests/bus_test_can.cpp
    )
    target_link_libraries(bus_test PRIVATE aether_gui Qt6::Widgets Qt6::Core Qt6::Test)
    add_test(NAME bus_test COMMAND bus_test)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DevTools.cmake)

include(GNUInstallDirs)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/dist/aetherbus.service.in
        ${CMAKE_CURRENT_BINARY_DIR}/aetherbus.service
        @ONLY
    )

    install(TARGETS aether_core aetherbus
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY src/core/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aether/core
        FILES_MATCHING PATTERN "*.h"
    )
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/aetherbus.service
        DESTINATION /lib/systemd/system
    )
elseif(WIN32)
    install(TARGETS aetherbus RUNTIME DESTINATION bin)

    # Locate windeployqt
    if(TARGET Qt6::windeployqt)
        get_target_property(WINDEPLOYQT_EXECUTABLE Qt6::windeployqt IMPORTED_LOCATION)
    else()
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt
            HINTS "${QT6_INSTALL_PREFIX}/bin" "${Qt6_DIR}/../../../bin"
        )
    endif()

    # Run windeployqt on the installed executable to bundle Qt dependencies
    install(CODE "
        execute_process(COMMAND \"${WINDEPLOYQT_EXECUTABLE}\" --no-compiler-runtime \"\${CMAKE_INSTALL_PREFIX}/bin/aetherbus.exe\")
    ")
endif()

set(CPACK_PACKAGE_NAME "AetherBus")
set(CPACK_PACKAGE_VENDOR "AetherBus Project")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION "${AETHER_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modern lightweight serial-port interceptor and protocol sniffer")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")

# Debian packaging details
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "AetherBus Maintainers <beratatmaca@live.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# WiX Windows Installer details
set(CPACK_WIX_UPGRADE_GUID "D9C5F2A8-A29F-4D2A-B9C0-8B94F3C8F3B3")
set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico")

include(CPack)
