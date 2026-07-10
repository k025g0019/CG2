# PhysX Interop: Direct PhysX SDK Access

This tutorial shows how to get a raw PhysX SDK pointer from ovphysx and call
PhysX methods directly. The sample moves a kinematic rigid body using
`PxRigidDynamic::setKinematicTarget()`, then reads back the pose via a tensor
binding to confirm the move.

## Prerequisites

- Complete the [Hello World](hello_world.md) tutorial.
- Familiarity with the ovphysx C API (`ovphysx_get_physx_ptr()`).
- A C++17 compiler.

## Setup

The ovphysx SDK ships PhysX headers under `include/physx/`. Include them in your
project via the `ovphysx_PHYSX_INCLUDE_DIR` CMake variable (set automatically by
`find_package(ovphysx)`). No PhysX library linking is needed.

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(PhysxInteropCpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ovphysx REQUIRED)

add_executable(physx_interop_cpp main.cpp)

target_link_libraries(physx_interop_cpp PRIVATE ovphysx::ovphysx)

# PhysX SDK headers shipped with the ovphysx SDK (headers only, no library linking needed)
target_include_directories(physx_interop_cpp PRIVATE ${ovphysx_PHYSX_INCLUDE_DIR})

get_filename_component(OVPHYSX_TEST_DATA_DIR "${CMAKE_CURRENT_LIST_DIR}/../../data" ABSOLUTE)
target_compile_definitions(physx_interop_cpp PRIVATE
    OVPHYSX_TEST_DATA="${OVPHYSX_TEST_DATA_DIR}"
)
if(WIN32)
    ovphysx_copy_runtime_dlls(physx_interop_cpp)
endif()
```

Note: `ovphysx_PHYSX_INCLUDE_DIR` is set automatically by `find_package(ovphysx)`.

## Source

```cpp
#include "ovphysx/ovphysx.h"
#include "ovphysx/ovphysx_types.h"

// PhysX SDK headers (shipped with the ovphysx SDK under include/physx/)
#include "PxRigidDynamic.h"
#include "foundation/PxTransform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static bool check_result(ovphysx_result_t result, const char* context) {
    if (result.status != OVPHYSX_API_SUCCESS) {
        ovphysx_string_t err = ovphysx_get_last_error();
        fprintf(stderr, "ERROR in %s: ", context);
        if (err.ptr && err.length > 0) {
            fprintf(stderr, "%.*s\n", (int)err.length, err.ptr);
        } else {
            fprintf(stderr, "status=%d\n", (int)result.status);
        }
        return false;
    }
    return true;
}

static bool wait_op(ovphysx_handle_t handle, ovphysx_op_index_t op_index, const char* context) {
    ovphysx_op_wait_result_t wait_result = {};
    ovphysx_result_t result = ovphysx_wait_op(handle, op_index, 10ULL * 1000 * 1000 * 1000, &wait_result);

    bool ok = (result.status == OVPHYSX_API_SUCCESS && wait_result.num_errors == 0);
    if (!ok) {
        fprintf(stderr, "ERROR in %s: %s\n", context,
                wait_result.num_errors > 0 ? "async operation failed" : "wait failed");
    }
    ovphysx_destroy_wait_result(&wait_result);
    return ok;
}

int main() {
    printf("=== ovphysx PhysX Interop (C++ API) ===\n");

    // 1. Create instance (CPU mode for simplicity)
    ovphysx_handle_t handle = 0;
    ovphysx_create_args args = OVPHYSX_CREATE_ARGS_DEFAULT;
    args.device = OVPHYSX_DEVICE_CPU;

    ovphysx_result_t result = ovphysx_create_instance(&args, &handle);
    if (!check_result(result, "create_instance"))
        return 1;

    printf("Instance created.\n");

    // 2. Load USD scene (simple_physics_scene.usda includes a kinematic body)
    ovphysx_usd_handle_t usd_handle = 0;
    ovphysx_enqueue_result_t add_result = ovphysx_add_usd(
        handle,
        OVPHYSX_LITERAL(OVPHYSX_TEST_DATA "/simple_physics_scene.usda"),
        OVPHYSX_LITERAL(""),
        &usd_handle);

    if (add_result.status != OVPHYSX_API_SUCCESS) {
        ovphysx_string_t err = ovphysx_get_last_error();
        fprintf(stderr, "Failed to load USD scene\n");
        if (err.ptr) {
            fprintf(stderr, "%.*s\n", (int)err.length, err.ptr);
        }
        ovphysx_destroy_instance(handle);
        return 1;
    }
    if (!wait_op(handle, add_result.op_index, "add_usd")) {
        ovphysx_destroy_instance(handle);
        return 1;
    }

    printf("USD scene loaded.\n");

    // 3. Step once to initialize physics
    ovphysx_enqueue_result_t step_result = ovphysx_step(handle, 1.0f / 60.0f, 0.0f);
    if (step_result.status != OVPHYSX_API_SUCCESS) {
        fprintf(stderr, "Failed to enqueue step\n");
        ovphysx_destroy_instance(handle);
        return 1;
    }
    if (!wait_op(handle, step_result.op_index, "initial step")) {
        ovphysx_destroy_instance(handle);
        return 1;
    }

    printf("Initial simulation step completed.\n");

    // 4. Get PhysX pointer for the kinematic cube
    //    OVPHYSX_PHYSX_TYPE_ACTOR returns either PxRigidDynamic* or PxRigidStatic*,
    //    so cast to PxRigidActor* first, then validate the concrete type.
    void* actor_ptr = nullptr;
    result = ovphysx_get_physx_ptr(
        handle, "/World/KinematicCube", OVPHYSX_PHYSX_TYPE_ACTOR, &actor_ptr);
    if (!check_result(result, "get_physx_ptr")) {
        ovphysx_destroy_instance(handle);
        return 1;
    }

    physx::PxRigidActor* rigid_actor = static_cast<physx::PxRigidActor*>(actor_ptr);
    physx::PxRigidDynamic* actor = rigid_actor->is<physx::PxRigidDynamic>();
    if (!actor) {
        fprintf(stderr, "ERROR: /World/KinematicCube is not a PxRigidDynamic\n");
        ovphysx_destroy_instance(handle);
        return 1;
    }
    printf("Got PxRigidDynamic* for /World/KinematicCube\n");

    // 5. Set kinematic target to move the cube from (0,2,0) to (3,2,0)
    physx::PxTransform target(physx::PxVec3(3.0f, 2.0f, 0.0f));
    actor->setKinematicTarget(target);
    printf("Set kinematic target to (3, 2, 0)\n");

    // 6. Step again so PhysX moves the kinematic body to the target
    step_result = ovphysx_step(handle, 1.0f / 60.0f, 1.0f / 60.0f);
    if (step_result.status != OVPHYSX_API_SUCCESS) {
        fprintf(stderr, "Failed to enqueue step\n");
        ovphysx_destroy_instance(handle);
        return 1;
    }
    if (!wait_op(handle, step_result.op_index, "target step")) {
        ovphysx_destroy_instance(handle);
        return 1;
    }

    // 7. Read back pose via tensor binding to verify the body moved
    ovphysx_tensor_binding_handle_t pose_binding = 0;
    ovphysx_tensor_binding_desc_t pose_desc = {};
    pose_desc.pattern = OVPHYSX_LITERAL("/World/KinematicCube");
    pose_desc.tensor_type = OVPHYSX_TENSOR_RIGID_BODY_POSE_F32;

    result = ovphysx_create_tensor_binding(handle, &pose_desc, &pose_binding);
    if (!check_result(result, "create_tensor_binding")) {
        ovphysx_destroy_instance(handle);
        return 1;
    }

    // Allocate a [1, 7] tensor: [x, y, z, qx, qy, qz, qw]
    float pose_data[7] = {};
    int64_t shape[2] = {1, 7};
    DLTensor pose_tensor = {};
    pose_tensor.data = pose_data;
    pose_tensor.ndim = 2;
    pose_tensor.shape = shape;
    pose_tensor.strides = nullptr;
    pose_tensor.byte_offset = 0;
    pose_tensor.dtype = {kDLFloat, 32, 1};
    pose_tensor.device = {kDLCPU, 0};

    result = ovphysx_read_tensor_binding(handle, pose_binding, &pose_tensor);
    if (!check_result(result, "read_tensor_binding")) {
        ovphysx_destroy_tensor_binding(handle, pose_binding);
        ovphysx_destroy_instance(handle);
        return 1;
    }

    printf("Pose after setKinematicTarget: pos=(%.3f, %.3f, %.3f) quat=(%.3f, %.3f, %.3f, %.3f)\n",
           pose_data[0], pose_data[1], pose_data[2],
           pose_data[3], pose_data[4], pose_data[5], pose_data[6]);

    // Verify the body moved to the target position
    const float tolerance = 0.1f;
    bool moved = (pose_data[0] > 3.0f - tolerance) && (pose_data[0] < 3.0f + tolerance) &&
                 (pose_data[1] > 2.0f - tolerance) && (pose_data[1] < 2.0f + tolerance);

    if (moved) {
        printf("SUCCESS: Kinematic body moved to target position via direct PhysX API call.\n");
    } else {
        fprintf(stderr, "FAILED: Expected position near (3, 2, 0), got (%.3f, %.3f, %.3f)\n",
                pose_data[0], pose_data[1], pose_data[2]);
        ovphysx_destroy_tensor_binding(handle, pose_binding);
        ovphysx_destroy_instance(handle);
        return 1;
    }

    // 8. Cleanup
    ovphysx_destroy_tensor_binding(handle, pose_binding);
    ovphysx_destroy_instance(handle);
    printf("Cleanup complete\n");

    return 0;
}
```

## How It Works

1. **Create instance and load USD** — standard ovphysx workflow.
2. **Step once** — initializes the PhysX scene so rigid body actors exist.
3. **Get pointer** — `ovphysx_get_physx_ptr()` returns a `void*` for the prim path.
4. **Cast and validate** — `OVPHYSX_PHYSX_TYPE_ACTOR` can return either a
   `PxRigidDynamic*` or `PxRigidStatic*`, so cast to `PxRigidActor*` first, then
   use `is<PxRigidDynamic>()` to validate the concrete type before calling
   `setKinematicTarget()`.
5. **Step again** — PhysX moves the kinematic body to the target pose.
6. **Verify** — read pose back via tensor binding to confirm the move.

## Pointer Lifecycle

- Pointers are valid until `ovphysx_remove_usd()`, `ovphysx_reset()`, or
  instance destruction.
- `ovphysx_step()` does **not** invalidate pointers.
- Do not call `release()` on returned pointers — ovphysx owns them.

## Thread Safety

PhysX APIs on returned pointers must only be called **between** simulation
steps — after `wait_op()` completes for the preceding step and before the next
`ovphysx_step()` call. See the
[developer guide](../developer_guide.md#thread-safety) for details.

## Result

After running the sample, you should see output confirming the kinematic body
moved to the target position `(3, 2, 0)` via a direct PhysX `setKinematicTarget()` call.
