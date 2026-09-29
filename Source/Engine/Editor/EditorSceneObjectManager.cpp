#include "EditorSceneObjectManager.h"

#include "EditorSharedState.h"
#include "Matrix.h"
#include "StringUtility.h"
#include "Source/Engine/Asset/AssetImportSettings.h"
#include "Source/Engine/Asset/AssetRegistry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#pragma warning(disable : 5045)

//========================================
// SceneObject描画リソース管理の構成
//========================================

// EditorSceneObjectは、編集用GameObjectをそのままGPUへ渡すものではない。
// Model/Sprite/Material/Transform等を、RendererがDrawしやすい形へ展開した描画Proxyである。
// このManagerは主に次を担当する。
//
//   GameObject/Asset側の指定
//     -> Model/TextureをLoadまたは共有Cacheから取得
//     -> Vertex/Index/Material/Skin Matrix用GPU Resourceを確保
//     -> EditorSceneObjectへView、Descriptor、World Matrixを保持
//     -> RendererがEditorSceneObjectを列挙してDraw
//
// 所有権を追うときは、Resource本体、Descriptor番号、Atlas範囲、共有Cacheの
// 参照数を別々に見る。SceneObjectを消すときは、GPU Resourceだけでなく
// これらの管理情報も対で返却しないと、Leakや再利用時の衝突になる。
namespace {
	//------------------------------
	// Skinning Pose補間
	//------------------------------

	// 2つのSkin Pose間をBone Matrixの各要素で線形補間する。
	// 実装が単純な反面、回転をQuaternionとして補間する方式ではないため、
	// 大角度差では不自然な回転や行列の直交性崩れが起こり得る。
	void InterpolateSkinMatrices(
		const std::vector<Matrix4x4>& firstMatrices,
		const std::vector<Matrix4x4>& secondMatrices,
		float interpolation,
		std::vector<Matrix4x4>& sampledMatrices) {
		if (firstMatrices.size() != secondMatrices.size()) {
			sampledMatrices = firstMatrices;
			return;
		}

		sampledMatrices.resize(firstMatrices.size());
		const float clampedInterpolation = (std::clamp)(interpolation, 0.0f, 1.0f);

		for (size_t boneIndex = 0u; boneIndex < firstMatrices.size(); boneIndex++) {
			for (int32_t row = 0; row < 4; row++) {
				for (int32_t column = 0; column < 4; column++) {
					const float firstValue = firstMatrices[boneIndex].matrix[row][column];
					const float secondValue = secondMatrices[boneIndex].matrix[row][column];
					sampledMatrices[boneIndex].matrix[row][column] =
						firstValue + (secondValue - firstValue) * clampedInterpolation;
				}
			}
		}
	}

	bool SampleSkinPose(
		const ModelData& modelData,
		int32_t clipIndex,
		float playbackTime,
		std::vector<Matrix4x4>& sampledMatrices) {
		// Clipが無効ならBind Pose相当の既定行列へ戻す。
		// 空配列を成功扱いにすると、Shaderが存在しないBone Bufferを読むためfalseを返す。
		if (clipIndex < 0 || clipIndex >= static_cast<int32_t>(modelData.animationClips.size())) {
			sampledMatrices = modelData.defaultSkinMatrices;
			return !sampledMatrices.empty();
		}

		const ModelAnimationClipData& clip =
			modelData.animationClips[static_cast<size_t>(clipIndex)];
		if (clip.skinPoseFrames.empty()) {
			sampledMatrices = modelData.defaultSkinMatrices;
			return !sampledMatrices.empty();
		}

		float sampleTime = (std::max)(playbackTime, 0.0f);
		if (clip.durationSeconds > 0.000001f) {
			sampleTime = std::fmod(sampleTime, clip.durationSeconds);
		}

		// 時刻以上になる最初のFrameを二分探索する。1Frameずつ探すより、
		// Key数が増えた場合もO(log N)で前後Frameを見つけられる。
		const auto upperFrameIterator = std::lower_bound(
			clip.skinPoseFrames.begin(),
			clip.skinPoseFrames.end(),
			sampleTime,
			[](const ModelSkinPoseFrameData& poseFrame, float targetTime) {
				return poseFrame.timeSeconds < targetTime;
			});

		if (upperFrameIterator == clip.skinPoseFrames.begin()) {
			sampledMatrices = upperFrameIterator->boneMatrices;
			return !sampledMatrices.empty();
		}

		if (upperFrameIterator == clip.skinPoseFrames.end()) {
			sampledMatrices = clip.skinPoseFrames.back().boneMatrices;
			return !sampledMatrices.empty();
		}

		const ModelSkinPoseFrameData& nextFrame = *upperFrameIterator;
		const ModelSkinPoseFrameData& previousFrame = *(upperFrameIterator - 1);
		const float frameDuration = nextFrame.timeSeconds - previousFrame.timeSeconds;
		const float interpolation = frameDuration > 0.000001f
			? (sampleTime - previousFrame.timeSeconds) / frameDuration
			: 0.0f;
		InterpolateSkinMatrices(
			previousFrame.boneMatrices,
			nextFrame.boneMatrices,
			interpolation,
			sampledMatrices);
		return !sampledMatrices.empty();
	}

	std::string TrimAvatarMaskToken(const std::string& text) {
		size_t firstCharacter = 0u;
		while (firstCharacter < text.size() &&
			std::isspace(static_cast<unsigned char>(text[firstCharacter])) != 0) {
			firstCharacter++;
		}

		size_t lastCharacter = text.size();
		while (lastCharacter > firstCharacter &&
			std::isspace(static_cast<unsigned char>(text[lastCharacter - 1u])) != 0) {
			lastCharacter--;
		}

		std::string trimmedText = text.substr(firstCharacter, lastCharacter - firstCharacter);
		if (trimmedText.size() >= 3u &&
			static_cast<unsigned char>(trimmedText[0]) == 0xefu &&
			static_cast<unsigned char>(trimmedText[1]) == 0xbbu &&
			static_cast<unsigned char>(trimmedText[2]) == 0xbfu) {
			trimmedText.erase(0u, 3u);
		}

		return trimmedText;
	}

	void ParseAvatarMaskText(
		std::string maskText,
		std::vector<std::string>& boneNames) {
		for (char& character : maskText) {
			if (character == '|' || character == ',' || character == ';') {
				character = '\n';
			}
		}

		std::istringstream maskStream(maskText);
		std::string maskLine;
		while (std::getline(maskStream, maskLine)) {
			const size_t commentPosition = maskLine.find('#');
			if (commentPosition != std::string::npos) {
				maskLine.erase(commentPosition);
			}

			std::string boneName = TrimAvatarMaskToken(maskLine);
			if (boneName.empty() || boneName[0] == '-') {
				continue;
			}

			if (boneName[0] == '+') {
				boneName = TrimAvatarMaskToken(boneName.substr(1u));
			}

			if (!boneName.empty() &&
				std::find(boneNames.begin(), boneNames.end(), boneName) == boneNames.end()) {
				boneNames.push_back(boneName);
			}
		}
	}

	bool TryResolveAvatarMaskPath(
		const std::string& avatarMaskAssetPath,
		std::filesystem::path& resolvedPath) {
		const std::filesystem::path requestedPath(avatarMaskAssetPath);
		std::error_code fileError;
		if (std::filesystem::is_regular_file(requestedPath, fileError) && !fileError) {
			resolvedPath = requestedPath;
			return true;
		}

		std::filesystem::path searchDirectory = std::filesystem::current_path(fileError);
		if (fileError) {
			return false;
		}

		for (int32_t parentDepth = 0; parentDepth < 6; parentDepth++) {
			const std::filesystem::path candidatePath = searchDirectory / requestedPath;
			fileError.clear();

			if (std::filesystem::is_regular_file(candidatePath, fileError) && !fileError) {
				resolvedPath = candidatePath;
				return true;
			}

			const std::filesystem::path parentDirectory = searchDirectory.parent_path();
			if (parentDirectory == searchDirectory) {
				break;
			}

			searchDirectory = parentDirectory;
		}

		return false;
	}

	bool LoadAvatarMaskBoneNames(
		const std::string& avatarMaskAssetPath,
		std::vector<std::string>& boneNames) {
		boneNames.clear();
		if (avatarMaskAssetPath.empty()) {
			return false;
		}

		std::filesystem::path resolvedPath;
		if (TryResolveAvatarMaskPath(avatarMaskAssetPath, resolvedPath)) {
			std::ifstream maskFile(resolvedPath, std::ios::binary);
			if (!maskFile.is_open()) {
				return false;
			}

			std::ostringstream maskText;
			maskText << maskFile.rdbuf();
			ParseAvatarMaskText(maskText.str(), boneNames);
			return !boneNames.empty();
		}

		const bool isInlineMask =
			avatarMaskAssetPath.find_first_of("|,;") != std::string::npos ||
			(avatarMaskAssetPath.find('/') == std::string::npos &&
			 avatarMaskAssetPath.find('\\') == std::string::npos &&
			 avatarMaskAssetPath.find('.') == std::string::npos);
		if (!isInlineMask) {
			return false;
		}

		ParseAvatarMaskText(avatarMaskAssetPath, boneNames);
		return !boneNames.empty();
	}

	std::string ToLowerAvatarMaskText(const std::string& text) {
		std::string lowerText = text;
		for (char& character : lowerText) {
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}

		return lowerText;
	}

	bool IsBoneEnabledByAvatarMask(
		const std::string& boneName,
		const std::vector<std::string>& maskBoneNames) {
		const std::string lowerBoneName = ToLowerAvatarMaskText(boneName);
		for (const std::string& maskBoneName : maskBoneNames) {
			std::string lowerMaskBoneName = ToLowerAvatarMaskText(maskBoneName);
			const bool usesPrefixMatch = !lowerMaskBoneName.empty() && lowerMaskBoneName.back() == '*';
			if (usesPrefixMatch) {
				lowerMaskBoneName.pop_back();
			}

			if ((!usesPrefixMatch && lowerBoneName == lowerMaskBoneName) ||
				(usesPrefixMatch && lowerBoneName.starts_with(lowerMaskBoneName))) {
				return true;
			}
		}

		return false;
	}
}

void EditorSceneObjectManager::Initialize(ID3D12Device* device) {
	device_ = device;  // CreateObject で ConstantBuffer を作るため Device を保持する
	InitializeObjectBufferPools();
}

void EditorSceneObjectManager::Update() {
}

void EditorSceneObjectManager::Draw() {
}

int32_t EditorSceneObjectManager::CreateObject(
	EditorSceneObjectType type,
	int32_t textureIndex,
	const Transforms& initialTransform,
	const std::string& name) {
	// GPU Resource を作れないため、Device 未設定なら生成失敗
	if (device_ == nullptr) {
		return -1;
	}

	// 描画用オブジェクトの初期データ
	EditorSceneObject sceneObject{};
	sceneObject.type = type;
	sceneObject.meshType = EditorModelMeshType::Plane;
	sceneObject.usesCustomMesh = false;
	sceneObject.gameObjectId = -1;
	sceneObject.textureIndex = textureIndex;
	sceneObject.transform = initialTransform;
	sceneObject.worldMatrix = MakeAffineMatrix(
		initialTransform.scale,
		initialTransform.rotate,
		initialTransform.translate);
	sceneObject.name = name;
	sceneObject.assetPath.clear();
	sceneObject.textureAssetPath.clear();
	uint32_t objectBufferSlot = 0u;
	if (!AcquireObjectBufferSlot(objectBufferSlot)) {
		return -1;
	}
	constexpr size_t transformationStride =
		(sizeof(TransformationMatrix) + kConstantBufferAlignment - 1u) & ~(kConstantBufferAlignment - 1u);
	constexpr size_t materialStride =
		(sizeof(Material) + kConstantBufferAlignment - 1u) & ~(kConstantBufferAlignment - 1u);
	const size_t transformationOffset = static_cast<size_t>(objectBufferSlot) * transformationStride;
	const size_t materialOffset = static_cast<size_t>(objectBufferSlot) * materialStride;
	sceneObject.transformationResource = transformationPoolResource_;
	sceneObject.transformationData = reinterpret_cast<TransformationMatrix*>(
		transformationPoolData_ + transformationOffset);
	sceneObject.gameTransformationResource = gameTransformationPoolResource_;
	sceneObject.gameTransformationData = reinterpret_cast<TransformationMatrix*>(
		gameTransformationPoolData_ + transformationOffset);
	sceneObject.materialResource = materialPoolResource_;
	sceneObject.materialData = reinterpret_cast<Material*>(materialPoolData_ + materialOffset);
	sceneObject.transformationGpuAddress =
		transformationPoolResource_->GetGPUVirtualAddress() + transformationOffset;
	sceneObject.gameTransformationGpuAddress =
		gameTransformationPoolResource_->GetGPUVirtualAddress() + transformationOffset;
	sceneObject.materialGpuAddress = materialPoolResource_->GetGPUVirtualAddress() + materialOffset;
	sceneObject.objectBufferSlot = objectBufferSlot;
	sceneObject.usesSharedObjectBuffers = true;
	// Materialのpaddingも0へ揃え、同値Materialをmemcmpで安全にBatch判定できるようにする。
	std::memset(sceneObject.transformationData, 0, sizeof(TransformationMatrix));
	std::memset(sceneObject.gameTransformationData, 0, sizeof(TransformationMatrix));
	std::memset(sceneObject.materialData, 0, sizeof(Material));
	sceneObject.customTextureResource = nullptr;
	sceneObject.customTextureUploadResource = nullptr;
	sceneObject.customTextureSrvGpuHandle = {};
	sceneObject.customTextureDescriptorIndex = -1;
	sceneObject.materialTextureAssetPaths.fill("");
	sceneObject.materialTextureResources.fill(nullptr);
	sceneObject.materialTextureUploadResources.fill(nullptr);
	sceneObject.materialTextureSrvGpuHandles.fill({});
	sceneObject.materialTextureDescriptorIndices.fill(-1);
	sceneObject.customMeshVertexResource = nullptr;
	sceneObject.customMeshVertexBufferView = {};
	sceneObject.customMeshVertexCount = 0u;
	sceneObject.customMeshIndexResource = nullptr;
	sceneObject.customMeshIndexBufferView = {};
	sceneObject.customMeshIndexCount = 0u;
	sceneObject.usesSharedCustomMesh = false;
	sceneObject.currentSkinMatrixResource = nullptr;
	sceneObject.currentSkinMatrixData = nullptr;
	sceneObject.previousSkinMatrixResource = nullptr;
	sceneObject.previousSkinMatrixData = nullptr;
	sceneObject.skinMatrixCount = 0u;
	sceneObject.currentSkinClipIndex = -1;
	sceneObject.currentSkinTime = -1.0f;
	sceneObject.usesSkinning = false;
	sceneObject.customMeshLocalBoundsCenter = {0.0f, 0.0f, 0.0f};
	sceneObject.customMeshLocalBoundsSize = {1.0f, 1.0f, 1.0f};

	sceneObject.transformationData->WVP = MakeIdentity4x4();  // 初回描画前の行列を単位行列にしておく
	sceneObject.transformationData->previousWVP = MakeIdentity4x4();
	sceneObject.transformationData->temporalParams = {};
	sceneObject.transformationData->World = MakeIdentity4x4();
	sceneObject.transformationData->lightWVP = MakeIdentity4x4();
	sceneObject.transformationData->reflectionClipPlane = {};
	sceneObject.transformationData->reflectionClipParams = {};
	sceneObject.transformationData->oceanParams0 = {};
	sceneObject.transformationData->oceanParams1 = {};
	sceneObject.transformationData->oceanParams2 = {};
	sceneObject.transformationData->oceanParams3 = {};
	sceneObject.transformationData->oceanParams4 = {};
	sceneObject.transformationData->oceanParams5 = {};
	sceneObject.transformationData->oceanWaveData0 = {};
	sceneObject.transformationData->oceanWaveData1 = {};
	sceneObject.transformationData->surfaceParams0 = {};
	sceneObject.transformationData->surfaceParams1 = {};
	sceneObject.transformationData->oceanRenderParams = {};
	sceneObject.gameTransformationData->WVP = MakeIdentity4x4();
	sceneObject.gameTransformationData->previousWVP = MakeIdentity4x4();
	sceneObject.gameTransformationData->temporalParams = {};
	sceneObject.gameTransformationData->World = MakeIdentity4x4();
	sceneObject.gameTransformationData->lightWVP = MakeIdentity4x4();
	sceneObject.gameTransformationData->reflectionClipPlane = {};
	sceneObject.gameTransformationData->reflectionClipParams = {};
	sceneObject.gameTransformationData->oceanParams0 = {};
	sceneObject.gameTransformationData->oceanParams1 = {};
	sceneObject.gameTransformationData->oceanParams2 = {};
	sceneObject.gameTransformationData->oceanParams3 = {};
	sceneObject.gameTransformationData->oceanParams4 = {};
	sceneObject.gameTransformationData->oceanParams5 = {};
	sceneObject.gameTransformationData->oceanWaveData0 = {};
	sceneObject.gameTransformationData->oceanWaveData1 = {};
	sceneObject.gameTransformationData->surfaceParams0 = {};
	sceneObject.gameTransformationData->surfaceParams1 = {};
	sceneObject.gameTransformationData->oceanRenderParams = {};
	sceneObject.materialData->color = {1.0f, 1.0f, 1.0f, 1.0f};  // Mesh の初期色は白。Inspector の Renderer 色で上書きされる。
	sceneObject.materialData->enableLighting =
		type == EditorSceneObjectType::Model ? TRUE : FALSE;  // Model はライトあり、Sprite は Texture 色をそのまま出す。
	sceneObject.materialData->useTexture =
		type == EditorSceneObjectType::Sprite ? TRUE : FALSE;  // Model は白い形状として始め、Sprite だけ Texture を使う。
	sceneObject.materialData->metallic = 0.0f;
	sceneObject.materialData->roughness = 0.5f;
	sceneObject.materialData->reflectance = 0.0f;
	sceneObject.materialData->ior = 1.0f;
	sceneObject.materialData->emissionStrength = 0.0f;
	sceneObject.materialData->reflectionMode = 0.0f;
	sceneObject.materialData->reflectionProbeIntensity = 0.0f;
	sceneObject.materialData->reflectionReserved = 0.0f;
	sceneObject.materialData->materialPadding0 = 0.0f;
	sceneObject.materialData->materialPadding1 = 0.0f;
	sceneObject.materialData->reflectionProbeCenter = {0.0f, 0.0f, 0.0f};
	sceneObject.materialData->reflectionProbeBoxProjection = 0.0f;
	sceneObject.materialData->reflectionProbeExtent = {1.0f, 1.0f, 1.0f};
	sceneObject.materialData->materialPadding2 = 0.0f;
	sceneObject.materialData->uvTransform = MakeIdentity4x4();
	sceneObject.materialData->normalScale = 1.0f;
	sceneObject.materialData->ambientOcclusionStrength = 1.0f;
	sceneObject.materialData->heightScale = 0.02f;
	sceneObject.materialData->alphaCutoff = 0.5f;
	sceneObject.materialData->clearCoat = 0.0f;
	sceneObject.materialData->clearCoatRoughness = 0.1f;
	sceneObject.materialData->transmission = 0.0f;
	sceneObject.materialData->subsurface = 0.0f;
	sceneObject.materialData->anisotropy = 0.0f;
	sceneObject.materialData->anisotropyRotation = 0.0f;
	sceneObject.materialData->specularTint = 0.0f;
	sceneObject.materialData->sheen = 0.0f;
	sceneObject.materialData->emissionColor = {1.0f, 1.0f, 1.0f};
	sceneObject.materialData->sheenTint = 0.5f;
	sceneObject.materialData->useNormalMap = FALSE;
	sceneObject.materialData->useMetallicMap = FALSE;
	sceneObject.materialData->useRoughnessMap = FALSE;
	sceneObject.materialData->useAmbientOcclusionMap = FALSE;
	sceneObject.materialData->useEmissionMap = FALSE;
	sceneObject.materialData->useHeightMap = FALSE;
	sceneObject.materialData->useOpacityMap = FALSE;
	sceneObject.materialData->alphaMode = 0;
	sceneObject.materialData->doubleSided = FALSE;
	sceneObject.materialData->materialThickness = 0.1f;
	sceneObject.materialData->materialWetness = 0.0f;
	sceneObject.materialData->materialWaterlineHeight = 0.0f;
	sceneObject.materialData->uvTiling = {1.0f, 1.0f};
	sceneObject.materialData->uvOffset = {0.0f, 0.0f};
	sceneObject.materialData->oceanEnabled = 0.0f;
	sceneObject.materialData->oceanFoamStrength = 0.0f;
	sceneObject.materialData->oceanRoughness = 0.12f;
	sceneObject.materialData->oceanColorBlendScale = 1.15f;
	sceneObject.materialData->oceanDeepColor = {0.0f, 0.0f, 0.0f};
	sceneObject.materialData->oceanPerPixelDisplacementStrength = 0.0f;
	sceneObject.materialData->oceanDetailNormalStrength = 0.0f;
	sceneObject.materialData->oceanFoamThreshold = 0.58f;
	sceneObject.materialData->oceanAbsorptionDistance = 18.0f;
	sceneObject.materialData->oceanRefractionDistortion = 0.08f;
	sceneObject.materialData->oceanWaterDepth = 80.0f;
	sceneObject.materialData->oceanCrestSharpness = 0.65f;
	sceneObject.materialData->oceanPerPixelDisplacementSteps = 4.0f;
	sceneObject.materialData->oceanPerPixelDisplacementDistance = 45.0f;
	sceneObject.materialData->surfaceMode = 0;
	sceneObject.materialData->materialWaterlineWidth = 0.25f;
	sceneObject.materialData->surfaceMaterialPadding1 = 0.0f;
	sceneObject.materialData->surfaceMaterialPadding2 = 0.0f;
	sceneObject.materialData->oceanSunDiffuseInfluence = 1.0f;
	sceneObject.materialData->oceanSunSpecularInfluence = 1.0f;
	sceneObject.materialData->oceanSunGlitterInfluence = 1.0f;
	sceneObject.materialData->oceanSkyReflectionInfluence = 1.0f;
	sceneObject.materialData->oceanAmbientInfluence = 1.0f;
	sceneObject.materialData->oceanDiffuseFloor = 0.22f;
	sceneObject.materialData->oceanGlitterIntensity = 1.0f;
	sceneObject.materialData->oceanGlitterSharpness = 0.5f;
	sceneObject.materialData->oceanGlitterDensity = 1.0f;
	sceneObject.materialData->oceanGlitterThreshold = 0.0f;
	sceneObject.materialData->oceanGlitterMaxClamp = 7.5f;
	sceneObject.materialData->oceanLightingExtensionPadding0 = 0.0f;
	sceneObject.materialData->oceanMacroReflectionInfluence = 1.0f;
	sceneObject.materialData->oceanCurvatureInfluence = 1.0f;
	sceneObject.materialData->oceanTroughOcclusionStrength = 0.08f;
	sceneObject.materialData->oceanCrestHazeStrength = 0.16f;
	sceneObject.materialData->oceanCrestDetailBoost = 0.18f;
	sceneObject.materialData->oceanSlopeRefractionInfluence = 0.35f;
	sceneObject.materialData->oceanMediumWaveStrength = 1.35f;
	sceneObject.materialData->oceanWaveColorSeparation = 0.22f;
	sceneObject.materialData->oceanShapeRoughnessVariation = 0.18f;
	sceneObject.materialData->oceanDetailFilterSharpness = 1.55f;
	sceneObject.materialData->oceanGrazingShapeVisibility = 0.35f;
	sceneObject.materialData->oceanDebugView = 0.0f;
	sceneObject.cullMode = 0;
	sceneObjects_.push_back(sceneObject);

	// 追加した配列番号を呼び出し側の選択管理へ返す
	return static_cast<int32_t>(sceneObjects_.size() - 1);
}

//========================================
// SceneObject Texture設定処理
//========================================

bool EditorSceneObjectManager::SetCustomTexture(int32_t sceneObjectIndex, const std::string& textureAssetPath) {
	// SceneObject番号、Device、Pathのどれかが無効ならResourceを変更しない。
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size()) ||
		device_ == nullptr ||
		textureAssetPath.empty()) {
		return false;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	// 同じTextureが有効な状態で設定済みなら、参照数を増減させず現在の借用を維持する。
	if (sceneObject.textureAssetPath == textureAssetPath &&
		sceneObject.customTextureResource != nullptr &&
		sceneObject.customTextureSrvGpuHandle.ptr != 0u) {
		return true;
	}

	// 先に旧Textureの参照を返す。成功後まで保持すると、一時的に2枚分のDescriptorを消費する。
	ClearCustomTexture(sceneObjectIndex);
	// Upload Bufferと実体は共有Cacheが所有し、SceneObjectはSRVを借りるだけにする。
	const bool isLoaded = AcquireSharedModelTexture(
		textureAssetPath,
		sceneObject.customTextureResource,
		sceneObject.customTextureSrvGpuHandle,
		sceneObject.customTextureDescriptorIndex);

	if (isLoaded) {
		sceneObject.textureAssetPath = textureAssetPath;
	}

	return isLoaded;
}

D3D12_GPU_DESCRIPTOR_HANDLE EditorSceneObjectManager::GetOrLoadUiTexture(
	const std::string& textureAssetPath) {
	if (textureAssetPath.empty()) {
		return {};
	}

	const auto cachedTextureIterator = cachedUiTextures_.find(textureAssetPath);

	// UI Textureは同じIcon等を毎Frame要求するため、PathをKeyにSRVを再利用する。
	if (cachedTextureIterator != cachedUiTextures_.end()) {
		return cachedTextureIterator->second.srvGpuHandle;
	}

	CachedUiTexture cachedTexture{};
	const bool isLoaded = LoadTextureResource(
		textureAssetPath,
		cachedTexture.textureResource,
		cachedTexture.uploadResource,
		cachedTexture.srvGpuHandle,
		cachedTexture.descriptorIndex);

	if (!isLoaded) {
		// 存在しないパスを毎フレーム再読込しないよう、失敗結果も空Handleとして保持する。
		cachedUiTextures_.emplace(textureAssetPath, cachedTexture);
		return {};
	}

	const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = cachedTexture.srvGpuHandle;
	cachedUiTextures_.emplace(textureAssetPath, cachedTexture);
	return textureHandle;
}

bool EditorSceneObjectManager::SetMaterialTexture(
	int32_t sceneObjectIndex,
	EditorMaterialTextureSlot textureSlot,
	const std::string& textureAssetPath) {
	const int32_t textureSlotIndex = static_cast<int32_t>(textureSlot);
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size()) ||
		textureSlotIndex < 0 ||
		textureSlotIndex >= static_cast<int32_t>(EditorMaterialTextureSlot::Count) ||
		textureAssetPath.empty()) {
		return false;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	// Slot列挙値をMaterial Texture配列のIndexとして使用する。
	const size_t textureSlotArrayIndex = static_cast<size_t>(textureSlotIndex);
	if (sceneObject.materialTextureAssetPaths[textureSlotArrayIndex] == textureAssetPath &&
		sceneObject.materialTextureResources[textureSlotArrayIndex] != nullptr &&
		sceneObject.materialTextureSrvGpuHandles[textureSlotArrayIndex].ptr != 0u) {
		return true;
	}

	ClearMaterialTexture(sceneObjectIndex, textureSlot);
	const bool isLoaded = AcquireSharedModelTexture(
		textureAssetPath,
		sceneObject.materialTextureResources[textureSlotArrayIndex],
		sceneObject.materialTextureSrvGpuHandles[textureSlotArrayIndex],
		sceneObject.materialTextureDescriptorIndices[textureSlotArrayIndex]);

	if (isLoaded) {
		sceneObject.materialTextureAssetPaths[textureSlotArrayIndex] = textureAssetPath;
	}

	return isLoaded;
}

void EditorSceneObjectManager::ClearCustomTexture(int32_t sceneObjectIndex) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size())) {
		return;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];

	if (sceneObject.customTextureResource == nullptr &&
		sceneObject.customTextureUploadResource == nullptr &&
		sceneObject.customTextureSrvGpuHandle.ptr == 0u &&
		sceneObject.customTextureDescriptorIndex < 0 &&
		sceneObject.textureAssetPath.empty()) {
		return;
	}

	// 実体は共有Cacheが持つため、ここでは参照を返して借りたHandleを捨てるだけにする。
	ReleaseSharedModelTexture(sceneObject.textureAssetPath);
	sceneObject.customTextureResource = nullptr;
	sceneObject.customTextureUploadResource = nullptr;
	sceneObject.customTextureSrvGpuHandle = {};
	sceneObject.customTextureDescriptorIndex = -1;
	sceneObject.textureAssetPath.clear();
}

void EditorSceneObjectManager::ClearMaterialTexture(
	int32_t sceneObjectIndex,
	EditorMaterialTextureSlot textureSlot) {
	const int32_t textureSlotIndex = static_cast<int32_t>(textureSlot);
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size()) ||
		textureSlotIndex < 0 ||
		textureSlotIndex >= static_cast<int32_t>(EditorMaterialTextureSlot::Count)) {
		return;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	const size_t textureSlotArrayIndex = static_cast<size_t>(textureSlotIndex);

	if (sceneObject.materialTextureResources[textureSlotArrayIndex] == nullptr &&
		sceneObject.materialTextureUploadResources[textureSlotArrayIndex] == nullptr &&
		sceneObject.materialTextureSrvGpuHandles[textureSlotArrayIndex].ptr == 0u &&
		sceneObject.materialTextureDescriptorIndices[textureSlotArrayIndex] < 0 &&
		sceneObject.materialTextureAssetPaths[textureSlotArrayIndex].empty()) {
		return;
	}

	ReleaseSharedModelTexture(sceneObject.materialTextureAssetPaths[textureSlotArrayIndex]);
	sceneObject.materialTextureResources[textureSlotArrayIndex] = nullptr;
	sceneObject.materialTextureUploadResources[textureSlotArrayIndex] = nullptr;
	sceneObject.materialTextureSrvGpuHandles[textureSlotArrayIndex] = {};
	sceneObject.materialTextureDescriptorIndices[textureSlotArrayIndex] = -1;
	sceneObject.materialTextureAssetPaths[textureSlotArrayIndex].clear();
}

void EditorSceneObjectManager::ClearAllMaterialTextures(int32_t sceneObjectIndex) {
	// BaseColor/Normal/Metallic等の全Slotを同じ解放経路へ通し、参照数の戻し忘れを防ぐ。
	for (int32_t textureSlotIndex = 0;
		 textureSlotIndex < static_cast<int32_t>(EditorMaterialTextureSlot::Count);
		 textureSlotIndex++) {
		ClearMaterialTexture(sceneObjectIndex, static_cast<EditorMaterialTextureSlot>(textureSlotIndex));
	}
}

bool EditorSceneObjectManager::LoadTextureResource(
	const std::string& textureAssetPath,
	ID3D12Resource*& textureResource,
	ID3D12Resource*& uploadResource,
	D3D12_GPU_DESCRIPTOR_HANDLE& srvGpuHandle,
	int32_t& descriptorIndex) {
	using namespace EditorSharedState;

	// 参照先が無いTextureは黙って白いObjectになるだけで、原因がSceneなのか描画なのか判別できない。
	// Pathは消さずに保持したまま、Consoleへ1回だけ理由を出す(毎フレーム出すとLogが埋まる)。
	const std::filesystem::path resolvedTexturePath = ResolveEngineOrProjectFilePath(textureAssetPath);
	if (!textureAssetPath.empty() && !std::filesystem::exists(resolvedTexturePath)) {
		static std::unordered_set<std::string> reportedMissingTexturePaths;

		if (reportedMissingTexturePaths.insert(textureAssetPath).second) {
			Log("Asset: Textureが見つかりません " + textureAssetPath +
				" (参照は保持しています。Pathを直すか、Assetを戻してください)");
		}

		return false;
	}

	if (device_ == nullptr ||
		textureAssetPath.empty() ||
		g_commandAllocator == nullptr ||
		g_commandList == nullptr ||
		g_commandQueue == nullptr ||
		g_fence == nullptr ||
		g_fenceEvent == nullptr) {
		return false;
	}

	//------------------------------
	// SRV Descriptor確保
	//------------------------------

	// Texture本体だけ作れてもShaderから参照するDescriptorが無ければ描画できない。
	descriptorIndex = AcquireCustomTextureDescriptorIndex();
	if (descriptorIndex < 0) {
		return false;
	}

	using namespace EditorSharedState;

	// AssetId経由でTexture Import Settings(sRGB/NormalMap/Mipmap/MaxSize)を解決する。
	// 未登録Assetや設定Storeに実体が無い場合は、変更前と同じ既定値(sRGB/Mipmap有効・制限無し)で読む。
	bool textureForceSrgb = true;
	bool textureGenerateMipmaps = true;
	int32_t textureMaxSize = 0;
	const AssetRecord* textureRecord = AssetRegistry::Get().FindByPath(textureAssetPath);
	if (textureRecord != nullptr) {
		const AssetImportMetadata* textureImportMetadata =
			AssetImportSettingsStore::Get().Find(textureRecord->id);
		if (textureImportMetadata != nullptr) {
			textureForceSrgb = textureImportMetadata->texture.isNormalMap
				? false
				: textureImportMetadata->texture.srgb;
			textureGenerateMipmaps = textureImportMetadata->texture.generateMipmaps;
			textureMaxSize = textureImportMetadata->texture.maxSize;
		}
	}

	//------------------------------
	// CPU画像読込・Mip生成
	//------------------------------

	// ScratchImageはCPU Memory上の画像列。ここではまだGPUがSampleできる状態ではない。
	DirectX::ScratchImage mipImages = LoadTexture(
		ConvertString(textureAssetPath),
		textureForceSrgb,
		textureGenerateMipmaps,
		textureMaxSize);
	const DirectX::TexMetadata textureMetadata = mipImages.GetMetadata();
	textureResource = CreateTextureResource(device_, textureMetadata);
	if (textureResource == nullptr) {
		ReleaseCustomTextureDescriptorIndex(descriptorIndex);
		descriptorIndex = -1;
		return false;
	}

	//------------------------------
	// GPU Texture転送
	//------------------------------

	// Copy Commandを記録するAllocator/Listは、前回実行完了後でなければResetできない。
	HRESULT commandResult = g_commandAllocator->Reset();
	if (FAILED(commandResult)) {
		ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
		return false;
	}

	commandResult = g_commandList->Reset(g_commandAllocator.Get(), nullptr);
	if (FAILED(commandResult)) {
		ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
		return false;
	}

	// Upload Heapを中継し、Default HeapのTextureへ全MipのCopy Commandを積む。
	uploadResource = UploadTextureData(device_, g_commandList.Get(), textureResource, mipImages);
	if (uploadResource == nullptr) {
		ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
		return false;
	}

	commandResult = g_commandList->Close();
	if (FAILED(commandResult)) {
		ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
		return false;
	}

	// CommandをQueueへ提出しただけではCopy完了ではない。直後にSRVとして公開する前にFenceを待つ。
	ID3D12CommandList* commandLists[] = {g_commandList.Get()};
	g_commandQueue->ExecuteCommandLists(1, commandLists);
	g_fenceValue++;
	commandResult = g_commandQueue->Signal(g_fence.Get(), g_fenceValue);
	if (FAILED(commandResult)) {
		ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
		return false;
	}

	// この同期はLoadを単純にする代わりにCPUを停止させる。大量Assetでは非同期Uploadが改善候補になる。
	if (g_fence->GetCompletedValue() < g_fenceValue) {
		commandResult = g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
		if (FAILED(commandResult)) {
			ReleaseTextureResource(textureResource, uploadResource, srvGpuHandle, descriptorIndex);
			return false;
		}

		WaitForSingleObject(g_fenceEvent, INFINITE);
	}

	//------------------------------
	// Shader Resource View生成
	//------------------------------

	// Resource本体とSRVは別物。SRVがFormatとMip範囲をShaderへ公開する見方を定義する。
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = textureMetadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = static_cast<UINT>(textureMetadata.mipLevels);

	const D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle =
		GetCPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, static_cast<UINT>(descriptorIndex));
	srvGpuHandle =
		GetGPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, static_cast<UINT>(descriptorIndex));
	device_->CreateShaderResourceView(textureResource, &srvDesc, srvCpuHandle);
	return true;
}

void EditorSceneObjectManager::ReleaseTextureResource(
	ID3D12Resource*& textureResource,
	ID3D12Resource*& uploadResource,
	D3D12_GPU_DESCRIPTOR_HANDLE& srvGpuHandle,
	int32_t& descriptorIndex) {
	// Upload ResourceはGPU転送完了後に不要だが、所有経路を統一するためTextureと同時に解放する。
	if (uploadResource != nullptr) {
		uploadResource->Release();
		uploadResource = nullptr;
	}

	if (textureResource != nullptr) {
		textureResource->Release();
		textureResource = nullptr;
	}

	// Descriptor番号をFree Listへ返し、別Textureが同じHeap Slotを再利用できるようにする。
	if (descriptorIndex >= 0) {
		ReleaseCustomTextureDescriptorIndex(descriptorIndex);
	}

	srvGpuHandle = {};
	descriptorIndex = -1;
}

bool EditorSceneObjectManager::SetCustomModelMesh(
	int32_t sceneObjectIndex,
	const std::string& assetPath,
	const ModelData& modelData) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size()) ||
		modelData.vertices.empty()) {
		return false;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	ClearCustomModelMesh(sceneObjectIndex);

	SharedModelMesh* sharedMesh = nullptr;
	if (!assetPath.empty()) {
		auto sharedMeshIterator = sharedModelMeshes_.find(assetPath);
		if (sharedMeshIterator != sharedModelMeshes_.end()) {
			sharedMesh = &sharedMeshIterator->second;
		}
		else {
			SharedModelMesh newSharedMesh{};
			const size_t vertexBufferSize = sizeof(VertexData) * modelData.vertices.size();
			size_t vertexByteOffset = 0u;
			if (!AllocateMeshAtlasRange(
				vertexAtlasPages_,
				modelData.vertices.data(),
				vertexBufferSize,
				alignof(VertexData),
				newSharedMesh.vertexResource,
				vertexByteOffset)) {
				return false;
			}
			newSharedMesh.vertexBufferView.BufferLocation =
				newSharedMesh.vertexResource->GetGPUVirtualAddress() + vertexByteOffset;
			newSharedMesh.vertexBufferView.SizeInBytes = static_cast<UINT>(vertexBufferSize);
			newSharedMesh.vertexBufferView.StrideInBytes = sizeof(VertexData);
			newSharedMesh.vertexCount = static_cast<uint32_t>(modelData.vertices.size());

			if (!modelData.indices.empty()) {
				const size_t indexBufferSize = sizeof(uint32_t) * modelData.indices.size();
				size_t indexByteOffset = 0u;
				if (!AllocateMeshAtlasRange(
					indexAtlasPages_,
					modelData.indices.data(),
					indexBufferSize,
					alignof(uint32_t),
					newSharedMesh.indexResource,
					indexByteOffset)) {
					return false;
				}
				newSharedMesh.indexBufferView.BufferLocation =
					newSharedMesh.indexResource->GetGPUVirtualAddress() + indexByteOffset;
				newSharedMesh.indexBufferView.SizeInBytes = static_cast<UINT>(indexBufferSize);
				newSharedMesh.indexBufferView.Format = DXGI_FORMAT_R32_UINT;
				newSharedMesh.indexCount = static_cast<uint32_t>(modelData.indices.size());
			}

			auto insertedMesh = sharedModelMeshes_.emplace(assetPath, newSharedMesh);
			sharedMesh = &insertedMesh.first->second;
		}
	}

	if (sharedMesh != nullptr) {
		sharedMesh->referenceCount++;
		sceneObject.customMeshVertexResource = sharedMesh->vertexResource;
		sceneObject.customMeshVertexBufferView = sharedMesh->vertexBufferView;
		sceneObject.customMeshVertexCount = sharedMesh->vertexCount;
		sceneObject.customMeshIndexResource = sharedMesh->indexResource;
		sceneObject.customMeshIndexBufferView = sharedMesh->indexBufferView;
		sceneObject.customMeshIndexCount = sharedMesh->indexCount;
		sceneObject.usesSharedCustomMesh = true;
		sceneObject.assetPath = assetPath;
	}
	else {
		return false;
	}

	const std::vector<Matrix4x4>* initialSkinMatrices =
		!modelData.defaultSkinMatrices.empty() ? &modelData.defaultSkinMatrices : nullptr;
	if (initialSkinMatrices == nullptr) {
		for (const ModelAnimationClipData& animationClip : modelData.animationClips) {
			if (!animationClip.skinPoseFrames.empty() &&
				!animationClip.skinPoseFrames.front().boneMatrices.empty()) {
				initialSkinMatrices = &animationClip.skinPoseFrames.front().boneMatrices;
				break;
			}
		}
	}

	if (initialSkinMatrices != nullptr && !initialSkinMatrices->empty()) {
		const size_t skinBufferSize = sizeof(Matrix4x4) * initialSkinMatrices->size();
		sceneObject.currentSkinMatrixResource = CreateVertexResource(skinBufferSize);
		sceneObject.previousSkinMatrixResource = CreateVertexResource(skinBufferSize);

		if (sceneObject.currentSkinMatrixResource == nullptr ||
			sceneObject.previousSkinMatrixResource == nullptr) {
			ClearCustomModelMesh(sceneObjectIndex);
			return false;
		}

		const HRESULT currentSkinMapResult = sceneObject.currentSkinMatrixResource->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(&sceneObject.currentSkinMatrixData));
		const HRESULT previousSkinMapResult = sceneObject.previousSkinMatrixResource->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(&sceneObject.previousSkinMatrixData));

		if (FAILED(currentSkinMapResult) || FAILED(previousSkinMapResult) ||
			sceneObject.currentSkinMatrixData == nullptr ||
			sceneObject.previousSkinMatrixData == nullptr) {
			ClearCustomModelMesh(sceneObjectIndex);
			return false;
		}

		std::memcpy(
			sceneObject.currentSkinMatrixData,
			initialSkinMatrices->data(),
			skinBufferSize);
		std::memcpy(
			sceneObject.previousSkinMatrixData,
			initialSkinMatrices->data(),
			skinBufferSize);
		sceneObject.skinMatrixCount = static_cast<uint32_t>(initialSkinMatrices->size());
		sceneObject.currentSkinClipIndex = -1;
		sceneObject.currentSkinTime = -1.0f;
		sceneObject.currentSkinMaskPath.clear();
		sceneObject.currentSkinMaskBoneNames.clear();
		sceneObject.usesSkinMask = false;
		sceneObject.usesSkinning = true;
	}

	sceneObject.customMeshLocalBoundsCenter = modelData.localBoundsCenter;
	sceneObject.customMeshLocalBoundsSize = modelData.localBoundsSize;
	sceneObject.assetPath = assetPath;
	sceneObject.usesCustomMesh = true;
	return true;
}

bool EditorSceneObjectManager::UpdateSkinnedPose(
	int32_t sceneObjectIndex,
	const ModelData& modelData,
	int32_t clipIndex,
	float playbackTime,
	const std::string& avatarMaskAssetPath) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size())) {
		return false;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	if (!sceneObject.usesSkinning ||
		sceneObject.currentSkinMatrixData == nullptr ||
		sceneObject.previousSkinMatrixData == nullptr ||
		sceneObject.skinMatrixCount == 0u) {
		return false;
	}

	const bool avatarMaskChanged = sceneObject.currentSkinMaskPath != avatarMaskAssetPath;
	if (avatarMaskChanged) {
		sceneObject.currentSkinMaskPath = avatarMaskAssetPath;
		sceneObject.usesSkinMask = LoadAvatarMaskBoneNames(
			avatarMaskAssetPath,
			sceneObject.currentSkinMaskBoneNames);
	}

	if (!avatarMaskChanged &&
		sceneObject.currentSkinClipIndex == clipIndex &&
		std::abs(sceneObject.currentSkinTime - playbackTime) <= 0.000001f) {
		return true;
	}

	std::vector<Matrix4x4> sampledMatrices;
	if (!SampleSkinPose(modelData, clipIndex, playbackTime, sampledMatrices) ||
		sampledMatrices.size() != static_cast<size_t>(sceneObject.skinMatrixCount)) {
		return false;
	}

	if (sceneObject.usesSkinMask &&
		modelData.defaultSkinMatrices.size() == sampledMatrices.size() &&
		modelData.skinBoneNames.size() == sampledMatrices.size()) {
		for (size_t boneIndex = 0u; boneIndex < sampledMatrices.size(); boneIndex++) {
			if (!IsBoneEnabledByAvatarMask(
				modelData.skinBoneNames[boneIndex],
				sceneObject.currentSkinMaskBoneNames)) {
				sampledMatrices[boneIndex] = modelData.defaultSkinMatrices[boneIndex];
			}
		}
	}

	const size_t skinBufferSize =
		sizeof(Matrix4x4) * static_cast<size_t>(sceneObject.skinMatrixCount);
	std::memcpy(
		sceneObject.previousSkinMatrixData,
		sceneObject.currentSkinMatrixData,
		skinBufferSize);
	std::memcpy(
		sceneObject.currentSkinMatrixData,
		sampledMatrices.data(),
		skinBufferSize);
	sceneObject.currentSkinClipIndex = clipIndex;
	sceneObject.currentSkinTime = playbackTime;
	return true;
}

void EditorSceneObjectManager::ClearSkinningResources(int32_t sceneObjectIndex) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size())) {
		return;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	if (sceneObject.currentSkinMatrixResource != nullptr) {
		sceneObject.currentSkinMatrixResource->Release();
		sceneObject.currentSkinMatrixResource = nullptr;
	}

	if (sceneObject.previousSkinMatrixResource != nullptr) {
		sceneObject.previousSkinMatrixResource->Release();
		sceneObject.previousSkinMatrixResource = nullptr;
	}

	sceneObject.currentSkinMatrixData = nullptr;
	sceneObject.previousSkinMatrixData = nullptr;
	sceneObject.skinMatrixCount = 0u;
	sceneObject.currentSkinClipIndex = -1;
	sceneObject.currentSkinTime = -1.0f;
	sceneObject.currentSkinMaskPath.clear();
	sceneObject.currentSkinMaskBoneNames.clear();
	sceneObject.usesSkinMask = false;
	sceneObject.usesSkinning = false;
}

void EditorSceneObjectManager::ClearCustomModelMesh(int32_t sceneObjectIndex) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size())) {
		return;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];
	ClearSkinningResources(sceneObjectIndex);
	if (sceneObject.usesSharedCustomMesh) {
		auto sharedMeshIterator = sharedModelMeshes_.find(sceneObject.assetPath);
		if (sharedMeshIterator != sharedModelMeshes_.end()) {
			SharedModelMesh& sharedMesh = sharedMeshIterator->second;
			sharedMesh.referenceCount = (std::max)(sharedMesh.referenceCount - 1, 0);
			if (sharedMesh.referenceCount == 0) {
				// Atlasページ内の領域は他Meshと共有するため個別Releaseしない。
				sharedModelMeshes_.erase(sharedMeshIterator);
			}
		}
	}
	else {
		if (sceneObject.customMeshVertexResource != nullptr) {
			sceneObject.customMeshVertexResource->Release();
		}
		if (sceneObject.customMeshIndexResource != nullptr) {
			sceneObject.customMeshIndexResource->Release();
		}
	}
	sceneObject.customMeshVertexResource = nullptr;
	sceneObject.customMeshVertexBufferView = {};
	sceneObject.customMeshVertexCount = 0u;
	sceneObject.customMeshIndexResource = nullptr;
	sceneObject.customMeshIndexBufferView = {};
	sceneObject.customMeshIndexCount = 0u;
	sceneObject.usesSharedCustomMesh = false;
	sceneObject.customMeshLocalBoundsCenter = {0.0f, 0.0f, 0.0f};
	sceneObject.customMeshLocalBoundsSize = {1.0f, 1.0f, 1.0f};
	sceneObject.usesCustomMesh = false;
	sceneObject.assetPath.clear();
}

void EditorSceneObjectManager::InvalidateAssetResources(const std::string& assetPath) {
	if (assetPath.empty()) {
		return;
	}

	auto normalizePath = [](const std::string& path) {
		std::string normalizedPath = std::filesystem::path(path).lexically_normal().generic_string();
		std::transform(normalizedPath.begin(), normalizedPath.end(), normalizedPath.begin(), [](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
		return normalizedPath;
	};
	const std::string normalizedAssetPath = normalizePath(assetPath);

	for (int32_t sceneObjectIndex = 0;
		sceneObjectIndex < static_cast<int32_t>(sceneObjects_.size());
		sceneObjectIndex++) {
		EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];

		if (normalizePath(sceneObject.assetPath) == normalizedAssetPath) {
			ClearCustomModelMesh(sceneObjectIndex);
		}
		if (normalizePath(sceneObject.textureAssetPath) == normalizedAssetPath) {
			ClearCustomTexture(sceneObjectIndex);
		}

		for (int32_t textureSlotIndex = 0;
			textureSlotIndex < static_cast<int32_t>(EditorMaterialTextureSlot::Count);
			textureSlotIndex++) {
			const size_t textureSlotArrayIndex = static_cast<size_t>(textureSlotIndex);
			if (normalizePath(sceneObject.materialTextureAssetPaths[textureSlotArrayIndex]) == normalizedAssetPath) {
				ClearMaterialTexture(
					sceneObjectIndex,
					static_cast<EditorMaterialTextureSlot>(textureSlotIndex));
			}
		}
	}

	// 上でSceneObject側の参照を外しているため、ここへ残るのは参照0または失敗記録だけになる。
	// まだ参照が残っているEntryを消すと借用中のSRVが宙に浮くため、その場合は触らない。
	for (auto iterator = sharedModelTextures_.begin(); iterator != sharedModelTextures_.end();) {
		if (normalizePath(iterator->first) != normalizedAssetPath ||
			iterator->second.referenceCount > 0) {
			++iterator;
			continue;
		}

		SharedModelTexture& sharedTexture = iterator->second;
		ReleaseTextureResource(
			sharedTexture.textureResource,
			sharedTexture.uploadResource,
			sharedTexture.srvGpuHandle,
			sharedTexture.descriptorIndex);
		iterator = sharedModelTextures_.erase(iterator);
	}

	for (auto iterator = cachedUiTextures_.begin(); iterator != cachedUiTextures_.end();) {
		if (normalizePath(iterator->first) != normalizedAssetPath) {
			++iterator;
			continue;
		}

		CachedUiTexture& cachedTexture = iterator->second;
		ReleaseTextureResource(
			cachedTexture.textureResource,
			cachedTexture.uploadResource,
			cachedTexture.srvGpuHandle,
			cachedTexture.descriptorIndex);
		iterator = cachedUiTextures_.erase(iterator);
	}
}

//========================================
// SceneObject描画リソース解放処理
//========================================

void EditorSceneObjectManager::ReleaseObject(int32_t sceneObjectIndex) {
	if (sceneObjectIndex < 0 ||
		sceneObjectIndex >= static_cast<int32_t>(sceneObjects_.size())) {
		return;
	}

	EditorSceneObject& sceneObject = sceneObjects_[static_cast<size_t>(sceneObjectIndex)];  // 指定番号の描画用 GPU Resource を解放する
	// 共有TextureとMeshは各専用関数で参照数を戻し、Object固有Bufferより先に関連付けを外す。
	ClearCustomTexture(sceneObjectIndex);
	ClearAllMaterialTextures(sceneObjectIndex);
	ClearCustomModelMesh(sceneObjectIndex);
	// Pool利用ObjectはResource本体を共有しているため、ComPtr/ResourceをReleaseせずSlotだけ返す。
	if (sceneObject.usesSharedObjectBuffers) {
		ReleaseObjectBufferSlot(sceneObject.objectBufferSlot);
	}
	else {
		if (sceneObject.transformationResource != nullptr) {
			sceneObject.transformationResource->Release();
		}
		if (sceneObject.gameTransformationResource != nullptr) {
			sceneObject.gameTransformationResource->Release();
		}
		if (sceneObject.materialResource != nullptr) {
			sceneObject.materialResource->Release();
		}
	}
	// CPU側のMapped PointerとGPU Addressも無効化し、解放後の書込を防ぐ。
	sceneObject.transformationResource = nullptr;
	sceneObject.transformationData = nullptr;
	sceneObject.gameTransformationResource = nullptr;
	sceneObject.gameTransformationData = nullptr;
	sceneObject.materialResource = nullptr;
	sceneObject.materialData = nullptr;
	sceneObject.transformationGpuAddress = 0u;
	sceneObject.gameTransformationGpuAddress = 0u;
	sceneObject.materialGpuAddress = 0u;
	sceneObject.usesSharedObjectBuffers = false;
}

void EditorSceneObjectManager::ReleaseAll() {
	// 各要素の GPU Resource を先に解放する
	for (int32_t sceneObjectIndex = 0;
	     sceneObjectIndex < static_cast<int32_t>(sceneObjects_.size());
	     sceneObjectIndex++) {
		ReleaseObject(sceneObjectIndex);
	}

	sceneObjects_.clear();  // Resource 解放後に配列自体を空にする

	// UI CacheはSceneObjectの参照数管理外なので、Scene配列解放後にCache所有Resourceを直接解放する。
	for (auto& cachedTexturePair : cachedUiTextures_) {
		CachedUiTexture& cachedTexture = cachedTexturePair.second;
		ReleaseTextureResource(
			cachedTexture.textureResource,
			cachedTexture.uploadResource,
			cachedTexture.srvGpuHandle,
			cachedTexture.descriptorIndex);
	}

	cachedUiTextures_.clear();

	for (auto& sharedTexturePair : sharedModelTextures_) {
		SharedModelTexture& sharedTexture = sharedTexturePair.second;
		ReleaseTextureResource(
			sharedTexture.textureResource,
			sharedTexture.uploadResource,
			sharedTexture.srvGpuHandle,
			sharedTexture.descriptorIndex);
	}

	sharedModelTextures_.clear();

	sharedModelMeshes_.clear();
	ReleaseMeshAtlasPages(indexAtlasPages_);
	ReleaseMeshAtlasPages(vertexAtlasPages_);
	ReleaseObjectBufferPools();
	hasFreedTextureDescriptor_ = false;
}

std::vector<EditorSceneObject>& EditorSceneObjectManager::GetSceneObjects() {
	return sceneObjects_;
}

const std::vector<EditorSceneObject>& EditorSceneObjectManager::GetSceneObjects() const {
	return sceneObjects_;
}

int32_t EditorSceneObjectManager::AcquireCustomTextureDescriptorIndex() {
	// 解放済みSlotを優先して再利用し、Descriptor HeapのIndexが増え続けるのを防ぐ。
	if (!freeCustomTextureDescriptorIndices_.empty()) {
		int32_t descriptorIndex = freeCustomTextureDescriptorIndices_.back();
		freeCustomTextureDescriptorIndices_.pop_back();
		return descriptorIndex;
	}

	// Heap Capacityを超えるIndexはGPU Handle計算がHeap外を指すため、-1で失敗を通知する。
	if (nextCustomTextureDescriptorIndex_ >=
		static_cast<int32_t>(EditorSharedState::kRuntimeSrvDescriptorHeapCapacity)) {
		return -1;
	}

	int32_t descriptorIndex = nextCustomTextureDescriptorIndex_;
	nextCustomTextureDescriptorIndex_++;
	return descriptorIndex;
}

void EditorSceneObjectManager::ReleaseCustomTextureDescriptorIndex(int32_t descriptorIndex) {
	// Engine固定Textureが使用する予約領域はRuntime Assetへ再配布しない。
	if (descriptorIndex < static_cast<int32_t>(EditorSharedState::kRuntimeReservedSrvDescriptorCount)) {
		return;
	}

	// 同じIndexを2回Free Listへ入れると、2 Textureへ同一Descriptorを配ってしまう。
	if (std::find(
		    freeCustomTextureDescriptorIndices_.begin(),
		    freeCustomTextureDescriptorIndices_.end(),
		    descriptorIndex) != freeCustomTextureDescriptorIndices_.end()) {
		return;
	}

	freeCustomTextureDescriptorIndices_.push_back(descriptorIndex);
	// SRVが空いたので、上限超過で失敗した画像を次の要求で1回だけ読み直せるようにする。
	hasFreedTextureDescriptor_ = true;
}

//========================================
// Texture共有Cache処理
//========================================

bool EditorSceneObjectManager::AcquireSharedModelTexture(
	const std::string& textureAssetPath,
	ID3D12Resource*& textureResource,
	D3D12_GPU_DESCRIPTOR_HANDLE& srvGpuHandle,
	int32_t& descriptorIndex) {
	textureResource = nullptr;
	srvGpuHandle = {};
	descriptorIndex = -1;
	if (textureAssetPath.empty()) {
		return false;
	}

	// Pathが同じTextureはResource/SRVを共有し、Objectごとの重複UploadとVRAM消費を防ぐ。
	const auto sharedTextureIterator = sharedModelTextures_.find(textureAssetPath);
	if (sharedTextureIterator != sharedModelTextures_.end()) {
		SharedModelTexture& sharedTexture = sharedTextureIterator->second;

		if (!sharedTexture.loadFailed && sharedTexture.textureResource != nullptr) {
			sharedTexture.referenceCount++;
			textureResource = sharedTexture.textureResource;
			srvGpuHandle = sharedTexture.srvGpuHandle;
			descriptorIndex = sharedTexture.descriptorIndex;
			return true;
		}

		// 失敗はCacheへ残す。毎フレーム読み直すとLoadTextureResourceのGPU全同期が積み上がる。
		// SRVが空いた直後だけ、記録を捨てて1回だけ読み直す。
		if (!hasFreedTextureDescriptor_) {
			return false;
		}
		hasFreedTextureDescriptor_ = false;
		sharedModelTextures_.erase(sharedTextureIterator);
	}

	SharedModelTexture sharedTexture{};
	const bool isLoaded = LoadTextureResource(
		textureAssetPath,
		sharedTexture.textureResource,
		sharedTexture.uploadResource,
		sharedTexture.srvGpuHandle,
		sharedTexture.descriptorIndex);
	if (!isLoaded) {
		sharedTexture.loadFailed = true;
		sharedModelTextures_.insert_or_assign(textureAssetPath, sharedTexture);
		return false;
	}

	// 新規Loadに成功した時点で、要求元SceneObjectの1参照を登録する。
	sharedTexture.referenceCount = 1;
	textureResource = sharedTexture.textureResource;
	srvGpuHandle = sharedTexture.srvGpuHandle;
	descriptorIndex = sharedTexture.descriptorIndex;
	sharedModelTextures_.insert_or_assign(textureAssetPath, sharedTexture);
	return true;
}

void EditorSceneObjectManager::ReleaseSharedModelTexture(const std::string& textureAssetPath) {
	if (textureAssetPath.empty()) {
		return;
	}

	const auto sharedTextureIterator = sharedModelTextures_.find(textureAssetPath);
	if (sharedTextureIterator == sharedModelTextures_.end()) {
		return;
	}

	SharedModelTexture& sharedTexture = sharedTextureIterator->second;
	if (sharedTexture.loadFailed) {
		// 失敗記録は参照を持たない。次の読み直し判断まで残す。
		return;
	}

	// 借用Objectが1つ減ったことを記録し、最後の参照だけがResourceを実際に解放する。
	sharedTexture.referenceCount--;
	if (sharedTexture.referenceCount > 0) {
		return;
	}

	ReleaseTextureResource(
		sharedTexture.textureResource,
		sharedTexture.uploadResource,
		sharedTexture.srvGpuHandle,
		sharedTexture.descriptorIndex);
	sharedModelTextures_.erase(textureAssetPath);
}

ID3D12Resource* EditorSceneObjectManager::CreateVertexResource(size_t sizeInBytes) const {
	// Device がない場合は GPU Resource を作れない
	if (device_ == nullptr || sizeInBytes == 0u) {
		return nullptr;
	}

	// 頻繁にCPUから頂点/定数を書き換える用途なので、Map可能なUpload Heapを選ぶ。
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource* resource = nullptr;
	const HRESULT createResult = device_->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource));
	if (FAILED(createResult)) {
		return nullptr;
	}

	return resource;
}

bool EditorSceneObjectManager::AllocateMeshAtlasRange(
	std::vector<MeshAtlasPage>& atlasPages,
	const void* sourceData,
	size_t dataSize,
	size_t alignment,
	ID3D12Resource*& resource,
	size_t& byteOffset) {
	resource = nullptr;
	byteOffset = 0u;
	if (sourceData == nullptr || dataSize == 0u || alignment == 0u) {
		return false;
	}

	//------------------------------
	// 既存Atlas Page割当
	//------------------------------

	// Alignment境界までOffsetを進め、Vertex/Index Viewが要求するByte境界を守る。
	auto tryAllocate = [&](MeshAtlasPage& page) {
		const size_t alignedOffset = (page.used + alignment - 1u) & ~(alignment - 1u);
		if (page.mappedData == nullptr || alignedOffset + dataSize > page.capacity) {
			return false;
		}
		std::memcpy(page.mappedData + alignedOffset, sourceData, dataSize);
		page.used = alignedOffset + dataSize;
		resource = page.resource;
		byteOffset = alignedOffset;
		return true;
	};

	for (MeshAtlasPage& page : atlasPages) {
		if (tryAllocate(page)) {
			return true;
		}
	}

	//------------------------------
	// Atlas Page追加
	//------------------------------

	// 既存Pageに空きが無い場合だけ16MiB単位で増やす。巨大Meshは1件が入るSizeまで拡張する。
	constexpr size_t kDefaultAtlasPageSize = 16u * 1024u * 1024u;
	MeshAtlasPage newPage{};
	newPage.capacity = (std::max)(kDefaultAtlasPageSize, dataSize + alignment);
	newPage.resource = CreateVertexResource(newPage.capacity);
	if (newPage.resource == nullptr ||
		FAILED(newPage.resource->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(&newPage.mappedData))) ||
		newPage.mappedData == nullptr) {
		if (newPage.resource != nullptr) {
			newPage.resource->Release();
		}
		return false;
	}

	atlasPages.push_back(newPage);
	return tryAllocate(atlasPages.back());
}

void EditorSceneObjectManager::ReleaseMeshAtlasPages(
	std::vector<MeshAtlasPage>& atlasPages) {
	for (MeshAtlasPage& page : atlasPages) {
		if (page.resource != nullptr) {
			page.resource->Unmap(0, nullptr);
			page.resource->Release();
		}
		page = {};
	}
	atlasPages.clear();
}

bool EditorSceneObjectManager::InitializeObjectBufferPools() {
	if (transformationPoolResource_ != nullptr &&
		gameTransformationPoolResource_ != nullptr &&
		materialPoolResource_ != nullptr) {
		return true;
	}
	ReleaseObjectBufferPools();
	if (device_ == nullptr) {
		return false;
	}

	// Constant Buffer Viewの開始Addressは256byte境界が必要なので、構造体Sizeを切り上げる。
	constexpr size_t transformationStride =
		(sizeof(TransformationMatrix) + kConstantBufferAlignment - 1u) & ~(kConstantBufferAlignment - 1u);
	constexpr size_t materialStride =
		(sizeof(Material) + kConstantBufferAlignment - 1u) & ~(kConstantBufferAlignment - 1u);
	transformationPoolResource_ = CreateVertexResource(
		transformationStride * kObjectBufferCapacity);
	gameTransformationPoolResource_ = CreateVertexResource(
		transformationStride * kObjectBufferCapacity);
	materialPoolResource_ = CreateVertexResource(materialStride * kObjectBufferCapacity);
	if (transformationPoolResource_ == nullptr || gameTransformationPoolResource_ == nullptr ||
		materialPoolResource_ == nullptr) {
		ReleaseObjectBufferPools();
		return false;
	}

	const HRESULT sceneMapResult = transformationPoolResource_->Map(
		0, nullptr, reinterpret_cast<void**>(&transformationPoolData_));
	const HRESULT gameMapResult = gameTransformationPoolResource_->Map(
		0, nullptr, reinterpret_cast<void**>(&gameTransformationPoolData_));
	const HRESULT materialMapResult = materialPoolResource_->Map(
		0, nullptr, reinterpret_cast<void**>(&materialPoolData_));
	if (FAILED(sceneMapResult) || FAILED(gameMapResult) || FAILED(materialMapResult) ||
		transformationPoolData_ == nullptr || gameTransformationPoolData_ == nullptr ||
		materialPoolData_ == nullptr) {
		ReleaseObjectBufferPools();
		return false;
	}
	return true;
}

bool EditorSceneObjectManager::AcquireObjectBufferSlot(uint32_t& slot) {
	if (!InitializeObjectBufferPools()) {
		return false;
	}
	// 破棄ObjectのSlotを先に再利用し、Pool上の未使用領域を増やさない。
	if (!freeObjectBufferSlots_.empty()) {
		slot = freeObjectBufferSlots_.back();
		freeObjectBufferSlots_.pop_back();
		return true;
	}
	if (nextObjectBufferSlot_ >= kObjectBufferCapacity) {
		return false;
	}
	slot = nextObjectBufferSlot_++;
	return true;
}

void EditorSceneObjectManager::ReleaseObjectBufferSlot(uint32_t slot) {
	if (slot < kObjectBufferCapacity) {
		freeObjectBufferSlots_.push_back(slot);
	}
}

void EditorSceneObjectManager::ReleaseObjectBufferPools() {
	auto releasePool = [](ID3D12Resource*& resource, uint8_t*& mappedData) {
		if (resource != nullptr) {
			if (mappedData != nullptr) {
				resource->Unmap(0, nullptr);
			}
			resource->Release();
		}
		resource = nullptr;
		mappedData = nullptr;
	};
	releasePool(transformationPoolResource_, transformationPoolData_);
	releasePool(gameTransformationPoolResource_, gameTransformationPoolData_);
	releasePool(materialPoolResource_, materialPoolData_);
	freeObjectBufferSlots_.clear();
	nextObjectBufferSlot_ = 0u;
}

ID3D12Resource* EditorSceneObjectManager::CreateTransformationResource() const {
	// Device がない場合は GPU Resource を作れない
	if (device_ == nullptr) {
		return nullptr;
	}

	// CPU から書き込むため Upload Heap に作る
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// TransformationMatrix 1 個分だけ入る Buffer Resource
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeof(TransformationMatrix);
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource* resource = nullptr;
	// ConstantBuffer 用の Upload Buffer を作成する
	HRESULT createResult = device_->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource));
	if (FAILED(createResult)) {
		return nullptr;
	}

	return resource;
}

ID3D12Resource* EditorSceneObjectManager::CreateMaterialResource() const {
	// Device がない場合は GPU Resource を作れない
	if (device_ == nullptr) {
		return nullptr;
	}

	// CPU から毎フレーム色や Texture 使用設定を書き換えるため Upload Heap に作る
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// Material 1 個分だけ入る Buffer Resource
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeof(Material);
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ID3D12Resource* resource = nullptr;
	// PixelShader の b0 へ渡す Material 用 Upload Buffer を作成する
	HRESULT createResult = device_->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&resource));
	if (FAILED(createResult)) {
		return nullptr;
	}

	return resource;
}
