# Building Against the ovphysx Source Tree

This tutorial shows how to build your application against the ovphysx source
checkout using CMake's `add_subdirectory()`. This lets you edit ovphysx sources
and have your application automatically recompile the changed files. The compile
and link step does not need an installed SDK; running the app still needs an
installed ovphysx runtime layout for Carbonite and PhysX plugins.

For the installed-SDK approach using `find_package()`, see the
[Hello World tutorial](hello_world.md) and the [Quickstart guide](quickstart.md).

## Prerequisites

- A local clone of the [PhysX repository](https://github.com/NVIDIA-Omniverse/PhysX)
- CMake 3.16 or newer
- A C/C++ compiler (GCC 11+, MSVC 2022, or Clang 14+)

## CMakeLists.txt

Your application's `CMakeLists.txt` uses `add_subdirectory()` to include
ovphysx and links against `ovphysx::ovphysx` — the same target name used by the
installed SDK's `find_package()`:

```cmake
# hello_world_source_link — demonstrates building against the ovphysx source tree
# using add_subdirectory() instead of find_package().
#
# Usage (from the repo root):
#   cmake -S omni/ovphysx/tests/c_samples/hello_world_source_link -B /tmp/sl_test
#   cmake --build /tmp/sl_test
#   /tmp/sl_test/hello_world_source_link
#
# Or with a custom source checkout:
#   cmake -S . -B _build -DOVPHYSX_SOURCE_DIR=/path/to/ovphysx
cmake_minimum_required(VERSION 3.16)
project(HelloWorldSourceLink C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Point at the ovphysx source tree. Default assumes monorepo layout.
if(NOT DEFINED OVPHYSX_SOURCE_DIR)
    get_filename_component(OVPHYSX_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
endif()

if(NOT EXISTS "${OVPHYSX_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "ovphysx source not found at ${OVPHYSX_SOURCE_DIR}.\n"
        "Pass -DOVPHYSX_SOURCE_DIR=<path> to point at your ovphysx checkout.")
endif()

# Include ovphysx as a subdirectory. Dependencies are auto-fetched by default
# (OVPHYSX_FETCH_DEPS=ON). Pass -DOVPHYSX_FETCH_DEPS=OFF to skip.
add_subdirectory("${OVPHYSX_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/ovphysx")

add_executable(hello_world_source_link main.c)

# Strict C compilation flags (same as hello_world_c)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hello_world_source_link PRIVATE
        -Werror=implicit-function-declaration
        -pedantic
    )
endif()
if(MSVC)
    target_compile_options(hello_world_source_link PRIVATE /TC)
endif()

target_link_libraries(hello_world_source_link PRIVATE ovphysx::ovphysx)

get_filename_component(OVPHYSX_TEST_DATA_DIR
    "${OVPHYSX_SOURCE_DIR}/tests/data" ABSOLUTE)
target_compile_definitions(hello_world_source_link PRIVATE
    OVPHYSX_TEST_DATA="${OVPHYSX_TEST_DATA_DIR}"
)

# Set up runtime library paths so the executable finds ovphysx and its deps.
ovphysx_setup_source_link_runtime(hello_world_source_link)
```

Key points:
- `OVPHYSX_SOURCE_DIR` points at the `omni/ovphysx` directory in the repo
- Dependencies are auto-fetched at configure time (`OVPHYSX_FETCH_DEPS=ON` by default)
- `ovphysx_setup_source_link_runtime()` configures RPATH (Linux) or copies DLLs (Windows) so the executable finds `libovphysx` at runtime

## Build and Run

```bash
# Configure (first run fetches dependencies automatically)
cmake -S my_app -B my_app/_build

# Build
cmake --build my_app/_build --parallel 8

# Run (OVPHYSX_LIB anchors runtime config/schema/plugin discovery)
OVPHYSX_LIB=/path/to/ovphysx/_install/lib/libovphysx.so ./my_app/_build/my_app
```

The first configure takes a few minutes to fetch packman dependencies. Subsequent
configures are instant.

### Edit-Rebuild Cycle

After the initial build, editing ovphysx sources triggers recompilation of only
the changed files:

```bash
# Edit ovphysx source.
vim omni/ovphysx/src/ovphysx/ovphysx.cpp

# Rebuild — only changed files recompile
cmake --build my_app/_build --parallel 8
```

## Runtime Plugin Discovery

The ovphysx runtime loads Carbonite and PhysX plugins via `dlopen` at startup.
These plugins are not part of the `add_subdirectory()` build; they come from the
installed SDK's flattened runtime layout. Build and install the SDK once, then
point `OVPHYSX_LIB` at the installed ovphysx library so the runtime can find the
matching `config.toml`, plugins, and USD schema paths:

```bash
# One-time setup
cd omni/ovphysx
./build.sh
cmake --build _build --target install_sdk

# Then run your app with:
OVPHYSX_LIB=$(pwd)/_install/lib/libovphysx.so ./my_app/_build/my_app
```

On Windows, use `OVPHYSX_LIB=%cd%\_install\bin\ovphysx.dll`.

Pass `-DOVPHYSX_FETCH_DEPS=OFF` on subsequent configures if you don't want the
auto-fetch to re-check dependencies.

## Configuration Reference

| Variable | Default | Description |
|----------|---------|-------------|
| `OVPHYSX_SOURCE_DIR` | *(monorepo-relative)* | Path to the `omni/ovphysx` directory |
| `OVPHYSX_FETCH_DEPS` | `ON` | Auto-fetch packman dependencies at configure time |
| `OVPHYSX_LIB` | *(env var)* | Path to the ovphysx shared library. Python loads this library directly; native/source-link runs use its directory to find `config.toml`, plugins, and USD schema paths. |

## Result

After this tutorial, you can build your application against the ovphysx source
tree and iterate on ovphysx code without reinstalling the SDK after every edit.
For the complete sample code, see `tests/c_samples/hello_world_source_link/`.
