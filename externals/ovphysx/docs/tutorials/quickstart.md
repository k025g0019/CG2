# Quickstart

ovphysx is a self-contained SDK for USD-based physics simulation, available as a Python wheel and a C/C++ package.
This guide shows how to get started with both. You learn the required SDK layout, CMake configuration, runtime setup, and where to go next.

## Prerequisites

For both Python and C:

- Prepare a simple USD file such as `scene.usda` for the sample load call.
- Install CUDA Toolkit and a compatible NVIDIA driver (recommended, but can be skipped if only CPU simulation is used).

## Quick Start by Language

### Python

Install the wheel:

```bash
pip install ovphysx
```

```python
import ovphysx
from ovphysx import PhysX

physx = PhysX()
physx.add_usd("scene.usda")
physx.step(1.0 / 60.0, 0.0)
physx.release()
```

### C/C++

Download the ovphysx SDK package from the [GitHub Releases](https://github.com/NVIDIA-Omniverse/PhysX/releases) page and extract it to a local path.
You also need CMake 3.16 or newer and a C or C++ compiler.

**SDK Directory Layout**

```text
ovphysx/
├── SKILLS.md             # Skills index
├── skills/               # Agent skills runbooks
├── include/ovphysx/     # Public headers (ovphysx.h, ovphysx_types.h, and related headers)
├── lib/                 # Shared libraries and CMake package config
│   └── cmake/ovphysx/   # find_package(ovphysx) support
├── plugins/             # Carbonite, PhysX, and USD runtime plugins
│   └── gpu/             # GPU-only plugins (loaded only when GPU is enabled)
├── samples/             # CI-tested C sample source + USD data
├── docs/                # Documentation (HTML + Markdown)
└── LICENSE.txt
```

**Build Your First App**

**CMakeLists.txt**

1. Create a `CMakeLists.txt` file in your app directory.

   Add this minimal configuration:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp C)

find_package(ovphysx REQUIRED)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE ovphysx::ovphysx)

if(WIN32)
    ovphysx_copy_runtime_dlls(my_app)
endif()
```

#### Source

2. Create a source file for your executable.

   Minimal `main()` example with error checking on the most critical call:

```c
#include "ovphysx/ovphysx.h"
#include <stdio.h>

int main() {
    ovphysx_create_args create_args = OVPHYSX_CREATE_ARGS_DEFAULT;
    ovphysx_handle_t handle = 0;
    ovphysx_result_t result = ovphysx_create_instance(&create_args, &handle);
    if (result.status != OVPHYSX_API_SUCCESS) {
        ovphysx_string_t err = ovphysx_get_last_error();
        if (err.ptr)
            fprintf(stderr, "Failed to create instance: %.*s\n", (int)err.length, err.ptr);
        return 1;
    }

    ovphysx_string_t prefix_str = {NULL, 0};
    ovphysx_usd_handle_t usd_handle = 0;
    ovphysx_enqueue_result_t add_result =
        ovphysx_add_usd(handle, ovphysx_cstr("scene.usda"), prefix_str, &usd_handle);
    if (add_result.status != OVPHYSX_API_SUCCESS) {
        ovphysx_string_t err = ovphysx_get_last_error();
        if (err.ptr)
            fprintf(stderr, "Failed to add USD: %.*s\n", (int)err.length, err.ptr);
        ovphysx_destroy_instance(handle);
        return 1;
    }

    ovphysx_enqueue_result_t step_result = ovphysx_step(handle, 1.0f / 60.0f, 0.0f);
    if (step_result.status != OVPHYSX_API_SUCCESS) {
        ovphysx_string_t err = ovphysx_get_last_error();
        if (err.ptr)
            fprintf(stderr, "Failed to step simulation: %.*s\n", (int)err.length, err.ptr);
        ovphysx_destroy_instance(handle);
        return 1;
    }

    ovphysx_destroy_instance(handle);
    return 0;
}
```

**Configure and Build**

3. Configure and build the app.

   Point `CMAKE_PREFIX_PATH` at the SDK root (the directory containing `include/`, `lib/`, and `plugins/`):

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/ovphysx
cmake --build build
```

## Runtime Libraries (C/C++)

### Linux

The CMake config bakes RPATH into your executable automatically.
No `LD_LIBRARY_PATH` manipulation is needed. Run the binary directly.

### Windows

Use the `ovphysx_copy_runtime_dlls(<target>)` helper provided by the CMake package
(already shown in the `CMakeLists.txt` example).
This copies the required DLLs and plugins next to your executable at build time,
so no ovphysx-specific `PATH` entries are required.

## Result

You now have a minimal C or C++ application that links against `ovphysx::ovphysx`, loads a USD stage, and runs a simulation step.

## Next Steps

- Full C API reference: see the [C API Reference](../api.md)
- More tutorials: [Hello World](hello_world.md), [Tensor Bindings](tensor_bindings.md), [Contact Binding](contact_binding.md)
