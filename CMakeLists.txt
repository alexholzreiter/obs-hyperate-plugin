cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C CXX)

option(HYPERATE_WITH_LIBWEBSOCKETS "Enable the HypeRate WebSocket client through libwebsockets" ON)
set(
  HYPERATE_API_TOKEN
  "a7f9d18c4e2b91f0c8d6e5a13b7f9c42e1d8a5f6b3c7d9e0f4a1c8b2d6e9f735"
  CACHE STRING
  "HypeRate API token compiled into private builds so users do not need to enter one"
)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs CONFIG QUIET)
if(NOT libobs_FOUND)
  message(
    FATAL_ERROR
      "Could not find libobs.\n"
      "OBS Studio installed as an app is usually not enough for native plugin development.\n"
      "Install or build the OBS Studio development package, then configure with one of:\n"
      "  cmake --preset macos-ninja -Dlibobs_DIR=/path/to/directory/containing/libobsConfig.cmake\n"
      "  cmake --preset macos-ninja -DCMAKE_PREFIX_PATH=/path/to/obs-install-prefix\n"
      "You can run scripts/find-libobs-macos.sh to search common local locations."
  )
endif()
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HYPERATE_API_TOKEN="${HYPERATE_API_TOKEN}")
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HYPERATE_PLUGIN_VERSION="${PLUGIN_VERSION}")
if(WIN32)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Gdi32 User32)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/heart_rate/heart_rate_state.cpp
    src/hyperate/hyperate_client.cpp
    src/sources/bpm_display_source.cpp
    src/sources/hyperate_input_source.cpp
    src/sources/threshold_toggle_source.cpp
    src/filters/heartbeat_camera_shake.cpp
    src/filters/heartbeat_glow.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)

if(APPLE)
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      "-framework CoreFoundation"
      "-framework CoreGraphics"
      "-framework CoreText"
  )
endif()

if(HYPERATE_WITH_LIBWEBSOCKETS)
  # On Windows we link libwebsockets statically via vcpkg. Its exported CMake
  # package carries the transitive dependencies (OpenSSL, zlib, libuv), so all of
  # them end up inside obs-hyperate.dll and no sibling DLLs ship. macOS and Linux
  # keep the proven pkg-config path below.
  if(WIN32)
    find_package(libwebsockets CONFIG QUIET)
    foreach(_candidate websockets libwebsockets::websockets websockets_static)
      if(TARGET ${_candidate})
        set(HYPERATE_LWS_TARGET ${_candidate})
        break()
      endif()
    endforeach()
  endif()

  if(NOT HYPERATE_LWS_TARGET)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(LIBWEBSOCKETS QUIET libwebsockets)
    endif()

    if(NOT LIBWEBSOCKETS_FOUND)
      find_path(LIBWEBSOCKETS_INCLUDE_DIR libwebsockets.h)
      find_library(LIBWEBSOCKETS_LIBRARY NAMES websockets libwebsockets)
      if(LIBWEBSOCKETS_INCLUDE_DIR AND LIBWEBSOCKETS_LIBRARY)
        set(LIBWEBSOCKETS_FOUND TRUE)
        set(LIBWEBSOCKETS_INCLUDE_DIRS "${LIBWEBSOCKETS_INCLUDE_DIR}")
        set(LIBWEBSOCKETS_LIBRARIES "${LIBWEBSOCKETS_LIBRARY}")
      endif()
    endif()
  endif()
endif()

if(HYPERATE_WITH_LIBWEBSOCKETS AND HYPERATE_LWS_TARGET)
  message(STATUS "Using libwebsockets CMake target: ${HYPERATE_LWS_TARGET}")
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HYPERATE_HAVE_LIBWEBSOCKETS=1)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${HYPERATE_LWS_TARGET})

  if(WIN32)
    # System libraries the static dependencies rely on: OpenSSL needs the TLS and
    # cert-store APIs, libuv needs sockets plus dbghelp (SymGetOptions,
    # MiniDumpWriteDump) and the shell/COM APIs for process handling. winhttp is
    # used to read the Windows system proxy for the connection fallback.
    target_link_libraries(
      ${CMAKE_PROJECT_NAME}
      PRIVATE
        ws2_32
        crypt32
        iphlpapi
        secur32
        bcrypt
        userenv
        advapi32
        dbghelp
        ole32
        shell32
        psapi
        winhttp
    )
  endif()
elseif(HYPERATE_WITH_LIBWEBSOCKETS AND LIBWEBSOCKETS_FOUND)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE HYPERATE_HAVE_LIBWEBSOCKETS=1)
  target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
  target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${LIBWEBSOCKETS_LIBRARY_DIRS})
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ${LIBWEBSOCKETS_LIBRARIES})
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE ${LIBWEBSOCKETS_CFLAGS_OTHER})

  find_package(OpenSSL QUIET)
  if(OpenSSL_FOUND)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  else()
    find_path(
      OPENSSL_INCLUDE_DIR
      openssl/ssl.h
      PATHS /opt/homebrew/opt/openssl@3/include /usr/local/opt/openssl@3/include
    )
    if(OPENSSL_INCLUDE_DIR)
      target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${OPENSSL_INCLUDE_DIR}")
    endif()
  endif()
elseif(HYPERATE_WITH_LIBWEBSOCKETS)
  message(
    WARNING
      "libwebsockets was not found. The plugin will build, but the HypeRate socket client will report unavailable until libwebsockets is installed."
  )
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
