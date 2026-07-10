---
name: tensor-bindings-cpu
description: Create tensor bindings to read and write physics simulation data on CPU using numpy arrays. Use when you need to exchange simulation state (poses, velocities, joint targets) with your application via tensors.
version: "0.1.0"
author: NVIDIA Omniverse Physics
tags:
  - ovphysx
  - physics
  - tensor-bindings
  - cpu
tools:
  - Read
  - Shell
compatibility: "ovphysx 0.4 wheel or SDK; Python examples require NumPy, and C examples require SDK headers and libraries."
---

# Tensor Bindings: CPU Read and Write

Tensor bindings map USD prim patterns to typed tensor views, enabling bulk data exchange with NumPy, PyTorch, Warp, or any other DLPack-compatible framework.

## When to Use

Use this skill when a caller needs bulk CPU tensor reads or writes for simulation state, such as poses, velocities, or joint targets, through the public TensorBindingsAPI.

## Instructions

1. Read `docs/tutorials/tensor_bindings.md` and the sample for the caller's language before changing code.
2. Load the USD scene, wait for it to finish, create bindings once from stable prim patterns, then reuse them to read or write tensors with the binding shape and dtype.
3. Use Shell to run the Python sample or compile the C sample after adapting the scene path and tensor type.

## Python

```python
from ovphysx import PhysX
from ovphysx.types import TensorType
import numpy as np

physx = PhysX(device="cpu")
physx.add_usd("scene.usda")
physx.wait_all()

# Create a tensor binding for DOF velocity targets
binding = physx.create_tensor_binding(
    pattern="/World/articulation/articulationLink*",
    tensor_type=TensorType.ARTICULATION_DOF_VELOCITY_TARGET,
)

# Write control inputs
targets = np.zeros(binding.shape, dtype=np.float32)
targets[0, 0] = 25.0  # set first DOF velocity target
binding.write(targets)

# Step and read back
physx.step(0.01, 0.0)
physx.wait_all()

output = np.zeros(binding.shape, dtype=np.float32)
binding.read(output)

# Clean up
binding.destroy()
physx.release()
```

Full sample:
- `samples/python_samples/tensor_bindings.py` (wheel)
- Source checkout: `tests/python_samples/tensor_bindings.py`

## C

Full sample:
- `samples/c_samples/tensor_bindings_c/main.c` (SDK)
- Source checkout: `tests/c_samples/tensor_bindings_c/main.c`

## Common tensor types

| Constant | Data |
|----------|------|
| `TensorType.RIGID_BODY_POSE` | Rigid body positions + quaternions |
| `TensorType.ARTICULATION_DOF_POSITION` | Joint positions |
| `TensorType.ARTICULATION_DOF_VELOCITY_TARGET` | Joint velocity drive targets |
| `TensorType.ARTICULATION_LINK_POSE` | Articulation link poses |

See `include/ovphysx/ovphysx_types.h` for the full list.

## Key APIs

| Python | C |
|--------|---|
| `physx.create_tensor_binding(pattern, tensor_type)` | `ovphysx_create_tensor_binding()` |
| `binding.read(output)` | `ovphysx_read_tensor_binding()` |
| `binding.write(input)` | `ovphysx_write_tensor_binding()` |
| `binding.destroy()` | `ovphysx_destroy_tensor_binding()` |

## Partial updates (RL-style)

TensorBindings supports selectively applying actions without changing the binding:
- **Masked write**: pass a bool/uint8 mask of shape `[N]` (1 = update, 0 = keep old value).
  - Python: `binding.write(tensor, mask=mask)`
  - C: `ovphysx_write_tensor_binding_masked()`
- **Indexed write**: pass an int32 index tensor of shape `[K]` (rows to update).
  - Python: `binding.write(tensor, indices=indices)`
  - C: `ovphysx_write_tensor_binding(..., index_tensor)`

## References

- Docs: `docs/tutorials/tensor_bindings.md`
- Python sample: `samples/python_samples/tensor_bindings.py` (wheel; source: `tests/python_samples/tensor_bindings.py`)
- C sample: `samples/c_samples/tensor_bindings_c/main.c` (SDK; source: `tests/c_samples/tensor_bindings_c/main.c`)
