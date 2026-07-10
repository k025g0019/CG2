# ovphysx Skills

This file indexes the available skills for ovphysx.
A skill is a focused playbook (Markdown + code snippets) that teaches an agent or human how to accomplish one task with ovphysx.

Installing a skill does NOT build ovphysx. It only provides docs and snippets.
Installing ovphysx itself is separate:
- Python: `pip install ovphysx`
- C/C++: download the SDK package or build from source (headers + libs from `_install/`)

Notes:
- These skills focus on the public TensorBindingsAPI workflow (bulk read/write via DLPack tensors).
- The bundled samples are CI-tested and are the authoritative full examples;
  skills keep only the shortest task-oriented pattern.
- Each `SKILL.md` declares agent-readable frontmatter, including a
  skill-document version, owner, tags, and tool requirements.

## Dependencies

- ovphysx bundles its own OpenUSD libraries. It is not needed to install
  `usd-core` as a separate dependency -- doing so may cause version conflicts.
- ovphysx exchanges tensor data via DLPack, so any framework that understands
  DLPack can consume simulation outputs. Common optional companions are
  `numpy` (CPU tensors) and `torch` (GPU tensors), but they are not
  dependencies of this package. Add whichever you need to your own project
  requirements, for example in your `pyproject.toml`:
  ```toml
  dependencies = ["ovphysx", "numpy"]
  ```

## Quick start

- Smallest possible workflow: use `basic-workflow`.
- Replicated environments for RL: use `clone-environments`.
- Bulk simulation I/O on CPU: use `tensor-bindings-cpu`.
- Bulk simulation I/O on GPU (CUDA): use `tensor-bindings-gpu`.

## Skills

### `basic-workflow`
- **Goal**: Create an instance, load a USD scene, step simulation, clean up.
- **Skill version**: 0.1.0
- **APIs**: `PhysX()`, `add_usd()`, `step()`, `release()` / C: `ovphysx_create_instance()`, `ovphysx_add_usd()`, `ovphysx_step()`, `ovphysx_destroy_instance()`
- **Doc**: [skills/basic-workflow/SKILL.md](skills/basic-workflow/SKILL.md)
- **References**:
  - Docs: `docs/tutorials/hello_world.md`
  - Python sample: `samples/python_samples/hello_world.py` (wheel; source: `tests/python_samples/hello_world.py`)
  - C sample: `samples/c_samples/hello_world_c/main.c` (SDK; source: `tests/c_samples/hello_world_c/main.c`)

### `tensor-bindings-cpu`
- **Goal**: Create tensor bindings, write control inputs, step, read back state on CPU.
- **Skill version**: 0.1.0
- **APIs**: `create_tensor_binding()`, `.read()`, `.write()` / C: `ovphysx_create_tensor_binding()`, `ovphysx_read_tensor_binding()`, `ovphysx_write_tensor_binding()`
- **Doc**: [skills/tensor-bindings-cpu/SKILL.md](skills/tensor-bindings-cpu/SKILL.md)
- **References**:
  - Docs: `docs/tutorials/tensor_bindings.md`
  - Python sample: `samples/python_samples/tensor_bindings.py` (wheel; source: `tests/python_samples/tensor_bindings.py`)
  - C sample: `samples/c_samples/tensor_bindings_c/main.c` (SDK; source: `tests/c_samples/tensor_bindings_c/main.c`)

### `clone-environments`
- **Goal**: Clone one USD environment subtree into many runtime-only PhysX environments before `warmup_gpu()` or the first simulation step.
- **Skill version**: 0.1.0
- **APIs**: Python: `PhysX.clone()` / C: `ovphysx_clone()`
- **Doc**: [skills/clone-environments/SKILL.md](skills/clone-environments/SKILL.md)
- **References**:
  - Docs: `docs/tutorials/cloning.md`
  - Python sample: `samples/python_samples/clone.py` (wheel; source: `tests/python_samples/clone.py`)
  - C sample: `samples/c_samples/clone_c/main.c` (SDK; source: `tests/c_samples/clone_c/main.c`)

### `tensor-bindings-gpu`
- **Goal**: Read and write simulation data on GPU using CUDA device pointers and DLPack.
- **Skill version**: 0.1.0
- **APIs**: Same as CPU bindings, with `device="gpu"`.
- **Doc**: [skills/tensor-bindings-gpu/SKILL.md](skills/tensor-bindings-gpu/SKILL.md)
- **References**:
  - Docs: `docs/developer_guide.md`
  - C sample: `samples/c_samples/tensor_bindings_gpu_c/main.c` (SDK; source: `tests/c_samples/tensor_bindings_gpu_c/main.c`)
