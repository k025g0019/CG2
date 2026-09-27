#include "EditorAssetUtility.h"

#include "Source/Engine/Asset/AssetImportSettings.h"
#include "Source/Engine/Asset/AssetRegistry.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma warning(push, 0)
#include <Windows.h>
#include <fbxsdk.h>
#include <meshoptimizer.h>
#pragma warning(pop)

#pragma warning(disable : 4623 4626 5027 5045 5245)

namespace {
	constexpr unsigned char kUtf8Bom[] = {0xEFu, 0xBBu, 0xBFu};  // テキストアセットを UTF-8 BOM 付きで保存する。

	std::filesystem::path ResolveInstalledEngineAssetPath(const std::filesystem::path& requestedPath) {
		if (requestedPath.empty() || requestedPath.is_absolute()) return requestedPath;
		std::error_code fileError;
		if (std::filesystem::exists(requestedPath, fileError) && !fileError) return requestedPath;
		std::wstring executablePath(32768U, L'\0');
		const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
		if (length == 0U || length >= executablePath.size()) return requestedPath;
		executablePath.resize(length);
		const std::filesystem::path installedPath =
			std::filesystem::path(executablePath).parent_path() / requestedPath;
		fileError.clear();
		return std::filesystem::exists(installedPath, fileError) && !fileError ? installedPath : requestedPath;
	}

	void InitializeDefaultMaterialData(ModelData& modelData) {
		modelData.material = {};
		modelData.material.name = "Default";
		modelData.material.baseColor = {1.0f, 1.0f, 1.0f};
		modelData.material.intensity = 1.0f;
		modelData.material.metallic = 0.0f;
		modelData.material.roughness = 0.5f;
		modelData.material.reflectance = 0.0f;
		modelData.material.ior = 1.0f;
		modelData.material.alpha = 1.0f;
		modelData.material.uvLayoutTextureFilePath.clear();
		modelData.materials.clear();
		modelData.animationClips.clear();
		modelData.skinBoneNames.clear();
		modelData.defaultSkinMatrices.clear();
		modelData.localBoundsCenter = {0.0f, 0.0f, 0.0f};
		modelData.localBoundsSize = {1.0f, 1.0f, 1.0f};
	}

	void UpdateModelLocalBounds(ModelData& modelData) {
		if (modelData.vertices.empty()) {
			modelData.localBoundsCenter = {0.0f, 0.0f, 0.0f};
			modelData.localBoundsSize = {1.0f, 1.0f, 1.0f};
			return;
		}

		Vector3 minPosition = {
			modelData.vertices[0].position.x,
			modelData.vertices[0].position.y,
			modelData.vertices[0].position.z};
		Vector3 maxPosition = minPosition;

		for (const VertexData& vertex : modelData.vertices) {
			const Vector3 position = {
				vertex.position.x,
				vertex.position.y,
				vertex.position.z};
			minPosition.x = (std::min)(minPosition.x, position.x);
			minPosition.y = (std::min)(minPosition.y, position.y);
			minPosition.z = (std::min)(minPosition.z, position.z);
			maxPosition.x = (std::max)(maxPosition.x, position.x);
			maxPosition.y = (std::max)(maxPosition.y, position.y);
			maxPosition.z = (std::max)(maxPosition.z, position.z);
		}

		modelData.localBoundsCenter = {
			(minPosition.x + maxPosition.x) * 0.5f,
			(minPosition.y + maxPosition.y) * 0.5f,
			(minPosition.z + maxPosition.z) * 0.5f};
		modelData.localBoundsSize = {
			(maxPosition.x - minPosition.x),
			(maxPosition.y - minPosition.y),
			(maxPosition.z - minPosition.z)};
	}

	// AssetId単位で保存されたImport設定(Scale/Normal再計算/UV反転)を、Cache登録前の
	// 非indexed三角形列(modelData.vertices)へ直接反映する。Move/Renameしても
	// AssetImportSettingsStoreはAssetId経由で解決するため設定は消えない。
	void ApplyModelImportSettings(ModelData& modelData, const std::string& normalizedPath) {
		const AssetRecord* record = AssetRegistry::Get().FindByPath(normalizedPath);
		if (record == nullptr) {
			return;
		}

		const AssetImportMetadata* metadata = AssetImportSettingsStore::Get().Find(record->id);
		if (metadata == nullptr) {
			return;
		}

		const ModelImportSettings& settings = metadata->model;

		if (settings.importScale != 1.0f) {
			for (VertexData& vertex : modelData.vertices) {
				vertex.position.x *= settings.importScale;
				vertex.position.y *= settings.importScale;
				vertex.position.z *= settings.importScale;
			}
		}

		if (settings.generateNormals) {
			// 非indexed三角形列を3頂点ずつ読み、面法線をそのまま3頂点へ書き込む(Flat Shading相当)。
			for (size_t triangleStart = 0u; triangleStart + 2u < modelData.vertices.size(); triangleStart += 3u) {
				VertexData& vertexA = modelData.vertices[triangleStart];
				VertexData& vertexB = modelData.vertices[triangleStart + 1u];
				VertexData& vertexC = modelData.vertices[triangleStart + 2u];

				const Vector3 edgeAB = {
					vertexB.position.x - vertexA.position.x,
					vertexB.position.y - vertexA.position.y,
					vertexB.position.z - vertexA.position.z};
				const Vector3 edgeAC = {
					vertexC.position.x - vertexA.position.x,
					vertexC.position.y - vertexA.position.y,
					vertexC.position.z - vertexA.position.z};
				Vector3 faceNormal = {
					edgeAB.y * edgeAC.z - edgeAB.z * edgeAC.y,
					edgeAB.z * edgeAC.x - edgeAB.x * edgeAC.z,
					edgeAB.x * edgeAC.y - edgeAB.y * edgeAC.x};
				const float faceNormalLength = std::sqrt(
					faceNormal.x * faceNormal.x +
					faceNormal.y * faceNormal.y +
					faceNormal.z * faceNormal.z);

				if (faceNormalLength > 0.00001f) {
					faceNormal.x /= faceNormalLength;
					faceNormal.y /= faceNormalLength;
					faceNormal.z /= faceNormalLength;
					vertexA.normal = faceNormal;
					vertexB.normal = faceNormal;
					vertexC.normal = faceNormal;
				}
			}
		}

		if (settings.flipUVs) {
			for (VertexData& vertex : modelData.vertices) {
				vertex.texcoord.y = 1.0f - vertex.texcoord.y;
			}
		}
	}

	void OptimizeModelVertices(ModelData& modelData) {
		//============================================================
		// meshoptimizer で頂点を整理する。
		// 描画側は非 indexed 三角形列なので、
		// 一度 indexed 化して最適化し、最後に三角形列へ戻す。
		//============================================================
		if (modelData.vertices.size() < 3u) {
			return;
		}

		const size_t sourceVertexCount = modelData.vertices.size();
		std::vector<unsigned int> sourceIndices(sourceVertexCount);
		for (size_t index = 0; index < sourceVertexCount; ++index) {
			sourceIndices[index] = static_cast<unsigned int>(index);
		}

		std::vector<unsigned int> vertexRemap(sourceVertexCount);
		const size_t uniqueVertexCount = meshopt_generateVertexRemap(
			vertexRemap.data(),
			sourceIndices.data(),
			sourceIndices.size(),
			modelData.vertices.data(),
			modelData.vertices.size(),
			sizeof(VertexData));

		if (uniqueVertexCount == 0u) {
			return;
		}

		std::vector<unsigned int> remappedIndices(sourceIndices.size());
		meshopt_remapIndexBuffer(
			remappedIndices.data(),
			sourceIndices.data(),
			sourceIndices.size(),
			vertexRemap.data());

		std::vector<VertexData> remappedVertices(uniqueVertexCount);
		meshopt_remapVertexBuffer(
			remappedVertices.data(),
			modelData.vertices.data(),
			modelData.vertices.size(),
			sizeof(VertexData),
			vertexRemap.data());

		std::vector<unsigned int> cacheOptimizedIndices(remappedIndices.size());
		meshopt_optimizeVertexCache(
			cacheOptimizedIndices.data(),
			remappedIndices.data(),
			remappedIndices.size(),
			uniqueVertexCount);

		std::vector<unsigned int> overdrawOptimizedIndices(cacheOptimizedIndices.size());
		meshopt_optimizeOverdraw(
			overdrawOptimizedIndices.data(),
			cacheOptimizedIndices.data(),
			cacheOptimizedIndices.size(),
			&remappedVertices[0].position.x,
			uniqueVertexCount,
			sizeof(VertexData),
			1.05f);

		std::vector<VertexData> vertexFetchOptimizedVertices(uniqueVertexCount);
		std::vector<unsigned int> vertexFetchOptimizedIndices = overdrawOptimizedIndices;
		const size_t optimizedVertexCount = meshopt_optimizeVertexFetch(
			vertexFetchOptimizedVertices.data(),
			vertexFetchOptimizedIndices.data(),
			vertexFetchOptimizedIndices.size(),
			remappedVertices.data(),
			uniqueVertexCount,
			sizeof(VertexData));

		vertexFetchOptimizedVertices.resize(optimizedVertexCount);

		std::vector<VertexData> expandedTriangleVertices{};
		expandedTriangleVertices.reserve(vertexFetchOptimizedIndices.size());
		for (unsigned int optimizedIndex : vertexFetchOptimizedIndices) {
			if (optimizedIndex >= vertexFetchOptimizedVertices.size()) {
				continue;
			}

			expandedTriangleVertices.push_back(
				vertexFetchOptimizedVertices[static_cast<size_t>(optimizedIndex)]);
		}

		if (!expandedTriangleVertices.empty()) {
			modelData.vertices = std::move(expandedTriangleVertices);
		}
	}

	std::string TrimText(const std::string& text) {
		const size_t first = text.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			return "";
		}

		const size_t last = text.find_last_not_of(" \t\r\n");
		return text.substr(first, last - first + 1u);
	}

	int32_t ParseIntOrDefault(const std::string& text, int32_t defaultValue) {
		char* endPointer = nullptr;
		const long value = std::strtol(text.c_str(), &endPointer, 10);
		if (endPointer == text.c_str()) {
			return defaultValue;
		}

		return static_cast<int32_t>(value);
	}

	float ParseFloatOrDefault(const std::string& text, float defaultValue) {
		char* endPointer = nullptr;
		const float value = std::strtof(text.c_str(), &endPointer);
		if (endPointer == text.c_str()) {
			return defaultValue;
		}

		return value;
	}

	bool ParseBoolOrDefault(const std::string& text, bool defaultValue) {
		std::string loweredText = text;
		for (char& character : loweredText) {
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}

		if (loweredText == "true" || loweredText == "1") {
			return true;
		}

		if (loweredText == "false" || loweredText == "0") {
			return false;
		}

		return defaultValue;
	}

	Vector4 ParseColorOrDefault(const std::string& text, const Vector4& defaultColor) {
		std::stringstream stream(text);
		std::string element;
		float values[4] = {defaultColor.x, defaultColor.y, defaultColor.z, defaultColor.w};
		for (int32_t valueIndex = 0; valueIndex < 4; ++valueIndex) {
			if (!std::getline(stream, element, ',')) {
				break;
			}

			values[valueIndex] = ParseFloatOrDefault(TrimText(element), values[valueIndex]);
		}

		return {values[0], values[1], values[2], values[3]};
	}

	void FinalizeModelMetadata(ModelData& modelData) {
		if (modelData.materials.empty()) {
			modelData.materials.push_back(modelData.material);
		}
		else {
			modelData.material = modelData.materials.front();
		}
	}

	// FBX の FileName は exporter や作成環境次第で UTF-8 ではなく、CP932 などの
	// Windows ANSI コードページのまま入ることがある。std::filesystem::path に
	// std::string を直接渡すと、MSVC が UTF-8 として変換して system_error を送出する。
	bool TryConvertNarrowTextToWide(
		const std::string& text,
		const UINT codePage,
		const DWORD flags,
		std::wstring& wideText) {
		if (text.empty()) {
			wideText.clear();
			return true;
		}

		const int requiredLength = MultiByteToWideChar(
			codePage,
			flags,
			text.data(),
			static_cast<int>(text.size()),
			nullptr,
			0);
		if (requiredLength <= 0) {
			return false;
		}

		wideText.resize(static_cast<size_t>(requiredLength));
		return MultiByteToWideChar(
			codePage,
			flags,
			text.data(),
			static_cast<int>(text.size()),
			wideText.data(),
			requiredLength) == requiredLength;
	}

	bool TryConvertWideTextToUtf8(const std::wstring& wideText, std::string& utf8Text) {
		if (wideText.empty()) {
			utf8Text.clear();
			return true;
		}

		const int requiredLength = WideCharToMultiByte(
			CP_UTF8,
			0,
			wideText.data(),
			static_cast<int>(wideText.size()),
			nullptr,
			0,
			nullptr,
			nullptr);
		if (requiredLength <= 0) {
			return false;
		}

		utf8Text.resize(static_cast<size_t>(requiredLength));
		return WideCharToMultiByte(
			CP_UTF8,
			0,
			wideText.data(),
			static_cast<int>(wideText.size()),
			utf8Text.data(),
			requiredLength,
			nullptr,
			nullptr) == requiredLength;
	}

	bool TryCreatePathFromFbxText(const std::string& text, std::filesystem::path& path) {
		std::wstring wideText;
		// 現行 exporter の UTF-8 を優先し、古い Windows exporter のローカルコードページを
		// フォールバックにする。どちらにも変換できない参照はインポート対象にしない。
		if (!TryConvertNarrowTextToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS, wideText) &&
			!TryConvertNarrowTextToWide(text, CP_ACP, 0, wideText)) {
			return false;
		}

		path = std::filesystem::path(wideText);
		return true;
	}

	bool TryCreatePathFromUtf8Text(const std::string& text, std::filesystem::path& path) {
		std::wstring wideText;
		if (!TryConvertNarrowTextToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS, wideText)) {
			return false;
		}

		path = std::filesystem::path(wideText);
		return true;
	}

	bool PathExistsWithoutThrowing(const std::string& utf8Path) {
		std::filesystem::path path;
		if (!TryCreatePathFromUtf8Text(utf8Path, path)) {
			return false;
		}

		std::error_code error;
		return std::filesystem::exists(path, error) && !error;
	}

	std::string MakeTexturePathRelativeToAsset(const std::string& assetPath, const std::string& texturePath) {
		if (texturePath.empty()) {
			return "";
		}

		std::filesystem::path textureFilePath;
		std::filesystem::path assetFilePath;
		if (!TryCreatePathFromFbxText(texturePath, textureFilePath) ||
			!TryCreatePathFromUtf8Text(assetPath, assetFilePath)) {
			return "";
		}

		const std::filesystem::path resolvedPath = textureFilePath.is_absolute()
			? textureFilePath
			: (assetFilePath.parent_path() / textureFilePath).lexically_normal();

		std::string resolvedUtf8Path;
		return TryConvertWideTextToUtf8(resolvedPath.native(), resolvedUtf8Path) ? resolvedUtf8Path : "";
	}

	std::string TryGetFbxTexturePath(const FbxProperty& property, const std::string& assetPath) {
		if (!property.IsValid()) {
			return "";
		}

		std::string fallbackTexturePath;
		const int32_t fileTextureCount = property.GetSrcObjectCount();
		for (int32_t textureIndex = 0; textureIndex < fileTextureCount; ++textureIndex) {
			FbxObject* textureObject = property.GetSrcObject(textureIndex);
			if (textureObject == nullptr) {
				continue;
			}

			const char* className = textureObject->GetRuntimeClassId().GetName();
			if (className == nullptr || std::string(className) != "FbxFileTexture") {
				continue;
			}

			FbxFileTexture* fileTexture = static_cast<FbxFileTexture*>(textureObject);

			const char* fileName = fileTexture->GetFileName();
			if (fileName != nullptr && fileName[0] != '\0') {
				const std::string resolvedTexturePath = MakeTexturePathRelativeToAsset(assetPath, fileName);
				if (PathExistsWithoutThrowing(resolvedTexturePath)) {
					return resolvedTexturePath;
				}

				fallbackTexturePath = resolvedTexturePath;
			}

			const char* relativeFileName = fileTexture->GetRelativeFileName();
			if (relativeFileName != nullptr && relativeFileName[0] != '\0') {
				const std::string resolvedTexturePath = MakeTexturePathRelativeToAsset(assetPath, relativeFileName);
				if (PathExistsWithoutThrowing(resolvedTexturePath)) {
					return resolvedTexturePath;
				}

				// 元PCの絶対パスより、FBX横を基準にした相対パスを優先して保持する。
				fallbackTexturePath = resolvedTexturePath;
			}
		}

		return fallbackTexturePath;
	}

	std::string TryGetFirstFbxTexturePath(
		FbxSurfaceMaterial* surfaceMaterial,
		const std::string& assetPath,
		std::initializer_list<const char*> propertyNames) {
		if (surfaceMaterial == nullptr) {
			return "";
		}

		// DCC と FBX Exporter でプロパティ名が異なるため、用途ごとの候補を順に調べる。
		for (const char* propertyName : propertyNames) {
			const FbxProperty property = surfaceMaterial->FindProperty(propertyName, false);
			const std::string texturePath = TryGetFbxTexturePath(property, assetPath);
			if (!texturePath.empty()) {
				return texturePath;
			}
		}

		return "";
	}

	void AppendFbxMaterialData(
		ModelData& modelData,
		FbxSurfaceMaterial* surfaceMaterial,
		const std::string& assetPath) {
		if (surfaceMaterial == nullptr) {
			return;
		}

		MaterialData materialData{};
		const char* materialName = surfaceMaterial->GetName();
		materialData.name = materialName != nullptr && materialName[0] != '\0' ? materialName : "Material";
		materialData.textureFilePath.clear();
		materialData.baseColor = {1.0f, 1.0f, 1.0f};
		materialData.intensity = 1.0f;
		materialData.metallic = 0.0f;
		materialData.roughness = 0.5f;
		materialData.reflectance = 0.0f;
		materialData.ior = 1.0f;
		materialData.alpha = 1.0f;
		materialData.uvLayoutTextureFilePath.clear();

		const FbxProperty diffuseProperty = surfaceMaterial->FindProperty("DiffuseColor", false);
		if (diffuseProperty.IsValid()) {
			const FbxDouble3 diffuseColor = diffuseProperty.Get<FbxDouble3>();
			materialData.baseColor = {
				static_cast<float>(diffuseColor[0]),
				static_cast<float>(diffuseColor[1]),
				static_cast<float>(diffuseColor[2])};
		}

		if (materialData.textureFilePath.empty()) {
			materialData.textureFilePath = TryGetFbxTexturePath(diffuseProperty, assetPath);
		}

		if (materialData.textureFilePath.empty()) {
			const FbxProperty baseColorProperty = surfaceMaterial->FindProperty("BaseColor", false);
			materialData.textureFilePath = TryGetFbxTexturePath(baseColorProperty, assetPath);
		}


		//============================================================
		// FBX PBR テクスチャ
		//============================================================

		materialData.normalTextureFilePath = TryGetFirstFbxTexturePath(
			surfaceMaterial,
			assetPath,
			{"NormalMap", "Normal", "Bump"});
		materialData.metallicTextureFilePath = TryGetFirstFbxTexturePath(
			surfaceMaterial,
			assetPath,
			{"Metalness", "Metallic", "MetallicFactor"});
		materialData.roughnessTextureFilePath = TryGetFirstFbxTexturePath(
			surfaceMaterial,
			assetPath,
			{"Roughness", "RoughnessFactor"});
		materialData.ambientOcclusionTextureFilePath = TryGetFirstFbxTexturePath(
			surfaceMaterial,
			assetPath,
			{"AmbientOcclusion", "Occlusion", "AO"});
		materialData.emissionTextureFilePath = TryGetFirstFbxTexturePath(
			surfaceMaterial,
			assetPath,
			{"EmissiveColor", "EmissionColor", "Emission"});
		materialData.heightTextureFilePath = TryGetFirstFbxTexturePath(
			surfaceMaterial,
			assetPath,
			{"DisplacementColor", "Displacement", "Height", "Bump"});
		materialData.opacityTextureFilePath = TryGetFirstFbxTexturePath(
			surfaceMaterial,
			assetPath,
			{"TransparencyFactor", "TransparentColor", "Opacity"});

		const FbxProperty transparencyProperty = surfaceMaterial->FindProperty("TransparencyFactor", false);
		if (transparencyProperty.IsValid()) {
			materialData.alpha = 1.0f - static_cast<float>(transparencyProperty.Get<FbxDouble>());
		}

		const FbxProperty reflectionProperty = surfaceMaterial->FindProperty("ReflectionFactor", false);
		if (reflectionProperty.IsValid()) {
			materialData.reflectance = static_cast<float>(reflectionProperty.Get<FbxDouble>());
		}

		const FbxProperty specularProperty = surfaceMaterial->FindProperty("SpecularFactor", false);
		if (specularProperty.IsValid()) {
			materialData.reflectance = (std::max)(
				materialData.reflectance,
				static_cast<float>(specularProperty.Get<FbxDouble>()));
		}

		const FbxProperty metallicProperty = surfaceMaterial->FindProperty("Metalness", false);
		if (metallicProperty.IsValid()) {
			materialData.metallic = static_cast<float>(metallicProperty.Get<FbxDouble>());
		}

		const FbxProperty metallicFactorProperty = surfaceMaterial->FindProperty("MetallicFactor", false);
		if (metallicFactorProperty.IsValid()) {
			materialData.metallic = static_cast<float>(metallicFactorProperty.Get<FbxDouble>());
		}

		const FbxProperty refractionProperty = surfaceMaterial->FindProperty("RefractionIndex", false);
		if (refractionProperty.IsValid()) {
			materialData.ior = static_cast<float>(refractionProperty.Get<FbxDouble>());
		}

		const FbxProperty roughnessProperty = surfaceMaterial->FindProperty("Roughness", false);
		if (roughnessProperty.IsValid()) {
			materialData.roughness = static_cast<float>(roughnessProperty.Get<FbxDouble>());
		}

		const FbxProperty roughnessFactorProperty = surfaceMaterial->FindProperty("RoughnessFactor", false);
		if (roughnessFactorProperty.IsValid()) {
			materialData.roughness = static_cast<float>(roughnessFactorProperty.Get<FbxDouble>());
		}

		const FbxProperty shininessProperty = surfaceMaterial->FindProperty("Shininess", false);
		if (shininessProperty.IsValid()) {
			const float shininess = static_cast<float>(shininessProperty.Get<FbxDouble>());
			const float normalizedShininess = (std::clamp)(shininess / 100.0f, 0.0f, 1.0f);
			materialData.roughness = 1.0f - normalizedShininess;
		}

		modelData.materials.push_back(materialData);
	}

	bool HasFbxTransformAnimation(FbxNode* node, FbxAnimLayer* animationLayer) {
		if (node == nullptr || animationLayer == nullptr) {
			return false;
		}

		const char* curveChannels[] = {
			FBXSDK_CURVENODE_COMPONENT_X,
			FBXSDK_CURVENODE_COMPONENT_Y,
			FBXSDK_CURVENODE_COMPONENT_Z,
		};

		for (const char* curveChannel : curveChannels) {
			if (node->LclTranslation.GetCurve(animationLayer, curveChannel) != nullptr ||
				node->LclRotation.GetCurve(animationLayer, curveChannel) != nullptr ||
				node->LclScaling.GetCurve(animationLayer, curveChannel) != nullptr) {
				return true;
			}
		}

		return false;
	}

	FbxNode* FindFbxAnimatedNode(
		FbxNode* node,
		FbxAnimLayer* animationLayer,
		FbxNode*& firstAnimatedNode) {
		if (node == nullptr) {
			return nullptr;
		}

		if (HasFbxTransformAnimation(node, animationLayer)) {
			if (firstAnimatedNode == nullptr) {
				firstAnimatedNode = node;
			}

			// Mesh Node 自身の Transform Key は GameObject Transform へ安全に適用できる。
			if (node->GetMesh() != nullptr) {
				return node;
			}
		}

		const int32_t childCount = static_cast<int32_t>(node->GetChildCount());
		for (int32_t childIndex = 0; childIndex < childCount; childIndex++) {
			FbxNode* meshAnimatedNode = FindFbxAnimatedNode(
				node->GetChild(childIndex),
				animationLayer,
				firstAnimatedNode);

			if (meshAnimatedNode != nullptr && meshAnimatedNode->GetMesh() != nullptr) {
				return meshAnimatedNode;
			}
		}

		return firstAnimatedNode;
	}

	struct FbxSkinBindingData {
		FbxNode* meshNode = nullptr;  // Skin Cluster を持つ Mesh Node
		FbxNode* boneNode = nullptr;  // Cluster が参照する Bone Node
		FbxAMatrix meshBindGlobal{};  // Bind 時の Mesh Global 行列
		FbxAMatrix boneBindGlobal{};  // Bind 時の Bone Global 行列
	};

	struct VertexSkinInfluenceData {
		std::array<uint32_t, 4u> boneIndices{};
		std::array<float, 4u> boneWeights{};
	};

	Matrix4x4 ConvertFbxSkinMatrix(const FbxAMatrix& fbxMatrix) {
		// FBX の column-vector / 右手系を、エンジンの row-vector / X 反転座標へ変換する。
		Matrix4x4 rowMatrix{};

		for (int32_t row = 0; row < 4; row++) {
			for (int32_t column = 0; column < 4; column++) {
				rowMatrix.matrix[row][column] =
					static_cast<float>(fbxMatrix[column][row]);
			}
		}

		Matrix4x4 handednessMatrix = MakeIdentity4x4();
		handednessMatrix.matrix[0][0] = -1.0f;
		return Multiply(Multiply(handednessMatrix, rowMatrix), handednessMatrix);
	}

	Matrix4x4 EvaluateFbxSkinMatrix(
		const FbxSkinBindingData& binding,
		const FbxTime& sampleTime) {
		if (binding.meshNode == nullptr || binding.boneNode == nullptr) {
			return MakeIdentity4x4();
		}

		const FbxAMatrix meshCurrentGlobal =
			binding.meshNode->EvaluateGlobalTransform(sampleTime);
		const FbxAMatrix boneCurrentGlobal =
			binding.boneNode->EvaluateGlobalTransform(sampleTime);
		const FbxAMatrix skinMatrix =
			meshCurrentGlobal.Inverse() *
			boneCurrentGlobal *
			binding.boneBindGlobal.Inverse() *
			binding.meshBindGlobal;
		return ConvertFbxSkinMatrix(skinMatrix);
	}

	void InsertFbxSkinInfluence(
		VertexSkinInfluenceData& influenceData,
		uint32_t boneIndex,
		float boneWeight) {
		if (boneWeight <= 0.000001f) {
			return;
		}

		for (size_t influenceIndex = 0u;
			 influenceIndex < influenceData.boneWeights.size();
			 influenceIndex++) {
			if (influenceData.boneWeights[influenceIndex] > 0.0f &&
				influenceData.boneIndices[influenceIndex] == boneIndex) {
				influenceData.boneWeights[influenceIndex] += boneWeight;
				return;
			}
		}

		size_t replacementIndex = 0u;
		for (size_t influenceIndex = 1u;
			 influenceIndex < influenceData.boneWeights.size();
			 influenceIndex++) {
			if (influenceData.boneWeights[influenceIndex] <
				influenceData.boneWeights[replacementIndex]) {
				replacementIndex = influenceIndex;
			}
		}

		if (boneWeight > influenceData.boneWeights[replacementIndex]) {
			influenceData.boneIndices[replacementIndex] = boneIndex;
			influenceData.boneWeights[replacementIndex] = boneWeight;
		}
	}

	void NormalizeFbxSkinInfluence(VertexSkinInfluenceData& influenceData) {
		float totalWeight = 0.0f;

		for (float boneWeight : influenceData.boneWeights) {
			totalWeight += boneWeight;
		}

		if (totalWeight <= 0.000001f) {
			return;
		}

		const float inverseTotalWeight = 1.0f / totalWeight;
		for (float& boneWeight : influenceData.boneWeights) {
			boneWeight *= inverseTotalWeight;
		}
	}

	void AppendFbxAnimationClips(
		ModelData& modelData,
		FbxScene* scene,
		const std::vector<FbxSkinBindingData>& skinBindings) {
		if (scene == nullptr) {
			return;
		}

		FbxArray<FbxString*> animationStackNames{};
		scene->FillAnimStackNameArray(animationStackNames);
		const int32_t animationStackCount = animationStackNames.Size();
		for (int32_t animationStackIndex = 0; animationStackIndex < animationStackCount; ++animationStackIndex) {
			const char* clipName = animationStackNames[animationStackIndex] != nullptr
				? animationStackNames[animationStackIndex]->Buffer()
				: nullptr;
			FbxAnimStack* animationStack = static_cast<FbxAnimStack*>(
				clipName != nullptr ? scene->FindSrcObject(clipName) : nullptr);
			if (animationStack == nullptr) {
				continue;
			}

			ModelAnimationClipData clipData{};
			clipData.name = clipName != nullptr && clipName[0] != '\0' ? clipName : "Clip";
			FbxTakeInfo* takeInfo = clipName != nullptr ? scene->GetTakeInfo(clipName) : nullptr;
			const FbxTimeSpan clipTimeSpan = takeInfo != nullptr
				? takeInfo->mLocalTimeSpan
				: animationStack->GetLocalTimeSpan();
			clipData.durationSeconds = static_cast<float>(
				clipTimeSpan.GetDuration().GetSecondDouble());

			scene->SetCurrentAnimationStack(animationStack);
			FbxAnimLayer* animationLayer = static_cast<FbxAnimLayer*>(animationStack->GetMember(0));
			FbxNode* firstAnimatedNode = nullptr;
			FbxNode* animatedNode = FindFbxAnimatedNode(
				scene->GetRootNode(),
				animationLayer,
				firstAnimatedNode);
			const double sourceFrameRate = FbxTime::GetFrameRate(
				scene->GetGlobalSettings().GetTimeMode());
			const float sampleFrameRate = (std::clamp)(
				static_cast<float>(sourceFrameRate),
				15.0f,
				60.0f);
			const int32_t sampleCount = clipData.durationSeconds > 0.0f
				? (std::min)(
					static_cast<int32_t>(std::ceil(clipData.durationSeconds * sampleFrameRate)) + 1,
					3600)
				: 1;
			const double clipStartSeconds = clipTimeSpan.GetStart().GetSecondDouble();

			if (animatedNode != nullptr && clipData.durationSeconds > 0.0f) {
				clipData.animatedNodeName = animatedNode->GetName();

				for (int32_t sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++) {
					const float keyframeTime = (std::min)(
						static_cast<float>(sampleIndex) / sampleFrameRate,
						clipData.durationSeconds);
					FbxTime sampleTime{};
					sampleTime.SetSecondDouble(
						clipStartSeconds + static_cast<double>(keyframeTime));
					const FbxAMatrix localTransform = animatedNode->EvaluateLocalTransform(sampleTime);
					const FbxVector4 translation = localTransform.GetT();
					const FbxVector4 rotationDegrees = localTransform.GetR();
					const FbxVector4 scale = localTransform.GetS();

					ModelAnimationKeyframeData keyframe{};
					keyframe.timeSeconds = keyframeTime;
					keyframe.translation = {
						-static_cast<float>(translation[0]),
						static_cast<float>(translation[1]),
						static_cast<float>(translation[2])};
					keyframe.rotation = {
						static_cast<float>(rotationDegrees[0]) * (3.14159265f / 180.0f),
						-static_cast<float>(rotationDegrees[1]) * (3.14159265f / 180.0f),
						-static_cast<float>(rotationDegrees[2]) * (3.14159265f / 180.0f)};
					keyframe.scale = {
						static_cast<float>(scale[0]),
						static_cast<float>(scale[1]),
						static_cast<float>(scale[2])};
					clipData.keyframes.push_back(keyframe);
				}
			}

			if (!skinBindings.empty()) {
				clipData.skinPoseFrames.reserve(static_cast<size_t>(sampleCount));

				for (int32_t sampleIndex = 0; sampleIndex < sampleCount; sampleIndex++) {
					const float frameTime = clipData.durationSeconds > 0.0f
						? (std::min)(
							static_cast<float>(sampleIndex) / sampleFrameRate,
							clipData.durationSeconds)
						: 0.0f;
					FbxTime sampleTime{};
					sampleTime.SetSecondDouble(
						clipStartSeconds + static_cast<double>(frameTime));

					ModelSkinPoseFrameData poseFrame{};
					poseFrame.timeSeconds = frameTime;
					poseFrame.boneMatrices.reserve(skinBindings.size());

					for (const FbxSkinBindingData& skinBinding : skinBindings) {
						poseFrame.boneMatrices.push_back(
							EvaluateFbxSkinMatrix(skinBinding, sampleTime));
					}

					clipData.skinPoseFrames.push_back(std::move(poseFrame));
				}
			}

			modelData.animationClips.push_back(clipData);
		}

		for (int32_t animationStackIndex = 0; animationStackIndex < animationStackCount; animationStackIndex++) {
			delete animationStackNames[animationStackIndex];
		}
	}

	struct CachedModelAsset {
		ModelData modelData;  // 読み込み済みメッシュ。描画と MeshCollider で共有する。
		std::filesystem::file_time_type lastWriteTime{};  // ファイル更新を検知してキャッシュを作り直す。
		std::chrono::steady_clock::time_point nextValidationTime{};  // 毎フレームのファイルシステム確認を避ける。
		bool includesAnimation = false;  // true なら FBX Animation のサンプリングまで完了している。
	};

	std::unordered_map<std::string, CachedModelAsset> g_cachedModelAssets;  // 同じ asset を毎フレーム再パースしないための簡易キャッシュ。
	std::unordered_map<std::string, std::string> g_modelCacheKeyByRequestedPath;  // 旧 resources path と移動後 path を同じ cache entry へ結ぶ。

	std::string ToLowerText(const std::string& text) {
		std::string lowerText = text;
		for (char& character : lowerText) {
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}
		return lowerText;
	}

	std::string NormalizeAssetPath(const std::string& path) {
		std::string normalizedPath = ToLowerText(path);
		for (char& character : normalizedPath) {
			if (character == '\\') {
				character = '/';
			}
		}
		return normalizedPath;
	}

	// シーンやアセット設定から来るパスは UTF-8 のはずだが、壊れた旧データを
	// std::filesystem に直接渡して Editor 全体を止めないようにする。
	std::string NormalizeFilesystemPathForLookup(const std::string& pathText) {
		std::filesystem::path path;
		if (!TryCreatePathFromUtf8Text(pathText, path)) {
			return NormalizeAssetPath(pathText);
		}

		std::string normalizedUtf8Path;
		if (!TryConvertWideTextToUtf8(path.lexically_normal().native(), normalizedUtf8Path)) {
			return NormalizeAssetPath(pathText);
		}

		return NormalizeAssetPath(normalizedUtf8Path);
	}

	std::string ResolveEditorDefaultAssetPath(const std::string& path) {
		const std::string normalizedPath = NormalizeAssetPath(path);

		if (normalizedPath == "resources/uvchecker.png" ||
			normalizedPath.ends_with("/resources/uvchecker.png")) {
			return "resources/editorDefault/uvChecker.png";
		}

		if (normalizedPath == "resources/monsterball.png" ||
			normalizedPath.ends_with("/resources/monsterball.png")) {
			return "resources/editorDefault/monsterBall.png";
		}

		if (normalizedPath == "resources/ball.png" ||
			normalizedPath.ends_with("/resources/ball.png")) {
			return "resources/editorDefault/ball.png";
		}

		if (normalizedPath == "resources/sibahu.png" ||
			normalizedPath.ends_with("/resources/sibahu.png")) {
			return "resources/editorDefault/sibahu.png";
		}

		if (normalizedPath == "resources/uvcube.fbx" ||
			normalizedPath.ends_with("/resources/uvcube.fbx")) {
			return "resources/editorDefault/UVCube.fbx";
		}

		if (normalizedPath == "resources/box.fbx" ||
			normalizedPath.ends_with("/resources/box.fbx")) {
			return "resources/editorDefault/box.fbx";
		}

		if (normalizedPath == "resources/cone.fbx" ||
			normalizedPath.ends_with("/resources/cone.fbx")) {
			return "resources/editorDefault/cone.fbx";
		}

		if (normalizedPath == "resources/icocube.fbx" ||
			normalizedPath.ends_with("/resources/icocube.fbx")) {
			return "resources/editorDefault/ICOCube.fbx";
		}

		if (normalizedPath == "resources/en.fbx" ||
			normalizedPath.ends_with("/resources/en.fbx")) {
			return "resources/editorDefault/en.fbx";
		}

		return path;
	}

	bool MatchesBuiltInPrimitivePath(const std::string& normalizedPath, const char* builtInPath) {
		if (builtInPath == nullptr) {
			return false;
		}

		const std::string builtInText = builtInPath;
		return normalizedPath == builtInText ||
			normalizedPath.ends_with("/" + builtInText);
	}

	bool TryGetBuiltInPrimitiveMeshType(
		const std::string& normalizedPath,
		EditorModelMeshType& meshType) {
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/uvcube.fbx") ||
			MatchesBuiltInPrimitivePath(normalizedPath, "resources/editordefault/uvcube.fbx")) {
			meshType = EditorModelMeshType::Cube;
			return true;
		}
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/box.fbx") ||
			MatchesBuiltInPrimitivePath(normalizedPath, "resources/editordefault/box.fbx")) {
			meshType = EditorModelMeshType::Box;
			return true;
		}
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/cylinder.fbx")) {
			meshType = EditorModelMeshType::Cylinder;
			return true;
		}
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/cone.fbx") ||
			MatchesBuiltInPrimitivePath(normalizedPath, "resources/editordefault/cone.fbx")) {
			meshType = EditorModelMeshType::Cone;
			return true;
		}
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/to-tasu.fbx") ||
			MatchesBuiltInPrimitivePath(normalizedPath, "resources/torus.fbx")) {
			meshType = EditorModelMeshType::Torus;
			return true;
		}
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/icocube.fbx") ||
			MatchesBuiltInPrimitivePath(normalizedPath, "resources/editordefault/icocube.fbx")) {
			meshType = EditorModelMeshType::Ico;
			return true;
		}
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/sphere.fbx")) {
			meshType = EditorModelMeshType::Sphere;
			return true;
		}

		return false;
	}

	Vector3 SubtractVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return Vector3{
			firstValue.x - secondValue.x,
			firstValue.y - secondValue.y,
			firstValue.z - secondValue.z};
	}

	Vector3 CrossVector3(const Vector3& firstValue, const Vector3& secondValue) {
		return Vector3{
			firstValue.y * secondValue.z - firstValue.z * secondValue.y,
			firstValue.z * secondValue.x - firstValue.x * secondValue.z,
			firstValue.x * secondValue.y - firstValue.y * secondValue.x};
	}

	Vector3 NormalizeVector3(const Vector3& vector) {
		float length = std::sqrt(
			vector.x * vector.x +
			vector.y * vector.y +
			vector.z * vector.z);
		if (length <= 0.0001f) {
			return Vector3{0.0f, 1.0f, 0.0f};
		}

		return Vector3{
			vector.x / length,
			vector.y / length,
			vector.z / length};
	}

	void AppendTriangle(
		ModelData& modelData,
		const Vector3& firstPosition,
		const Vector3& secondPosition,
		const Vector3& thirdPosition,
		const Vector2& firstTexcoord,
		const Vector2& secondTexcoord,
		const Vector2& thirdTexcoord) {
		// 3 頂点から法線を作り、UV 付きの描画用三角形としてそのまま展開する。
		const Vector3 edge01 = SubtractVector3(secondPosition, firstPosition);
		const Vector3 edge02 = SubtractVector3(thirdPosition, firstPosition);
		const Vector3 normal = NormalizeVector3(CrossVector3(edge01, edge02));

		const VertexData vertices[3] = {
			{{firstPosition.x, firstPosition.y, firstPosition.z, 1.0f}, firstTexcoord, normal},
			{{secondPosition.x, secondPosition.y, secondPosition.z, 1.0f}, secondTexcoord, normal},
			{{thirdPosition.x, thirdPosition.y, thirdPosition.z, 1.0f}, thirdTexcoord, normal}};

		modelData.vertices.insert(modelData.vertices.end(), std::begin(vertices), std::end(vertices));
	}

	void AppendSkinnedTriangleWithNormals(
		ModelData& modelData,
		const std::array<Vector3, 3u>& positions,
		const std::array<Vector2, 3u>& texcoords,
		const std::array<Vector3, 3u>& normals,
		const std::array<VertexSkinInfluenceData, 3u>& influences) {
		for (size_t vertexIndex = 0u; vertexIndex < positions.size(); vertexIndex++) {
			const VertexSkinInfluenceData& influenceData = influences[vertexIndex];
			VertexData vertex{};
			vertex.position = {
				positions[vertexIndex].x,
				positions[vertexIndex].y,
				positions[vertexIndex].z,
				1.0f};
			vertex.texcoord = texcoords[vertexIndex];
			vertex.normal = NormalizeVector3(normals[vertexIndex]);
			vertex.boneIndices = influenceData.boneIndices;
			vertex.boneWeights = {
				influenceData.boneWeights[0],
				influenceData.boneWeights[1],
				influenceData.boneWeights[2],
				influenceData.boneWeights[3]};
			modelData.vertices.push_back(vertex);
		}
	}

	struct ObjFaceVertexIndex {
		int32_t positionIndex;  // v の何番目を使うか
		int32_t texcoordIndex;  // vt の何番目を使うか。未指定なら -1
	};

	int32_t ParseObjIndexText(const std::string& indexText, int32_t sourceCount) {
		if (indexText.empty()) {
			return -1;
		}

		const int32_t rawIndex = std::stoi(indexText);
		if (rawIndex > 0) {
			return rawIndex - 1;
		}
		if (rawIndex < 0) {
			return sourceCount + rawIndex;
		}

		return -1;
	}

	bool TryParseObjFaceVertexIndex(
		const std::string& token,
		int32_t positionCount,
		int32_t texcoordCount,
		ObjFaceVertexIndex& faceVertexIndex) {
		if (token.empty()) {
			return false;
		}

		std::istringstream tokenStream(token);
		std::string positionIndexText;
		std::string texcoordIndexText;
		std::getline(tokenStream, positionIndexText, '/');
		std::getline(tokenStream, texcoordIndexText, '/');

		faceVertexIndex.positionIndex = ParseObjIndexText(positionIndexText, positionCount);
		faceVertexIndex.texcoordIndex = ParseObjIndexText(texcoordIndexText, texcoordCount);
		return faceVertexIndex.positionIndex >= 0;
	}

	bool LoadObjModel(const std::string& assetPath, ModelData& modelData) {
		InitializeDefaultMaterialData(modelData);
		std::ifstream file(assetPath);
		if (!file.is_open()) {
			return false;
		}

		std::vector<Vector3> positions;  // OBJ の v 行を一時保持する。
		std::vector<Vector2> texcoords;  // OBJ の vt 行を一時保持する。
		std::string line;
		bool isFirstLine = true;
		while (std::getline(file, line)) {
			if (isFirstLine) {
				isFirstLine = false;
				if (line.size() >= sizeof(kUtf8Bom) &&
					static_cast<unsigned char>(line[0]) == kUtf8Bom[0] &&
					static_cast<unsigned char>(line[1]) == kUtf8Bom[1] &&
					static_cast<unsigned char>(line[2]) == kUtf8Bom[2]) {
					line.erase(0U, sizeof(kUtf8Bom));
				}
			}
			std::istringstream lineStream(line);
			std::string identifier;
			lineStream >> identifier;

			if (identifier == "v") {
				Vector3 position{};
				lineStream >> position.x >> position.y >> position.z;
				position.x *= -1.0f;  // 既存 OBJ ローダーと同じく X を反転して座標系を合わせる。
				positions.push_back(position);
				continue;
			}

			if (identifier == "vt") {
				Vector2 texcoord{};
				lineStream >> texcoord.x >> texcoord.y;
				texcoord.y = 1.0f - texcoord.y;  // DirectX の UV 原点に合わせて V を反転する。
				texcoords.push_back(texcoord);
				continue;
			}

			if (identifier != "f") {
				continue;
			}

			std::vector<ObjFaceVertexIndex> polygonIndices;
			std::string vertexToken;
			while (lineStream >> vertexToken) {
				ObjFaceVertexIndex faceVertexIndex{-1, -1};
				if (TryParseObjFaceVertexIndex(
					vertexToken,
					static_cast<int32_t>(positions.size()),
					static_cast<int32_t>(texcoords.size()),
					faceVertexIndex)) {
					polygonIndices.push_back(faceVertexIndex);
				}
			}

			if (polygonIndices.size() < 3) {
				continue;
			}

			for (size_t triangleIndex = 1; triangleIndex + 1 < polygonIndices.size(); ++triangleIndex) {
				const ObjFaceVertexIndex& firstVertex = polygonIndices[0];
				const ObjFaceVertexIndex& secondVertex = polygonIndices[triangleIndex + 1];
				const ObjFaceVertexIndex& thirdVertex = polygonIndices[triangleIndex];
				if (firstVertex.positionIndex < 0 ||
					secondVertex.positionIndex < 0 ||
					thirdVertex.positionIndex < 0 ||
					static_cast<size_t>(firstVertex.positionIndex) >= positions.size() ||
					static_cast<size_t>(secondVertex.positionIndex) >= positions.size() ||
					static_cast<size_t>(thirdVertex.positionIndex) >= positions.size()) {
					continue;
				}

				const Vector2 firstTexcoord =
					firstVertex.texcoordIndex >= 0 &&
					static_cast<size_t>(firstVertex.texcoordIndex) < texcoords.size()
						? texcoords[static_cast<size_t>(firstVertex.texcoordIndex)]
						: Vector2{0.0f, 0.0f};
				const Vector2 secondTexcoord =
					secondVertex.texcoordIndex >= 0 &&
					static_cast<size_t>(secondVertex.texcoordIndex) < texcoords.size()
						? texcoords[static_cast<size_t>(secondVertex.texcoordIndex)]
						: Vector2{0.0f, 0.0f};
				const Vector2 thirdTexcoord =
					thirdVertex.texcoordIndex >= 0 &&
					static_cast<size_t>(thirdVertex.texcoordIndex) < texcoords.size()
						? texcoords[static_cast<size_t>(thirdVertex.texcoordIndex)]
						: Vector2{0.0f, 0.0f};

				AppendTriangle(
					modelData,
					positions[static_cast<size_t>(firstVertex.positionIndex)],
					positions[static_cast<size_t>(secondVertex.positionIndex)],
					positions[static_cast<size_t>(thirdVertex.positionIndex)],
					firstTexcoord,
					secondTexcoord,
					thirdTexcoord);
			}
		}

		FinalizeModelMetadata(modelData);
		UpdateModelLocalBounds(modelData);
		return !modelData.vertices.empty();
	}

	bool LoadFbxModel(const std::string& assetPath, ModelData& modelData, bool includeAnimation) {
		InitializeDefaultMaterialData(modelData);
		FbxManager* fbxManager = FbxManager::Create();
		if (fbxManager == nullptr) {
			return false;
		}

		FbxIOSettings* ioSettings = FbxIOSettings::Create(fbxManager, IOSROOT);
		fbxManager->SetIOSettings(ioSettings);

		FbxImporter* importer = FbxImporter::Create(fbxManager, "");
		if (importer == nullptr) {
			fbxManager->Destroy();
			return false;
		}

		if (!importer->Initialize(assetPath.c_str(), -1, fbxManager->GetIOSettings())) {
			importer->Destroy();
			fbxManager->Destroy();
			return false;
		}

		FbxScene* scene = FbxScene::Create(fbxManager, "LoadedScene");
		if (scene == nullptr) {
			importer->Destroy();
			fbxManager->Destroy();
			return false;
		}

		if (!importer->Import(scene)) {
			scene->Destroy();
			importer->Destroy();
			fbxManager->Destroy();
			return false;
		}
		importer->Destroy();

		// FBX SDK の静的定数 FbxSystemUnit::m は環境によってリンクできないため、
		// 1m = 100cm の単位を直接作り、Blender/FBX の cm 扱いで巨大化しないようにする。
		FbxSystemUnit::ConversionOptions conversionOptions{};
		conversionOptions.mConvertRrsNodes = true;
		conversionOptions.mConvertLimits = true;
		conversionOptions.mConvertClusters = true;
		conversionOptions.mConvertLightIntensity = true;
		conversionOptions.mConvertPhotometricLProperties = true;
		conversionOptions.mConvertCameraClipPlanes = true;
		const FbxSystemUnit meterSystemUnit(100.0);
		meterSystemUnit.ConvertScene(scene, conversionOptions);

		FbxGeometryConverter geometryConverter(fbxManager);
		geometryConverter.Triangulate(scene, true);  // MeshCollider と描画は三角形リストで扱う

		auto appendMaterialNode = [&](auto&& appendMaterialNodeSelf, FbxNode* node) -> void {
			if (node == nullptr) {
				return;
			}

			const int32_t materialCount = node->GetMaterialCount();
			for (int32_t materialIndex = 0; materialIndex < materialCount; ++materialIndex) {
				AppendFbxMaterialData(modelData, node->GetMaterial(materialIndex), assetPath);
			}

			const int32_t childCount = static_cast<int32_t>(node->GetChildCount());
			for (int32_t childIndex = 0; childIndex < childCount; childIndex++) {
				appendMaterialNodeSelf(appendMaterialNodeSelf, node->GetChild(childIndex));
			}
		};
		appendMaterialNode(appendMaterialNode, scene->GetRootNode());

		std::vector<FbxSkinBindingData> skinBindings;

		auto appendMeshNode = [&](auto&& appendMeshNodeSelf, FbxNode* node) -> void {
			if (node == nullptr) {
				return;
			}

			FbxMesh* mesh = node->GetMesh();
			if (mesh != nullptr) {
				FbxAMatrix geometryTransform{};
				geometryTransform.SetT(node->GetGeometricTranslation(FbxNode::eSourcePivot));
				geometryTransform.SetR(node->GetGeometricRotation(FbxNode::eSourcePivot));
				geometryTransform.SetS(node->GetGeometricScaling(FbxNode::eSourcePivot));

				const int32_t controlPointCount =
					static_cast<int32_t>(mesh->GetControlPointsCount());
				std::vector<VertexSkinInfluenceData> controlPointInfluences(
					static_cast<size_t>((std::max)(controlPointCount, 0)));
				const int32_t skinDeformerCount = static_cast<int32_t>(
					mesh->GetDeformerCount(FbxDeformer::eSkin));

				for (int32_t skinDeformerIndex = 0;
					 skinDeformerIndex < skinDeformerCount;
					 skinDeformerIndex++) {
					FbxSkin* skin = static_cast<FbxSkin*>(
						mesh->GetDeformer(skinDeformerIndex, FbxDeformer::eSkin));
					if (skin == nullptr) {
						continue;
					}

					const int32_t clusterCount = static_cast<int32_t>(skin->GetClusterCount());
					for (int32_t clusterIndex = 0; clusterIndex < clusterCount; clusterIndex++) {
						FbxCluster* cluster = skin->GetCluster(clusterIndex);
						FbxNode* boneNode = cluster != nullptr ? cluster->GetLink() : nullptr;

						if (cluster == nullptr || boneNode == nullptr) {
							continue;
						}

						FbxSkinBindingData skinBinding{};
						skinBinding.meshNode = node;
						skinBinding.boneNode = boneNode;
						cluster->GetTransformMatrix(skinBinding.meshBindGlobal);
						cluster->GetTransformLinkMatrix(skinBinding.boneBindGlobal);
						const uint32_t boneIndex = static_cast<uint32_t>(skinBindings.size());
						skinBindings.push_back(skinBinding);
						modelData.skinBoneNames.push_back(
							std::string(node->GetName()) + "/" + boneNode->GetName());

						const int32_t clusterControlPointCount =
							static_cast<int32_t>(cluster->GetControlPointIndicesCount());
						const int32_t* controlPointIndices = cluster->GetControlPointIndices();
						const double* controlPointWeights = cluster->GetControlPointWeights();

						for (int32_t influenceIndex = 0;
							 influenceIndex < clusterControlPointCount;
							 influenceIndex++) {
							const int32_t controlPointIndex = controlPointIndices[influenceIndex];

							if (controlPointIndex < 0 || controlPointIndex >= controlPointCount) {
								continue;
							}

							InsertFbxSkinInfluence(
								controlPointInfluences[static_cast<size_t>(controlPointIndex)],
								boneIndex,
								static_cast<float>(controlPointWeights[influenceIndex]));
						}
					}
				}

				for (VertexSkinInfluenceData& influenceData : controlPointInfluences) {
					NormalizeFbxSkinInfluence(influenceData);
				}

				FbxStringList uvSetNames{};
				mesh->GetUVSetNames(uvSetNames);
				const char* primaryUvSetName =
					uvSetNames.GetCount() > 0 ? uvSetNames[0] : nullptr;
				const int32_t polygonCount = static_cast<int32_t>(mesh->GetPolygonCount());
				for (int32_t polygonIndex = 0; polygonIndex < polygonCount; polygonIndex++) {
					if (mesh->GetPolygonSize(polygonIndex) != 3) {
						continue;
					}

					std::array<Vector3, 3u> positions{};
					std::array<Vector3, 3u> normals{};
					std::array<Vector2, 3u> texcoords{};
					std::array<VertexSkinInfluenceData, 3u> influences{};
					bool isValidTriangle = true;
					for (int32_t vertexIndex = 0; vertexIndex < 3; vertexIndex++) {
						const int32_t controlPointIndex = mesh->GetPolygonVertex(polygonIndex, vertexIndex);
						if (controlPointIndex < 0 ||
							controlPointIndex >= mesh->GetControlPointsCount()) {
							isValidTriangle = false;
							continue;
						}

						const FbxVector4 localControlPoint = mesh->GetControlPointAt(controlPointIndex);
						influences[static_cast<size_t>(vertexIndex)] =
							controlPointInfluences[static_cast<size_t>(controlPointIndex)];
						const FbxVector4 fbxPosition = geometryTransform.MultT(localControlPoint);

						// GameObject 側の Transform で配置・回転・拡縮するため、
						// ここでは FBX のローカル頂点だけを読み、Node のグローバル配置は焼き込まない。
						const double localPositionX = fbxPosition[0];
						const double localPositionY = fbxPosition[1];
						const double localPositionZ = fbxPosition[2];

						positions[vertexIndex] = Vector3{
							-static_cast<float>(localPositionX),
							static_cast<float>(localPositionY),
							static_cast<float>(localPositionZ)};

						FbxVector4 fbxNormal{};
						if (mesh->GetPolygonVertexNormal(polygonIndex, vertexIndex, fbxNormal)) {
							const FbxVector4 transformedNormal = geometryTransform.MultT(FbxVector4{
								fbxNormal[0],
								fbxNormal[1],
								fbxNormal[2],
								0.0});
							normals[vertexIndex] = NormalizeVector3({
								-static_cast<float>(transformedNormal[0]),
								static_cast<float>(transformedNormal[1]),
								static_cast<float>(transformedNormal[2])});
						}
						else {
							normals[vertexIndex] = {0.0f, 1.0f, 0.0f};
						}

						if (primaryUvSetName != nullptr) {
							FbxVector2 fbxTexcoord{};
							bool isUnmappedUv = false;
							const bool hasUv = mesh->GetPolygonVertexUV(
								polygonIndex,
								vertexIndex,
								primaryUvSetName,
								fbxTexcoord,
								isUnmappedUv);
							if (hasUv && !isUnmappedUv) {
								texcoords[vertexIndex] = Vector2{
									static_cast<float>(fbxTexcoord[0]),
									1.0f - static_cast<float>(fbxTexcoord[1])};
							}
						}
					}

					if (!isValidTriangle) {
						continue;
					}

					// X 反転で面の表裏が逆になるため、2 番目と 3 番目を入れ替えて三角形を追加する。
					// FBX の頂点法線をそのまま使い、Blender の smooth shade が面法線へ潰れないようにする。
					AppendSkinnedTriangleWithNormals(
						modelData,
						{positions[0], positions[2], positions[1]},
						{texcoords[0], texcoords[2], texcoords[1]},
						{normals[0], normals[2], normals[1]},
						{influences[0], influences[2], influences[1]});
				}
			}

			const int32_t childCount = static_cast<int32_t>(node->GetChildCount());
			for (int32_t childIndex = 0; childIndex < childCount; childIndex++) {
				appendMeshNodeSelf(appendMeshNodeSelf, node->GetChild(childIndex));
			}
		};

		appendMeshNode(appendMeshNode, scene->GetRootNode());

		if (!skinBindings.empty()) {
			FbxTime defaultPoseTime{};
			defaultPoseTime.SetSecondDouble(0.0);
			modelData.defaultSkinMatrices.reserve(skinBindings.size());

			for (const FbxSkinBindingData& skinBinding : skinBindings) {
				modelData.defaultSkinMatrices.push_back(
					EvaluateFbxSkinMatrix(skinBinding, defaultPoseTime));
			}
		}

		if (includeAnimation) {
			AppendFbxAnimationClips(modelData, scene, skinBindings);
		}

		scene->Destroy();
		fbxManager->Destroy();

		if (modelData.vertices.empty()) {
			return false;
		}

		FinalizeModelMetadata(modelData);
		UpdateModelLocalBounds(modelData);
		return true;
	}
}

bool EditorAssetUtility::HasFilterText(const char* filterText) {
	// nullptr と空文字は「検索なし」として扱う
	return filterText != nullptr && filterText[0] != '\0';
}

bool EditorAssetUtility::MatchesFilter(const std::string& text, const char* filterText) {
	// 検索文字がない場合は全アセットを表示対象にする
	if (!HasFilterText(filterText)) {
		return true;
	}

	// find が npos 以外なら検索語を含む
	return text.find(filterText) != std::string::npos;
}

bool EditorAssetUtility::HasExtension(const std::string& path, const char* extension) {
	if (extension == nullptr) {
		return false;
	}

	const size_t extensionLength = std::char_traits<char>::length(extension);

	if (path.size() < extensionLength) {
		return false;
	}

	const size_t extensionStart = path.size() - extensionLength;

	// パス全体をコピーせず、拡張子部分だけを大文字小文字を無視して比較する
	for (size_t characterIndex = 0; characterIndex < extensionLength; ++characterIndex) {
		const unsigned char pathCharacter =
			static_cast<unsigned char>(path[extensionStart + characterIndex]);
		const unsigned char extensionCharacter =
			static_cast<unsigned char>(extension[characterIndex]);

		if (std::tolower(pathCharacter) != std::tolower(extensionCharacter)) {
			return false;
		}
	}

	return true;
}

std::string EditorAssetUtility::GetFilename(const std::string& path) {
	size_t slashPosition = path.find_last_of("/\\");  // Windows と Unix の両区切り文字を同時に探す
	if (slashPosition == std::string::npos) {
		return path;
	}

	return path.substr(slashPosition + 1);
}

int32_t EditorAssetUtility::GetTextureIndex(const std::vector<std::string>& textureFilePaths, const std::string& path) {
	const std::string normalizedRequestedPath = NormalizeFilesystemPathForLookup(path);
	const std::string normalizedResolvedRequestedPath =
		NormalizeFilesystemPathForLookup(ResolveEditorDefaultAssetPath(path));

	for (uint32_t textureIndex = 0;
		 textureIndex < static_cast<uint32_t>(textureFilePaths.size());
		 textureIndex++) {
		// 登録済みテクスチャパスと完全一致した番号を返す
		if (textureFilePaths[textureIndex] == path) {
			return static_cast<int32_t>(textureIndex);
		}

		const std::string normalizedRegisteredPath =
			NormalizeFilesystemPathForLookup(textureFilePaths[textureIndex]);
		if (normalizedRegisteredPath == normalizedRequestedPath ||
			normalizedRegisteredPath == normalizedResolvedRequestedPath) {
			return static_cast<int32_t>(textureIndex);
		}
	}

	// 見つからない場合は無効値として -1 を返す
	return -1;
}

EditorModelMeshType EditorAssetUtility::GetModelMeshType(const std::string& path) {
	EditorModelMeshType meshType = EditorModelMeshType::Plane;
	if (TryGetBuiltInPrimitiveMeshType(NormalizeAssetPath(path), meshType)) {
		return meshType;
	}

	return EditorModelMeshType::Plane;
}

bool EditorAssetUtility::IsBuiltInPrimitiveAssetPath(const std::string& path) {
	EditorModelMeshType meshType = EditorModelMeshType::Plane;
	return TryGetBuiltInPrimitiveMeshType(NormalizeAssetPath(path), meshType);
}

const ModelData* EditorAssetUtility::GetModelAssetData(const std::string& path, bool includeAnimation) {
	if (path.empty()) {
		return nullptr;
	}

	std::filesystem::path filePath;
	if (!TryCreatePathFromUtf8Text(path, filePath)) {
		return nullptr;
	}

	std::error_code fileError;
	if (!std::filesystem::exists(filePath, fileError) || fileError) {
		std::filesystem::path resolvedFilePath;
		if (!TryCreatePathFromUtf8Text(ResolveEditorDefaultAssetPath(path), resolvedFilePath)) {
			return nullptr;
		}

		fileError.clear();
		if (!std::filesystem::exists(resolvedFilePath, fileError) || fileError) {
			// Project配布では内蔵PrimitiveをProjectへ複製しない。
			// Project側に無いresources配下だけ、導入済みEngineから読む。
			resolvedFilePath = ResolveInstalledEngineAssetPath(resolvedFilePath);
			fileError.clear();
			if (!std::filesystem::exists(resolvedFilePath, fileError) || fileError) return nullptr;
		}

		filePath = resolvedFilePath;
	}

	std::string resolvedFilePathUtf8;
	if (!TryConvertWideTextToUtf8(filePath.native(), resolvedFilePathUtf8)) {
		return nullptr;
	}

	const std::string normalizedPath = NormalizeFilesystemPathForLookup(resolvedFilePathUtf8);
	const std::string normalizedRequestedPath = NormalizeFilesystemPathForLookup(path);
	const std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(filePath, fileError);
	if (!fileError) {
		auto cacheIterator = g_cachedModelAssets.find(normalizedPath);
		if (cacheIterator != g_cachedModelAssets.end() &&
			cacheIterator->second.lastWriteTime == lastWriteTime &&
			(!includeAnimation || cacheIterator->second.includesAnimation)) {
			cacheIterator->second.nextValidationTime = std::chrono::steady_clock::now() + std::chrono::seconds(1);
			g_modelCacheKeyByRequestedPath[normalizedRequestedPath] = normalizedPath;
			g_modelCacheKeyByRequestedPath[normalizedPath] = normalizedPath;
			return cacheIterator->second.modelData.vertices.empty() ? nullptr : &cacheIterator->second.modelData;
		}
	}

	// Import設定でAnimationを含めない指定があれば、呼び出し側の要求より優先して除外する
	// (Import設定はAssetそのものの方針、includeAnimationは呼び出し側の一時的な要求のため)。
	bool resolvedIncludeAnimation = includeAnimation;
	if (includeAnimation) {
		const AssetRecord* animationGateRecord = AssetRegistry::Get().FindByPath(normalizedPath);
		if (animationGateRecord != nullptr) {
			const AssetImportMetadata* animationGateMetadata =
				AssetImportSettingsStore::Get().Find(animationGateRecord->id);
			if (animationGateMetadata != nullptr && !animationGateMetadata->model.importAnimation) {
				resolvedIncludeAnimation = false;
			}
		}
	}

	ModelData loadedModelData{};
	bool isLoaded = false;
	const bool isFbxAsset = HasExtension(path, ".fbx");
	if (HasExtension(path, ".obj")) {
		isLoaded = LoadObjModel(resolvedFilePathUtf8, loadedModelData);
	}
	else if (isFbxAsset) {
		isLoaded = LoadFbxModel(resolvedFilePathUtf8, loadedModelData, resolvedIncludeAnimation);
	}

	if (!isLoaded) {
		return nullptr;
	}

	ApplyModelImportSettings(loadedModelData, normalizedPath);
	OptimizeModelVertices(loadedModelData);

	CachedModelAsset& cachedAsset = g_cachedModelAssets[normalizedPath];
	cachedAsset.modelData = std::move(loadedModelData);
	cachedAsset.lastWriteTime = lastWriteTime;
	cachedAsset.nextValidationTime = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	cachedAsset.includesAnimation = !isFbxAsset || resolvedIncludeAnimation;
	g_modelCacheKeyByRequestedPath[normalizedRequestedPath] = normalizedPath;
	g_modelCacheKeyByRequestedPath[normalizedPath] = normalizedPath;
	return cachedAsset.modelData.vertices.empty() ? nullptr : &cachedAsset.modelData;
}

const ModelData* EditorAssetUtility::GetSharedModelAssetData(const std::string& path, bool includeAnimation) {
	const std::string requestedCacheKey = NormalizeFilesystemPathForLookup(path);
	auto aliasIterator = g_modelCacheKeyByRequestedPath.find(requestedCacheKey);
	const std::string& modelCacheKey =
		aliasIterator != g_modelCacheKeyByRequestedPath.end() ? aliasIterator->second : requestedCacheKey;
	auto cacheIterator = g_cachedModelAssets.find(modelCacheKey);

	if (cacheIterator != g_cachedModelAssets.end() &&
		(!includeAnimation || cacheIterator->second.includesAnimation) &&
		std::chrono::steady_clock::now() < cacheIterator->second.nextValidationTime) {
		return cacheIterator->second.modelData.vertices.empty() ? nullptr : &cacheIterator->second.modelData;
	}

	return GetModelAssetData(path, includeAnimation);
}

void EditorAssetUtility::InvalidateModelAssetCache(const std::string& path) {
	if (path.empty()) {
		return;
	}

	const std::string requestedCacheKey = NormalizeFilesystemPathForLookup(path);
	const auto aliasIterator = g_modelCacheKeyByRequestedPath.find(requestedCacheKey);
	const std::string modelCacheKey =
		aliasIterator != g_modelCacheKeyByRequestedPath.end()
		? aliasIterator->second
		: requestedCacheKey;

	g_cachedModelAssets.erase(modelCacheKey);

	// 同じ実ファイルを指す旧 resources path などの別名も消し、古い参照へ戻らないようにする。
	for (auto iterator = g_modelCacheKeyByRequestedPath.begin();
		iterator != g_modelCacheKeyByRequestedPath.end();) {
		if (iterator->first == requestedCacheKey || iterator->second == modelCacheKey) {
			iterator = g_modelCacheKeyByRequestedPath.erase(iterator);
		}
		else {
			++iterator;
		}
	}
}

bool EditorAssetUtility::LoadModelAsset(const std::string& path, ModelData& modelData) {
	modelData = {};
	const ModelData* cachedModelData = GetModelAssetData(path, true);
	if (cachedModelData == nullptr) {
		return false;
	}

	modelData = *cachedModelData;
	return !modelData.vertices.empty();
}

bool EditorAssetUtility::GetModelColliderBounds(
	const std::string& path,
	Vector3& colliderCenter,
	Vector3& colliderSize) {
	const ModelData* modelData = GetSharedModelAssetData(path, false);  // 描画キャッシュから Bounds だけを参照する。
	if (modelData == nullptr || modelData->vertices.empty()) {
		return false;
	}

	Vector3 minimumPosition = {
		modelData->vertices[0].position.x,
		modelData->vertices[0].position.y,
		modelData->vertices[0].position.z};
	Vector3 maximumPosition = minimumPosition;
	for (const VertexData& vertex : modelData->vertices) {
		minimumPosition.x = (std::min)(minimumPosition.x, vertex.position.x);
		minimumPosition.y = (std::min)(minimumPosition.y, vertex.position.y);
		minimumPosition.z = (std::min)(minimumPosition.z, vertex.position.z);
		maximumPosition.x = (std::max)(maximumPosition.x, vertex.position.x);
		maximumPosition.y = (std::max)(maximumPosition.y, vertex.position.y);
		maximumPosition.z = (std::max)(maximumPosition.z, vertex.position.z);
	}

	colliderCenter = {
		(minimumPosition.x + maximumPosition.x) * 0.5f,
		(minimumPosition.y + maximumPosition.y) * 0.5f,
		(minimumPosition.z + maximumPosition.z) * 0.5f};
	colliderSize = {
		(std::max)(maximumPosition.x - minimumPosition.x, 0.01f),
		(std::max)(maximumPosition.y - minimumPosition.y, 0.01f),
		(std::max)(maximumPosition.z - minimumPosition.z, 0.01f)};
	return true;
}

bool EditorAssetUtility::IsRenderTextureAssetPath(const std::string& path) {
	return HasExtension(path, ".rendertexture");
}

EditorRenderTextureAsset EditorAssetUtility::MakeDefaultRenderTextureAsset() {
	EditorRenderTextureAsset asset{};
	asset.width = 1920;
	asset.height = 1080;
	asset.useHdr = true;
	asset.useDepth = true;
	asset.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
	return asset;
}

bool EditorAssetUtility::LoadRenderTextureAsset(const std::string& path, EditorRenderTextureAsset& asset) {
	asset = MakeDefaultRenderTextureAsset();
	if (!IsRenderTextureAssetPath(path)) {
		return false;
	}

	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}

	std::string line;
	bool isFirstLine = true;
	while (std::getline(file, line)) {
		if (isFirstLine &&
			line.size() >= 3u &&
			static_cast<unsigned char>(line[0]) == 0xEFu &&
			static_cast<unsigned char>(line[1]) == 0xBBu &&
			static_cast<unsigned char>(line[2]) == 0xBFu) {
			line.erase(0, 3);
		}
		isFirstLine = false;

		line = TrimText(line);
		if (line.empty() || line[0] == '#') {
			continue;
		}

		const size_t delimiterPosition = line.find('=');
		if (delimiterPosition == std::string::npos) {
			continue;
		}

		const std::string key = TrimText(line.substr(0, delimiterPosition));
		const std::string value = TrimText(line.substr(delimiterPosition + 1u));
		if (key == "width") {
			asset.width = (std::clamp)(ParseIntOrDefault(value, asset.width), 1, 8192);
		}
		else if (key == "height") {
			asset.height = (std::clamp)(ParseIntOrDefault(value, asset.height), 1, 8192);
		}
		else if (key == "format") {
			asset.useHdr = value != "LDR";
		}
		else if (key == "useDepth") {
			asset.useDepth = ParseBoolOrDefault(value, asset.useDepth);
		}
		else if (key == "clearColor") {
			asset.clearColor = ParseColorOrDefault(value, asset.clearColor);
		}
	}

	return true;
}

bool EditorAssetUtility::SaveRenderTextureAsset(const std::string& path, const EditorRenderTextureAsset& asset) {
	if (!IsRenderTextureAssetPath(path)) {
		return false;
	}

	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}

	const int32_t width = (std::clamp)(asset.width, 1, 8192);
	const int32_t height = (std::clamp)(asset.height, 1, 8192);
	file.write(reinterpret_cast<const char*>(kUtf8Bom), static_cast<std::streamsize>(sizeof(kUtf8Bom)));
	file << "# ManoEngine RenderTexture\r\n";
	file << "# Camera の出力先や PostProcess の中間結果として使う描画用 Texture 設定です。\r\n";
	file << "width=" << width << "\r\n";
	file << "height=" << height << "\r\n";
	file << "format=" << (asset.useHdr ? "HDR" : "LDR") << "\r\n";
	file << "useDepth=" << (asset.useDepth ? "true" : "false") << "\r\n";
	file << "clearColor="
	     << asset.clearColor.x << ","
	     << asset.clearColor.y << ","
	     << asset.clearColor.z << ","
	     << asset.clearColor.w << "\r\n";
	return file.good();
}

bool EditorAssetUtility::CreateDefaultRenderTextureAsset(const std::string& path) {
	return SaveRenderTextureAsset(path, MakeDefaultRenderTextureAsset());
}
