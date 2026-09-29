cmake_minimum_required(VERSION 3.20)

project(fdcsds VERSION 1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Link the MSVC runtime statically so the .exe has no DLL dependencies.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# FLTK, fetched and built as a static library
# ---------------------------------------------------------------------------

include(FetchContent)

set(FLTK_BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_TEST          OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_EXAMPLES      OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLUID         OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLTK_OPTIONS  OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_GL            OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FORMS         OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_HTML_DOCS     OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_PDF_DOCS      OFF CACHE BOOL "" FORCE)
set(FLTK_MSVC_RUNTIME_DLL    OFF CACHE BOOL "" FORCE)  # match our static MSVC runtime

# Always use FLTK's bundled image libraries. System copies (e.g. Homebrew's,
# which are arm64 only) would add runtime dependencies and break universal
# macOS builds.
set(FLTK_USE_SYSTEM_LIBJPEG  OFF CACHE BOOL "" FORCE)
set(FLTK_USE_SYSTEM_LIBPNG   OFF CACHE BOOL "" FORCE)
set(FLTK_USE_SYSTEM_ZLIB     OFF CACHE BOOL "" FORCE)

FetchContent_Declare(fltk
  GIT_REPOSITORY https://github.com/fltk/fltk.git
  GIT_TAG        release-1.4.5
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(fltk)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# fdcsds
# ---------------------------------------------------------------------------

add_executable(fdcsds WIN32
  src/main.cpp
  src/engine.cpp
  src/tcp_server.cpp
)

# Platform code lives in separate source files chosen here, so the sources
# themselves contain no conditional compilation.
if(WIN32)
  target_sources(fdcsds PRIVATE
    src/serial_win32.cpp
    src/net_win32.cpp
    resources/fdcsds.rc
  )
  target_link_libraries(fdcsds PRIVATE ws2_32)
  target_compile_definitions(fdcsds PRIVATE _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX)
elseif(APPLE)
  target_sources(fdcsds PRIVATE
    src/serial_posix.cpp
    src/serial_macos.cpp
    src/net_posix.cpp
  )
else()
  target_sources(fdcsds PRIVATE
    src/serial_posix.cpp
    src/serial_linux.cpp
    src/net_posix.cpp
  )
endif()

target_link_libraries(fdcsds PRIVATE fltk::fltk Threads::Threads)

target_compile_definitions(fdcsds PRIVATE
  FDCSDS_VERSION="${PROJECT_VERSION}"
)

if(MINGW)
  target_link_options(fdcsds PRIVATE -static)
endif()

# ---------------------------------------------------------------------------
# Tests: the protocol engine against a scripted transport
# ---------------------------------------------------------------------------

enable_testing()
add_executable(engine_test tests/engine_test.cpp src/engine.cpp)
target_link_libraries(engine_test PRIVATE Threads::Threads)
add_test(NAME engine COMMAND engine_test)
