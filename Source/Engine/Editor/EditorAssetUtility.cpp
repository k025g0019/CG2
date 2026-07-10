#include "EditorAssetUtility.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push, 0)
#include <fbxsdk.h>
#pragma warning(pop)

#pragma warning(disable : 4623 4626 5027 5045 5245)

namespace {
	constexpr unsigned char kUtf8Bom[] = {0xEFu, 0xBBu, 0xBFu};  // テキストアセットを UTF-8 BOM 付きで保存する。

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
		modelData.material.uvLayoutTextureFilePath = "resources/uvChecker.png";
		modelData.materials.clear();
		modelData.animationClips.clear();
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

	std::string MakeTexturePathRelativeToAsset(const std::string& assetPath, const std::string& texturePath) {
		if (texturePath.empty()) {
			return "";
		}

		const std::filesystem::path textureFilePath(texturePath);
		if (textureFilePath.is_absolute()) {
			return textureFilePath.generic_string();
		}

		const std::filesystem::path assetDirectory = std::filesystem::path(assetPath).parent_path();
		return (assetDirectory / textureFilePath).lexically_normal().generic_string();
	}

	std::string TryGetFbxTexturePath(const FbxProperty& property, const std::string& assetPath) {
		if (!property.IsValid()) {
			return "";
		}

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
				return MakeTexturePathRelativeToAsset(assetPath, fileName);
			}

			const char* relativeFileName = fileTexture->GetRelativeFileName();
			if (relativeFileName != nullptr && relativeFileName[0] != '\0') {
				return MakeTexturePathRelativeToAsset(assetPath, relativeFileName);
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
		materialData.uvLayoutTextureFilePath = "resources/uvChecker.png";

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

		if (materialData.textureFilePath.empty()) {
			const FbxProperty emissiveProperty = surfaceMaterial->FindProperty("EmissiveColor", false);
			materialData.textureFilePath = TryGetFbxTexturePath(emissiveProperty, assetPath);
		}

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

	void AppendFbxAnimationClips(ModelData& modelData, FbxScene* scene) {
		if (scene == nullptr) {
			return;
		}

		FbxArray<FbxString*> animationStackNames{};
		scene->FillAnimStackNameArray(animationStackNames);
		const int32_t animationStackCount = animationStackNames.Size();
		for (int32_t animationStackIndex = 0; animationStackIndex < animationStackCount; ++animationStackIndex) {
			ModelAnimationClipData clipData{};
			const char* clipName = animationStackNames[animationStackIndex]->Buffer();
			clipData.name = clipName != nullptr && clipName[0] != '\0' ? clipName : "Clip";
			FbxTakeInfo* takeInfo = clipName != nullptr ? scene->GetTakeInfo(clipName) : nullptr;
			if (takeInfo != nullptr) {
				clipData.durationSeconds = static_cast<float>(takeInfo->mLocalTimeSpan.GetDuration().GetSecondDouble());
			}
			else {
				clipData.durationSeconds = 0.0f;
			}

			modelData.animationClips.push_back(clipData);
		}

		for (int32_t animationStackIndex = 0; animationStackIndex < animationStackCount; ++animationStackIndex) {
			delete animationStackNames[animationStackIndex];
		}
	}

	struct CachedModelAsset {
		ModelData modelData;  // 読み込み済みメッシュ。描画と MeshCollider で共有する。
		std::filesystem::file_time_type lastWriteTime{};  // ファイル更新を検知してキャッシュを作り直す。
	};

	std::unordered_map<std::string, CachedModelAsset> g_cachedModelAssets;  // 同じ asset を毎フレーム再パースしないための簡易キャッシュ。

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
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/uvcube.fbx")) {
			meshType = EditorModelMeshType::Cube;
			return true;
		}
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/box.fbx")) {
			meshType = EditorModelMeshType::Box;
			return true;
		}
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/cylinder.fbx")) {
			meshType = EditorModelMeshType::Cylinder;
			return true;
		}
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/cone.fbx")) {
			meshType = EditorModelMeshType::Cone;
			return true;
		}
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/to-tasu.fbx") ||
			MatchesBuiltInPrimitivePath(normalizedPath, "resources/torus.fbx")) {
			meshType = EditorModelMeshType::Torus;
			return true;
		}
		if (MatchesBuiltInPrimitivePath(normalizedPath, "resources/icocube.fbx")) {
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

	void AppendTriangleWithNormals(
		ModelData& modelData,
		const Vector3& firstPosition,
		const Vector3& secondPosition,
		const Vector3& thirdPosition,
		const Vector2& firstTexcoord,
		const Vector2& secondTexcoord,
		const Vector2& thirdTexcoord,
		const Vector3& firstNormal,
		const Vector3& secondNormal,
		const Vector3& thirdNormal) {
		const VertexData vertices[3] = {
			{{firstPosition.x, firstPosition.y, firstPosition.z, 1.0f}, firstTexcoord, NormalizeVector3(firstNormal)},
			{{secondPosition.x, secondPosition.y, secondPosition.z, 1.0f}, secondTexcoord, NormalizeVector3(secondNormal)},
			{{thirdPosition.x, thirdPosition.y, thirdPosition.z, 1.0f}, thirdTexcoord, NormalizeVector3(thirdNormal)}};

		modelData.vertices.insert(modelData.vertices.end(), std::begin(vertices), std::end(vertices));
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
		while (std::getline(file, line)) {
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

	bool LoadFbxModel(const std::string& assetPath, ModelData& modelData) {
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
		AppendFbxAnimationClips(modelData, scene);

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

				FbxStringList uvSetNames{};
				mesh->GetUVSetNames(uvSetNames);
				const char* primaryUvSetName =
					uvSetNames.GetCount() > 0 ? uvSetNames[0] : nullptr;
				const int32_t polygonCount = static_cast<int32_t>(mesh->GetPolygonCount());
				for (int32_t polygonIndex = 0; polygonIndex < polygonCount; polygonIndex++) {
					if (mesh->GetPolygonSize(polygonIndex) != 3) {
						continue;
					}

					Vector3 positions[3]{};
					Vector3 normals[3]{};
					Vector2 texcoords[3]{};
					bool isValidTriangle = true;
					for (int32_t vertexIndex = 0; vertexIndex < 3; vertexIndex++) {
						const int32_t controlPointIndex = mesh->GetPolygonVertex(polygonIndex, vertexIndex);
						if (controlPointIndex < 0 ||
							controlPointIndex >= mesh->GetControlPointsCount()) {
							isValidTriangle = false;
							continue;
						}

						const FbxVector4 localControlPoint = mesh->GetControlPointAt(controlPointIndex);
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
					AppendTriangleWithNormals(
						modelData,
						positions[0],
						positions[2],
						positions[1],
						texcoords[0],
						texcoords[2],
						texcoords[1],
						normals[0],
						normals[2],
						normals[1]);
				}
			}

			const int32_t childCount = static_cast<int32_t>(node->GetChildCount());
			for (int32_t childIndex = 0; childIndex < childCount; childIndex++) {
				appendMeshNodeSelf(appendMeshNodeSelf, node->GetChild(childIndex));
			}
		};

		appendMeshNode(appendMeshNode, scene->GetRootNode());

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

	std::string pathText = path;
	std::string extensionText = extension;

	// 大文字小文字を無視するため、比較前に両方を小文字化する
	for (char& character : pathText) {
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	}
	for (char& character : extensionText) {
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	}

	if (pathText.size() < extensionText.size()) {
		return false;
	}

	// パス末尾の extensionText.size() 文字だけを拡張子として比較する
	return pathText.compare(pathText.size() - extensionText.size(), extensionText.size(), extensionText) == 0;
}

std::string EditorAssetUtility::GetFilename(const std::string& path) {
	size_t slashPosition = path.find_last_of("/\\");  // Windows と Unix の両区切り文字を同時に探す
	if (slashPosition == std::string::npos) {
		return path;
	}

	return path.substr(slashPosition + 1);
}

int32_t EditorAssetUtility::GetTextureIndex(const std::vector<std::string>& textureFilePaths, const std::string& path) {
	for (uint32_t textureIndex = 0;
		 textureIndex < static_cast<uint32_t>(textureFilePaths.size());
		 textureIndex++) {
		// 登録済みテクスチャパスと完全一致した番号を返す
		if (textureFilePaths[textureIndex] == path) {
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

bool EditorAssetUtility::LoadModelAsset(const std::string& path, ModelData& modelData) {
	modelData = {};
	if (path.empty()) {
		return false;
	}

	const std::filesystem::path filePath(path);
	std::error_code fileError;
	if (!std::filesystem::exists(filePath, fileError) || fileError) {
		return false;
	}

	const std::string normalizedPath = NormalizeAssetPath(filePath.generic_string());
	const std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(filePath, fileError);
	if (!fileError) {
		auto cacheIterator = g_cachedModelAssets.find(normalizedPath);
		if (cacheIterator != g_cachedModelAssets.end() &&
			cacheIterator->second.lastWriteTime == lastWriteTime) {
			modelData = cacheIterator->second.modelData;
			return !modelData.vertices.empty();
		}
	}

	ModelData loadedModelData{};
	bool isLoaded = false;
	if (HasExtension(path, ".obj")) {
		isLoaded = LoadObjModel(filePath.generic_string(), loadedModelData);
	}
	else if (HasExtension(path, ".fbx")) {
		isLoaded = LoadFbxModel(filePath.generic_string(), loadedModelData);
	}

	if (!isLoaded) {
		return false;
	}

	CachedModelAsset& cachedAsset = g_cachedModelAssets[normalizedPath];
	cachedAsset.modelData = loadedModelData;
	cachedAsset.lastWriteTime = lastWriteTime;
	modelData = cachedAsset.modelData;
	return !modelData.vertices.empty();
}

bool EditorAssetUtility::IsRenderTextureAssetPath(const std::string& path) {
	return HasExtension(path, ".rendertexture");
}

EditorRenderTextureAsset EditorAssetUtility::MakeDefaultRenderTextureAsset() {
	EditorRenderTextureAsset asset{};
	asset.width = 1280;
	asset.height = 720;
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
	file << "# CG2 RenderTexture\r\n";
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
