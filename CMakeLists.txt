cmake_minimum_required(VERSION 3.21)
project(Waltosh VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Network Multimedia)

# Litecoin engine (ltc_core) - expect ./ltcengine in this repo.
set(LTCENGINE_BUILD_CLI ON CACHE BOOL "Build ltcengine CLI/TUI")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ltcengine/CMakeLists.txt")
    set(LTCENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ltcengine")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ltcengine/CMakeLists.txt")
    set(LTCENGINE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ltcengine")
else()
    message(FATAL_ERROR "ltcengine not found. Place it at ./ltcengine")
endif()
message(STATUS "Using ltcengine from ${LTCENGINE_SOURCE_DIR}")
add_subdirectory(${LTCENGINE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/ltcengine)

include(FetchContent)
set(QLEMENTINE_SANDBOX OFF CACHE BOOL "" FORCE)
set(QLEMENTINE_SHOWCASE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    qlementine
    GIT_REPOSITORY https://github.com/oclero/qlementine.git
    GIT_TAG v1.4.2
    PATCH_COMMAND
        ${CMAKE_COMMAND}
            -DQLEMENTINE_PATCH=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/qlementine-v1.4.2.patch
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/apply-qlementine-patch.cmake
)
FetchContent_MakeAvailable(qlementine)

add_library(waltosh_core STATIC
    core/WalletCore.cpp
)
target_include_directories(waltosh_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(waltosh_core PUBLIC ltc_core)
target_compile_features(waltosh_core PUBLIC cxx_std_20)

set(WALTOSH_SOURCES
    UI/main.cpp
    UI/MainWindow.h
    UI/MainWindow.cpp
    UI/CoreBridge.h
    UI/CoreBridge.cpp
    UI/AppSettings.h
    UI/AppSettings.cpp
    UI/MarketTypes.h
    UI/PriceFetcher.h
    UI/PriceFetcher.cpp
    UI/PriceService.h
    UI/PriceService.cpp
    UI/LivePriceSocket.h
    UI/LivePriceSocket.cpp
    UI/PriceChartWidget.h
    UI/PriceChartWidget.cpp
    UI/MnemonicBackupDialog.h
    UI/MnemonicBackupDialog.cpp
    UI/MicaEffect.h
    UI/MicaEffect.cpp
    UI/WaylandBackdrop.h
    UI/WaylandBackdrop.cpp
    UI/icons.qrc
)

qt_add_executable(Waltosh ${WALTOSH_SOURCES})

# GUI subsystem on Windows (no console/CMD flash on double-click).
set_target_properties(Waltosh PROPERTIES WIN32_EXECUTABLE TRUE)

# Embed multi-size app icon into the Windows EXE (Explorer / taskbar / Alt-Tab).
if(WIN32)
    set(_waltosh_icon_rc "${CMAKE_CURRENT_BINARY_DIR}/waltosh_icon.rc")
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/branding/waltosh.ico" _waltosh_ico_path)
    file(WRITE "${_waltosh_icon_rc}" "IDI_ICON1 ICON \"${_waltosh_ico_path}\"\n")
    target_sources(Waltosh PRIVATE "${_waltosh_icon_rc}")
endif()

target_include_directories(Waltosh PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/UI
)
target_link_libraries(Waltosh PRIVATE
    waltosh_core
    qlementine
    Qt6::Widgets
    Qt6::Svg
    Qt6::Network
    Qt6::Multimedia
)

if(WIN32)
    target_link_libraries(Waltosh PRIVATE dwmapi)
    target_compile_definitions(Waltosh PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    # secp256k1 is shared on Windows - keep DLL beside Waltosh.exe
    add_custom_command(TARGET Waltosh POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:secp256k1>
            $<TARGET_FILE_DIR:Waltosh>
        COMMENT "Copy libsecp256k1 DLL next to Waltosh"
    )
elseif(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    find_package(X11)
    find_program(WAYLAND_SCANNER wayland-scanner)

    pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)

    if(X11_FOUND)
        target_include_directories(Waltosh PRIVATE ${X11_INCLUDE_DIR})
        target_link_libraries(Waltosh PRIVATE ${X11_LIBRARIES})
        target_compile_definitions(Waltosh PRIVATE HAVE_X11)
        message(STATUS "Linux backdrop: KWin X11 blur enabled")
    endif()

    if(WAYLAND_SCANNER)
        set(WAYLAND_PROTO_XML "${CMAKE_CURRENT_SOURCE_DIR}/UI/wayland/ext-background-effect-v1.xml")
        set(WAYLAND_PROTO_HEADER "${CMAKE_CURRENT_BINARY_DIR}/ext-background-effect-v1-client.h")

        add_custom_command(
            OUTPUT ${WAYLAND_PROTO_HEADER}
            COMMAND ${WAYLAND_SCANNER} client-header ${WAYLAND_PROTO_XML} ${WAYLAND_PROTO_HEADER}
            DEPENDS ${WAYLAND_PROTO_XML}
            COMMENT "Generating ext-background-effect-v1 Wayland client header"
        )

        target_sources(Waltosh PRIVATE ${WAYLAND_PROTO_HEADER})
        target_include_directories(Waltosh PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(Waltosh PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
        target_include_directories(Waltosh PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
        target_compile_definitions(Waltosh PRIVATE HAVE_WAYLAND)
        message(STATUS "Linux backdrop: ext-background-effect-v1 (COSMIC / Wayland) enabled")
    else()
        message(STATUS "Linux backdrop: wayland-scanner not found - Wayland blur disabled")
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS Waltosh
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set(CPACK_PACKAGE_NAME "waltosh")
set(CPACK_PACKAGE_VENDOR "WALTOSH")
set(CPACK_PACKAGE_CONTACT "https://github.com/hdmain/Waltosh")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "WALTOSH - Litecoin SPV wallet (DRMSketch GUI + ltcengine)")
set(CPACK_PACKAGE_DESCRIPTION "Cross-platform Litecoin wallet using Qt6 DRMSketch UI and the ltcengine SPV crypto engine.")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "WALTOSH maintainers")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
# Do not let dpkg-shlibdeps emit Ubuntu 24.04-only *t64 package names
# (e.g. libqt6gui6t64), which break installs on older Debian/Ubuntu.
# Explicit Depends accept either classic or t64 Qt6 package names.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libc6 (>= 2.35), libstdc++6 (>= 12), libgcc-s1 (>= 3.0), \
     libqt6core6 (>= 6.2.0) | libqt6core6t64 (>= 6.2.0), \
     libqt6gui6 (>= 6.2.0) | libqt6gui6t64 (>= 6.2.0), \
     libqt6widgets6 (>= 6.2.0) | libqt6widgets6t64 (>= 6.2.0), \
     libqt6network6 (>= 6.2.0) | libqt6network6t64 (>= 6.2.0), \
     libqt6svg6 (>= 6.2.0) | libqt6svg6t64 (>= 6.2.0), \
     libqt6multimedia6 (>= 6.2.0) | libqt6multimedia6t64 (>= 6.2.0), \
     libqt6dbus6 (>= 6.2.0) | libqt6dbus6t64 (>= 6.2.0), \
     libqt6opengl6 (>= 6.2.0) | libqt6opengl6t64 (>= 6.2.0), \
     libx11-6, libxkbcommon0, libwayland-client0")
set(CPACK_GENERATOR "DEB")
include(CPack)
