#include "EditorOceanFftManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <random>

namespace {
	constexpr uint32_t kComputeRootConstantCount = 16u;
	constexpr uint32_t kComputeUavCount = 8u;
	constexpr uint32_t kComputeSurfaceSampleSrvRootIndex = 9u;
	constexpr uint32_t kComputeThreadGroupSize = 16u;
	constexpr float kOceanGravity = 9.81f;

	float GetVectorLength(float x, float y) {
		return std::sqrt(x * x + y * y);
	}

	Vector2 NormalizeDirection(const Vector2& direction, const Vector2& fallbackDirection) {
		const float directionLength = GetVectorLength(direction.x, direction.y);

		if (directionLength <= 0.0001f) {
			return fallbackDirection;
		}

		return {
			direction.x / directionLength,
			direction.y / directionLength
		};
	}

}

bool EditorOceanFftManager::Initialize(
	ID3D12Device* device,
	IDxcBlob* updateSpectrumShaderBlob,
	IDxcBlob* fftRowShaderBlob,
	IDxcBlob* transposeShaderBlob,
	IDxcBlob* finalizeShaderBlob) {
	Finalize();

	if (device == nullptr ||
		updateSpectrumShaderBlob == nullptr ||
		fftRowShaderBlob == nullptr ||
		transposeShaderBlob == nullptr ||
		finalizeShaderBlob == nullptr) {
		return false;
	}

	device_ = device;

	if (!CreateRootSignatureAndPipelineStates(
		updateSpectrumShaderBlob,
		fftRowShaderBlob,
		transposeShaderBlob,
		finalizeShaderBlob) ||
		!CreateFallbackResources() ||
		!CreateSurfaceSampleResources()) {
		Finalize();
		return false;
	}

	isInitialized_ = true;
	return true;
}

bool EditorOceanFftManager::Execute(
	ID3D12GraphicsCommandList* commandList,
	const EditorOceanRenderSettings& oceanSettings,
	float oceanElapsedTime) {
	if (!isInitialized_ || commandList == nullptr || !oceanSettings.isEnabled) {
		return false;
	}

	const bool needsSpectrumRebuild = NeedsSpectrumRebuild(oceanSettings);
	const bool hasSimulationSettingsChanged = HasSimulationSettingsChanged(oceanSettings);
	const float effectiveOceanTime =
		oceanElapsedTime * oceanSettings.timeScale * oceanSettings.waveSpeed;

	if (needsSpectrumRebuild &&
		!CreateSimulationResources(oceanSettings)) {
		return false;
	}

	// Play前はOcean時刻を固定する。設定変更もSample要求もない待機フレームでは直前のGPU結果を再利用する。
	if (hasValidOutput_ &&
		!needsSpectrumRebuild &&
		!hasSimulationSettingsChanged &&
		std::fabs(lastExecutedOceanTime_ - effectiveOceanTime) <= 0.000001f &&
		queuedSurfaceSampleRequests_.empty()) {
		return true;
	}

	activeSettings_ = oceanSettings;
	const bool shouldUpdateThisFrame =
		!hasValidOutput_ ||
		needsSpectrumRebuild ||
		hasSimulationSettingsChanged ||
		fftResolution_ < kMaximumFftResolution ||
		(updateFrameCounter_ % 2u) == 0u;
	updateFrameCounter_++;

	if (!shouldUpdateThisFrame) {
		return true;
	}

	if (isInitialSpectrumUploadPending_) {
		commandList->CopyResource(
			initialSpectrumResource_.Get(),
			initialSpectrumUploadResource_.Get());

		D3D12_RESOURCE_BARRIER initialSpectrumBarrier{};
		initialSpectrumBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		initialSpectrumBarrier.Transition.pResource = initialSpectrumResource_.Get();
		initialSpectrumBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		initialSpectrumBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		initialSpectrumBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		commandList->ResourceBarrier(1u, &initialSpectrumBarrier);
		isInitialSpectrumUploadPending_ = false;
	}

	if (areOutputsShaderReadable_) {
		std::array<D3D12_RESOURCE_BARRIER, 2u> outputBarriers{};

		for (uint32_t outputIndex = 0u; outputIndex < outputBarriers.size(); outputIndex++) {
			D3D12_RESOURCE_BARRIER& outputBarrier = outputBarriers[outputIndex];
			outputBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			outputBarrier.Transition.pResource = outputIndex == 0u
				? displacementOutputResource_.Get()
				: normalFoamOutputResource_.Get();
			outputBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			outputBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
			outputBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}

		commandList->ResourceBarrier(
			static_cast<UINT>(outputBarriers.size()),
			outputBarriers.data());
		areOutputsShaderReadable_ = false;
	}

	OceanFftConstants constants{};
	constants.oceanTime = effectiveOceanTime;
	constants.gravity = kOceanGravity;
	constants.waterDepth = (std::max)(oceanSettings.waterDepth, 0.1f);
	constants.fftResolution = fftResolution_;
	constants.domainLength = domainLength_;
	constants.maxWaveHeight = (std::max)(oceanSettings.maxWaveHeight, 0.01f);
	constants.choppiness = oceanSettings.choppiness;
	constants.foamThreshold = (std::clamp)(oceanSettings.foamThreshold, 0.0f, 1.0f);
	constants.foamStrength = (std::max)(oceanSettings.foamStrength, 0.0f);
	constants.crestSharpness = (std::clamp)(oceanSettings.crestSharpness, 0.0f, 1.0f);
	constants.heightScale = spectrumHeightScale_;
	constants.sampleCount = static_cast<uint32_t>((std::min)(
		queuedSurfaceSampleRequests_.size(),
		static_cast<size_t>(kMaximumSurfaceSampleCount)));

	if (constants.sampleCount > 0u) {
		void* mappedRequests = nullptr;
		D3D12_RANGE noReadRange{0u, 0u};
		const HRESULT mapResult = surfaceSampleRequestResource_->Map(
			0u,
			&noReadRange,
			&mappedRequests);

		if (FAILED(mapResult) || mappedRequests == nullptr) {
			return false;
		}

		const size_t requestDataSize =
			static_cast<size_t>(constants.sampleCount) * sizeof(SurfaceSampleRequest);
		std::memcpy(
			mappedRequests,
			queuedSurfaceSampleRequests_.data(),
			requestDataSize);
		surfaceSampleRequestResource_->Unmap(0u, nullptr);
	}

	commandList->SetComputeRootSignature(computeRootSignature_.Get());
	commandList->SetComputeRoot32BitConstants(
		0u,
		kComputeRootConstantCount,
		&constants,
		0u);
	commandList->SetPipelineState(updateSpectrumPipelineState_.Get());
	commandList->SetComputeRootUnorderedAccessView(
		1u,
		initialSpectrumResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		2u,
		heightFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		3u,
		displacementXFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		4u,
		displacementZFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		5u,
		gradientXFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		6u,
		gradientZFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		7u,
		temporaryFieldResource_->GetGPUVirtualAddress());

	const uint32_t computeGroupCount =
		(fftResolution_ + kComputeThreadGroupSize - 1u) / kComputeThreadGroupSize;
	commandList->Dispatch(computeGroupCount, computeGroupCount, 1u);
	InsertUavBarrier(commandList, heightFieldResource_.Get());
	InsertUavBarrier(commandList, displacementXFieldResource_.Get());
	InsertUavBarrier(commandList, displacementZFieldResource_.Get());
	InsertUavBarrier(commandList, gradientXFieldResource_.Get());
	InsertUavBarrier(commandList, gradientZFieldResource_.Get());

	ExecuteFft2D(commandList, heightFieldResource_.Get());
	ExecuteFft2D(commandList, displacementXFieldResource_.Get());
	ExecuteFft2D(commandList, displacementZFieldResource_.Get());
	ExecuteFft2D(commandList, gradientXFieldResource_.Get());
	ExecuteFft2D(commandList, gradientZFieldResource_.Get());

	commandList->SetPipelineState(finalizePipelineState_.Get());
	commandList->SetComputeRootUnorderedAccessView(
		1u,
		displacementOutputResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		2u,
		normalFoamOutputResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		3u,
		heightFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		4u,
		displacementXFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		5u,
		displacementZFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		6u,
		gradientXFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		7u,
		gradientZFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		8u,
		surfaceSampleOutputResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootShaderResourceView(
		kComputeSurfaceSampleSrvRootIndex,
		surfaceSampleRequestResource_->GetGPUVirtualAddress());
	commandList->Dispatch(computeGroupCount, computeGroupCount, 1u);
	InsertUavBarrier(commandList, displacementOutputResource_.Get());
	InsertUavBarrier(commandList, normalFoamOutputResource_.Get());
	InsertUavBarrier(commandList, surfaceSampleOutputResource_.Get());

	if (constants.sampleCount > 0u) {
		D3D12_RESOURCE_BARRIER sampleCopyBarrier{};
		sampleCopyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		sampleCopyBarrier.Transition.pResource = surfaceSampleOutputResource_.Get();
		sampleCopyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		sampleCopyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		sampleCopyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		commandList->ResourceBarrier(1u, &sampleCopyBarrier);

		const UINT64 sampleResultSize =
			static_cast<UINT64>(constants.sampleCount) * sizeof(SurfaceSampleResult);
		commandList->CopyBufferRegion(
			surfaceSampleReadbackResource_.Get(),
			0u,
			surfaceSampleOutputResource_.Get(),
			0u,
			sampleResultSize);

		std::swap(
			sampleCopyBarrier.Transition.StateBefore,
			sampleCopyBarrier.Transition.StateAfter);
		commandList->ResourceBarrier(1u, &sampleCopyBarrier);
		submittedSurfaceSampleCount_ = constants.sampleCount;
		hasPendingSurfaceSampleReadback_ = true;
		queuedSurfaceSampleRequests_.clear();
		queuedSurfaceSampleIndices_.clear();
	}

	std::array<D3D12_RESOURCE_BARRIER, 2u> outputBarriers{};

	for (uint32_t outputIndex = 0u; outputIndex < outputBarriers.size(); outputIndex++) {
		D3D12_RESOURCE_BARRIER& outputBarrier = outputBarriers[outputIndex];
		outputBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		outputBarrier.Transition.pResource = outputIndex == 0u
			? displacementOutputResource_.Get()
			: normalFoamOutputResource_.Get();
		outputBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		outputBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		outputBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	}

	commandList->ResourceBarrier(
		static_cast<UINT>(outputBarriers.size()),
		outputBarriers.data());
	areOutputsShaderReadable_ = true;
	hasValidOutput_ = true;
	lastExecutedOceanTime_ = effectiveOceanTime;
	return true;
}

void EditorOceanFftManager::BindGraphicsResources(
	ID3D12GraphicsCommandList* commandList) const {
	if (!isInitialized_ || commandList == nullptr) {
		return;
	}

	ID3D12Resource* displacementResource = hasValidOutput_
		? displacementOutputResource_.Get()
		: fallbackDisplacementResource_.Get();
	ID3D12Resource* normalFoamResource = hasValidOutput_
		? normalFoamOutputResource_.Get()
		: fallbackNormalFoamResource_.Get();

	if (displacementResource == nullptr || normalFoamResource == nullptr) {
		return;
	}

	commandList->SetGraphicsRootShaderResourceView(
		18u,
		displacementResource->GetGPUVirtualAddress());
	commandList->SetGraphicsRootShaderResourceView(
		19u,
		normalFoamResource->GetGPUVirtualAddress());
}

void EditorOceanFftManager::BindPostProcessDisplacement(
	ID3D12GraphicsCommandList* commandList,
	uint32_t rootParameterIndex) const {
	if (!isInitialized_ || commandList == nullptr) {
		return;
	}

	ID3D12Resource* displacementResource = hasValidOutput_
		? displacementOutputResource_.Get()
		: fallbackDisplacementResource_.Get();
	if (displacementResource == nullptr) {
		return;
	}

	commandList->SetGraphicsRootShaderResourceView(
		rootParameterIndex,
		displacementResource->GetGPUVirtualAddress());
}

void EditorOceanFftManager::ApplyToSceneObject(EditorSceneObject& sceneObject) const {
	if (!sceneObject.ocean.isEnabled || !IsReadyFor(sceneObject.ocean)) {
		return;
	}

	auto applyMetadata = [this](TransformationMatrix* transformationData) {
		if (transformationData == nullptr) {
			return;
		}

		transformationData->oceanParams0.x = 2.0f;
		transformationData->oceanWaveData1[15u] = {
			static_cast<float>(fftResolution_),
			domainLength_,
			1.0f / (std::max)(domainLength_, 0.001f),
			1.0f / static_cast<float>((std::max)(fftResolution_, 1u))
		};
	};

	applyMetadata(sceneObject.transformationData);
	applyMetadata(sceneObject.gameTransformationData);
}

bool EditorOceanFftManager::QueueSurfaceSample(
	uint64_t sampleKey,
	const Vector2& localPosition,
	SurfaceSample& surfaceSample) {
	surfaceSample = {};

	const auto resolvedIterator = resolvedSurfaceSamples_.find(sampleKey);
	const bool hasResolvedSample = resolvedIterator != resolvedSurfaceSamples_.end();

	if (hasResolvedSample) {
		surfaceSample = resolvedIterator->second;
	}

	const auto queuedIterator = queuedSurfaceSampleIndices_.find(sampleKey);

	if (queuedIterator != queuedSurfaceSampleIndices_.end()) {
		SurfaceSampleRequest& request =
			queuedSurfaceSampleRequests_[queuedIterator->second];
		request.localPositionX = localPosition.x;
		request.localPositionZ = localPosition.y;
		return hasResolvedSample;
	}

	if (queuedSurfaceSampleRequests_.size() >= kMaximumSurfaceSampleCount) {
		return hasResolvedSample;
	}

	SurfaceSampleRequest request{};
	request.localPositionX = localPosition.x;
	request.localPositionZ = localPosition.y;
	request.sampleKeyLow = static_cast<uint32_t>(sampleKey & 0xffffffffull);
	request.sampleKeyHigh = static_cast<uint32_t>(sampleKey >> 32u);
	queuedSurfaceSampleIndices_[sampleKey] = queuedSurfaceSampleRequests_.size();
	queuedSurfaceSampleRequests_.push_back(request);
	return hasResolvedSample;
}

void EditorOceanFftManager::ResolveReadback() {
	if (!hasPendingSurfaceSampleReadback_ ||
		submittedSurfaceSampleCount_ == 0u ||
		surfaceSampleReadbackResource_ == nullptr) {
		return;
	}

	const SIZE_T readSize =
		static_cast<SIZE_T>(submittedSurfaceSampleCount_) * sizeof(SurfaceSampleResult);
	D3D12_RANGE readRange{0u, readSize};
	void* mappedResults = nullptr;
	const HRESULT mapResult = surfaceSampleReadbackResource_->Map(
		0u,
		&readRange,
		&mappedResults);

	if (FAILED(mapResult) || mappedResults == nullptr) {
		return;
	}

	const SurfaceSampleResult* sampleResults =
		static_cast<const SurfaceSampleResult*>(mappedResults);

	for (uint32_t sampleIndex = 0u;
		sampleIndex < submittedSurfaceSampleCount_;
		sampleIndex++) {
		const SurfaceSampleResult& sampleResult = sampleResults[sampleIndex];
		const uint64_t sampleKey =
			static_cast<uint64_t>(sampleResult.sampleKeyLow) |
			(static_cast<uint64_t>(sampleResult.sampleKeyHigh) << 32u);
		const Vector3 currentDisplacement{
			sampleResult.displacementAndTime[0],
			sampleResult.surfacePositionFoam[1],
			sampleResult.displacementAndTime[1]};
		const float currentSampleTime = sampleResult.displacementAndTime[2];
		Vector3 surfaceVelocity{0.0f, 0.0f, 0.0f};
		const auto previousDisplacementIterator =
			previousSurfaceDisplacements_.find(sampleKey);
		const auto previousTimeIterator = previousSurfaceSampleTimes_.find(sampleKey);

		if (previousDisplacementIterator != previousSurfaceDisplacements_.end() &&
			previousTimeIterator != previousSurfaceSampleTimes_.end()) {
			const float sampleDeltaTime = currentSampleTime - previousTimeIterator->second;

			if (sampleDeltaTime > 0.0001f) {
				const Vector3 displacementDelta{
					currentDisplacement.x - previousDisplacementIterator->second.x,
					currentDisplacement.y - previousDisplacementIterator->second.y,
					currentDisplacement.z - previousDisplacementIterator->second.z};
				surfaceVelocity = {
					displacementDelta.x / sampleDeltaTime,
					displacementDelta.y / sampleDeltaTime,
					displacementDelta.z / sampleDeltaTime};
			}
		}

		SurfaceSample& surfaceSample = resolvedSurfaceSamples_[sampleKey];
		surfaceSample.localPosition = {
			sampleResult.surfacePositionFoam[0],
			sampleResult.surfacePositionFoam[1],
			sampleResult.surfacePositionFoam[2]};
		surfaceSample.localNormal = {
			sampleResult.surfaceNormal[0],
			sampleResult.surfaceNormal[1],
			sampleResult.surfaceNormal[2]};
		surfaceSample.localVelocity = surfaceVelocity;
		surfaceSample.foam = sampleResult.surfacePositionFoam[3];
		surfaceSample.isValid = true;
		previousSurfaceDisplacements_[sampleKey] = currentDisplacement;
		previousSurfaceSampleTimes_[sampleKey] = currentSampleTime;
	}

	D3D12_RANGE noWriteRange{0u, 0u};
	surfaceSampleReadbackResource_->Unmap(0u, &noWriteRange);
	submittedSurfaceSampleCount_ = 0u;
	hasPendingSurfaceSampleReadback_ = false;
}

bool EditorOceanFftManager::IsReadyFor(
	const EditorOceanRenderSettings& oceanSettings) const {
	return hasValidOutput_ &&
		hasActiveSettings_ &&
		NormalizeFftResolution(oceanSettings.gridResolution) == fftResolution_ &&
		CalculateDomainLength(oceanSettings) == domainLength_ &&
		oceanSettings.waveHeight == activeSettings_.waveHeight &&
		oceanSettings.waveLength == activeSettings_.waveLength &&
		oceanSettings.waveSpeed == activeSettings_.waveSpeed &&
		oceanSettings.timeScale == activeSettings_.timeScale &&
		oceanSettings.choppiness == activeSettings_.choppiness &&
		oceanSettings.primaryDirection.x == activeSettings_.primaryDirection.x &&
		oceanSettings.primaryDirection.y == activeSettings_.primaryDirection.y &&
		oceanSettings.secondaryDirection.x == activeSettings_.secondaryDirection.x &&
		oceanSettings.secondaryDirection.y == activeSettings_.secondaryDirection.y &&
		oceanSettings.secondaryWaveScale == activeSettings_.secondaryWaveScale &&
		oceanSettings.rippleScale == activeSettings_.rippleScale &&
		oceanSettings.rippleStrength == activeSettings_.rippleStrength &&
		oceanSettings.windSpeed == activeSettings_.windSpeed &&
		oceanSettings.waterDepth == activeSettings_.waterDepth &&
		oceanSettings.directionSpread == activeSettings_.directionSpread &&
		oceanSettings.swellStrength == activeSettings_.swellStrength &&
		oceanSettings.spectrumSeed == activeSettings_.spectrumSeed &&
		oceanSettings.maxWaveHeight == activeSettings_.maxWaveHeight &&
		oceanSettings.crestSharpness == activeSettings_.crestSharpness &&
		oceanSettings.foamStrength == activeSettings_.foamStrength &&
		oceanSettings.foamThreshold == activeSettings_.foamThreshold;
}

void EditorOceanFftManager::Finalize() {
	surfaceSampleReadbackResource_.Reset();
	surfaceSampleOutputResource_.Reset();
	surfaceSampleRequestResource_.Reset();
	normalFoamOutputResource_.Reset();
	displacementOutputResource_.Reset();
	temporaryFieldResource_.Reset();
	gradientZFieldResource_.Reset();
	gradientXFieldResource_.Reset();
	displacementZFieldResource_.Reset();
	displacementXFieldResource_.Reset();
	heightFieldResource_.Reset();
	initialSpectrumUploadResource_.Reset();
	initialSpectrumResource_.Reset();
	fallbackNormalFoamResource_.Reset();
	fallbackDisplacementResource_.Reset();
	finalizePipelineState_.Reset();
	transposePipelineState_.Reset();
	fftRowPipelineState_.Reset();
	updateSpectrumPipelineState_.Reset();
	computeRootSignature_.Reset();
	device_.Reset();
	activeSettings_ = {};
	queuedSurfaceSampleRequests_.clear();
	queuedSurfaceSampleIndices_.clear();
	resolvedSurfaceSamples_.clear();
	previousSurfaceDisplacements_.clear();
	previousSurfaceSampleTimes_.clear();
	fftResolution_ = 0u;
	updateFrameCounter_ = 0u;
	domainLength_ = 1.0f;
	spectrumHeightScale_ = 1.0f;
	lastExecutedOceanTime_ = -1.0f;
	submittedSurfaceSampleCount_ = 0u;
	isInitialized_ = false;
	hasActiveSettings_ = false;
	isInitialSpectrumUploadPending_ = false;
	areOutputsShaderReadable_ = false;
	hasValidOutput_ = false;
	hasPendingSurfaceSampleReadback_ = false;
}

bool EditorOceanFftManager::CreateRootSignatureAndPipelineStates(
	IDxcBlob* updateSpectrumShaderBlob,
	IDxcBlob* fftRowShaderBlob,
	IDxcBlob* transposeShaderBlob,
	IDxcBlob* finalizeShaderBlob) {
	std::array<D3D12_ROOT_PARAMETER, 2u + kComputeUavCount> rootParameters{};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[0].Constants.ShaderRegister = 0u;
	rootParameters[0].Constants.RegisterSpace = 0u;
	rootParameters[0].Constants.Num32BitValues = kComputeRootConstantCount;

	for (uint32_t uavIndex = 0u; uavIndex < kComputeUavCount; uavIndex++) {
		D3D12_ROOT_PARAMETER& rootParameter = rootParameters[1u + uavIndex];
		rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameter.Descriptor.ShaderRegister = uavIndex;
		rootParameter.Descriptor.RegisterSpace = 0u;
	}

	D3D12_ROOT_PARAMETER& sampleRequestRootParameter =
		rootParameters[1u + kComputeUavCount];
	sampleRequestRootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	sampleRequestRootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	sampleRequestRootParameter.Descriptor.ShaderRegister = 0u;
	sampleRequestRootParameter.Descriptor.RegisterSpace = 0u;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDescription{};
	rootSignatureDescription.NumParameters = static_cast<UINT>(rootParameters.size());
	rootSignatureDescription.pParameters = rootParameters.data();
	rootSignatureDescription.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT result = D3D12SerializeRootSignature(
		&rootSignatureDescription,
		D3D_ROOT_SIGNATURE_VERSION_1,
		signatureBlob.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (FAILED(result) || signatureBlob == nullptr) {
		return false;
	}

	result = device_->CreateRootSignature(
		0u,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(computeRootSignature_.GetAddressOf()));

	if (FAILED(result) || computeRootSignature_ == nullptr) {
		return false;
	}

	auto createPipelineState = [this](
		IDxcBlob* shaderBlob,
		Microsoft::WRL::ComPtr<ID3D12PipelineState>& pipelineState) {
		D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDescription{};
		pipelineStateDescription.pRootSignature = computeRootSignature_.Get();
		pipelineStateDescription.CS = {
			shaderBlob->GetBufferPointer(),
			shaderBlob->GetBufferSize()
		};
		return device_->CreateComputePipelineState(
			&pipelineStateDescription,
			IID_PPV_ARGS(pipelineState.GetAddressOf()));
	};

	result = createPipelineState(updateSpectrumShaderBlob, updateSpectrumPipelineState_);

	if (FAILED(result) || updateSpectrumPipelineState_ == nullptr) {
		return false;
	}

	result = createPipelineState(fftRowShaderBlob, fftRowPipelineState_);

	if (FAILED(result) || fftRowPipelineState_ == nullptr) {
		return false;
	}

	result = createPipelineState(transposeShaderBlob, transposePipelineState_);

	if (FAILED(result) || transposePipelineState_ == nullptr) {
		return false;
	}

	result = createPipelineState(finalizeShaderBlob, finalizePipelineState_);
	return SUCCEEDED(result) && finalizePipelineState_ != nullptr;
}

bool EditorOceanFftManager::CreateFallbackResources() {
	constexpr UINT64 kFallbackBufferSize = sizeof(float) * 4u;

	if (!CreateBuffer(
		kFallbackBufferSize,
		D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		fallbackDisplacementResource_) ||
		!CreateBuffer(
		kFallbackBufferSize,
		D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		fallbackNormalFoamResource_)) {
		return false;
	}

	std::array<float, 4u> fallbackDisplacement{};
	std::array<float, 4u> fallbackNormalFoam{0.0f, 1.0f, 0.0f, 0.0f};
	void* mappedData = nullptr;
	D3D12_RANGE noReadRange{0u, 0u};
	HRESULT result = fallbackDisplacementResource_->Map(0u, &noReadRange, &mappedData);

	if (FAILED(result) || mappedData == nullptr) {
		return false;
	}

	std::memcpy(mappedData, fallbackDisplacement.data(), kFallbackBufferSize);
	fallbackDisplacementResource_->Unmap(0u, nullptr);
	mappedData = nullptr;
	result = fallbackNormalFoamResource_->Map(0u, &noReadRange, &mappedData);

	if (FAILED(result) || mappedData == nullptr) {
		return false;
	}

	std::memcpy(mappedData, fallbackNormalFoam.data(), kFallbackBufferSize);
	fallbackNormalFoamResource_->Unmap(0u, nullptr);
	return true;
}

bool EditorOceanFftManager::CreateSurfaceSampleResources() {
	const UINT64 requestBufferSize =
		static_cast<UINT64>(kMaximumSurfaceSampleCount) * sizeof(SurfaceSampleRequest);
	const UINT64 resultBufferSize =
		static_cast<UINT64>(kMaximumSurfaceSampleCount) * sizeof(SurfaceSampleResult);

	return CreateBuffer(
		requestBufferSize,
		D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		surfaceSampleRequestResource_) &&
		CreateBuffer(
			resultBufferSize,
			D3D12_HEAP_TYPE_DEFAULT,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			surfaceSampleOutputResource_) &&
		CreateBuffer(
			resultBufferSize,
			D3D12_HEAP_TYPE_READBACK,
			D3D12_RESOURCE_FLAG_NONE,
			D3D12_RESOURCE_STATE_COPY_DEST,
			surfaceSampleReadbackResource_);
}

bool EditorOceanFftManager::CreateSimulationResources(
	const EditorOceanRenderSettings& oceanSettings) {
	normalFoamOutputResource_.Reset();
	displacementOutputResource_.Reset();
	temporaryFieldResource_.Reset();
	gradientZFieldResource_.Reset();
	gradientXFieldResource_.Reset();
	displacementZFieldResource_.Reset();
	displacementXFieldResource_.Reset();
	heightFieldResource_.Reset();
	initialSpectrumUploadResource_.Reset();
	initialSpectrumResource_.Reset();

	fftResolution_ = NormalizeFftResolution(oceanSettings.gridResolution);
	domainLength_ = CalculateDomainLength(oceanSettings);
	const UINT64 spectrumValueCount =
		static_cast<UINT64>(fftResolution_) * static_cast<UINT64>(fftResolution_);
	const UINT64 spectrumBufferSize = spectrumValueCount * sizeof(SpectrumValue);
	const UINT64 outputBufferSize = spectrumValueCount * sizeof(float) * 4u;

	if (!CreateBuffer(
		spectrumBufferSize,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COPY_DEST,
		initialSpectrumResource_) ||
		!CreateBuffer(
		spectrumBufferSize,
		D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_FLAG_NONE,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		initialSpectrumUploadResource_)) {
		return false;
	}

	std::array<Microsoft::WRL::ComPtr<ID3D12Resource>*, 6u> fieldResources = {
		&heightFieldResource_,
		&displacementXFieldResource_,
		&displacementZFieldResource_,
		&gradientXFieldResource_,
		&gradientZFieldResource_,
		&temporaryFieldResource_
	};

	for (Microsoft::WRL::ComPtr<ID3D12Resource>* fieldResource : fieldResources) {
		if (!CreateBuffer(
			spectrumBufferSize,
			D3D12_HEAP_TYPE_DEFAULT,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			*fieldResource)) {
			return false;
		}
	}

	if (!CreateBuffer(
		outputBufferSize,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		displacementOutputResource_) ||
		!CreateBuffer(
		outputBufferSize,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		normalFoamOutputResource_)) {
		return false;
	}

	void* mappedSpectrumData = nullptr;
	D3D12_RANGE noReadRange{0u, 0u};
	const HRESULT mapResult = initialSpectrumUploadResource_->Map(
		0u,
		&noReadRange,
		&mappedSpectrumData);

	if (FAILED(mapResult) || mappedSpectrumData == nullptr) {
		return false;
	}

	BuildInitialSpectrum(
		oceanSettings,
		static_cast<SpectrumValue*>(mappedSpectrumData),
		static_cast<uint32_t>(spectrumValueCount));
	initialSpectrumUploadResource_->Unmap(0u, nullptr);
	activeSettings_ = oceanSettings;
	hasActiveSettings_ = true;
	isInitialSpectrumUploadPending_ = true;
	areOutputsShaderReadable_ = false;
	hasValidOutput_ = false;
	resolvedSurfaceSamples_.clear();
	previousSurfaceDisplacements_.clear();
	previousSurfaceSampleTimes_.clear();
	submittedSurfaceSampleCount_ = 0u;
	hasPendingSurfaceSampleReadback_ = false;
	updateFrameCounter_ = 0u;
	return true;
}

bool EditorOceanFftManager::CreateBuffer(
	UINT64 size,
	D3D12_HEAP_TYPE heapType,
	D3D12_RESOURCE_FLAGS flags,
	D3D12_RESOURCE_STATES initialState,
	Microsoft::WRL::ComPtr<ID3D12Resource>& resource) const {
	if (device_ == nullptr || size == 0u) {
		return false;
	}

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = heapType;

	D3D12_RESOURCE_DESC resourceDescription{};
	resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDescription.Width = size;
	resourceDescription.Height = 1u;
	resourceDescription.DepthOrArraySize = static_cast<UINT16>(1u);
	resourceDescription.MipLevels = static_cast<UINT16>(1u);
	resourceDescription.SampleDesc.Count = 1u;
	resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDescription.Flags = flags;

	const HRESULT result = device_->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDescription,
		initialState,
		nullptr,
		IID_PPV_ARGS(resource.ReleaseAndGetAddressOf()));
	return SUCCEEDED(result) && resource != nullptr;
}

void EditorOceanFftManager::BuildInitialSpectrum(
	const EditorOceanRenderSettings& oceanSettings,
	SpectrumValue* destination,
	uint32_t spectrumValueCount) {
	if (destination == nullptr ||
		spectrumValueCount != fftResolution_ * fftResolution_) {
		return;
	}

	const Vector2 primaryDirection = NormalizeDirection(
		oceanSettings.primaryDirection,
		{1.0f, 0.0f});
	const float safeWindSpeed = (std::max)(oceanSettings.windSpeed, 0.1f);
	const float windX = primaryDirection.x * safeWindSpeed;
	const float windZ = primaryDirection.y * safeWindSpeed;
	const float windLength = safeWindSpeed * safeWindSpeed / kOceanGravity;
	constexpr float kPhillipsAmplitude = 0.078f;
	constexpr float kDirectionalBaseWeight = 0.22f;
	constexpr float kDirectionalAlignWeight = 0.90f;
	constexpr float kOppositeWaveScale = 0.16f;
	constexpr float kLongWaveDampingScale = 0.0045f;
	constexpr float kShortWaveDampingScale = 0.0014f;
	const float inverseSqrtTwo = 1.0f / std::sqrt(2.0f);
	const int32_t halfResolution = static_cast<int32_t>(fftResolution_ / 2u);
	const float waveNumberScale =
		2.0f * std::numbers::pi_v<float> / domainLength_;
	const uint32_t randomSeed = static_cast<uint32_t>(
		(std::max)(oceanSettings.spectrumSeed, 0.0f) * 4099.0f) + 0x6d2b79f5u;
	std::mt19937 randomGenerator(randomSeed);
	std::normal_distribution<float> gaussianDistribution(0.0f, 1.0f);
	double spectrumEnergy = 0.0;

	for (uint32_t zIndex = 0u; zIndex < fftResolution_; zIndex++) {
		const int32_t signedZ = static_cast<int32_t>(zIndex) < halfResolution
			? static_cast<int32_t>(zIndex)
			: static_cast<int32_t>(zIndex) - static_cast<int32_t>(fftResolution_);
		const float waveNumberZ = static_cast<float>(signedZ) * waveNumberScale;

		for (uint32_t xIndex = 0u; xIndex < fftResolution_; xIndex++) {
			const int32_t signedX = static_cast<int32_t>(xIndex) < halfResolution
				? static_cast<int32_t>(xIndex)
				: static_cast<int32_t>(xIndex) - static_cast<int32_t>(fftResolution_);
			const float waveNumberX = static_cast<float>(signedX) * waveNumberScale;
			const float waveNumberSquared =
				waveNumberX * waveNumberX + waveNumberZ * waveNumberZ;
			const uint32_t spectrumIndex = zIndex * fftResolution_ + xIndex;

			if (waveNumberSquared <= 0.00000001f) {
				destination[spectrumIndex] = {};
				continue;
			}

			const float waveNumberLength = std::sqrt(waveNumberSquared);
			const float directionDot =
				(waveNumberX * windX + waveNumberZ * windZ) /
				(waveNumberLength * safeWindSpeed);
			float directionalWeight =
				kDirectionalBaseWeight +
				kDirectionalAlignWeight * std::fabs(directionDot) * std::fabs(directionDot);

			if (directionDot < 0.0f) {
				directionalWeight *= kOppositeWaveScale;
			}

			const float longWaveDamping = windLength * kLongWaveDampingScale;
			const float spectrumValue =
				kPhillipsAmplitude *
				std::exp(-1.0f / (waveNumberSquared * windLength * windLength)) /
				(waveNumberSquared * waveNumberSquared) *
				directionalWeight *
				std::exp(-waveNumberSquared * longWaveDamping * longWaveDamping) *
				std::exp(
					-waveNumberSquared * kShortWaveDampingScale *
					windLength * windLength);

			const float randomScale =
				std::sqrt((std::max)(spectrumValue, 0.0f)) * inverseSqrtTwo;
			SpectrumValue& spectrum = destination[spectrumIndex];
			spectrum.real = gaussianDistribution(randomGenerator) * randomScale;
			spectrum.imaginary = gaussianDistribution(randomGenerator) * randomScale;
			spectrumEnergy += static_cast<double>(
				spectrum.real * spectrum.real + spectrum.imaginary * spectrum.imaginary);
		}
	}

	// 元のPhillipsスペクトル自体は変更せず、IFFT後のワールド高さだけを調整する。
	const float targetRootMeanSquareHeight = (std::min)(
		(std::max)(oceanSettings.waveHeight, 0.0f) * 0.72f,
		(std::max)(oceanSettings.maxWaveHeight, 0.01f) * 0.62f);
	const double inverseTransformScale = static_cast<double>(spectrumValueCount);
	const double heightScale = spectrumEnergy > 0.00000001
		? static_cast<double>(targetRootMeanSquareHeight) * inverseTransformScale /
			std::sqrt(spectrumEnergy * 2.0)
		: 0.0;
	spectrumHeightScale_ = static_cast<float>(heightScale);
}

bool EditorOceanFftManager::NeedsSpectrumRebuild(
	const EditorOceanRenderSettings& oceanSettings) const {
	return !hasActiveSettings_ ||
		NormalizeFftResolution(oceanSettings.gridResolution) != fftResolution_ ||
		CalculateDomainLength(oceanSettings) != domainLength_ ||
		oceanSettings.waveHeight != activeSettings_.waveHeight ||
		oceanSettings.waveLength != activeSettings_.waveLength ||
		oceanSettings.primaryDirection.x != activeSettings_.primaryDirection.x ||
		oceanSettings.primaryDirection.y != activeSettings_.primaryDirection.y ||
		oceanSettings.secondaryDirection.x != activeSettings_.secondaryDirection.x ||
		oceanSettings.secondaryDirection.y != activeSettings_.secondaryDirection.y ||
		oceanSettings.secondaryWaveScale != activeSettings_.secondaryWaveScale ||
		oceanSettings.rippleScale != activeSettings_.rippleScale ||
		oceanSettings.rippleStrength != activeSettings_.rippleStrength ||
		oceanSettings.windSpeed != activeSettings_.windSpeed ||
		oceanSettings.directionSpread != activeSettings_.directionSpread ||
		oceanSettings.swellStrength != activeSettings_.swellStrength ||
		oceanSettings.spectrumSeed != activeSettings_.spectrumSeed;
}

bool EditorOceanFftManager::HasSimulationSettingsChanged(
	const EditorOceanRenderSettings& oceanSettings) const {
	return !hasActiveSettings_ ||
		oceanSettings.maxWaveHeight != activeSettings_.maxWaveHeight ||
		oceanSettings.waveSpeed != activeSettings_.waveSpeed ||
		oceanSettings.timeScale != activeSettings_.timeScale ||
		oceanSettings.choppiness != activeSettings_.choppiness ||
		oceanSettings.waterDepth != activeSettings_.waterDepth ||
		oceanSettings.crestSharpness != activeSettings_.crestSharpness ||
		oceanSettings.foamStrength != activeSettings_.foamStrength ||
		oceanSettings.foamThreshold != activeSettings_.foamThreshold;
}

void EditorOceanFftManager::ExecuteFft2D(
	ID3D12GraphicsCommandList* commandList,
	ID3D12Resource* fieldResource) {
	if (commandList == nullptr || fieldResource == nullptr || temporaryFieldResource_ == nullptr) {
		return;
	}

	const uint32_t transposeGroupCount =
		(fftResolution_ + kComputeThreadGroupSize - 1u) / kComputeThreadGroupSize;

	commandList->SetPipelineState(fftRowPipelineState_.Get());
	commandList->SetComputeRootUnorderedAccessView(1u, fieldResource->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		2u,
		temporaryFieldResource_->GetGPUVirtualAddress());
	commandList->Dispatch(fftResolution_, 1u, 1u);
	InsertUavBarrier(commandList, temporaryFieldResource_.Get());

	commandList->SetPipelineState(transposePipelineState_.Get());
	commandList->SetComputeRootUnorderedAccessView(
		1u,
		temporaryFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(2u, fieldResource->GetGPUVirtualAddress());
	commandList->Dispatch(transposeGroupCount, transposeGroupCount, 1u);
	InsertUavBarrier(commandList, fieldResource);

	commandList->SetPipelineState(fftRowPipelineState_.Get());
	commandList->SetComputeRootUnorderedAccessView(1u, fieldResource->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(
		2u,
		temporaryFieldResource_->GetGPUVirtualAddress());
	commandList->Dispatch(fftResolution_, 1u, 1u);
	InsertUavBarrier(commandList, temporaryFieldResource_.Get());

	commandList->SetPipelineState(transposePipelineState_.Get());
	commandList->SetComputeRootUnorderedAccessView(
		1u,
		temporaryFieldResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootUnorderedAccessView(2u, fieldResource->GetGPUVirtualAddress());
	commandList->Dispatch(transposeGroupCount, transposeGroupCount, 1u);
	InsertUavBarrier(commandList, fieldResource);
}

uint32_t EditorOceanFftManager::NormalizeFftResolution(int32_t requestedResolution) {
	const uint32_t clampedResolution = static_cast<uint32_t>((std::clamp)(
		requestedResolution,
		64,
		static_cast<int32_t>(kMaximumFftResolution)));
	uint32_t normalizedResolution = 64u;

	while (normalizedResolution * 2u <= clampedResolution) {
		normalizedResolution *= 2u;
	}

	return normalizedResolution;
}

float EditorOceanFftManager::CalculateDomainLength(
	const EditorOceanRenderSettings& oceanSettings) {
	return (std::max)(
		oceanSettings.size * 8.0f,
		oceanSettings.waveLength * 32.0f);
}

void EditorOceanFftManager::InsertUavBarrier(
	ID3D12GraphicsCommandList* commandList,
	ID3D12Resource* resource) {
	if (commandList == nullptr || resource == nullptr) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = resource;
	commandList->ResourceBarrier(1u, &barrier);
}
