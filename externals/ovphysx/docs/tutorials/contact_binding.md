# Contact Binding: Reading Contact Forces

Contact bindings let you read contact forces between **sensor** bodies and
**filter** bodies. A sensor is a rigid body prim (or a set of prims matched by a
USD path pattern) whose contacts you want to measure. A filter is a second set of
bodies whose contacts with each sensor you want to isolate.

No extra USD authoring is needed beyond the rigid bodies themselves.

## Prerequisites

- Complete the [Tensor Bindings](tensor_bindings.md) tutorial.
- Your USD scene has rigid body prims in contact (or that will come into contact
  during simulation).
- For CUDA output tensors, enable DirectGPU TensorAPI before creating the
  `PhysX` instance; `device="gpu"` alone only selects GPU dynamics. See
  [GPU Warmup and Determinism](../developer_guide.md#gpu-warmup-and-determinism).

## Key Concepts

- **Create the binding before the first step** whose contacts you want to observe.
  The binding registers an internal contact-report callback. No contact data exists
  until at least one `step()` has completed.
- Create contact bindings once outside simulation loops and reuse them. In Python,
  use the context-manager form or call `cb.destroy()` when finished; otherwise
  garbage collection emits `ResourceWarning` when it eventually releases the
  native binding.
- Reading before the first step returns all-zeros tensors.
- `dt` for the impulse-to-force conversion (`force = impulse / dt`) is taken
  automatically from the last `step()` call. You do not pass it manually.
- Result tensor shapes:
  - Net forces: `[S, 3]` — one 3-D force vector per matched sensor prim.
  - Force matrix: `[S, F, 3]` — force vectors per (sensor, filter) pair.
  - Detailed contact data: contact forces and separations use `[C, 1]`;
    positions and normals use `[C, 3]`; all are indexed by `[S, F]`
    count/start-index tensors.
  - Detailed friction data: friction forces and points use `[C, 3]` buffers
    indexed by `[S, F]` count/start-index tensors.

## Python

### Full Binding + Destroy

```python

    # --- 1. Initialize SDK and load scene ---
    physx = PhysX(device="cpu")
    data_dir = Path(__file__).resolve().parent.parent / "data"
    physx.add_usd(str(data_dir / "boxes_falling_on_groundplane.usda"))
    physx.wait_all()

    # --- 2. Create a contact binding BEFORE the first step ---
    # sensor_patterns: bodies whose contact forces you want to read.
    # filter_patterns: bodies to measure contacts against (one per sensor).
    # The binding must be created before any step() call whose contacts you
    # want to observe.
    cb = physx.create_contact_binding(
        sensor_patterns=["/World/Cube1"],
        filter_patterns=["/World/GroundPlane/CollisionMesh"],
        filters_per_sensor=1,
        max_contact_data_count=256,
    )

    sensor_count = cb.sensor_count   # number of matched sensor prims
    filter_count = cb.filter_count   # number of filter prims per sensor

    print(f"Sensors: {sensor_count}, filters per sensor: {filter_count}")

    # --- 3. Simulate until boxes land ---
    for _ in range(120):
        physx.step(1.0 / 60.0, 0.0)
    physx.wait_all()

    # --- 4. Read net contact forces: shape [S, 3] ---
    # dt is taken automatically from the last step() call.
    net_forces = np.zeros((sensor_count, 3), dtype=np.float32)
    cb.read_net_forces(net_forces)
    print("Net contact forces [S, 3]:", net_forces)

    # --- 5. Read contact force matrix: shape [S, F, 3] ---
    force_matrix = np.zeros((sensor_count, filter_count, 3), dtype=np.float32)
    cb.read_force_matrix(force_matrix)
    print("Contact force matrix [S, F, 3]:", force_matrix)

    # --- 6. Clean up first demo ---
    cb.destroy()

```

### Context-Manager Form (Recommended)

```python
    with physx.create_contact_binding(sensor_patterns=["/World/Cube1"]) as cb2:
        for _ in range(60):
            physx.step(1.0 / 60.0, 0.0)
        physx.wait_all()
        out = np.zeros((cb2.sensor_count, 3), dtype=np.float32)
        cb2.read_net_forces(out)
        print("Net forces (context manager):", out)
    # cb2 is automatically destroyed here
```

## C

```c
    /* 1. Initialize SDK */
    ovphysx_create_args args = OVPHYSX_CREATE_ARGS_DEFAULT;
    args.device = OVPHYSX_DEVICE_CPU;

    ovphysx_handle_t handle = 0;
    r = ovphysx_create_instance(&args, &handle);
    if (!check_result(r, "ovphysx_create_instance")) return 1;

    /* 2. Load scene */
    ovphysx_usd_handle_t usd_handle = 0;
    er = ovphysx_add_usd(handle,
                         ovphysx_cstr(OVPHYSX_TEST_DATA "/boxes_falling_on_groundplane.usda"),
                         (ovphysx_string_t){NULL, 0}, &usd_handle);
    if (!check_enqueue(er, "ovphysx_add_usd")) { ovphysx_destroy_instance(handle); return 1; }
    if (!wait_op(handle, er.op_index, "add_usd")) { ovphysx_destroy_instance(handle); return 1; }

    /* 3. Create contact binding BEFORE the first step.
     *    sensor: the falling box.  filter: the ground plane. */
    ovphysx_string_t sensors[1];
    sensors[0] = ovphysx_cstr("/World/Cube1");

    ovphysx_string_t filters[1];
    filters[0] = ovphysx_cstr("/World/GroundPlane/CollisionMesh");

    ovphysx_contact_binding_handle_t cb = 0;
    r = ovphysx_create_contact_binding(
        handle,
        sensors, 1,     /* 1 sensor pattern */
        filters, 1,     /* 1 filter pattern per sensor */
        256,            /* max raw contact pairs */
        &cb);
    if (!check_result(r, "ovphysx_create_contact_binding")) {
        ovphysx_destroy_instance(handle); return 1;
    }

    /* 4. Query matched sensor / filter counts */
    int32_t sensor_count = 0, filter_count = 0;
    r = ovphysx_get_contact_binding_spec(handle, cb, &sensor_count, &filter_count);
    if (!check_result(r, "ovphysx_get_contact_binding_spec")) {
        ovphysx_destroy_contact_binding(handle, cb);
        ovphysx_destroy_instance(handle);
        return 1;
    }
    printf("Sensors: %d  Filters per sensor: %d\n", sensor_count, filter_count);

    /* 5. Simulate until the box lands */
    for (int i = 0; i < 120; i++) {
        er = ovphysx_step(handle, 1.0f / 60.0f, 0.0f);
        if (!check_enqueue(er, "ovphysx_step")) {
            ovphysx_destroy_contact_binding(handle, cb);
            ovphysx_destroy_instance(handle);
            return 1;
        }
    }
    if (!wait_op(handle, er.op_index, "step")) {
        ovphysx_destroy_contact_binding(handle, cb);
        ovphysx_destroy_instance(handle);
        return 1;
    }

    /* 6. Read net contact forces: shape [S, 3].
     *    dt is taken automatically from the last ovphysx_step() call. */
    float* net_data   = NULL;
    int64_t* net_shp  = NULL;
    DLTensor net_tensor = make_tensor_f32_2d(
        (size_t)sensor_count, 3, &net_data, &net_shp);

    r = ovphysx_read_contact_net_forces(handle, cb, &net_tensor);
    if (!check_result(r, "ovphysx_read_contact_net_forces")) {
        free(net_data); free(net_shp);
        ovphysx_destroy_contact_binding(handle, cb);
        ovphysx_destroy_instance(handle);
        return 1;
    }
    printf("Net contact forces [%d, 3]:\n", sensor_count);
    for (int s = 0; s < sensor_count; s++) {
        printf("  sensor %d: fx=%.3f  fy=%.3f  fz=%.3f\n",
               s,
               net_data[s * 3 + 0],
               net_data[s * 3 + 1],
               net_data[s * 3 + 2]);
    }
    free(net_data); free(net_shp);

    /* 7. Read contact force matrix: shape [S, F, 3]. */
    float* mat_data   = NULL;
    int64_t* mat_shp  = NULL;
    DLTensor mat_tensor = make_tensor_f32_3d(
        (size_t)sensor_count, (size_t)filter_count, 3,
        &mat_data, &mat_shp);

    r = ovphysx_read_contact_force_matrix(handle, cb, &mat_tensor);
    if (!check_result(r, "ovphysx_read_contact_force_matrix")) {
        free(mat_data); free(mat_shp);
        ovphysx_destroy_contact_binding(handle, cb);
        ovphysx_destroy_instance(handle);
        return 1;
    }
    printf("Contact force matrix [%d, %d, 3]:\n", sensor_count, filter_count);
    for (int s = 0; s < sensor_count; s++) {
        for (int f = 0; f < filter_count; f++) {
            int base = (s * filter_count + f) * 3;
            printf("  [%d][%d]: fx=%.3f  fy=%.3f  fz=%.3f\n",
                   s, f,
                   mat_data[base + 0],
                   mat_data[base + 1],
                   mat_data[base + 2]);
        }
    }
    free(mat_data); free(mat_shp);

    printf("Contact binding sample completed successfully\n");

    /* 8. Destroy contact binding */
    ovphysx_destroy_contact_binding(handle, cb);
```

## Unfiltered Contacts

Pass `filter_patterns=None` and `filters_per_sensor=0` to collect contacts with
all bodies:

```python
cb = physx.create_contact_binding(
    sensor_patterns=["/World/robot/ee"],
    max_contact_data_count=512,
)
```

In C:

```c
ovphysx_string_t sensors[] = { ovphysx_cstr("/World/robot/ee") };
ovphysx_contact_binding_handle_t cb;
ovphysx_create_contact_binding(handle, sensors, 1, NULL, 0, 512, &cb);
```

## Multiple Sensors and Filters

The `filter_patterns` array is **flat** and must have length
`len(sensor_patterns) * filters_per_sensor`. Each block of `filters_per_sensor`
entries corresponds to one sensor:

```python
# 2 sensors, 2 filters each -> 4 filter entries total
cb = physx.create_contact_binding(
    sensor_patterns=["/World/robot_0/ee", "/World/robot_1/ee"],
    filter_patterns=[
        "/World/obstacle_A", "/World/obstacle_B",  # filters for robot_0/ee
        "/World/obstacle_A", "/World/obstacle_B",  # filters for robot_1/ee
    ],
    filters_per_sensor=2,
)
# force_matrix shape: [2, 2, 3]
```

## Detailed Contact and Friction Data

Use `cb.max_contact_data_count` to allocate reusable flat buffers. For each
sensor/filter pair, `counts[s, f]` and `start_indices[s, f]` identify the valid
slice inside the flat buffers.

`cb.sensor_paths` returns the resolved sensor paths in row order.
`cb.filter_paths` returns a nested `[sensor][filter]` list in column order.

Create the binding with `filter_patterns`, `filters_per_sensor > 0`, and
`max_contact_data_count > 0` before calling `read_contact_data()` or
`read_friction_data()`. The aggregate `read_net_forces()` and
`read_force_matrix()` calls do not require this detailed-contact capacity.
`counts` and `start_indices` may be `int32` or `uint32`; NumPy's default integer
dtype is usually `int64`, so allocate these arrays with an explicit dtype.

```python
C = cb.max_contact_data_count
contact_forces = np.zeros((C, 1), dtype=np.float32)
positions = np.zeros((C, 3), dtype=np.float32)
normals = np.zeros((C, 3), dtype=np.float32)
separations = np.zeros((C, 1), dtype=np.float32)
counts = np.zeros((cb.sensor_count, cb.filter_count), dtype=np.int32)
starts = np.zeros((cb.sensor_count, cb.filter_count), dtype=np.int32)

cb.read_contact_data(
    contact_forces,
    positions,
    normals,
    separations,
    counts,
    starts,
)

s = 0
f = 0
start = starts[s, f]
stop = start + counts[s, f]
sensor_filter_positions = positions[start:stop]

friction_forces = np.zeros((C, 3), dtype=np.float32)
friction_points = np.zeros((C, 3), dtype=np.float32)
friction_counts = np.zeros((cb.sensor_count, cb.filter_count), dtype=np.int32)
friction_starts = np.zeros((cb.sensor_count, cb.filter_count), dtype=np.int32)

cb.read_friction_data(
    friction_forces,
    friction_points,
    friction_counts,
    friction_starts,
)
```

Friction data is per friction anchor, not a pre-summed `[S, F, 3]` pair
force. To build a pair-level friction force tensor, sum
`friction_forces[start:stop]` for each `(sensor, filter)` pair using the
matching `friction_counts` and `friction_starts` entries.

This flat representation matches the underlying PhysX tensor API and avoids a
fixed per-pair contact-point dimension. Build a padded `[S, F, K, ...]` view in
application code only if that layout is useful for a specific algorithm.
