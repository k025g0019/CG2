# Cloning: Replicate Environments

This tutorial shows how to use the clone API to replicate sub-sections of a USD scene. Cloning creates copies in the internal physics representation (not USD prims), optimized for large-scale parallel simulation.

## Prerequisites

- Complete the [Hello World](hello_world.md) tutorial.
- Use a USD scene with a hierarchy suitable for cloning (e.g. `/World/envs/env0`).

## Code Language

### Python

```python
def main():
    # Initialize PhysX SDK
    physx = PhysX(device="cpu")

    # Load USD scene containing /World/envs/env0
    script_dir = Path(__file__).resolve().parent
    usd_path = script_dir / ".." / "data" / "basic_simulation.usda"

    print(f"Loading USD scene: {usd_path}")
    usd_handle, _ = physx.add_usd(str(usd_path))
    physx.wait_all()

    # Clone env0 to create env1, env2, env3
    targets = ["/World/envs/env1", "/World/envs/env2", "/World/envs/env3"]
    print(f"Cloning /World/envs/env0 to {len(targets)} targets...")
    physx.clone("/World/envs/env0", targets)
    physx.wait_all()
    print(f"  Created {len(targets)} clones successfully")

    # Create a tensor binding to read rigid body poses across all environments
    # The pattern matches the "table" rigid body in every cloned environment
    pose_binding = physx.create_tensor_binding(
        pattern="/World/envs/env*/table",
        tensor_type=TensorType.RIGID_BODY_POSE,
    )
    print(f"  Rigid body binding: count={pose_binding.count}, shape={pose_binding.shape}")

    # Run simulation with all environments
    print("Running 10 simulation steps...")
    dt = 1.0 / 60.0
    for i in range(10):
        physx.step(dt, i * dt)
    physx.wait_all()
    print("  All steps completed")

    # Read poses from all environments after simulation
    poses = np.zeros(pose_binding.shape, dtype=np.float32)
    pose_binding.read(poses)
    for env_idx in range(poses.shape[0]):
        px, py, pz = poses[env_idx, 0:3]
        print(f"  env{env_idx}: pos=({px:.4f}, {py:.4f}, {pz:.4f})")

    print("Clone sample completed successfully")

    pose_binding.destroy()
    physx.remove_usd(usd_handle)
    physx.release()
    print("Cleanup complete")
```

### C

```c
static int wait_op_success(ovphysx_handle_t handle, ovphysx_enqueue_result_t res, uint64_t timeout_ns) {
  if (res.status != OVPHYSX_API_SUCCESS) {
    return 0;
  }
  ovphysx_op_wait_result_t wait_result = {0};
  ovphysx_result_t wait_res = ovphysx_wait_op(handle, res.op_index, timeout_ns, &wait_result);
  int success = (wait_res.status == OVPHYSX_API_SUCCESS && wait_result.num_errors == 0);
  ovphysx_destroy_wait_result(&wait_result);
  return success;
}

int main() {
  printf("=== ovphysx Clone Example (C API) ===\n\n");

  // Initialize create args with defaults
  ovphysx_create_args create_args = OVPHYSX_CREATE_ARGS_DEFAULT;

  // Create PhysX instance
  printf("Creating PhysX instance...\n");
  ovphysx_handle_t handle = 0;
  ovphysx_result_t create_res = ovphysx_create_instance(&create_args, &handle);
  if (create_res.status != OVPHYSX_API_SUCCESS) {
    fprintf(stderr, "Failed to create PhysX instance\n");
    return 1;
  }
  printf("  [OK] PhysX instance created\n\n");

  // Load USD scene with physics content
  printf("Loading USD scene...\n");
  ovphysx_usd_handle_t usd_handle = 0;
  ovphysx_enqueue_result_t load_res = ovphysx_add_usd(handle, ovphysx_cstr(OVPHYSX_TEST_DATA "/basic_simulation.usda"), ovphysx_cstr(""), &usd_handle);
  if (!wait_op_success(handle, load_res, 10ULL * 1000 * 1000 * 1000)) {
    fprintf(stderr, "USD loading failed or timed out\n");
    ovphysx_destroy_instance(handle);
    return 1;
  }
  printf("  [OK] USD scene loaded\n\n");

  // Clone the environment (source: env0, targets: env1, env2, env3)
  printf("Cloning /World/envs/env0 to create env1, env2, env3...\n");
  const char* clone_targets[] = {
    "/World/envs/env1",
    "/World/envs/env2",
    "/World/envs/env3"
  };
  enum { NUM_TARGETS = 3 };

  ovphysx_string_t target_strings[NUM_TARGETS];
  for (uint32_t i = 0; i < NUM_TARGETS; ++i) {
    target_strings[i] = ovphysx_cstr(clone_targets[i]);
  }

  ovphysx_enqueue_result_t clone_res = ovphysx_clone(
      handle,
      ovphysx_cstr("/World/envs/env0"),
      target_strings,
      NUM_TARGETS,
      NULL);
  if (!wait_op_success(handle, clone_res, 10ULL * 1000 * 1000 * 1000)) {
    fprintf(stderr, "Clone operation failed or timed out\n");
    ovphysx_destroy_instance(handle);
    return 1;
  }
  printf("  [OK] Created 3 clones successfully\n\n");

  // Run a few simulation steps to verify clones work correctly
  printf("Running simulation with clones (10 steps)...\n");
  for (int i = 0; i < 10; i++) {
    ovphysx_enqueue_result_t step_res = ovphysx_step(handle, 1.0f/60.0f, (float)i / 60.0f);
    if (!wait_op_success(handle, step_res, 10ULL * 1000 * 1000 * 1000)) {
      fprintf(stderr, "Failed to run simulation step %d\n", i);
      ovphysx_destroy_instance(handle);
      return 1;
    }
  }
  printf("  [OK] All 10 simulation steps completed successfully\n\n");

  printf("=== Clone Example Completed Successfully ===\n");

  ovphysx_result_t destroy_res = ovphysx_destroy_instance(handle);
  printf("Cleanup complete\n");

  return 0;
}
```

## Result

After this tutorial, you can replicate environments via the clone API and simulate all copies together.
