#pragma once

#include "EditorCommonTypes.h"
#include "EditorSceneObject.h"

#include <cstdint>
#include <string>
#include <vector>

class EditorAssetUtility {
public:
	static bool HasFilterText(const char* filterText);  // 検索文字列が空ではないかを調べる
	static bool MatchesFilter(const std::string& text, const char* filterText);  // アセット名が検索文字列を含むかを調べる
	static bool HasExtension(const std::string& path, const char* extension);  // パス末尾の拡張子を大小文字無視で比較する
	static std::string GetFilename(const std::string& path);  // パスからファイル名だけを取り出す
	static int32_t GetTextureIndex(const std::vector<std::string>& textureFilePaths, const std::string& path);  // テクスチャパス配列内の一致位置を返す
	static EditorModelMeshType GetModelMeshType(const std::string& path);  // FBX / OBJ のファイル名から基本形メッシュ種別を返す
	static bool IsBuiltInPrimitiveAssetPath(const std::string& path);  // 内部基本形として扱う resources 内モデルかを返す
	static bool LoadModelAsset(const std::string& path, ModelData& modelData);  // OBJ / FBX から描画と物理で使う三角形頂点列を読み込む
};
