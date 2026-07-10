# ovphysx Overview

ovphysx is a self-contained PhysX runtime with Omni PhysX capabilities, exposed as a C API with Python bindings.
It loads USD scenes, runs simulation, and reads or writes data via DLPack for zero-copy tensor exchange.
It currently supports Windows (x86_64) and Linux (x86_64, aarch64) platforms.

This library is AI-agent friendly: `SKILLS.md` and the `skills/` directory ship with both the Python wheel and the C SDK package. `AGENTS.md` is also available in the source repository for quick codebase onboarding.

This page explains how ovphysx fits into the stack, what workflows it supports, and where to find build, tutorial, and API details.

## Who This Is For

- Python-first users who consume the wheel and integrate ovphysx into apps or pipelines.
- C/C++ users who integrate the SDK in native applications.

## What You Can Do

- Load USD scenes into a runtime stage.
- Replicate subtrees for large-scale parallel environments by pairing with `ovstage` and calling `stage.clone_subtree()` (the bridge consumes clone events and drives PhysX-side replication).
- Write simulation inputs (positions, velocities, controls) via tensor bindings or, when paired with `ovstage`, via attribute writes through the stage.
- Step the simulation.
- Read results (updated simulation state) back into your tensors or out through the attached stage.

To get started, see the [Quickstart](tutorials/quickstart.md).

## Relationship to PhysX SDK and Omni PhysX

ovphysx packages the Omni PhysX runtime (USD integration, streaming execution model, extensions) on top of the PhysX SDK.
In other words, it is the standalone SDK layer that exposes omni.physx capabilities through a stable C API and Python bindings.
The dependency stack is: PhysX SDK → omni.physx → ovphysx.

## Core Concepts

- Stream-ordered execution: calls run in submission order and see prior writes without extra sync.
- Tensor bindings: synchronous read/write operations for physics data (rigid body poses, velocities, etc.). Includes pattern matching (glob patterns) to bind to multiple prims at once (`/World/robot*`, `/World/env[N]/robot`).
- Thread safety: multiple instances are safe across threads, a single instance is not.
- DLPack interoperability: share memory with NumPy, PyTorch, and other DLPack consumers.

## End-to-End Usage in Python

Typical usage of ovphysx:

- Create and release instances with `PhysX()` and `physx.release()`.
- Load or remove USD with `physx.add_usd()` and `physx.remove_usd()`.
- Pair with an `ovstage.Stage` via `physx.attach_stage(stage)` to drive control attributes (and clone propagation) from the application through ovstage.
- Create tensor bindings with `physx.create_tensor_binding()` specifying tensor type and a prim path pattern or explicit prim paths.
- Write and read tensor data with `binding.write()` and `binding.read()` using NumPy arrays or dlpack-compatible buffers.
- Step the simulation with `physx.step()`.

See the [Python API Reference](python_api.rst) for the full Python API surface.

## C/C++ SDK

The C API mirrors the Python flow:
- Create and destroy instances with `ovphysx_create_instance()` and `ovphysx_destroy_instance()`.
- Load or reset USD with `ovphysx_add_usd()`, `ovphysx_remove_usd()`, and `ovphysx_reset()`.
- Create tensor bindings with `ovphysx_create_tensor_binding()` specifying tensor type and prim paths.
- Write and read tensor data with `ovphysx_write_tensor_binding()` and `ovphysx_read_tensor_binding()`.
- Step the simulation with `ovphysx_step()`.


See the [C API Reference](api.md) for the full C API surface.

## Release and Distribution

- Python users consume the wheel as the primary distribution (`pip install ovphysx`).
- C/C++ users consume the SDK package with headers and shared libraries (see the [GitHub release page](https://github.com/NVIDIA-Omniverse/PhysX/releases)).

### Wheel Contents and Environment Notes

The Python wheel (`pip install ovphysx`) is intentionally large because it ships a complete, self-contained runtime.
No external NVIDIA or USD packages are required.

#### What the Wheel Bundles

- **ovphysx shared libraries** (`libovphysx.so` / `ovphysx.dll`)
- **Carbonite runtime** (`libcarb.so` and plugins)
- **PhysX plugins** (simulation, tensors)
- **USD libraries** (scene loading and schema support)

The only Python dependency is `packaging`.

### USD Coexistence and Version Checking

ovphysx includes runtime safeguards for processes where an OV namespaced USD runtime may already be loaded by another package, such as another NVIDIA Omniverse library. Classic host USD, including `usd-core`, is intentionally separate and is not reused as ovphysx's runtime.

When ovphysx shares a process with another OV USD-aware subsystem such as ovrtx,
call `ovphysx_register_schema_paths()` and the peer subsystem's equivalent
before the first USD stage open so USD's schema registry sees all plugin roots.

When ovphysx starts, it checks whether a namespaced USD runtime is already loaded in the process. ovphysx then takes one of three paths:
- **No USD loaded**: ovphysx preloads its own bundled USD libraries automatically.
- **Compatible OV namespaced USD loaded**: ovphysx uses the existing USD (skips preload).
- **Incompatible OV namespaced USD loaded**: ovphysx fails with a detailed error message showing the required vs. found version and remediation steps.

Detection uses `dlopen(RTLD_NOLOAD)` on Linux and `GetModuleHandle` on Windows to inspect process memory without side effects.
The required USD version is specified in `config.toml` using PEP 440 version specifiers (e.g. `==25.11`).

You can control this behavior with `/ovphysx/skipUsdLibPreload` to bypass automatic USD preload.

## Versioning and Compatibility

ovphysx follows semantic versioning for the SDK surface (C API and Python bindings).

Backward compatibility guarantees that come with semantic versioning apply only to releases at or after v1.0; pre-1.0 releases may include breaking changes between minor and patch versions.

Functions exposed in experimental or internal folders (or prefixes) have no guarantees and may change or be removed without notice.

### Version Management

The project version is defined in `VERSION` (e.g., `0.1.0` or `0.1.4-specialFeature`).

- `CMakeLists.txt` reads `VERSION` and generates `include/ovphysx/version.h`
- Python wheels convert to PEP 440 (`X.Y.Z-suffix` → `X.Y.Z.suffix`)
- C++ archives keep the original semver format

### API/ABI Policy

- **Major**: breaking C API or ABI changes, removed symbols, or incompatible data layout changes.
- **Minor**: backwards-compatible API additions, new symbols, or optional features.
- **Patch**: backwards-compatible bug fixes and internal changes only.

For v1.0 and later releases, backward compatibility is expected across patch and minor releases for the published C API and Python API. ABI compatibility is maintained within a major line; SONAME uses the major version on Linux to make ABI expectations explicit.

When a breaking change is required at the C API level, consider adding a versioned entrypoint (e.g., `ovphysx_create_instance_v2`) to preserve compatibility, similar to CUDA-style `*_v2` APIs. Deprecated APIs should remain for at least one minor release with clear deprecation notes.

Python bindings validate that the package version and native library version match at runtime (base semver), and will raise a clear error if they diverge. This check can be bypassed for advanced cases via `ignore_version_mismatch=True` on `PhysX`.


## Runtime Warnings

ovphysx bundles PhysX, Carbonite, and USD runtime components. On startup and during
simulation, you may see warnings from these downstream dependencies such as:

- `[Warning] PhysXFoundation: Unable to create GPU Foundation` — appears on machines
  without an NVIDIA GPU; the SDK falls back to CPU simulation transparently.
- `[Error] [omni.physx.cooking.plugin] registry is false` — a non-fatal initialization
  message from the cooking subsystem.
- `Warning: Error loading lib ... module 'pxr.*': ModuleNotFoundError` — the bundled
  USD libraries attempt to load optional Python USD bindings (`pxr`). These warnings
  are harmless when using ovphysx standalone without a separate USD Python installation.

These messages do not indicate a problem with your application. They originate from
the bundled runtime dependencies and will be addressed in future releases.

To suppress most startup noise, set the log level before creating an instance:
- **C:** `ovphysx_set_log_level(OVPHYSX_LOG_ERROR);`
- **Python:** `ovphysx.set_log_level(ovphysx.LogLevel.ERROR)`

## PhysX SDK and Omni PhysX Documentation

ovphysx uses the PhysX SDK and Omni PhysX plugins under the hood.
For broader PhysX and Omni PhysX guidance see the respective developer resources:
- [Omni PhysX User Guide](https://docs.omniverse.nvidia.com/kit/docs/omni_physics/latest/index.html)
- [PhysX Documentation](https://nvidia-omniverse.github.io/PhysX/)

## Conclusion

You now have a high-level view of ovphysx capabilities, compatibility policy, and distribution options. For build and runtime details, see the [Developer Guide](developer_guide.md). For hands-on usage, continue with [Hello World](tutorials/hello_world.md).
