#pragma once

#include <string>

#pragma warning(push)
#pragma warning(disable : 4820)

struct EditorNativeScriptAssetResult {
	bool isSucceeded = false;  // 生成が最後まで成功したかどうか。
	std::string sanitizedScriptName;  // クラス名と DLL 名に使えるよう整形した名前。
	std::string scriptDirectoryPath;  // 生成した script フォルダ。
	std::string headerFilePath;  // 生成した .h の相対パス。
	std::string sourceFilePath;  // 生成した .cpp の相対パス。
	std::string buildDebugFilePath;  // 生成した Debug ビルド bat の相対パス。
	std::string buildReleaseFilePath;  // 生成した Release ビルド bat の相対パス。
	std::string dllFilePath;  // Script Component に割り当てる想定 DLL パス。
	std::string message;  // GUI に出す成否メッセージ。
};

class EditorNativeScriptAssetManager {
public:
	static EditorNativeScriptAssetResult CreateNativeScriptAsset(const std::string& requestedScriptName);  // GUI から C++ Script 雛形をまとめて生成する。

private:
	static std::string SanitizeScriptName(const std::string& requestedScriptName);  // クラス名や DLL 名に使えない文字を除去する。
	static std::string MakeHeaderText(const std::string& scriptName);  // DLL 側の状態クラスを生成する .h テンプレート。
	static std::string MakeSourceText(const std::string& scriptName);  // EditorScriptApi を使う .cpp テンプレート。
	static std::string MakeBuildScriptText(const std::string& scriptName, bool isDebug);  // DLL を cl /LD で作る bat テンプレート。
	static bool WriteUtf8BomFile(const std::string& filePath, const std::string& text);  // 文字化け防止のため UTF-8 BOM 付きで保存する。
};

#pragma warning(pop)
