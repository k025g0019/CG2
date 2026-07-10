# Tensor Bindings: Read and Write Simulation Data

This tutorial shows how to read and write simulation data through tensor bindings after you load a USD stage. You learn how to use path patterns to bind multiple prims in one call.

## Prerequisites

- Complete the [Hello World](hello_world.md) tutorial.
- Use a USD scene that contains physics-enabled prims matching your binding pattern.

## Code Language

### Python

```python
    # Create tensor bindings for DOF velocity targets (control inputs)
    # Pattern matches all articulation links - the API will automatically map to DOFs
    print("Creating tensor binding for DOF velocity targets...")
    velocity_target_binding = physx.create_tensor_binding(
        pattern="/World/articulation/articulationLink*",
        tensor_type=TensorType.ARTICULATION_DOF_VELOCITY_TARGET,
    )
    print(f"  DOF count: {velocity_target_binding.shape[1]}")

    # Create tensor binding for link poses (state observation)
    print("Creating tensor binding for link poses...")
    link_pose_binding = physx.create_tensor_binding(
        pattern="/World/articulation/articulationLink*",
        tensor_type=TensorType.ARTICULATION_LINK_POSE,
    )
    print(f"  Link count: {link_pose_binding.shape[1]}, Pose dims: {link_pose_binding.shape[2]}")

    # Set joint drive velocities (alternating +25 and -25 rad/s)
    num_dofs = velocity_target_binding.shape[1]
    velocity_targets = np.zeros(velocity_target_binding.shape, dtype=np.float32)
    for i in range(num_dofs):
        velocity_targets[0, i] = 25.0 if i % 2 == 0 else -25.0

    print("Setting DOF velocity targets (alternating ±25 rad/s)...")
    velocity_target_binding.write(velocity_targets)
    print(f"  Velocity targets: {velocity_targets[0, :5]}... (first 5 DOFs)")

    # Run extended simulation with periodic readback
    print("\nRunning 1000 simulation steps...")

    # Allocate buffer for reading link poses
    link_poses = np.zeros(link_pose_binding.shape, dtype=np.float32)

    dt = 0.01
    for i in range(1000):
        # Step simulation forward
        current_time = i * dt
        physx.step(dt, current_time)
        physx.wait_all()

        # Read link poses periodically (tensor API is synchronous, no wait needed)
        if i % 100 == 0 or i == 999:
            link_pose_binding.read(link_poses)

            # Extract first link's pose (position + quaternion)
            # Pose format: [px, py, pz, qx, qy, qz, qw]
            px, py, pz = link_poses[0, 0, 0:3]
            qx, qy, qz, qw = link_poses[0, 0, 3:7]

            # Compute X-axis rotation angle (roll) from quaternion
            roll_x_rad = math.atan2(2.0 * (qw * qx + qy * qz), 1.0 - 2.0 * (qx * qx + qy * qy))
            deg_x = roll_x_rad * 180.0 / math.pi

            print(
                f"  Step {i:4d}: pos=({px:.6f}, {py:.6f}, {pz:.6f}), "
                f"quat(xyzw)=({qx:.6f}, {qy:.6f}, {qz:.6f}, {qw:.6f}), "
                f"rotation_x={deg_x:.2f}°"
            )

    print("\nCompleted 1000 simulation steps successfully!")

    velocity_target_binding.destroy()
    link_pose_binding.destroy()
    physx.remove_usd(usd_handle)
    physx.release()
    print("Cleanup complete")
```

### C

Create tensor bindings, write control targets, step, and read back state:

```c
    // 3. Create tensor bindings
    // 3a. DOF velocity target binding (write control targets)
    ovphysx_tensor_binding_handle_t dof_target_binding = 0;
    ovphysx_tensor_binding_desc_t dof_target_desc = {
        .pattern = OVPHYSX_LITERAL("/World/articulation"),
        .tensor_type = OVPHYSX_TENSOR_ARTICULATION_DOF_VELOCITY_TARGET_F32
    };

    result = ovphysx_create_tensor_binding(handle, &dof_target_desc, &dof_target_binding);
    if (!check_result(result, "create DOF target binding")) {
        ovphysx_destroy_instance(handle);
        return 1;
    }

    // 3b. Articulation link pose binding
    ovphysx_tensor_binding_handle_t link_pose_binding = 0;
    ovphysx_tensor_binding_desc_t link_pose_desc = {
        .pattern = OVPHYSX_LITERAL("/World/articulation"),
        .tensor_type = OVPHYSX_TENSOR_ARTICULATION_LINK_POSE_F32
    };

    result = ovphysx_create_tensor_binding(handle, &link_pose_desc, &link_pose_binding);
    if (!check_result(result, "create articulation link pose binding")) {
        ovphysx_destroy_instance(handle);
        return 1;
    }

    printf("Tensor bindings created.\n");

    // 4. Query binding specs and allocate tensors
    ovphysx_tensor_spec_t dof_spec, link_pose_spec;

    result = ovphysx_get_tensor_binding_spec(handle, dof_target_binding, &dof_spec);
    if (!check_result(result, "get_tensor_binding_spec (dof target)")) {
        ovphysx_destroy_instance(handle);
        return 1;
    }

    result = ovphysx_get_tensor_binding_spec(handle, link_pose_binding, &link_pose_spec);
    if (!check_result(result, "get_tensor_binding_spec (link pose)")) {
        ovphysx_destroy_instance(handle);
        return 1;
    }

    printf("\nBinding specs:\n");
    printf("  Articulation DOFs: shape=[%lld, %lld], ndim=%d\n",
           (long long)dof_spec.shape[0], (long long)dof_spec.shape[1], dof_spec.ndim);
    printf("  Articulation link poses: shape=[%lld, %lld, %lld], ndim=%d\n",
           (long long)link_pose_spec.shape[0],
           (long long)link_pose_spec.shape[1],
           (long long)link_pose_spec.shape[2],
           link_pose_spec.ndim);

    // Allocate CPU tensors
    const size_t dof_count = (size_t)dof_spec.shape[0];
    const size_t dof_components = (size_t)dof_spec.shape[1];
    const size_t link_pose_batch = (size_t)link_pose_spec.shape[0];
    const size_t link_count = (size_t)link_pose_spec.shape[1];
    const size_t link_pose_components = (size_t)link_pose_spec.shape[2];

    TensorBuffer dof_target_tensor = make_tensor_f32_2d(dof_count, dof_components);
    TensorBuffer link_pose_tensor = make_tensor_f32_3d(link_pose_batch, link_count, link_pose_components);

    // 5. Set initial DOF velocity targets and simulate
    printf("\n=== Setting initial DOF velocity targets ===\n");

    // Initialize all targets to 0.0
    float* dof_target_data = (float*)dof_target_tensor.data;
    for (size_t i = 0; i < dof_count * dof_components; i++) {
        dof_target_data[i] = 0.0f;
    }
    printf("\n=== Writing initial DOF velocity targets ===\n");

    result = ovphysx_write_tensor_binding(handle, dof_target_binding, &dof_target_tensor.tensor, NULL);
    if (!check_result(result, "write initial DOF targets")) {
        destroy_tensor(&dof_target_tensor);
        destroy_tensor(&link_pose_tensor);
        ovphysx_destroy_instance(handle);
        return 1;
    }

    // 6. Simulation loop
    const float dt = 1.0f / 60.0f;
    float sim_time = 0.0f;
    const size_t link_index_to_print = (link_count > 0) ? (link_count - 1) : 0;

    printf("Running 120 simulation steps...\n");
    for (int step = 0; step < 120; ++step) {
        // Update DOF targets every 50 steps
        if (step % 50 == 0) {
            // Alternate between positive and negative target velocities
            float target_vel = ((step / 50) % 2 == 0) ? 50.0f : -50.0f;
            for (size_t i = 0; i < dof_count * dof_components; ++i) {
                // Alternate direction for each DOF
                dof_target_data[i] = (i % 2 == 0) ? target_vel : -target_vel;
            }

            result = ovphysx_write_tensor_binding(handle, dof_target_binding, &dof_target_tensor.tensor, NULL);
            if (!check_result(result, "write DOF targets")) {
                destroy_tensor(&dof_target_tensor);
                destroy_tensor(&link_pose_tensor);
                ovphysx_destroy_instance(handle);
                return 1;
            }
        }

        // Step simulation
        ovphysx_enqueue_result_t step_result = ovphysx_step(handle, dt, sim_time);
        if (step_result.status != OVPHYSX_API_SUCCESS) {
            fprintf(stderr, "ERROR in step enqueue (status=%d)\n", (int)step_result.status);
            {
                ovphysx_string_t err = ovphysx_get_last_error();
                if (err.ptr && err.length > 0)
                    fprintf(stderr, "  %.*s\n", (int)err.length, err.ptr);
            }
            destroy_tensor(&dof_target_tensor);
            destroy_tensor(&link_pose_tensor);
            ovphysx_destroy_instance(handle);
            return 1;
        }
        if (!wait_op(handle, step_result.op_index, "step")) {
            destroy_tensor(&dof_target_tensor);
            destroy_tensor(&link_pose_tensor);
            ovphysx_destroy_instance(handle);
            return 1;
        }
        sim_time += dt;

        // Read and print state every 30 steps
        if (step % 30 == 0) {
            // Read articulation link poses
            result = ovphysx_read_tensor_binding(handle, link_pose_binding, &link_pose_tensor.tensor);
            if (!check_result(result, "read articulation link poses")) {
                destroy_tensor(&dof_target_tensor);
                destroy_tensor(&link_pose_tensor);
                ovphysx_destroy_instance(handle);
                return 1;
            }

            const float* link_pose_data = (const float*)link_pose_tensor.data;

            size_t articulation_index = 0;
            size_t link_pose_offset = (articulation_index * link_count + link_index_to_print) * link_pose_components;
            printf("Step %3d | Link %zu pos=(%.3f, %.3f, %.3f) quat=(%.3f, %.3f, %.3f, %.3f)\n",
                   step,
                   link_index_to_print,
                   link_pose_data[link_pose_offset + 0],
                   link_pose_data[link_pose_offset + 1],
                   link_pose_data[link_pose_offset + 2],
                   link_pose_data[link_pose_offset + 3],
                   link_pose_data[link_pose_offset + 4],
                   link_pose_data[link_pose_offset + 5],
                   link_pose_data[link_pose_offset + 6]);

        }
    }

    // Cleanup
    printf("\n=== Cleanup ===\n");

    destroy_tensor(&dof_target_tensor);
    destroy_tensor(&link_pose_tensor);

    ovphysx_destroy_tensor_binding(handle, dof_target_binding);
    ovphysx_destroy_tensor_binding(handle, link_pose_binding);

    printf("=== Articulation control sample completed successfully ===\n");
```

For GPU tensor bindings with CUDA, see `tensor_bindings_gpu_c/` in the samples
directory. For maximum performance in tensor-heavy loops, `device="gpu"` is not
enough by itself: enable DirectGPU TensorAPI before creating the `PhysX`
instance with `/physics/suppressReadback=true`. See
[GPU Warmup and Determinism](../developer_guide.md#gpu-warmup-and-determinism).

## Tensor Type Reference

Use this table to pre-allocate tensors without probing `binding.shape` at runtime.

Symbols:
- `N`: rigid body count in the binding
- `A`: articulation count in the binding
- `L`: max link count across matched articulations
- `D`: max DOF count across matched articulations
- `T`: max tendon count across matched articulations (fixed or spatial, depending on type)
- `M`: generalized coordinate count — `numDofs` for fixed-base, `numDofs + 6` for floating-base articulations
- `S`: max collision shape count per body/link in the binding
- `R`, `C`: Jacobian shape from `getJacobianShape()` — fixed-base: `R=L*6, C=D`; floating-base: `R=(L-1)*6+6, C=D+6`

**Rigid Body State**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_RIGID_BODY_POSE_F32` | `(N, 7)` | 2D | yes | yes | `pos.xyz + quat.xyzw` | World-frame rigid body transforms |
| `OVPHYSX_TENSOR_RIGID_BODY_VELOCITY_F32` | `(N, 6)` | 2D | yes | yes | `lin.xyz + ang.xyz` | World-frame linear and angular velocity |
| `OVPHYSX_TENSOR_RIGID_BODY_ACCELERATION_F32` | `(N, 6)` | 2D | yes | no | `lin_acc.xyz + ang_acc.xyz` | World-frame linear and angular acceleration |
| `OVPHYSX_TENSOR_RIGID_BODY_FORCE_F32` | `(N, 3)` | 2D | no | yes | `force.xyz` | Write-only force at center of mass (control input) |
| `OVPHYSX_TENSOR_RIGID_BODY_WRENCH_F32` | `(N, 9)` | 2D | no | yes | `force.xyz + torque.xyz + pos.xyz` | Write-only wrench-at-position in world frame |

**Rigid Body Properties (standalone, non-articulated bodies)**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_RIGID_BODY_MASS_F32` | `(N,)` | 1D | yes | yes | mass scalar | Scalar mass per rigid body |
| `OVPHYSX_TENSOR_RIGID_BODY_INV_MASS_F32` | `(N,)` | 1D | yes | no | inverse mass scalar | Computed from mass; read-only |
| `OVPHYSX_TENSOR_RIGID_BODY_INERTIA_F32` | `(N, 9)` | 2D | yes | yes | row-major 3x3 | Inertia tensor in body frame |
| `OVPHYSX_TENSOR_RIGID_BODY_INV_INERTIA_F32` | `(N, 9)` | 2D | yes | no | row-major 3x3 | Computed from inertia; read-only |
| `OVPHYSX_TENSOR_RIGID_BODY_COM_POSE_F32` | `(N, 7)` | 2D | yes | yes | `pos.xyz + quat.xyzw` | COM local pose in body frame |

Rigid body property tensors in this table are CPU tensors even when the
simulation is running on GPU. State tensors such as pose, velocity,
acceleration, force, and wrench use the simulation device.

For Python bindings, `binding.prim_paths` returns row metadata only; tensor
reads and writes keep using the shapes above. Rigid-body bindings return one
rigid body prim path per row. Articulation bindings return one articulation
root prim path per `A` row; link names remain available through
`binding.body_names`.

**Rigid Body Shape Properties**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_RIGID_BODY_SHAPE_FRICTION_AND_RESTITUTION_F32` | `(N, S, 3)` | 3D | yes | yes | `(static_friction, dynamic_friction, restitution)` | Per-shape material properties |
| `OVPHYSX_TENSOR_RIGID_BODY_CONTACT_OFFSET_F32` | `(N, S)` | 2D | yes | yes | offset scalar per shape | Distance at which contacts are generated |
| `OVPHYSX_TENSOR_RIGID_BODY_REST_OFFSET_F32` | `(N, S)` | 2D | yes | yes | offset scalar per shape | Rest separation between shapes |

Shape property tensors in this table are CPU tensors even when the simulation
is running on GPU.

**Articulation Root State**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_ARTICULATION_ROOT_POSE_F32` | `(A, 7)` | 2D | yes | yes | `pos.xyz + quat.xyzw` | Root body transform per articulation |
| `OVPHYSX_TENSOR_ARTICULATION_ROOT_VELOCITY_F32` | `(A, 6)` | 2D | yes | yes | `lin.xyz + ang.xyz` | Root body velocity per articulation |

**Articulation Link State**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_ARTICULATION_LINK_POSE_F32` | `(A, L, 7)` | 3D | yes | no | `pos.xyz + quat.xyzw` | Per-link pose; padded links are zero |
| `OVPHYSX_TENSOR_ARTICULATION_LINK_VELOCITY_F32` | `(A, L, 6)` | 3D | yes | no | `lin.xyz + ang.xyz` | Per-link velocity; read-only |
| `OVPHYSX_TENSOR_ARTICULATION_LINK_ACCELERATION_F32` | `(A, L, 6)` | 3D | yes | no | `lin_acc.xyz + ang_acc.xyz` | Per-link linear and angular acceleration; read-only |
| `OVPHYSX_TENSOR_ARTICULATION_LINK_WRENCH_F32` | `(A, L, 9)` | 3D | no | yes | `force.xyz + torque.xyz + pos.xyz` | Write-only per-link external wrench |

**Articulation DOF State and Control**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_ARTICULATION_DOF_POSITION_F32` | `(A, D)` | 2D | yes | yes | joint position scalar per DOF | Joint-space position in articulation DOF order |
| `OVPHYSX_TENSOR_ARTICULATION_DOF_VELOCITY_F32` | `(A, D)` | 2D | yes | yes | joint velocity scalar per DOF | Joint-space velocity in articulation DOF order |
| `OVPHYSX_TENSOR_ARTICULATION_DOF_POSITION_TARGET_F32` | `(A, D)` | 2D | yes | yes | target position scalar per DOF | Position-control targets |
| `OVPHYSX_TENSOR_ARTICULATION_DOF_VELOCITY_TARGET_F32` | `(A, D)` | 2D | yes | yes | target velocity scalar per DOF | Velocity-control targets |
| `OVPHYSX_TENSOR_ARTICULATION_DOF_ACTUATION_FORCE_F32` | `(A, D)` | 2D | yes | yes | actuation scalar per DOF | Readback is from staging buffer; may differ from solver-applied force |

**Articulation DOF Properties**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_ARTICULATION_DOF_STIFFNESS_F32` | `(A, D)` | 2D | yes | yes | stiffness scalar per DOF | PD position-control stiffness |
| `OVPHYSX_TENSOR_ARTICULATION_DOF_DAMPING_F32` | `(A, D)` | 2D | yes | yes | damping scalar per DOF | PD velocity-control damping |
| `OVPHYSX_TENSOR_ARTICULATION_DOF_LIMIT_F32` | `(A, D, 2)` | 3D | yes | yes | `(lower, upper)` per DOF | Joint position limits |
| `OVPHYSX_TENSOR_ARTICULATION_DOF_MAX_VELOCITY_F32` | `(A, D)` | 2D | yes | yes | max velocity scalar per DOF | Per-DOF velocity clamp |
| `OVPHYSX_TENSOR_ARTICULATION_DOF_MAX_FORCE_F32` | `(A, D)` | 2D | yes | yes | max force scalar per DOF | Per-DOF force/torque clamp |
| `OVPHYSX_TENSOR_ARTICULATION_DOF_ARMATURE_F32` | `(A, D)` | 2D | yes | yes | armature scalar per DOF | Added inertia at each DOF |
| `OVPHYSX_TENSOR_ARTICULATION_DOF_FRICTION_PROPERTIES_F32` | `(A, D, 3)` | 3D | yes | yes | `(static, dynamic, viscous)` per DOF | Friction coefficients at each DOF |

**Articulation Body Properties**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_ARTICULATION_BODY_MASS_F32` | `(A, L)` | 2D | yes | yes | mass scalar per link | Scalar mass per articulation link |
| `OVPHYSX_TENSOR_ARTICULATION_BODY_COM_POSE_F32` | `(A, L, 7)` | 3D | yes | yes | `pos.xyz + quat.xyzw` | COM local pose in body frame per link |
| `OVPHYSX_TENSOR_ARTICULATION_BODY_INERTIA_F32` | `(A, L, 9)` | 3D | yes | yes | row-major 3x3 | Inertia tensor in COM frame per link |
| `OVPHYSX_TENSOR_ARTICULATION_BODY_INV_MASS_F32` | `(A, L)` | 2D | yes | no | inverse mass scalar per link | Computed from mass; read-only |
| `OVPHYSX_TENSOR_ARTICULATION_BODY_INV_INERTIA_F32` | `(A, L, 9)` | 3D | yes | no | row-major 3x3 | Computed from inertia; read-only |

**Articulation Shape Properties**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_ARTICULATION_SHAPE_FRICTION_AND_RESTITUTION_F32` | `(A, S, 3)` | 3D | yes | yes | `(static_friction, dynamic_friction, restitution)` | Per-shape material properties per link |
| `OVPHYSX_TENSOR_ARTICULATION_CONTACT_OFFSET_F32` | `(A, S)` | 2D | yes | yes | offset scalar per shape | Distance at which contacts are generated |
| `OVPHYSX_TENSOR_ARTICULATION_REST_OFFSET_F32` | `(A, S)` | 2D | yes | yes | offset scalar per shape | Rest separation between shapes |

Shape property tensors in this table are CPU tensors even when the simulation
is running on GPU.

**Articulation Dynamics Queries (read-only)**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_ARTICULATION_JACOBIAN_F32` | `(A, R, C)` | 3D | yes | no | row-major | Shape from `getJacobianShape()`; see `R`, `C` in symbol legend above |
| `OVPHYSX_TENSOR_ARTICULATION_MASS_MATRIX_F32` | `(A, M, M)` | 3D | yes | no | row-major square | Generalized mass matrix; shape from `getGeneralizedMassMatrixShape()` |
| `OVPHYSX_TENSOR_ARTICULATION_CORIOLIS_AND_CENTRIFUGAL_FORCE_F32` | `(A, M)` | 2D | yes | no | force scalar per generalized coordinate | Combined Coriolis and centrifugal forces |
| `OVPHYSX_TENSOR_ARTICULATION_GRAVITY_FORCE_F32` | `(A, M)` | 2D | yes | no | force scalar per generalized coordinate | Gravity compensation forces |
| `OVPHYSX_TENSOR_ARTICULATION_LINK_INCOMING_JOINT_FORCE_F32` | `(A, L, 6)` | 3D | yes | no | `force.xyz + torque.xyz` | Incoming joint force and torque per link |
| `OVPHYSX_TENSOR_ARTICULATION_DOF_PROJECTED_JOINT_FORCE_F32` | `(A, D)` | 2D | yes | no | scalar per DOF | Projected joint forces |

**Fixed Tendon Properties**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_ARTICULATION_FIXED_TENDON_STIFFNESS_F32` | `(A, T)` | 2D | yes | yes | stiffness scalar per tendon | Requires articulation with fixed tendons |
| `OVPHYSX_TENSOR_ARTICULATION_FIXED_TENDON_DAMPING_F32` | `(A, T)` | 2D | yes | yes | damping scalar per tendon | Requires articulation with fixed tendons |
| `OVPHYSX_TENSOR_ARTICULATION_FIXED_TENDON_LIMIT_STIFFNESS_F32` | `(A, T)` | 2D | yes | yes | limit stiffness scalar per tendon | Requires articulation with fixed tendons |
| `OVPHYSX_TENSOR_ARTICULATION_FIXED_TENDON_LIMIT_F32` | `(A, T, 2)` | 3D | yes | yes | `(lower, upper)` per tendon | Fixed tendon position limits |
| `OVPHYSX_TENSOR_ARTICULATION_FIXED_TENDON_REST_LENGTH_F32` | `(A, T)` | 2D | yes | yes | rest length scalar per tendon | Requires articulation with fixed tendons |
| `OVPHYSX_TENSOR_ARTICULATION_FIXED_TENDON_OFFSET_F32` | `(A, T)` | 2D | yes | yes | offset scalar per tendon | Requires articulation with fixed tendons |

**Spatial Tendon Properties**

| Constant | Shape | Dimensionality | Read | Write | Component layout | Behavioral note |
|---|---|---|---|---|---|---|
| `OVPHYSX_TENSOR_ARTICULATION_SPATIAL_TENDON_STIFFNESS_F32` | `(A, T)` | 2D | yes | yes | stiffness scalar per tendon | Requires articulation with spatial tendons |
| `OVPHYSX_TENSOR_ARTICULATION_SPATIAL_TENDON_DAMPING_F32` | `(A, T)` | 2D | yes | yes | damping scalar per tendon | Requires articulation with spatial tendons |
| `OVPHYSX_TENSOR_ARTICULATION_SPATIAL_TENDON_LIMIT_STIFFNESS_F32` | `(A, T)` | 2D | yes | yes | limit stiffness scalar per tendon | Requires articulation with spatial tendons |
| `OVPHYSX_TENSOR_ARTICULATION_SPATIAL_TENDON_OFFSET_F32` | `(A, T)` | 2D | yes | yes | offset scalar per tendon | Requires articulation with spatial tendons |

For canonical enum definitions and low-level semantics, see `include/ovphysx/ovphysx_types.h`.

## Result

After this tutorial, you can create tensor bindings, push batched simulation inputs, and read back batched results.
