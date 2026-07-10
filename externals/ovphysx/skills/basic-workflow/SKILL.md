---
name: basic-workflow
description: Create an ovphysx instance, load a USD scene, step simulation, and clean up. Use when starting a new ovphysx project, testing basic integration, or learning the minimal workflow.
version: "0.1.0"
author: NVIDIA Omniverse Physics
tags:
  - ovphysx
  - physics
  - quickstart
tools:
  - Read
  - Shell
compatibility: "ovphysx 0.4 wheel or SDK; Python examples require the ovphysx Python package, and C examples require SDK headers and libraries."
---

# Basic Workflow: Load USD and Step

The smallest end-to-end ovphysx workflow.

## When to Use

Use this skill when a caller needs the smallest working ovphysx program, an installation smoke test, or a Python-to-C API mapping for instance creation, USD loading, stepping, and cleanup.

## Instructions

1. Read the referenced sample for the caller's language before changing code.
2. Keep the create, load, step, and destroy order shown here unless the caller asks for a larger workflow.
3. Use Shell to run the Python sample or compile the C sample after adapting the paths.

## Python

```bash
pip install ovphysx
```

```python
from ovphysx import PhysX

physx = PhysX()
physx.add_usd("scene.usda")
physx.step(1.0 / 60.0, 0.0)
physx.release()
```

Full sample:
- `samples/python_samples/hello_world.py` (wheel)
- Source checkout: `tests/python_samples/hello_world.py`

## C

```c
#include <ovphysx/ovphysx.h>
#include <stdio.h>

int main(void)
{
    ovphysx_create_args create_args = OVPHYSX_CREATE_ARGS_DEFAULT;
    ovphysx_handle_t handle = 0;
    ovphysx_result_t result = ovphysx_create_instance(&create_args, &handle);
    if (result.status != OVPHYSX_API_SUCCESS) {
        ovphysx_string_t err = ovphysx_get_last_error();
        if (err.ptr) {
            fprintf(stderr, "Failed to create instance: %.*s\n", (int)err.length, err.ptr);
        }
        return 1;
    }

    ovphysx_string_t prefix = {NULL, 0};
    ovphysx_usd_handle_t usd_handle = 0;
    ovphysx_enqueue_result_t add_result =
        ovphysx_add_usd(handle, ovphysx_cstr("scene.usda"), prefix, &usd_handle);
    if (add_result.status != OVPHYSX_API_SUCCESS) {
        ovphysx_string_t err = ovphysx_get_last_error();
        if (err.ptr) {
            fprintf(stderr, "Failed to add USD: %.*s\n", (int)err.length, err.ptr);
        }
        ovphysx_destroy_instance(handle);
        return 1;
    }

    ovphysx_enqueue_result_t step_result = ovphysx_step(handle, 1.0f / 60.0f, 0.0f);
    if (step_result.status != OVPHYSX_API_SUCCESS) {
        ovphysx_string_t err = ovphysx_get_last_error();
        if (err.ptr) {
            fprintf(stderr, "Failed to step simulation: %.*s\n", (int)err.length, err.ptr);
        }
        ovphysx_destroy_instance(handle);
        return 1;
    }

    ovphysx_destroy_instance(handle);
    return 0;
}
```

Full sample:
- `samples/c_samples/hello_world_c/main.c` (SDK)
- Source checkout: `tests/c_samples/hello_world_c/main.c`

Build the C sample with CMake:

```cmake
find_package(ovphysx REQUIRED)
add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE ovphysx::ovphysx)
if(WIN32) # optionally copy ovphysx dlls into bin folder
    ovphysx_copy_runtime_dlls(my_app)
endif()
```

Configure and build:

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/ovphysx
cmake --build build
```

## Key APIs

| Python | C |
|--------|---|
| `PhysX()` | `ovphysx_create_instance()` |
| `physx.add_usd(path)` | `ovphysx_add_usd()` |
| `physx.step(dt, time)` | `ovphysx_step()` |
| `physx.release()` | `ovphysx_destroy_instance()` |

## References

- Docs: `docs/tutorials/hello_world.md`
- Python sample: `samples/python_samples/hello_world.py` (wheel; source: `tests/python_samples/hello_world.py`)
- C sample: `samples/c_samples/hello_world_c/main.c` (SDK; source: `tests/c_samples/hello_world_c/main.c`)
- C header: `include/ovphysx/ovphysx.h`
