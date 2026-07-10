# Hello World: Load USD and Step

This tutorial shows the smallest end-to-end ovphysx workflow: create an instance, load a USD file, step simulation, and clean up resources. You can use this flow as the starting point for larger integrations.

## Prerequisites

- Prepare a readable USD file for the sample call.

## Code Language

### Python

Install the package first:

```bash
pip install ovphysx
```

```python
import ovphysx
from ovphysx import PhysX
from pathlib import Path

print("Using ovphysx version: ", ovphysx.__version__)

# Initialize PhysX
physx = PhysX()

# Load USD scene with physics setup
script_dir = Path(__file__).resolve().parent
usd_path = script_dir / ".." / "data" / "links_chain_sample.usda"
physx.add_usd(str(usd_path))

# Run a simulation step
dt = 1.0 / 60.0
elapsed_time = 0.0
physx.step(dt, elapsed_time)

print("Simulation step completed successfully")

physx.release()
print("Cleanup complete")
```

### C

Download the SDK from the [GitHub Releases](https://github.com/NVIDIA-Omniverse/PhysX/releases) page and extract it.

**CMakeLists.txt**

Every C sample uses `find_package(ovphysx)` and links against `ovphysx::ovphysx`. Here is the `CMakeLists.txt` for `hello_world_c`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(HelloWorldC C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(ovphysx REQUIRED)

add_executable(hello_world_c main.c)

# Explicitly set the language to C (not C++)
set_source_files_properties(main.c PROPERTIES LANGUAGE C)
set_target_properties(hello_world_c PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)


# Add compiler-specific flags to enforce strict C compilation
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # GCC/Clang: Add -Werror=implicit-function-declaration to catch C++ leakage
    target_compile_options(hello_world_c PRIVATE 
        -Werror=implicit-function-declaration
        -pedantic
    )
endif()

if(MSVC)
    # MSVC: Compile as C code explicitly
    target_compile_options(hello_world_c PRIVATE /TC)
endif()

target_link_libraries(hello_world_c PRIVATE ovphysx::ovphysx)
get_filename_component(OVPHYSX_TEST_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/../../data" ABSOLUTE)
target_compile_definitions(hello_world_c PRIVATE
    OVPHYSX_TEST_DATA="${OVPHYSX_TEST_DATA_DIR}"
)
if(WIN32)
    ovphysx_copy_runtime_dlls(hello_world_c)
endif()

```

Build by pointing `CMAKE_PREFIX_PATH` at the installed SDK (see [SDK Quickstart](quickstart.md) for details).

**Source**

```c
#include "ovphysx/ovphysx.h"
#include <stdio.h>

int main() {
  // Create PhysX instance with default args
  ovphysx_create_args create_args = OVPHYSX_CREATE_ARGS_DEFAULT;
  ovphysx_handle_t handle = 0;
  
  ovphysx_result_t result = ovphysx_create_instance(&create_args, &handle);
  if (result.status != OVPHYSX_API_SUCCESS) {
    fprintf(stderr, "Failed to create PhysX instance\n");
    return 1;
  }

  // Load USD scene
  ovphysx_string_t path_str = ovphysx_cstr(OVPHYSX_TEST_DATA "/simple_physics_scene.usda");
  ovphysx_string_t prefix_str = {NULL, 0};
  ovphysx_usd_handle_t usd_handle = 0;
  
  ovphysx_enqueue_result_t add_result = ovphysx_add_usd(handle, path_str, prefix_str, &usd_handle);
  if (add_result.status != OVPHYSX_API_SUCCESS) {
    fprintf(stderr, "Failed to load USD\n");
    ovphysx_destroy_instance(handle);
    return 1;
  }

  // Step the simulation
  ovphysx_enqueue_result_t step_result = ovphysx_step(handle, 0.016f, 0.0f);
  if (step_result.status != OVPHYSX_API_SUCCESS) {
    fprintf(stderr, "Failed to step simulation\n");
    ovphysx_destroy_instance(handle);
    return 1;
  }

  // Wait for step to complete
  ovphysx_op_wait_result_t step_wait_result = {0};
  ovphysx_result_t step_wait_status = ovphysx_wait_op(handle, step_result.op_index, UINT64_MAX, &step_wait_result);
  int step_ok = (step_wait_status.status == OVPHYSX_API_SUCCESS && step_wait_result.num_errors == 0);
  ovphysx_destroy_wait_result(&step_wait_result);
  if (!step_ok) {
    fprintf(stderr, "Simulation step failed\n");
    ovphysx_destroy_instance(handle);
    return 1;
  }

  printf("Simulation step completed successfully\n");

  ovphysx_destroy_instance(handle);
  printf("Cleanup complete\n");

  return 0;
}
```

## Result

After this tutorial, you can step the simulation from both Python and C and release all resources cleanly.
