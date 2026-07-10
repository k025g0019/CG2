# ovphysx SDK

ovphysx is a self-contained SDK for USD-based physics simulation with DLPack tensor interoperability,
available as a Python wheel and a C/C++ package.

- [Latest releases](https://github.com/NVIDIA-Omniverse/PhysX/releases)
- [Getting started](docs/tutorials/quickstart.md)
- [Full documentation](docs/index.md)

## Quick Start (C/C++)

Build and run a bundled sample to verify your SDK installation:

```bash
# Run from the extracted SDK root directory
cmake -B build -S samples/c_samples/hello_world_c -DCMAKE_PREFIX_PATH=$PWD
cmake --build build
./build/hello_world_c
```

See `samples/c_samples/` for more examples. Each sample has its own `CMakeLists.txt` that uses `find_package(ovphysx)`.

## AI agent / LLM instructions

This SDK ships step-by-step playbooks (skills) and code samples.
Start by reading [SKILLS.md](SKILLS.md) in this directory for a skills index.

Key resources:
- Skills (agent playbooks): `SKILLS.md` and `skills/`
- C code samples: `samples/c_samples/`
- Sample USD data: `samples/data/`
- Markdown documentation: `docs/`
- C API header: `include/ovphysx/ovphysx.h`

Important notes:
- This SDK bundles its own OpenUSD libraries. It is not needed to install
  OpenUSD separately.
- ovphysx exchanges tensor data via DLPack, so any framework that understands
  DLPack can consume simulation outputs.
