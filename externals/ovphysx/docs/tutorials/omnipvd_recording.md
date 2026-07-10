# OmniPVD Recording: Capture Physics Internals to .ovd

This tutorial shows how to record OmniPVD data from ovphysx to `.ovd` files for offline inspection in a Kit application. OmniPVD captures the full internal state of the PhysX simulation each frame — shapes, contacts, solver data — so you can debug and visualize physics behavior after the fact.

## Prerequisites

- Install ovphysx and confirm native libraries load.
- Prepare a USD file with physics objects.
- For offline inspection: a Kit application with the OmniPVD extension (`omni.physx.pvd`).

## Required Config

OmniPVD recording is controlled by two typed config fields:

| Config field (Python) | C builder | Description |
|---|---|---|
| `omnipvd_ovd_recording_directory` | `ovphysx_config_entry_omnipvd_ovd_recording_directory()` | Writable directory where `.ovd` files are saved |
| `omnipvd_output_enabled` | `ovphysx_config_entry_omnipvd_output_enabled()` | Enables OmniPVD data capture |

Both must be configured **before** the PhysX instance is created, because the recording pipeline is initialized during physics engine startup. Pass them via `PhysXConfig` (Python) or `config_entries` in `ovphysx_create_args` (C/C++).

The runtime auto-creates the recording directory if it does not exist.

## Code

### Python

```python
import glob
import os
import tempfile
from pathlib import Path

from ovphysx import PhysX, PhysXConfig

# Use a temporary directory for recording output.
# Replace with your own path for persistent recordings.
output_dir = tempfile.mkdtemp(prefix="ovphysx_pvd_")
print(f"OmniPVD recording directory: {output_dir}")

# Initialize PhysX with OmniPVD recording enabled.
# IMPORTANT: Both config fields must be passed at initialization, before the
# physics engine is created internally. The recording directory must be
# a valid, writable path.
physx = PhysX(
    config=PhysXConfig(
        omnipvd_ovd_recording_directory=output_dir,
        omnipvd_output_enabled=True,
    )
)

# Load a USD scene with physics objects
script_dir = Path(__file__).resolve().parent
usd_path = script_dir / ".." / "data" / "links_chain_sample.usda"
print(f"Loading USD scene: {usd_path}")

physx.add_usd(str(usd_path))
physx.wait_all()

# Run simulation — OmniPVD captures each frame automatically
dt = 1.0 / 60.0
n_steps = 120  # 2 seconds at 60 Hz
print(f"Simulating {n_steps} steps...")

for i in range(n_steps):
    physx.step_sync(dt, i * dt)

print("Simulation complete.")

# Destroying the instance finalizes the recording:
# the runtime renames tmp.ovd → <timestamp>_rec.ovd.
physx.release()
print("Cleanup complete")

# List the produced .ovd files
ovd_files = glob.glob(os.path.join(output_dir, "*_rec.ovd"))
if ovd_files:
    print(f"\nRecorded {len(ovd_files)} OVD file(s):")
    for f in ovd_files:
        size_kb = os.path.getsize(f) / 1024
        print(f"  {os.path.basename(f)}  ({size_kb:.1f} KB)")
    print("\nOpen these files in a Kit app with OmniPVD to inspect simulation data.")
else:
    print("\nWARNING: No .ovd files found. Check runtime logs for errors.")
```

### C++

**CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
project(OmniPvdRecordingCpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ovphysx REQUIRED)

add_executable(omnipvd_recording_cpp main.cpp)

target_link_libraries(omnipvd_recording_cpp PRIVATE ovphysx::ovphysx)
get_filename_component(OVPHYSX_TEST_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/../../data" ABSOLUTE)
target_compile_definitions(omnipvd_recording_cpp PRIVATE
    OVPHYSX_TEST_DATA="${OVPHYSX_TEST_DATA_DIR}"
)
if(WIN32)
    ovphysx_copy_runtime_dlls(omnipvd_recording_cpp)
endif()
```

**Source**

```cpp
#include "ovphysx/ovphysx.h"
#include "ovphysx/ovphysx_config.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

// Count files matching *_rec.ovd in the given directory.
static int count_ovd_files(const fs::path& dir) {
    int count = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const auto name = entry.path().filename().string();
        if (name.size() > 8 && name.substr(name.size() - 8) == "_rec.ovd")
            ++count;
    }
    return count;
}

int main() {
#if defined(__aarch64__) || defined(_M_ARM64)
    printf("OmniPVD is not supported on aarch64, skipping.\n");
    return 0;
#endif

    // Create a temporary output directory for OVD recording
    fs::path output_dir = fs::temp_directory_path() / "ovphysx_pvd_cpp_sample";
    std::error_code ec;
    fs::create_directories(output_dir, ec);
    if (ec) {
        fprintf(stderr, "Failed to create directory '%s': %s\n",
                output_dir.string().c_str(), ec.message().c_str());
        return 1;
    }
    std::string dir_str = output_dir.string();
    printf("OmniPVD recording directory: %s\n", dir_str.c_str());

    // Configure OmniPVD recording via typed config entries.
    // Both must be set before instance creation — the recording pipeline
    // initializes during physics engine startup.
    ovphysx_config_entry_t config[] = {
        ovphysx_config_entry_omnipvd_ovd_recording_directory(ovphysx_cstr(dir_str.c_str())),
        ovphysx_config_entry_omnipvd_output_enabled(true),
    };

    ovphysx_create_args create_args = OVPHYSX_CREATE_ARGS_DEFAULT;
    create_args.config_entries = config;
    create_args.config_entry_count = 2;
    ovphysx_handle_t handle = 0;

    ovphysx_result_t r = ovphysx_create_instance(&create_args, &handle);
    if (r.status != OVPHYSX_API_SUCCESS) {
        ovphysx_string_t err = ovphysx_get_last_error();
        fprintf(stderr, "Failed to create PhysX instance: %.*s\n",
                (int)(err.ptr ? err.length : 0), err.ptr ? err.ptr : "");
        return 1;
    }

    // Load USD scene
    ovphysx_string_t path_str = ovphysx_cstr(OVPHYSX_TEST_DATA "/simple_physics_scene.usda");
    ovphysx_string_t prefix_str = {nullptr, 0};
    ovphysx_usd_handle_t usd_handle = 0;

    ovphysx_enqueue_result_t add_result = ovphysx_add_usd(handle, path_str, prefix_str, &usd_handle);
    if (add_result.status != OVPHYSX_API_SUCCESS) {
        ovphysx_string_t err = ovphysx_get_last_error();
        fprintf(stderr, "Failed to load USD: %.*s\n",
                (int)(err.ptr ? err.length : 0), err.ptr ? err.ptr : "");
        ovphysx_destroy_instance(handle);
        return 1;
    }

    // Run simulation steps — OmniPVD records each frame
    const float dt = 1.0f / 60.0f;
    const int n_steps = 10;
    printf("Running %d simulation steps...\n", n_steps);

    for (int i = 0; i < n_steps; i++) {
        float current_time = static_cast<float>(i) * dt;
        ovphysx_result_t step_r = ovphysx_step_sync(handle, dt, current_time);
        if (step_r.status != OVPHYSX_API_SUCCESS) {
            ovphysx_string_t err = ovphysx_get_last_error();
            fprintf(stderr, "Step %d failed: %.*s\n", i,
                    (int)(err.ptr ? err.length : 0), err.ptr ? err.ptr : "");
            ovphysx_destroy_instance(handle);
            return 1;
        }
    }

    printf("Simulation complete.\n");

    // Destroying the instance finalizes the recording:
    // tmp.ovd is renamed to a timestamped *_rec.ovd file.
    ovphysx_destroy_instance(handle);

    // Verify that at least one OVD file was produced.
    // Return non-zero if not, so CI can catch regressions.
    int ovd_count = count_ovd_files(output_dir);
    if (ovd_count > 0) {
        printf("Recorded %d OVD file(s) in %s\n", ovd_count, dir_str.c_str());
        printf("Cleanup complete\n");
        return 0;
    }

    fprintf(stderr, "FAIL: No OVD files found in %s\n", dir_str.c_str());
    return 1;
}
```

## What Happens at Runtime

1. When `PhysX()` (or `ovphysx_create_instance`) is called with both config fields set, the runtime creates an OmniPVD writer backed by a file stream.
2. A temporary file `tmp.ovd` is created in the recording directory.
3. Each simulation step records a frame of physics state into `tmp.ovd`.
4. When the instance is destroyed (`release()` / `ovphysx_destroy_instance`), the runtime renames `tmp.ovd` to a timestamped file: `YYYY_MM_DD_HH_MM_SS_CC_rec.ovd` (where `CC` is a disambiguation counter).

If the recording directory is unset (empty string) or `omnipvd_output_enabled` is `false`, no recording takes place and no files are written.

## Inspecting .ovd Files in Kit

1. Open any Kit-based application (e.g., USD Composer, Isaac Sim).
2. Enable the **OmniPVD** extension: **Window > Extensions**, search for `omni.physx.pvd`, and enable it.
3. Use **File > Open** or the OmniPVD panel to load the `.ovd` file.
4. Use the timeline scrubber to step through recorded frames and inspect shapes, contacts, and solver state.

For more details on the Kit-side OmniPVD workflow, see the [PhysX Visual Debugger documentation](https://docs.omniverse.nvidia.com/kit/docs/omni_physics/latest/extensions/ux/source/omni.physx.pvd/docs/dev_guide/physx_visual_debugger.html).

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| No `.ovd` file after simulation | Config not set before instance creation | Pass both fields in `PhysXConfig` / `config_entries` at init |
| Empty recording directory | `omnipvd_output_enabled` is `false` | Set to `true` |
| `tmp.ovd` exists but no `*_rec.ovd` | Instance not properly destroyed | Ensure `release()` / `ovphysx_destroy_instance` is called |
| Runtime error about directory | Directory path is invalid or not writable | Use an absolute path to a writable location |

## Result

After this tutorial you can capture `.ovd` recordings from any ovphysx simulation and inspect them offline in Kit.
