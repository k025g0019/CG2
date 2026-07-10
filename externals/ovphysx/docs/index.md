# ovphysx

ovphysx is a self-contained library package offering the PhysX SDK and Omni PhysX capabilities behind a C API and corresponding Python bindings for inclusion in user applications.
It loads USD scenes, runs simulation, and allows reading and writing simulation data with DLPack interoperability.

To get started, see the [Quickstart](tutorials/quickstart.md).

> **Note**
>
> - **Maturity**: ovphysx is pre-release software and not yet mature.
> - **USD coexistence**: ovphysx ships an OV namespaced monolithic OpenUSD runtime. It can reuse an already-loaded compatible OV namespaced USD runtime from another OV library, but it does not treat a classic `usd-core` or host USD import as its runtime. In mixed OV processes, register each subsystem's schema paths before the first USD stage open or schema-registry access.
> - **API stability**: Parts of the API are still being completed and may change before 1.0.

ovphysx packages the Omni PhysX runtime (providing in particular USD-aware simulation and tensorized data access) on top of the PhysX SDK.
The dependency stack is: PhysX SDK → omni.physx (Omniverse integration) → ovphysx (standalone C/Python SDK).

The source repository of ovphysx can be found at [https://github.com/NVIDIA-Omniverse/PhysX/](https://github.com/NVIDIA-Omniverse/PhysX/).

External references of contained functionality:
- [Omni PhysX User Guide](https://docs.omniverse.nvidia.com/kit/docs/omni_physics/latest/index.html)
- [PhysX SDK Documentation](https://nvidia-omniverse.github.io/PhysX/)

## Table of Contents




