---
name: clone-environments
description: Clone one USD environment subtree into many runtime-only PhysX environments for RL-style batched simulation. Use when a caller needs replicated physics environments, clone API ordering, or target transform guidance.
version: "0.1.0"
author: NVIDIA Omniverse Physics
tags:
  - ovphysx
  - physics
  - cloning
  - rl
tools:
  - Read
  - Shell
compatibility: "ovphysx 0.4 wheel or SDK; source USD must contain the source prim path before cloning."
---

# Clone Environments

Use the clone API to replicate a USD source subtree into runtime-only PhysX copies.
The source hierarchy must exist in the loaded USD stage; targets are created in the runtime scene and do not edit the USD file.

Clone before `warmup_gpu()` or the first simulation step. Wait for the clone operation before creating bindings that should match cloned prims.

## When to Use

Use this skill when a caller needs many copies of one loaded environment for RL-style batched simulation, wants the clone API call order, or needs the target transform layout.

## Instructions

1. Read `docs/tutorials/cloning.md` and the sample for the caller's language before changing code.
2. Load the USD scene, wait for the load operation, call clone before `warmup_gpu()` or the first simulation step, and wait for clone completion before creating bindings.
3. Use Shell to run the Python sample or compile the C sample after adapting the source and target paths.

## Python

```python
from ovphysx import PhysX

physx = PhysX(device="cpu")
physx.add_usd("scene.usda")
physx.wait_all()

targets = ["/World/envs/env1", "/World/envs/env2", "/World/envs/env3"]
physx.clone("/World/envs/env0", targets)
physx.wait_all()

physx.release()
```

Use `parent_transforms` when each clone needs an initial parent pose.
Provide one transform per target: Python uses a list with `len(parent_transforms) == len(targets)`, and C uses a flat `num_targets * 7` float array.
Each transform is `(px, py, pz, qx, qy, qz, qw)`.

Full sample:
- `samples/python_samples/clone.py` (wheel)
- Source checkout: `tests/python_samples/clone.py`

## C

```c
#include <ovphysx/ovphysx.h>
#include <stddef.h>
#include <stdint.h>

static int wait_for_op(ovphysx_handle_t handle, ovphysx_op_index_t op_index)
{
    const uint64_t kTenSecondsNs = 10ULL * 1000ULL * 1000ULL * 1000ULL;

    ovphysx_op_wait_result_t wait_result = {0};
    ovphysx_result_t wait_status =
        ovphysx_wait_op(handle, op_index, kTenSecondsNs, &wait_result);
    int ok = wait_status.status == OVPHYSX_API_SUCCESS && wait_result.num_errors == 0;
    ovphysx_destroy_wait_result(&wait_result);
    return ok;
}

static int load_and_clone_envs(ovphysx_handle_t handle)
{
    ovphysx_usd_handle_t usd_handle = 0;
    ovphysx_enqueue_result_t add_result = ovphysx_add_usd(
        handle,
        ovphysx_cstr("scene.usda"),
        ovphysx_cstr(""),
        &usd_handle);
    if (add_result.status != OVPHYSX_API_SUCCESS) {
        return 0;
    }
    if (!wait_for_op(handle, add_result.op_index)) {
        return 0;
    }

    ovphysx_string_t targets[3] = {
        ovphysx_cstr("/World/envs/env1"),
        ovphysx_cstr("/World/envs/env2"),
        ovphysx_cstr("/World/envs/env3")
    };

    ovphysx_enqueue_result_t clone_result = ovphysx_clone(
        handle,
        ovphysx_cstr("/World/envs/env0"),
        targets,
        3,
        NULL);
    if (clone_result.status != OVPHYSX_API_SUCCESS) {
        return 0;
    }

    return wait_for_op(handle, clone_result.op_index);
}
```

Full sample:
- `samples/c_samples/clone_c/main.c` (SDK)
- Source checkout: `tests/c_samples/clone_c/main.c`

## Requirements

- Source path exists in the loaded USD stage.
- Target paths are unique and do not already exist.
- Clone before GPU warmup or the first simulation step.
- Configure collision isolation through USD collision groups/filtering, not through the clone API.

## Key APIs

| Python | C |
|--------|---|
| `physx.clone(source, targets)` | `ovphysx_clone()` |
| `physx.wait_all()` | `ovphysx_wait_op()` |

## References

- Docs: `docs/tutorials/cloning.md`
- Python sample: `samples/python_samples/clone.py` (wheel; source: `tests/python_samples/clone.py`)
- C sample: `samples/c_samples/clone_c/main.c` (SDK; source: `tests/c_samples/clone_c/main.c`)
