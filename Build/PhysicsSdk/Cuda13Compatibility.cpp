#include <cuda.h>
#include <driver_types.h>
#include <vector_types.h>

#include <cstdio>
#include <cstdlib>

#if CUDA_VERSION >= 13000
// ============================================================================
// CUDA 13 が生成する未使用の host launch wrapper のリンク互換性。
// PhysX は CudaKernelWrangler でカーネルを登録し、CudaCtx::launchKernel から
// CUDA driver API の cuLaunchKernel を使用する。CUDART とはリンクしない。
// この経路が実際に呼ばれた場合は、処理を成功扱いせず明示的に停止する。
// ============================================================================
extern "C" cudaError_t CUDARTAPI __cudaGetKernel(cudaKernel_t*, const void*) {
    std::fputs("PhysX CUDA 13: unexpected CUDART kernel lookup; use the driver API.\n", stderr);
    std::abort();
}

extern "C" cudaError_t CUDARTAPI __cudaLaunchKernel(
    cudaKernel_t, dim3, dim3, void**, size_t, cudaStream_t) {
    std::fputs("PhysX CUDA 13: unexpected CUDART kernel launch; use the driver API.\n", stderr);
    std::abort();
}
#endif
