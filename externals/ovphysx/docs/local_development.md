# Local Development

This guide covers the two ways to build an application against ovphysx, plus
the Python wheel workflow for rapid iteration.

## Building Against the Installed SDK

The simplest approach: build ovphysx once, install it, and point your app at the
install tree with `find_package()`.

```bash
# 1. Build ovphysx (fetches dependencies automatically)
cd omni/ovphysx
./build.sh            # Linux
build.bat             # Windows

# 2. Install the SDK into _install/
cmake --build _build --target install_sdk

# 3. Build your app against the installed SDK
cmake -S my_app -B my_app/_build \
    -DCMAKE_PREFIX_PATH="$(pwd)/_install"
cmake --build my_app/_build
```

Your app's `CMakeLists.txt` uses `find_package()`:

```cmake
find_package(ovphysx REQUIRED)
target_link_libraries(my_app PRIVATE ovphysx::ovphysx)
```

See `tests/c_samples/hello_world_c/` for a complete example.

## Building Against the Source Checkout

If you need to iterate on ovphysx code and have your app automatically
recompile changed sources, use `add_subdirectory()` instead.

```bash
# No prior build step needed — dependencies are fetched at configure time.
cmake -S my_app -B my_app/_build
cmake --build my_app/_build
```

Your app's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_app C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Point at the ovphysx source tree
set(OVPHYSX_SOURCE_DIR "/path/to/omni/ovphysx")
add_subdirectory("${OVPHYSX_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/ovphysx")

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE ovphysx::ovphysx)

# Set up RPATH / DLL copying so the executable finds libraries at runtime
ovphysx_setup_source_link_runtime(my_app)
```

The first `cmake` configure will automatically fetch packman dependencies
(controlled by `OVPHYSX_FETCH_DEPS`, default `ON`). Subsequent configures
skip the fetch if the dependencies already exist.

Edit-rebuild cycle:

```bash
# Edit ovphysx source.
vim omni/ovphysx/src/ovphysx/ovphysx.cpp

# Rebuild — only changed ovphysx sources recompile
cmake --build my_app/_build
```

`ovphysx_setup_source_link_runtime()` configures RPATH (Linux) or copies DLLs
(Windows) so that the executable can load `libovphysx.so` and its direct
dependencies.

**Running the executable:** The ovphysx runtime also needs Carbonite and PhysX
plugins at runtime (loaded via `dlopen`). The simplest approach is to build the
installed SDK once and point `OVPHYSX_LIB` at the installed ovphysx library so
the runtime can derive the matching config, schema, and plugin paths:

```bash
# One-time setup: build and install the SDK
cd omni/ovphysx && ./build.sh && cmake --build _build --target install_sdk

# Run with runtime layout from the installed SDK
OVPHYSX_LIB=omni/ovphysx/_install/lib/libovphysx.so ./my_app/_build/my_app
```

On Windows, use `OVPHYSX_LIB=omni\ovphysx\_install\bin\ovphysx.dll`.

See `tests/c_samples/hello_world_source_link/` for a complete example.

## Python Wheel Workflow

For Python users, the wheel packages ovphysx and all runtime dependencies into
a pip-installable archive.

```bash
cd omni/ovphysx

# Build everything and create the wheel in _dist/
./build.sh
cmake --build _build --target build_wheel

# Install into a virtual environment
uv pip install _dist/ovphysx-*.whl
```

For fast iteration without rebuilding the wheel, point `OVPHYSX_LIB` at the
build tree:

```bash
export OVPHYSX_LIB="$(pwd)/_build/linux-x86_64/release/libovphysx.so"
python my_script.py
```

## Configuration Reference

### CMake Cache Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `OVPHYSX_FETCH_DEPS` | `ON` | Automatically fetch packman dependencies at configure time |
| `LOCAL_TARGET_DEPS` | `<ovphysx>/_build/target-deps` | Override the directory where packman dependencies are stored |
| `OVPHYSX_DEV_PHYSX` | `OFF` | Build PhysX SDK from source instead of using prebuilt binaries |
| `OVPHYSX_DEV_SCHEMA` | `OFF` | Use locally-built physics schema |
| `OVPHYSX_WARNINGS_AS_ERRORS` | `ON` in CI, `OFF` otherwise | Treat compiler warnings as errors |
| `OVPHYSX_BIN_DIR` | *(empty)* | Advanced build-time helper path for Kit/PhysX plugin linkage; not a runtime environment variable |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `OVPHYSX_LIB` | Path to `libovphysx.so` / `ovphysx.dll`. Python loads this library directly; native/source-link runs use its directory to find `config.toml`, plugins, and USD schema paths. |
