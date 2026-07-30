#pragma once

#include "Source/Engine/Editor/EditorSceneObject.h"

#pragma warning(push, 0)
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#pragma warning(pop)

#include <cstdint>
#include <unordered_map>
#include <vector>

//================================================================
// Ocean の FFT 波面を既存の連続 LOD メッシュへ供給する GPU 管理クラス
//================================================================

class EditorOceanFftManager {
public:
	static constexpr uint32_t kMaximumFftResolution = 2048u;
	static constexpr uint32_t kMaximumSurfaceSampleCount = 1024u;

	struct SurfaceSample {
		Vector3 localPosition{0.0f, 0.0f, 0.0f};
		Vector3 localNormal{0.0f, 1.0f, 0.0f};
		Vector3 localVelocity{0.0f, 0.0f, 0.0f};
		float foam = 0.0f;
		bool isValid = false;
	};

	bool Initialize(
		ID3D12Device* device,
		IDxcBlob* updateSpectrumShaderBlob,
		IDxcBlob* fftRowShaderBlob,
		IDxcBlob* transposeShaderBlob,
		IDxcBlob* finalizeShaderBlob);
	bool Execute(
		ID3D12GraphicsCommandList* commandList,
		const EditorOceanRenderSettings& oceanSettings,
		float oceanElapsedTime);
	void BindGraphicsResources(ID3D12GraphicsCommandList* commandList) const;
	void BindPostProcessDisplacement(
		ID3D12GraphicsCommandList* commandList,
		uint32_t rootParameterIndex) const;  // Underwater Pixel Shader へ描画と同じ FFT 変位を渡す
	void ApplyToSceneObject(EditorSceneObject& sceneObject) const;
	bool QueueSurfaceSample(
		uint64_t sampleKey,
		const Vector2& localPosition,
		SurfaceSample& surfaceSample);
	void ResolveReadback();
	bool IsReadyFor(const EditorOceanRenderSettings& oceanSettings) const;
	void Finalize();

private:
	struct OceanFftConstants {
		float oceanTime = 0.0f;
		float gravity = 9.81f;
		float waterDepth = 1.0f;
		uint32_t fftResolution = 0u;
		float domainLength = 1.0f;
		float maxWaveHeight = 1.0f;
		float choppiness = 0.0f;
		float foamThreshold = 0.0f;
		float foamStrength = 0.0f;
		float crestSharpness = 0.0f;
		float heightScale = 1.0f;
		uint32_t sampleCount = 0u;
		float padding0 = 0.0f;
		float padding1 = 0.0f;
		float padding2 = 0.0f;
		float padding3 = 0.0f;
	};
	static_assert(
		sizeof(OceanFftConstants) == 64u,
		"Ocean FFT の Root Constants は HLSL cbuffer と同じ64 bytesである必要があります。");

	struct SpectrumValue {
		float real = 0.0f;
		float imaginary = 0.0f;
	};

	struct SurfaceSampleRequest {
		float localPositionX = 0.0f;
		float localPositionZ = 0.0f;
		uint32_t sampleKeyLow = 0u;
		uint32_t sampleKeyHigh = 0u;
	};

	struct SurfaceSampleResult {
		float surfacePositionFoam[4]{};
		float surfaceNormal[4]{};
		float displacementAndTime[4]{};
		uint32_t sampleKeyLow = 0u;
		uint32_t sampleKeyHigh = 0u;
		float padding0 = 0.0f;
		float padding1 = 0.0f;
	};

	static_assert(sizeof(SurfaceSampleRequest) == 16u);
	static_assert(sizeof(SurfaceSampleResult) == 64u);

	bool CreateRootSignatureAndPipelineStates(
		IDxcBlob* updateSpectrumShaderBlob,
		IDxcBlob* fftRowShaderBlob,
		IDxcBlob* transposeShaderBlob,
		IDxcBlob* finalizeShaderBlob);
	bool CreateFallbackResources();
	bool CreateSurfaceSampleResources();
	bool CreateSimulationResources(const EditorOceanRenderSettings& oceanSettings);
	bool CreateBuffer(
		UINT64 size,
		D3D12_HEAP_TYPE heapType,
		D3D12_RESOURCE_FLAGS flags,
		D3D12_RESOURCE_STATES initialState,
		Microsoft::WRL::ComPtr<ID3D12Resource>& resource) const;
	void BuildInitialSpectrum(
		const EditorOceanRenderSettings& oceanSettings,
		SpectrumValue* destination,
		uint32_t spectrumValueCount);
	bool NeedsSpectrumRebuild(const EditorOceanRenderSettings& oceanSettings) const;
	void ExecuteFft2D(
		ID3D12GraphicsCommandList* commandList,
		ID3D12Resource* fieldResource);
	static uint32_t NormalizeFftResolution(int32_t requestedResolution);
	static float CalculateDomainLength(const EditorOceanRenderSettings& oceanSettings);
	static void InsertUavBarrier(
		ID3D12GraphicsCommandList* commandList,
		ID3D12Resource* resource);

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> updateSpectrumPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> fftRowPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> transposePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> finalizePipelineState_;

	Microsoft::WRL::ComPtr<ID3D12Resource> initialSpectrumResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> initialSpectrumUploadResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> heightFieldResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> displacementXFieldResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> displacementZFieldResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> gradientXFieldResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> gradientZFieldResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> temporaryFieldResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> displacementOutputResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> normalFoamOutputResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> surfaceSampleRequestResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> surfaceSampleOutputResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> surfaceSampleReadbackResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> fallbackDisplacementResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> fallbackNormalFoamResource_;
	std::vector<SurfaceSampleRequest> queuedSurfaceSampleRequests_;
	std::unordered_map<uint64_t, size_t> queuedSurfaceSampleIndices_;
	std::unordered_map<uint64_t, SurfaceSample> resolvedSurfaceSamples_;
	std::unordered_map<uint64_t, Vector3> previousSurfaceDisplacements_;
	std::unordered_map<uint64_t, float> previousSurfaceSampleTimes_;

	EditorOceanRenderSettings activeSettings_{};
	uint32_t fftResolution_ = 0u;
	uint32_t updateFrameCounter_ = 0u;
	float domainLength_ = 1.0f;
	float spectrumHeightScale_ = 1.0f;
	uint32_t submittedSurfaceSampleCount_ = 0u;
	bool isInitialized_ = false;
	bool hasActiveSettings_ = false;
	bool isInitialSpectrumUploadPending_ = false;
	bool areOutputsShaderReadable_ = false;
	bool hasValidOutput_ = false;
	bool hasPendingSurfaceSampleReadback_ = false;
};
