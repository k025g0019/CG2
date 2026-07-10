# Developer Guide

Use this guide when you integrate, build, and operate ovphysx in your own application. You learn how to build the SDK, run samples, and apply core runtime rules for synchronization, threading, and resource ownership.

## Relationship and Consumption Model

ovphysx provides a stable C API and Python bindings on top of the Omni PhysX runtime, which itself uses the PhysX SDK.
Use ovphysx when you need USD-based physics simulation outside of Omniverse Kit with a small, self-contained SDK.

- Python: `pip install ovphysx` and `from ovphysx import PhysX`
- C/C++: download the SDK from the [GitHub Releases](https://github.com/NVIDIA-Omniverse/PhysX/releases) page, include headers from `include/ovphysx/`, and link against `ovphysx::ovphysx` via CMake

## Samples and Tutorials

The ovphysx samples are runnable references for SDK and wheel usage, designed to run in a clean environment matching how end users consume the wheel or SDK.

> **Note:** In the source repository, samples live under `tests/c_samples/` and `tests/python_samples/`. In the SDK package, these are installed to `samples/c_samples/` and `samples/python_samples/` respectively.

**Python Samples** (`tests/python_samples/`):

| Sample | Tutorial | Feature |
|---|---|---|
| `hello_world.py` | [Hello World](tutorials/hello_world.md) | Load USD + step (minimal workflow) |
| `tensor_bindings.py` | [Tensor Bindings](tutorials/tensor_bindings.md) | Read/write simulation data via tensor bindings |
| `contact_binding.py` | [Contact Binding](tutorials/contact_binding.md) | Read contact forces via sensor/filter bindings |
| `tensor_bindings_views.py` | | Build lightweight view wrappers on TensorBindingsAPI (advanced) |
| `omnipvd_recording.py` | [OmniPVD Recording](tutorials/omnipvd_recording.md) | Record physics internals to .ovd files |

**Extra Python Samples** (`tests/python_samples_extra/`):

| Sample | Tutorial | Feature |
|---|---|---|
| `visual_rerunio_sample/visualize_rigid_bodies.py` | [Rendering Handoff](tutorials/render_handoff.md) | Visualize rigid body simulation with Rerun |

**C/C++ Samples** (`tests/c_samples/`):

| Sample | Tutorial | Feature |
|---|---|---|
| `hello_world_c/` | [Hello World](tutorials/hello_world.md) | Minimal C hello world |
| `tensor_bindings_c/` | [Tensor Bindings](tutorials/tensor_bindings.md) | CPU tensor read/write |
| `tensor_bindings_gpu_c/` | | GPU tensor read/write with CUDA |
| `contact_binding_c/` | [Contact Binding](tutorials/contact_binding.md) | Contact force reading |
| `omnipvd_recording_cpp/` | [OmniPVD Recording](tutorials/omnipvd_recording.md) | Record physics internals to .ovd files |
| `physx_interop_cpp/` | [PhysX Interop](tutorials/physx_interop.md) | Direct PhysX SDK pointer access |

For SDK setup, see [SDK Quickstart](tutorials/quickstart.md).

For tensor binding shape/read/write semantics, see [Tensor Bindings](tutorials/tensor_bindings.md).
Canonical enum-level definitions are in `include/ovphysx/ovphysx_types.h` (`ovphysx_tensor_type_t`).

## Execution Model

ovphysx uses a stream-ordered execution model:

- Calls are enqueued in submission order and observe prior writes without extra sync.
- Asynchronous calls return an `op_index`; wait on it before consuming results outside the stream.
- Synchronous calls complete before returning and do not yield an `op_index`.

Use `ovphysx_wait_op()` (or `PhysX.wait_op()` in Python) to:
- synchronize before reading or modifying tensors on CPU/GPU if they're currently accessed by asynchronous operations inside ovphysx
- ensure correctness before external side-effects (logging, rendering, network I/O)

## Configuration

The SDK uses a typed config system for known settings, plus a raw Carbonite-setting override for arbitrary paths. Config entries are built
with convenience functions from `ovphysx_config.h` and passed at initialization or set at runtime.

### C

```c
#include "ovphysx/ovphysx_config.h"

// At initialization
ovphysx_create_args args = OVPHYSX_CREATE_ARGS_DEFAULT;
ovphysx_config_entry_t entries[] = {
    ovphysx_config_entry_disable_contact_processing(true),
    ovphysx_config_entry_num_threads(4),
    ovphysx_config_entry_carbonite(
        OVPHYSX_LITERAL("/physics/fabricUpdateVelocities"),
        OVPHYSX_LITERAL("true")),
};
args.config_entries = entries;
args.config_entry_count = 3;
ovphysx_create_instance(&args, &handle);

// At runtime
ovphysx_set_global_config(ovphysx_config_entry_num_threads(8));

// Typed getter
int32_t threads;
ovphysx_get_global_config_int32(OVPHYSX_CONFIG_NUM_THREADS, &threads);
```

### Python

```python
from ovphysx import PhysX, PhysXConfig, ConfigBool, ConfigInt32

# At initialization
physx = PhysX(config=PhysXConfig(
    disable_contact_processing=True,
    num_threads=4,
    carbonite_overrides={"/physics/fabricUpdateVelocities": True},
))

# Typed getter
threads = physx.get_config_int32(ConfigInt32.NUM_THREADS)
enabled = physx.get_config_bool(ConfigBool.DISABLE_CONTACT_PROCESSING)
```

The ``carbonite_overrides`` dict accepts arbitrary Carbonite setting paths for
settings not yet covered by the typed fields. Value types are auto-detected from the
string representation. Using a ``carbonite_overrides`` key that targets a path already
covered by a typed field raises ``ValueError``.

> **Note:** `carbonite_overrides` is write-only. There is no getter for arbitrary Carbonite paths; use the typed getters (`get_config_bool`, `get_config_int32`, etc.) for known settings.

Config is process-global (Carbonite-backed). All instances in the same process share the
same config state.

## Multi-Instance Support

The SDK supports multiple independent PhysX instances running concurrently within the same process.
Config is internally handled by Carbonite and therefore per-process, so different instances cannot use different config values in the same process.

**Important:** The Carbonite framework and its embedded plugin stack (including the Python interpreter) cannot be cleanly finalized and re-initialized within the same process. This means destroying all instances and then creating new ones is **not supported** as a general pattern. If you need isolated create/destroy cycles (e.g., for testing), run each cycle in a separate subprocess.

## Operation Indices and Polling

`op_index` values are single-use. After a successful wait, the index is consumed and must not be used again.
Polling is supported by passing `timeout_ns = 0` to `wait_op`.

## Threading

- Multiple ovphysx instances are safe to use concurrently across threads.
- A single instance is **not** thread-safe. Serialize access externally.
- Do not wait on the same `op_index` from multiple threads.

## Ownership and Lifetimes

- Tensor bindings, contact bindings, and attribute bindings own internal resources. They are automatically destroyed when the parent instance is destroyed via `ovphysx_destroy_instance()`. Explicit per-binding destruction (`ovphysx_destroy_tensor_binding`, `ovphysx_destroy_contact_binding`) is available for releasing resources earlier.
- In Python, use `with physx.create_tensor_binding(...) as binding:`, `with physx.create_contact_binding(...) as binding:`, or call `binding.destroy()` explicitly. If garbage collection cleans up a `TensorBinding` or `ContactBinding` first, ovphysx emits `ResourceWarning`. Python suppresses this warning category by default; run with `-W default` or set `PYTHONWARNINGS=default` to show it.
- Create tensor and contact bindings once outside simulation loops and reuse them across steps. Creating a new binding every step allocates new native resources and is slower than reusing the existing binding.
- On failure, call `ovphysx_get_last_error()` on the same thread to retrieve the error string (valid until the next ovphysx call on that thread).
- PhysX pointers from `ovphysx_get_physx_ptr()` are owned by ovphysx — do not call `release()` on them. See [PhysX Pointer Interop](#physx-pointer-interop) for lifetime details.
- Contact report buffers from `ovphysx_get_contact_report()` are valid until the next simulation step.

## Dependency Management

For SDK and wheel users, dependencies are bundled with the package:

- Runtime dependencies are loaded from the packaged plugins directory next to the library
  (`_install/plugins/` in the SDK, `ovphysx/plugins/` in the wheel).
- The wheel includes the native runtime stack and required plugins.
- Python exposes only TensorBindingsAPI; the legacy `ovphysx.tensors` compatibility layer
  is no longer shipped.
- No additional dependency fetch step is required for normal package usage.
- Auto-detects library location via `getLibraryDirectory()`
- Pre-loads shared libraries with `RTLD_GLOBAL` for plugin symbol resolution
- Offline-capable

For source builds, namespaced monolithic USD is the only supported layout. The
classic USD build switch is not present.

## Error Handling

**For C:**
- Check `result.status` on every call.
- On failure, call `ovphysx_get_last_error()` on the same thread to retrieve the error string (valid until the next ovphysx call on that thread).
- For `ovphysx_wait_op()`, iterate over `error_op_indices` and call `ovphysx_get_last_op_error()` per failed op index, then free the result with `ovphysx_destroy_wait_result()`.

**For Python:**
- Runtime errors are raised on failed calls.
- Use try/finally or context managers to ensure bindings are destroyed.
- Pass `raise_if_empty=True` to `create_tensor_binding()` when a zero-count binding should be treated as a configuration error.

## GPU Warmup and Determinism

GPU tensor reads require a warmup step that initializes DirectGPU buffers.
This is done automatically on the first tensor operation.
If deterministic initial state matters, call `ovphysx_warmup_gpu()` explicitly after USD load and before the first tensor read.

`device="gpu"` and DirectGPU TensorAPI are separate choices. `device="gpu"`
enables GPU PhysX dynamics, but high-throughput CUDA TensorBinding and
ContactBinding views require opting into DirectGPU before instance creation:

```python
from ovphysx import PhysX, PhysXConfig

physx = PhysX(
    device="gpu",
    config=PhysXConfig(
        carbonite_overrides={
            "/physics/suppressReadback": True,
            "/physics/suppressFabricUpdate": True,
        },
    ),
)
```

Use this for IsaacLab-style tensor workloads that read state or contact tensors
every step. Leave DirectGPU off for scenes that need contact modification, such
as surface velocity or custom contact callbacks.

## Scene Cloning

Two paths drive scene cloning, both fully supported and funneling into the same `ovphysx_clone` plugin / `IPhysxReplicator->replicate()` machinery:

1. **Direct API.** Standalone callers (no ovstage Stage attached) use `ovphysx_clone()` (C), `PhysX::clone()` (C++), or `PhysX.clone()` (Python). Async multi-target convenience: pass a source path, a list of target paths, and an optional flat array of per-target parent transforms `(px, py, pz, qx, qy, qz, qw)`. Returns an op_index; wait via `ovphysx_wait_op` or the language-specific equivalent. See [`tests/python_samples/clone.py`](../tests/python_samples/clone.py) and [`tests/c_samples/clone_c/main.c`](../tests/c_samples/clone_c/main.c).

2. **Bridge / ovstage flow.** Apps that have an attached `ovstage.Stage` call `ovstage_clone_subtree()` on the Stage; the OvstageBridge consumes the resulting `OVSTAGE_EVENT_CLONE` events surfaced by `ovstage_query_changes()` during `physx.step()` ingest and drives the same plugin via the synchronous internal helper `ovphysx_clone_replicate_internal()`. Per-target initial transforms are issued as ordinary `xformOp:translate` writes through ovstage in the same `begin_frame`/`end_frame` block as the clone, and ride the bridge's normal dirty-attribute walk. See `ovstage-notes/native-integration-plan.md` for the full event contract.

Use whichever fits your call-site. If your app already orchestrates via ovstage, the bridge flow comes for free; otherwise the direct API has zero ovstage dependency.


## Remote USD Loading

`add_usd()` accepts any URI that the Omniverse Client Library supports:

- **Local paths:** `/path/to/scene.usd` (as before)
- **Omniverse Nucleus:** `omniverse://server/path/scene.usd`
- **S3 (HTTPS):** `https://my-bucket.s3.us-east-1.amazonaws.com/path/scene.usd`
- **Azure Blob:** `https://account.blob.core.windows.net/container/scene.usd`

The client library is loaded automatically when an ovphysx instance is created. No additional setup is needed for Nucleus URIs when the server allows anonymous access.

> **Note:** Use HTTPS S3 URLs (virtual-hosted style), not `s3://` URIs. The OmniClient library resolves S3 assets via HTTPS and adds AWS authentication automatically when credentials are configured.

### Configuring S3 Credentials

For private S3 buckets, configure credentials before calling `add_usd()`.
The examples below use placeholder strings; do not commit real credentials in source code.

#### Python

```python
import ovphysx

physx = ovphysx.PhysX()

ovphysx.configure_s3(
    host="my-bucket.s3.us-east-1.amazonaws.com",
    bucket="my-bucket",
    region="us-east-1",
    access_key_id="<AWS_ACCESS_KEY_ID>",
    secret_access_key="<AWS_SECRET_ACCESS_KEY>",
    session_token=None,       # optional, for temporary credentials
)

physx.add_usd("https://my-bucket.s3.us-east-1.amazonaws.com/scenes/robot.usd")
```

#### C

```c
ovphysx_configure_s3(
    "my-bucket.s3.us-east-1.amazonaws.com",  /* host */
    "my-bucket",      /* bucket */
    "us-east-1",      /* region */
    "<AWS_ACCESS_KEY_ID>",  /* access_key_id */
    "<AWS_SECRET_ACCESS_KEY>",  /* secret_access_key */
    NULL               /* session_token (optional) */
);
```

#### C++

```cpp
ovphysx::PhysX::configureS3(
    "my-bucket.s3.us-east-1.amazonaws.com", "my-bucket", "us-east-1",
    "<AWS_ACCESS_KEY_ID>", "<AWS_SECRET_ACCESS_KEY>");
```

### Configuring Azure SAS Tokens

#### Python

```python
ovphysx.configure_azure_sas(
    host="myaccount.blob.core.windows.net",
    container="usd-assets",
    sas_token="<AZURE_SAS_TOKEN>",
)
```

#### C

```c
ovphysx_configure_azure_sas(
    "myaccount.blob.core.windows.net",
    "usd-assets",
    "<AZURE_SAS_TOKEN>"
);
```

### Notes

- Credentials are **process-global** — they apply to all ovphysx instances.
- Configure credentials **before** `add_usd()`.
- An ovphysx instance must exist before calling credential functions (the runtime must be loaded).
- The client library dependency is shared with ovrtx (Kit rendering) via the Carbonite plugin system — no static linking or version conflicts.

## Scene Queries

ovphysx provides three scene query functions covering raycast, sweep, and overlap:

| Function | Purpose | Geometry input |
|---|---|---|
| `ovphysx_raycast` | Cast a ray | Origin + direction |
| `ovphysx_sweep` | Move a shape along a direction | Geometry desc + direction |
| `ovphysx_overlap` | Test shape overlap at a position | Geometry desc |

Each function accepts a **mode** parameter:

- `CLOSEST` -- return the single closest hit (0 or 1 result)
- `ANY` -- return whether any hit exists (0 or 1, hit fields zeroed)
- `ALL` -- return all hits

**Geometry types** for sweep/overlap:

- `SPHERE` -- radius + center position
- `BOX` -- half-extents + position + orientation quaternion
- `SHAPE` -- any UsdGeomGPrim by prim path (meshes use convex approximation internally)

Hit results are stored in an **internal buffer** owned by the ovphysx instance, valid
until the next scene query call on the same instance.

### Python Example

```python
import ovphysx
from ovphysx import SceneQueryMode, SceneQueryGeometryType

# Raycast downward
hits = physx.raycast(
    origin=[0, 10, 0],
    direction=[0, -1, 0],
    distance=100.0,
    mode=SceneQueryMode.CLOSEST,
)
if hits:
    print(f"Hit at distance {hits[0]['distance']}")

# Sweep a sphere
hits = physx.sweep(
    geometry_type=SceneQueryGeometryType.SPHERE,
    direction=[1, 0, 0],
    distance=50.0,
    radius=0.5,
    position=[0, 1, 0],
)

# Overlap test
hits = physx.overlap(
    geometry_type=SceneQueryGeometryType.BOX,
    half_extent=[1, 1, 1],
    position=[0, 0, 0],
    rotation=[0, 0, 0, 1],
)
```

### Path Encoding

Hit results contain `collision`, `rigid_body`, and `material` fields as uint64-encoded
SdfPaths matching the internal Omni PhysX representation. Consumers that need to compare
hit paths against known prims should use the same encoding.


## PhysX Pointer Interop

For advanced use cases that go beyond the TensorBindingsAPI -- such as custom joint
manipulation or direct body property access -- ovphysx can return raw PhysX SDK object
pointers by USD prim path.

```c
void* ptr = NULL;
ovphysx_result_t r = ovphysx_get_physx_ptr(
    handle, "/World/physicsScene", OVPHYSX_PHYSX_TYPE_SCENE, &ptr);
// Cast: physx::PxScene* scene = static_cast<physx::PxScene*>(ptr);
```

The `ovphysx_physx_type_t` enum specifies which PhysX type to look up
(scene, actor, articulation link, joint, shape, material, etc.).
The C API returns `void*`; the caller casts to the appropriate PhysX SDK type.

Casting requires the PhysX SDK C++ headers (e.g. `PxScene.h`,
`PxRigidDynamic.h`). The ovphysx SDK ships these headers under
`include/physx/`; `find_package(ovphysx)` sets `ovphysx_PHYSX_INCLUDE_DIR`
to point there. No PhysX library linking is needed.

See the [PhysX Interop tutorial](tutorials/physx_interop.md) for a
complete sample that uses `setKinematicTarget()` on a `PxRigidDynamic*`.

The C++ experimental API provides a type-safe overload that deduces the
enum from the pointer type, preventing mismatches at compile time:

```cpp
physx::PxScene* scene = nullptr;
physx.getPhysXPtr("/World/physicsScene", scene);  // enum auto-deduced
```

Python returns integer pointer addresses for passing to C/C++ code:

```python
ptr = physx.get_physx_ptr("/World/physicsScene", ovphysx.PhysXType.SCENE)
```

### Pointer Lifetime

Returned pointers are valid from acquisition until `ovphysx_remove_usd()`,
`ovphysx_reset()`, or instance destruction. Calls to `ovphysx_step()` do
**not** invalidate existing pointers. Do not call
`release()` on returned pointers — ovphysx owns them.

### Thread Safety

PhysX APIs on returned pointers must only be called between simulation
steps — specifically after `wait_op()` completes for the preceding step
and before the next `ovphysx_step()` call. Calling PhysX APIs while a
step is in-flight is a data race.

## Contact Data

ovphysx provides two complementary APIs for reading contact information.
Choose the one that fits your use case:

| | Contact Binding | Contact Report |
|---|---|---|
| **Use when** | You need aggregate force tensors for RL rewards, safety limits, or force monitoring | You need per-contact-point geometry (position, normal, impulse) for custom contact sensors or collision analysis |
| **Data shape** | `[S, 3]` net forces or `[S, F, 3]` force matrix (DLPack tensors, GPU-compatible) | Variable-length arrays of event headers + contact points (raw buffers) |
| **API style** | Create a binding, then read tensors each step | Call once per step, receive pointers to internal buffers |
| **USD requirement** | No extra schema — just rigid body prims | Prims must have `PhysxContactReportAPI` applied |
| **Key functions** | `create_contact_binding()`, `read_contact_net_forces()`, `read_contact_force_matrix()` | `get_contact_report()` |

### Contact Binding (Aggregate Force Tensors)

Contact bindings give you aggregate net force vectors between sets of sensor
and filter bodies, delivered as DLPack tensors that work on both CPU and GPU.
No extra USD schema is required.

See the [Contact Binding tutorial](tutorials/contact_binding.md) for a full
walkthrough. Key points:

- Create the binding **before** the first step whose contacts you want to observe.
- Net forces shape: `[S, 3]` — one force vector per matched sensor.
- Force matrix shape: `[S, F, 3]` — per (sensor, filter) pair.
- CUDA output tensors require DirectGPU TensorAPI; `device="gpu"` by itself
  only selects GPU dynamics. See [GPU Warmup and Determinism](#gpu-warmup-and-determinism).
- `dt` for impulse-to-force conversion is taken automatically from the last step.

### Contact Report (Per-Point Event Data)

The contact report exposes the raw per-step contact events collected by the
Omni PhysX runtime. This gives you every individual contact point with
position, normal, impulse, and separation — useful for custom contact sensors,
collision debugging, or building higher-level contact processing.

Prims must have `PhysxContactReportAPI` applied in the USD stage for contacts
to be reported.

```c
const ovphysx_contact_event_header_t* headers = NULL;
const ovphysx_contact_point_t* data = NULL;
uint32_t num_headers = 0, num_data = 0;

// Basic contact report (headers + contact points)
ovphysx_get_contact_report(handle, &headers, &num_headers, &data, &num_data,
                           NULL, NULL);
// Access fields directly: headers[0].actor0, data[0].position[1], etc.

// With friction anchors
const ovphysx_friction_anchor_t* anchors = NULL;
uint32_t num_anchors = 0;
ovphysx_get_contact_report(handle, &headers, &num_headers, &data, &num_data,
                           &anchors, &num_anchors);
```

The friction anchor parameters are optional -- pass NULL to skip them.

The C API returns typed struct pointers defined in `ovphysx_types.h`.
The C++ experimental API provides aliases (`PhysX::ContactEventHeader`,
`PhysX::ContactPoint`, `PhysX::FrictionAnchor`) and a typed
`getContactReport()` method.

Data is valid until the next simulation step. A typical usage pattern:

1. Apply `PhysxContactReportAPI` to prims of interest.
2. `step()` + `wait_all()`.
3. Call `get_contact_report()` to read that step's contacts.
4. Parse event headers for contact pairs; index into contact data for per-point details.

In Python:

```python
report = physx.get_contact_report()
for i in range(report["num_headers"]):
    h = report["headers"][i]
    print(f"pair {i}: actor0={h.actor0:#x}, {h.numContactData} points")
for j in range(report["num_points"]):
    p = report["points"][j]
    print(f"  pos=({p.position[0]:.3f}, {p.position[1]:.3f}, {p.position[2]:.3f})")
```

## Running With Other Carbonite Users

ovphysx can share a process with other OV libraries that use Carbonite and the
same namespaced USD runtime. For example, if another library has already loaded
the namespaced USD monolith, ovphysx reuses that loaded USD library instead of
loading a second copy.

ovphysx does not reuse a PhysX plugin stack loaded by another Carbonite user. If
`omni.physx.plugin` or `IPhysxSimulation` is already registered before
`ovphysx_create_instance()` starts, creation fails with a clear error. ovphysx
must load and own its bundled PhysX plugins.

When another USD-aware subsystem, such as ovrtx, shares the same namespaced USD
runtime, publish every subsystem's schema/plugin paths before either subsystem
opens a USD stage or otherwise touches the USD schema registry:

```c
#include <stddef.h>
#include <ovphysx/ovphysx.h>
#include <ovrtx/ovrtx.h>

int main(void)
{
    ovphysx_register_schema_paths();
    ovrtx_register_schema_paths();

    ovphysx_create_args physx_args = OVPHYSX_CREATE_ARGS_DEFAULT;
    ovphysx_handle_t physx = OVPHYSX_INVALID_HANDLE;
    ovphysx_create_instance(&physx_args, &physx);

    ovrtx_config_t rtx_config = { 0 };
    ovrtx_renderer_t* renderer = NULL;
    ovrtx_create_renderer(&rtx_config, &renderer);
    return 0;
}
```

For ovphysx-only applications, this explicit call is optional:
`ovphysx_create_instance()` registers the same ovphysx schema path automatically.
The explicit call is for multi-subsystem processes where later initialization
order should not decide which USD schemas are visible.

Important process rules:

- Sharing an already-loaded namespaced USD library is supported.
- Call each subsystem's schema-path registration function before the first USD
  stage or schema-registry access. Late calls cannot repair an already-populated
  schema registry.
- Sharing a pre-loaded Carbonite PhysX plugin stack is not supported.
- Kit or Isaac Sim processes that already loaded `omni.physx` extensions are not
  supported ovphysx embedding targets.
- Device settings are process-global. If `/physics/cudaDevice` or
  `/physics/suppressReadback` is already set to a conflicting value,
  `ovphysx_create_instance()` fails instead of silently using the wrong mode.
- If another Carbonite user already set the app directory, ovphysx preserves it.

## Logging

ovphysx uses [Carbonite](https://docs.omniverse.nvidia.com/kit/docs/carbonite/) as its internal logging backend.
By default, the global log level is `LogLevel.WARNING` — only warnings and errors are emitted.

### Controlling the Log Level

#### Python

```python
import ovphysx

ovphysx.set_log_level(ovphysx.LogLevel.VERBOSE)
print(ovphysx.get_log_level())
```

#### C

```c
#include <ovphysx/ovphysx.h>

// Set before or after instance creation — applies globally to all outputs.
ovphysx_set_log_level(OVPHYSX_LOG_VERBOSE);

uint32_t current = ovphysx_get_log_level();
```

### Custom Log Callbacks (C)

Register one or more callbacks to receive log messages programmatically.
The caller must ensure the callback and any resources it references remain
valid until it is unregistered. If the callback and its resources naturally
outlive the process (e.g. a static function with no `user_data`), calling
`ovphysx_unregister_log_callback()` is not required. When called, it
guarantees the callback is not running on any thread and will never be
invoked again.

```c
void my_logger(uint32_t level, const char* message, void* user_data) {
    fprintf(stderr, "[%u] %s\n", level, message);
}

ovphysx_register_log_callback(my_logger, NULL);
// Run simulation here.
ovphysx_unregister_log_callback(my_logger, NULL);
// Safe to destroy any resources referenced by the callback here.
```

### Controlling Default Console Output

By default, Carbonite logs to the console. When a custom callback is registered
that also writes to the console, output may be doubled. Use
`ovphysx_enable_default_log_output()` to suppress the built-in console logger:

#### Python

```python
ovphysx.enable_python_logging()
ovphysx.enable_default_log_output(False)
```

#### C

```c
ovphysx_register_log_callback(my_logger, NULL);
ovphysx_enable_default_log_output(false);  // only my_logger receives messages now
```

### Python Logging Bridge

Route native log messages into Python's standard `logging` module:

```python
import logging
import ovphysx

# Route native messages to the "ovphysx" Python logger
ovphysx.enable_python_logging()

# Add a handler to see the output
logging.getLogger("ovphysx").addHandler(logging.StreamHandler())
logging.getLogger("ovphysx").setLevel(logging.DEBUG)

# Run simulation here; native messages appear in Python logging.

ovphysx.disable_python_logging()
```

## OmniPVD Recording

ovphysx supports recording PhysX simulation internals to `.ovd` files via OmniPVD. The resulting files can be opened in any Kit application with the OmniPVD extension (`omni.physx.pvd`) for frame-by-frame inspection of shapes, contacts, and solver state.

Recording is controlled by two typed config fields, both set at instance creation:

| Python field | C builder | Description |
|---|---|---|
| `omnipvd_ovd_recording_directory` | `ovphysx_config_entry_omnipvd_ovd_recording_directory()` | Directory for `.ovd` output |
| `omnipvd_output_enabled` | `ovphysx_config_entry_omnipvd_output_enabled()` | Enable recording |

Both must be set **before** the PhysX instance is created. The recording directory is auto-created if it does not exist.

```python
from ovphysx import PhysX, PhysXConfig

physx = PhysX(
    config=PhysXConfig(
        omnipvd_ovd_recording_directory="/tmp/pvd_output",
        omnipvd_output_enabled=True,
    )
)

physx.add_usd("scene.usda")
physx.wait_all()
for i in range(100):
    physx.step_sync(1/60, i/60)

physx.release()  # finalizes recording → <timestamp>_rec.ovd
```

No additional runtime dependencies or plugins are needed beyond the standard ovphysx SDK or wheel — the OmniPVD writer is built into the bundled PhysX runtime.

For the full tutorial with C examples and Kit inspection instructions, see [OmniPVD Recording](tutorials/omnipvd_recording.md).

You now have the core integration rules for building, running, and operating ovphysx. For the full C API reference, see the [C API Reference](api.md). For Python, see the [Python API Reference](python_api.rst).
