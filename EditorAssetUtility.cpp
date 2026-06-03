#include "EditorAssetUtility.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
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

	bool ContainsKeyword(const std::string& text, const char* keyword) {
		// keyword が filename に含まれるかだけを見る小さな判定関数。
		return keyword != nullptr && text.find(keyword) != std::string::npos;
	}

	bool IsBuiltInPrimitiveNormalizedPath(const std::string& normalizedPath) {
		static const std::array<const char*, 8> kBuiltInPrimitivePaths = {
			"resources/uvcube.fbx",
			"resources/box.fbx",
			"resources/cylinder.fbx",
			"resources/cone.fbx",
			"resources/to-tasu.fbx",
			"resources/torus.fbx",
			"resources/icocube.fbx",
			"resources/sphere.fbx"};

		for (const char* builtInPath : kBuiltInPrimitivePaths) {
			if (normalizedPath == builtInPath) {
				return true;
			}
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
		const Vector3& thirdPosition) {
		// 3 頂点から法線を作り、描画用の三角形としてそのまま展開する。
		const Vector3 edge01 = SubtractVector3(secondPosition, firstPosition);
		const Vector3 edge02 = SubtractVector3(thirdPosition, firstPosition);
		const Vector3 normal = NormalizeVector3(CrossVector3(edge01, edge02));

		const VertexData vertices[3] = {
			{{firstPosition.x, firstPosition.y, firstPosition.z, 1.0f}, {0.0f, 0.0f}, normal},
			{{secondPosition.x, secondPosition.y, secondPosition.z, 1.0f}, {0.0f, 0.0f}, normal},
			{{thirdPosition.x, thirdPosition.y, thirdPosition.z, 1.0f}, {0.0f, 0.0f}, normal}};

		modelData.vertices.insert(modelData.vertices.end(), std::begin(vertices), std::end(vertices));
	}

	bool TryParseFaceVertexIndex(const std::string& token, int32_t& positionIndex) {
		if (token.empty()) {
			return false;
		}

		std::istringstream tokenStream(token);
		std::string positionIndexText;
		std::getline(tokenStream, positionIndexText, '/');
		if (positionIndexText.empty()) {
			return false;
		}

		positionIndex = std::stoi(positionIndexText) - 1;
		return positionIndex >= 0;
	}

	bool LoadObjModel(const std::string& assetPath, ModelData& modelData) {
		std::ifstream file(assetPath);
		if (!file.is_open()) {
			return false;
		}

		std::vector<Vector3> positions;  // OBJ の v 行を一時保持する。
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

			if (identifier != "f") {
				continue;
			}

			std::vector<int32_t> polygonIndices;
			std::string vertexToken;
			while (lineStream >> vertexToken) {
				int32_t positionIndex = -1;
				if (TryParseFaceVertexIndex(vertexToken, positionIndex)) {
					polygonIndices.push_back(positionIndex);
				}
			}

			if (polygonIndices.size() < 3) {
				continue;
			}

			for (size_t triangleIndex = 1; triangleIndex + 1 < polygonIndices.size(); ++triangleIndex) {
				const int32_t firstIndex = polygonIndices[0];
				const int32_t secondIndex = polygonIndices[triangleIndex + 1];
				const int32_t thirdIndex = polygonIndices[triangleIndex];
				if (firstIndex < 0 ||
					secondIndex < 0 ||
					thirdIndex < 0 ||
					static_cast<size_t>(firstIndex) >= positions.size() ||
					static_cast<size_t>(secondIndex) >= positions.size() ||
					static_cast<size_t>(thirdIndex) >= positions.size()) {
					continue;
				}

				AppendTriangle(
					modelData,
					positions[static_cast<size_t>(firstIndex)],
					positions[static_cast<size_t>(secondIndex)],
					positions[static_cast<size_t>(thirdIndex)]);
			}
		}

		return !modelData.vertices.empty();
	}

	bool LoadFbxModel(const std::string& assetPath, ModelData& modelData) {
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

		FbxGeometryConverter geometryConverter(fbxManager);
		geometryConverter.Triangulate(scene, true);  // MeshCollider と描画は三角形リストで扱う

		auto appendMeshNode = [&](auto&& appendMeshNodeSelf, FbxNode* node) -> void {
			if (node == nullptr) {
				return;
			}

			FbxMesh* mesh = node->GetMesh();
			if (mesh != nullptr) {
				const FbxAMatrix nodeTransform = node->EvaluateGlobalTransform();
				const int32_t polygonCount = static_cast<int32_t>(mesh->GetPolygonCount());
				for (int32_t polygonIndex = 0; polygonIndex < polygonCount; polygonIndex++) {
					if (mesh->GetPolygonSize(polygonIndex) != 3) {
						continue;
					}

					Vector3 positions[3]{};
					bool isValidTriangle = true;
					for (int32_t vertexIndex = 0; vertexIndex < 3; vertexIndex++) {
						const int32_t controlPointIndex = mesh->GetPolygonVertex(polygonIndex, vertexIndex);
						if (controlPointIndex < 0 ||
							controlPointIndex >= mesh->GetControlPointsCount()) {
							isValidTriangle = false;
							continue;
						}

						const FbxVector4 fbxPosition =
							nodeTransform.MultT(mesh->GetControlPointAt(controlPointIndex));
						positions[vertexIndex] = Vector3{
							-static_cast<float>(fbxPosition[0]),
							static_cast<float>(fbxPosition[1]),
							static_cast<float>(fbxPosition[2])};
					}

					if (!isValidTriangle) {
						continue;
					}

					// X 反転で面の表裏が逆になるため、2 番目と 3 番目を入れ替えて三角形を追加する。
					AppendTriangle(modelData, positions[0], positions[2], positions[1]);
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
	std::string filename = GetFilename(path);  // 判定はフォルダではなくファイル名だけで行う

	for (char& character : filename) {
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	}

	if (ContainsKeyword(filename, "sphere") ||
		ContainsKeyword(filename, "ball")) {
		return EditorModelMeshType::Sphere;
	}

	if (filename.find("ico") != std::string::npos) {
		return EditorModelMeshType::Ico;
	}

	if (filename.find("uvcube") != std::string::npos ||
		filename.find("cube") != std::string::npos) {
		return EditorModelMeshType::Cube;
	}

	if (filename.find("box") != std::string::npos) {
		return EditorModelMeshType::Box;
	}

	if (filename.find("cylinder") != std::string::npos) {
		return EditorModelMeshType::Cylinder;
	}

	if (filename.find("cone") != std::string::npos) {
		return EditorModelMeshType::Cone;
	}

	if (filename.find("torus") != std::string::npos ||
		filename.find("to-tasu") != std::string::npos) {
		return EditorModelMeshType::Torus;
	}

	return EditorModelMeshType::Plane;
}

bool EditorAssetUtility::IsBuiltInPrimitiveAssetPath(const std::string& path) {
	return IsBuiltInPrimitiveNormalizedPath(NormalizeAssetPath(path));
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
