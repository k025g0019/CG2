#include <PxPhysicsAPI.h>
#include <NvBlast.h>
#include <cudamanager/PxCudaContextManager.h>
#include <gpu/PxGpu.h>
#include <string_view>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <malloc.h>

namespace {
// SDK オブジェクトを依存関係の逆順に解放する。
struct PhysicsRelease {
    template<class T> void operator()(T* object) const { object->release(); }
};
template<class T> using PhysicsOwner = std::unique_ptr<T, PhysicsRelease>;

void Require(bool condition, const char* message) {

    if (!condition) {
        throw std::runtime_error(message);
    }

}

using AlignedMemory = std::unique_ptr<void, decltype(&_aligned_free)>;
AlignedMemory Allocate(size_t size) {
    AlignedMemory memory(_aligned_malloc(size, 16U), &_aligned_free);
    Require(memory != nullptr, "SDK memory allocation failed");
    return memory;
}

// ============================================================================
// PhysX: 重力による落下と床との衝突を実際にシミュレーションする。
// ============================================================================
void CheckPhysX(bool useGpu) {
    physx::PxDefaultAllocator allocator;
    physx::PxDefaultErrorCallback errorCallback;
    PhysicsOwner<physx::PxFoundation> foundation(PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errorCallback));
    Require(foundation != nullptr, "PxCreateFoundation failed");
    PhysicsOwner<physx::PxPhysics> physics(PxCreatePhysics(PX_PHYSICS_VERSION, *foundation, physx::PxTolerancesScale()));
    Require(physics != nullptr, "PxCreatePhysics failed");
    PhysicsOwner<physx::PxDefaultCpuDispatcher> dispatcher(physx::PxDefaultCpuDispatcherCreate(1U));
    Require(dispatcher != nullptr, "CPU dispatcher creation failed");
    // CUDA は GPU シーンを作る時だけ初期化する。CPU 実行には GPU を要求しない。
    PhysicsOwner<physx::PxCudaContextManager> cudaContext;

    if (useGpu) {
        const physx::PxCudaContextManagerDesc cudaDesc;
        cudaContext.reset(PxCreateCudaContextManager(*foundation, cudaDesc, PxGetProfilerCallback()));
        Require(cudaContext != nullptr && cudaContext->contextIsValid(), "GPU test requires a valid CUDA context");
        std::cout << "CUDA device: " << cudaContext->getDeviceName() << '\n';
    }

    physx::PxSceneDesc sceneDesc(physics->getTolerancesScale());
    sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);
    sceneDesc.cpuDispatcher = dispatcher.get();
    sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;

    if (useGpu) {
        sceneDesc.cudaContextManager = cudaContext.get();
        sceneDesc.flags |= physx::PxSceneFlag::eENABLE_GPU_DYNAMICS;
        sceneDesc.flags |= physx::PxSceneFlag::eENABLE_PCM;
        sceneDesc.broadPhaseType = physx::PxBroadPhaseType::eGPU;
    }

    PhysicsOwner<physx::PxScene> scene(physics->createScene(sceneDesc));
    Require(scene != nullptr, "PhysX scene creation failed");
    PhysicsOwner<physx::PxMaterial> material(physics->createMaterial(0.5f, 0.5f, 0.0f));
    Require(material != nullptr, "PhysX material creation failed");
    PhysicsOwner<physx::PxRigidStatic> floor(physx::PxCreatePlane(*physics, physx::PxPlane(0.0f, 1.0f, 0.0f, 0.0f), *material));
    PhysicsOwner<physx::PxRigidDynamic> sphere(physx::PxCreateDynamic(*physics,
        physx::PxTransform(physx::PxVec3(0.0f, 2.0f, 0.0f)), physx::PxSphereGeometry(0.5f), *material, 1.0f));
    Require(floor != nullptr && sphere != nullptr, "PhysX rigid body creation failed");
    scene->addActor(*floor);
    scene->addActor(*sphere);

    for (int frameTimer = 0; frameTimer < 120; ++frameTimer) {
        scene->simulate(1.0f / 60.0f);
        Require(scene->fetchResults(true), "PhysX fetchResults failed");
    }

    // フラグだけでなく実際の GPU メモリ使用も確認し、CPU fallback を見逃さない。
    if (useGpu) {
        physx::PxSimulationStatistics sceneStatistics;
        scene->getSimulationStatistics(sceneStatistics);
        Require(scene->getFlags().isSet(physx::PxSceneFlag::eENABLE_GPU_DYNAMICS)
            && scene->getBroadPhaseType() == physx::PxBroadPhaseType::eGPU
            && sceneStatistics.gpuMemHeap > 0ULL, "GPU simulation was not active");
        std::cout << "GPU simulation heap: " << sceneStatistics.gpuMemHeap << " bytes\n";
    }

    const float sphereHeight = sphere->getGlobalPose().p.y;
    Require(std::isfinite(sphereHeight) && sphereHeight >= 0.45f && sphereHeight <= 0.55f,
        "Sphere did not settle on the floor");
    std::cout << "PhysX " << PX_PHYSICS_VERSION_MAJOR << '.' << PX_PHYSICS_VERSION_MINOR
        << (useGpu ? " GPU" : " CPU") << ": sphere height = " << sphereHeight << " (expected 0.5)\n";
}

// ============================================================================
// Blast: 2 つの support chunk の結合を壊し、2 actor への分離を確認する。
// ============================================================================
void CheckBlast() {
    NvBlastChunkDesc chunks[2]{};

    for (auto& chunk : chunks) {
        chunk.volume = 1.0f;
        chunk.parentChunkIndex = UINT32_MAX;
        chunk.flags = NvBlastChunkDesc::SupportFlag;
    }

    chunks[0].centroid[0] = -0.5f;
    chunks[1].centroid[0] = 0.5f;
    NvBlastBondDesc bond{};
    bond.chunkIndices[0] = 0U;
    bond.chunkIndices[1] = 1U;
    bond.bond.normal[0] = 1.0f;
    bond.bond.area = 1.0f;
    const NvBlastAssetDesc assetDesc{2U, chunks, 1U, &bond};
    auto assetMemory = Allocate(NvBlastGetAssetMemorySize(&assetDesc, nullptr));
    auto assetScratch = Allocate(NvBlastGetRequiredScratchForCreateAsset(&assetDesc, nullptr));
    NvBlastAsset* asset = NvBlastCreateAsset(assetMemory.get(), &assetDesc, assetScratch.get(), nullptr);
    Require(asset != nullptr, "NvBlastCreateAsset failed");
    auto familyMemory = Allocate(NvBlastAssetGetFamilyMemorySize(asset, nullptr));
    NvBlastFamily* family = NvBlastAssetCreateFamily(familyMemory.get(), asset, nullptr);
    Require(family != nullptr, "NvBlastAssetCreateFamily failed");
    auto actorScratch = Allocate(NvBlastFamilyGetRequiredScratchForCreateFirstActor(family, nullptr));
    const NvBlastActorDesc actorDesc{1.0f, nullptr, 1.0f, nullptr};
    NvBlastActor* actor = NvBlastFamilyCreateFirstActor(family, &actorDesc, actorScratch.get(), nullptr);
    Require(actor != nullptr, "NvBlastFamilyCreateFirstActor failed");
    NvBlastBondFractureData fracture{0U, 0U, 1U, 2.0f};
    const NvBlastFractureBuffers commands{1U, 0U, &fracture, nullptr};
    NvBlastActorApplyFracture(nullptr, actor, &commands, nullptr, nullptr);
    auto splitScratch = Allocate(NvBlastActorGetRequiredScratchForSplit(actor, nullptr));
    NvBlastActor* childActors[2]{};
    NvBlastActorSplitEvent splitEvent{nullptr, childActors};
    const uint32_t splitCount = NvBlastActorSplit(&splitEvent, actor, 2U, splitScratch.get(), nullptr, nullptr);
    Require(splitCount == 2U && NvBlastFamilyGetActorCount(family, nullptr) == 2U,
        "Blast bond fracture did not produce two actors");
    std::cout << "Blast 1.1.5: bond fracture produced " << splitCount << " actors\n";
}
}

int main(int argumentCount, char* argumentValues[]) {
    try {
        const bool useGpu = argumentCount >= 2 && std::string_view(argumentValues[1]) == "--gpu";
        CheckPhysX(useGpu);
        CheckBlast();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
