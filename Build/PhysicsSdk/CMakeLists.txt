cmake_minimum_required(VERSION 3.24)
project(CG2PhysicsSdk LANGUAGES C CXX)

# CG2 uses the DLL CRT in both configurations.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
get_filename_component(CG2_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CG2_SDK_OUTPUT "${CG2_ROOT}/ThirdParty/PhysicsSdk")
set(PX_GENERATE_GPU_PROJECTS ON CACHE BOOL "CPU and GPU SDK" FORCE)
# CUDA 13 no longer compiles Volta (sm_70); use the upstream supported subset.
set(PX_GENERATE_GPU_REDUCED_ARCHITECTURES ON CACHE BOOL "CUDA 13 architectures" FORCE)
set(PX_GENERATE_STATIC_LIBRARIES OFF CACHE BOOL "Shared PhysX" FORCE)
set(NV_USE_STATIC_WINCRT OFF CACHE BOOL "Match CG2 CRT" FORCE)
set(NV_USE_DEBUG_WINCRT ON CACHE BOOL "Debug CRT" FORCE)
set(PX_BUILDSNIPPETS OFF CACHE BOOL "No upstream samples" FORCE)
set(PX_BUILDPVDRUNTIME OFF CACHE BOOL "No OmniPVD application" FORCE)
add_subdirectory("${CG2_ROOT}/ThirdParty/PhysX/physx" physx-build)

# Blast 1.1.5 low-level API has no PhysX 3.4 dependency.
# Keep the original source intact and list the same files as upstream NvBlast.cmake.
set(blastSdk "${CG2_ROOT}/ThirdParty/Blast-1.1.5/sdk")
add_library(NvBlast SHARED
    "${blastSdk}/common/NvBlastAssert.cpp"
    "${blastSdk}/common/NvBlastAtomic.cpp"
    "${blastSdk}/common/NvBlastTime.cpp"
    "${blastSdk}/common/NvBlastTimers.cpp"
    "${blastSdk}/lowlevel/source/NvBlastActor.cpp"
    "${blastSdk}/lowlevel/source/NvBlastFamilyGraph.cpp"
    "${blastSdk}/lowlevel/source/NvBlastActorSerializationBlock.cpp"
    "${blastSdk}/lowlevel/source/NvBlastAsset.cpp"
    "${blastSdk}/lowlevel/source/NvBlastAssetHelper.cpp"
    "${blastSdk}/lowlevel/source/NvBlastFamily.cpp")
target_include_directories(NvBlast PUBLIC "${blastSdk}/lowlevel/include" PRIVATE "${blastSdk}/common")
target_compile_definitions(NvBlast PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX
    "$<$<CONFIG:Debug>:NV_DEBUG=1;NV_CHECKED=1>"
    "$<$<NOT:$<CONFIG:Debug>>:NV_DEBUG=0;NV_CHECKED=0>")

# A common layout keeps MSBuild independent of upstream compiler-directory names.
add_custom_target(CG2PhysicsSdkLibraries ALL)
set(sdkTargets PhysX PhysXCommon PhysXFoundation PhysXCooking PhysXExtensions
    PhysXPvdSDK PhysXCharacterKinematic PhysXVehicle PhysXVehicle2 PhysXGpu NvBlast)
foreach(sdkTarget IN LISTS sdkTargets)
    if(TARGET ${sdkTarget})
        add_dependencies(CG2PhysicsSdkLibraries ${sdkTarget})
        foreach(sdkConfig DEBUG CHECKED PROFILE RELEASE)
            string(TOLOWER "${sdkConfig}" sdkConfigDir)
            set_target_properties(${sdkTarget} PROPERTIES
                ARCHIVE_OUTPUT_DIRECTORY_${sdkConfig} "${CG2_SDK_OUTPUT}/lib/${sdkConfigDir}"
                RUNTIME_OUTPUT_DIRECTORY_${sdkConfig} "${CG2_SDK_OUTPUT}/bin/${sdkConfigDir}")
        endforeach()
        set_target_properties(${sdkTarget} PROPERTIES
            ARCHIVE_OUTPUT_DIRECTORY "${CG2_SDK_OUTPUT}/lib/$<CONFIG>"
            RUNTIME_OUTPUT_DIRECTORY "${CG2_SDK_OUTPUT}/bin/$<CONFIG>")
    endif()
endforeach()


# CUDA headers are UTF-8; pass the encoding to both MSVC and nvcc's host compiler.
set(gpuTargets PhysXGpu PhysXBroadphaseGpu PhysXCommonGpu PhysXNarrowphaseGpu
    PhysXSimulationControllerGpu PhysXSolverGpu PhysXCudaContextManager
    PhysXArticulationGpu PhysXGpuDependencies)
foreach(gpuTarget IN LISTS gpuTargets)
    target_compile_options(${gpuTarget} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/utf-8>")
endforeach()

# PhysX has its own CUDA registration/launch shim (CudaKernelWrangler.cpp).
# The new top-level upstream CMake also adds cudart, which duplicates these symbols.
# Keep the driver API linkage and use the SDK's existing shim consistently.
target_sources(PhysXGpu PRIVATE "${CMAKE_CURRENT_LIST_DIR}/Cuda13Compatibility.cpp")
get_target_property(gpuLinkLibraries PhysXGpu LINK_LIBRARIES)
list(FILTER gpuLinkLibraries EXCLUDE REGEX "cudart")
set_property(TARGET PhysXGpu PROPERTY LINK_LIBRARIES "${gpuLinkLibraries}")
foreach(gpuTarget IN LISTS gpuTargets)
    set_property(TARGET ${gpuTarget} PROPERTY CUDA_RUNTIME_LIBRARY None)
endforeach()
# CUDA 13 cuda.lib requests LIBCMT; CG2 and PhysX use the DLL CRT (/MD, /MDd).
# Resolve its CRT references through the selected DLL CRT instead of mixing heaps.
target_link_options(PhysXGpu PRIVATE /NODEFAULTLIB:LIBCMT /NODEFAULTLIB:LIBCMTD)
# CUDA 13's driver import shim loads nvcuda itself, so there is no direct import to delay.
set_target_properties(PhysXGpu PROPERTIES LINK_FLAGS_DEBUG "" LINK_FLAGS_CHECKED ""
    LINK_FLAGS_PROFILE "" LINK_FLAGS_RELEASE "")
# Match the filename used by PhysX's runtime LoadLibrary call in every config.
set_target_properties(PhysXGpu PROPERTIES OUTPUT_NAME PhysXGpu_64
    DEBUG_POSTFIX "" CHECKED_POSTFIX "" PROFILE_POSTFIX "" RELEASE_POSTFIX "")

add_executable(CG2PhysicsSdkSmoke Smoke.cpp)
add_dependencies(CG2PhysicsSdkSmoke PhysXGpu)
target_compile_features(CG2PhysicsSdkSmoke PRIVATE cxx_std_17)
target_compile_options(CG2PhysicsSdkSmoke PRIVATE /utf-8)
target_link_libraries(CG2PhysicsSdkSmoke PRIVATE PhysX PhysXCommon PhysXFoundation PhysXExtensions NvBlast)
set_target_properties(CG2PhysicsSdkSmoke PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CG2_SDK_OUTPUT}/bin/$<CONFIG>")
enable_testing()
add_test(NAME PhysicsSdkSmoke COMMAND CG2PhysicsSdkSmoke)

add_test(NAME PhysicsSdkGpuSmoke COMMAND CG2PhysicsSdkSmoke --gpu)
set_tests_properties(PhysicsSdkSmoke PhysicsSdkGpuSmoke PROPERTIES TIMEOUT 120)
