#pragma warning(disable : 5045)

#include "EditorNativeScriptAssetManager.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
	constexpr std::array<unsigned char, 3> kUtf8Bom{{0xEFu, 0xBBu, 0xBFu}};  // 保存時に先頭へ付ける UTF-8 BOM。
}

EditorNativeScriptAssetResult EditorNativeScriptAssetManager::CreateNativeScriptAsset(const std::string& requestedScriptName) {
	EditorNativeScriptAssetResult result{};
	result.sanitizedScriptName = SanitizeScriptName(requestedScriptName);

	if (result.sanitizedScriptName.empty()) {
		result.message = "C++ スクリプト名が空です。半角英数字で入力してください。";
		return result;
	}

	const std::filesystem::path scriptDirectoryPath =
		std::filesystem::path("resources") / "scripts" / result.sanitizedScriptName;

	std::error_code fileError;
	std::filesystem::create_directories(scriptDirectoryPath, fileError);
	if (fileError) {
		result.message = "script フォルダを作成できませんでした。";
		return result;
	}

	result.scriptDirectoryPath = scriptDirectoryPath.generic_string();
	result.headerFilePath = (scriptDirectoryPath / (result.sanitizedScriptName + ".h")).generic_string();
	result.sourceFilePath = (scriptDirectoryPath / (result.sanitizedScriptName + ".cpp")).generic_string();
	result.buildDebugFilePath = (scriptDirectoryPath / "build_debug.bat").generic_string();
	result.buildReleaseFilePath = (scriptDirectoryPath / "build_release.bat").generic_string();
	result.dllFilePath =
		(scriptDirectoryPath / "x64" / "Debug" / (result.sanitizedScriptName + ".dll")).generic_string();

	if (!WriteUtf8BomFile(result.headerFilePath, MakeHeaderText(result.sanitizedScriptName)) ||
		!WriteUtf8BomFile(result.sourceFilePath, MakeSourceText(result.sanitizedScriptName)) ||
		!WriteUtf8BomFile(result.buildDebugFilePath, MakeBuildScriptText(result.sanitizedScriptName, true)) ||
		!WriteUtf8BomFile(result.buildReleaseFilePath, MakeBuildScriptText(result.sanitizedScriptName, false))) {
		result.message = "C++ スクリプト雛形の保存に失敗しました。";
		return result;
	}

	result.isSucceeded = true;
	result.message = "C++ スクリプトを生成しました。build_debug.bat を実行すると DLL が作られます。";
	return result;
}

std::string EditorNativeScriptAssetManager::SanitizeScriptName(const std::string& requestedScriptName) {
	std::string sanitizedScriptName;
	sanitizedScriptName.reserve(requestedScriptName.size());

	for (const char letterValue : requestedScriptName) {
		const unsigned char letter = static_cast<unsigned char>(letterValue);
		const bool isNumber = letter >= '0' && letter <= '9';
		const bool isUpperAlphabet = letter >= 'A' && letter <= 'Z';
		const bool isLowerAlphabet = letter >= 'a' && letter <= 'z';
		const bool isUnderscore = letter == '_';

		if (isNumber || isUpperAlphabet || isLowerAlphabet || isUnderscore) {
			sanitizedScriptName.push_back(static_cast<char>(letter));
		}
	}

	if (!sanitizedScriptName.empty()) {
		const bool startsWithNumber = sanitizedScriptName.front() >= '0' && sanitizedScriptName.front() <= '9';
		if (startsWithNumber) {
			sanitizedScriptName.insert(sanitizedScriptName.begin(), '_');
		}
	}

	return sanitizedScriptName;
}

std::string EditorNativeScriptAssetManager::MakeHeaderText(const std::string& scriptName) {
	std::ostringstream headerText;
	headerText
		<< "#pragma once\r\n"
		<< "\r\n"
		<< "#include \"EditorScriptApi.h\"\r\n"
		<< "\r\n"
		<< "//================================================================\r\n"
		<< "// " << scriptName << " のユーザー編集用データ\r\n"
		<< "//================================================================\r\n"
		<< "\r\n"
		<< "class " << scriptName << " {\r\n"
		<< "public:\r\n"
		<< "\tfloat moveSpeed = 3.0f;  // Update で移動量を決める基本速度。\r\n"
		<< "\tfloat rotateSpeed = 1.0f;  // Update で回転量を決める基本速度。\r\n"
		<< "\tbool isStarted = false;  // Start が呼ばれたかどうかの状態。\r\n"
		<< "};\r\n";
	return headerText.str();
}

std::string EditorNativeScriptAssetManager::MakeSourceText(const std::string& scriptName) {
	std::ostringstream sourceText;
	sourceText
		<< "#include \"" << scriptName << ".h\"\r\n"
		<< "\r\n"
		<< "#include <string>\r\n"
		<< "#include <unordered_map>\r\n"
		<< "\r\n"
		<< "namespace {\r\n"
		<< "\tconst EditorScriptRuntimeApi* runtimeApi = nullptr;  // Editor 本体が渡す C++ 実行 API を保持する。\r\n"
		<< "\tstd::unordered_map<int32_t, " << scriptName << "> scriptStates;  // GameObject ごとの実行状態を保持する。\r\n"
		<< "\r\n"
		<< "\t" << scriptName << "& GetState(int32_t gameObjectId) {\r\n"
		<< "\t\treturn scriptStates[gameObjectId];\r\n"
		<< "\t}\r\n"
		<< "\r\n"
		<< "\tvoid LogActionPressed(int32_t gameObjectId, const char* actionMapName, const char* actionName) {\r\n"
		<< "\t\tstd::string logText = \"" << scriptName << "::Action self=\" + std::to_string(gameObjectId) +\r\n"
		<< "\t\t\t\" map=\" + actionMapName + \" action=\" + actionName;\r\n"
		<< "\t\truntimeApi->Log(logText.c_str());  // 任意の Action が押された時の確認用ログ。\r\n"
		<< "\t}\r\n"
		<< "}\r\n"
		<< "\r\n"
		<< "extern \"C\" __declspec(dllexport) bool EditorScript_Load(uint32_t apiVersion, const EditorScriptRuntimeApi* api) {\r\n"
		<< "\tif (apiVersion != kEditorScriptApiVersion || api == nullptr) {\r\n"
		<< "\t\treturn false;\r\n"
		<< "\t}\r\n"
		<< "\r\n"
		<< "\truntimeApi = api;  // 以後の Start / Update / FixedUpdate から使えるよう保存する。\r\n"
		<< "\treturn true;\r\n"
		<< "}\r\n"
		<< "\r\n"
		<< "extern \"C\" __declspec(dllexport) void EditorScript_Unload() {\r\n"
		<< "\tscriptStates.clear();  // DLL 再読込前に GameObject ごとの一時状態を破棄する。\r\n"
		<< "\truntimeApi = nullptr;\r\n"
		<< "}\r\n"
		<< "\r\n"
		<< "extern \"C\" __declspec(dllexport) void EditorScript_Start(int32_t gameObjectId) {\r\n"
		<< "\tif (runtimeApi == nullptr) {\r\n"
		<< "\t\treturn;\r\n"
		<< "\t}\r\n"
		<< "\r\n"
		<< "\t" << scriptName << "& state = GetState(gameObjectId);  // この GameObject 専用の設定を取り出す。\r\n"
		<< "\tstate.isStarted = true;\r\n"
		<< "\truntimeApi->Log(\"" << scriptName << "::Start\");\r\n"
		<< "}\r\n"
		<< "\r\n"
		<< "extern \"C\" __declspec(dllexport) void EditorScript_Update(int32_t gameObjectId, float deltaTime) {\r\n"
		<< "\tif (runtimeApi == nullptr) {\r\n"
		<< "\t\treturn;\r\n"
		<< "\t}\r\n"
		<< "\r\n"
		<< "\t" << scriptName << "& state = GetState(gameObjectId);  // Update 内で使う速度などを持つ状態。\r\n"
		<< "\tEditorScriptTransform transform = runtimeApi->GetTransform(gameObjectId);  // Editor 側の Transform を取得する。\r\n"
		<< "\tconst EditorScriptVector2 moveInput = runtimeApi->GetActionVector2(gameObjectId, \"Player\", \"Move\");  // .inputactions の Player/Move を読む。\r\n"
		<< "\r\n"
		<< "\t//============================================================\r\n"
		<< "\t// C++ で書き換える場所\r\n"
		<< "\t// 例1: Player/Move を使って XZ 平面を移動する\r\n"
		<< "\t// 例2: .inputactions に Action|Player|Dash|Button|Key|LeftShift を追加したら\r\n"
		<< "\t//       runtimeApi->WasActionJustPressed(gameObjectId, \"Player\", \"Dash\") で受ける\r\n"
		<< "\t// 例3: マウス座標が必要な時は runtimeApi->GetMousePosition() を使う\r\n"
		<< "\t//============================================================\r\n"
		<< "\ttransform.position.x += moveInput.x * state.moveSpeed * deltaTime;\r\n"
		<< "\ttransform.position.z += moveInput.y * state.moveSpeed * deltaTime;\r\n"
		<< "\r\n"
		<< "\tif (runtimeApi->WasActionJustPressed(gameObjectId, \"Player\", \"Jump\")) {\r\n"
		<< "\t\tLogActionPressed(gameObjectId, \"Player\", \"Jump\");  // 既定で入る Jump の受け取り例。\r\n"
		<< "\t}\r\n"
		<< "\r\n"
		<< "\tif (runtimeApi->WasActionJustPressed(gameObjectId, \"Player\", \"Fire\")) {\r\n"
		<< "\t\tconst EditorScriptVector2 mousePosition = runtimeApi->GetMousePosition();  // 既定で入る Fire とマウス位置の利用例。\r\n"
		<< "\t\tstd::string logText = \"" << scriptName << "::Fire mouse=(\" + std::to_string(mousePosition.x) + \", \" + std::to_string(mousePosition.y) + \")\";\r\n"
		<< "\t\truntimeApi->Log(logText.c_str());\r\n"
		<< "\t}\r\n"
		<< "\r\n"
		<< "\tif (runtimeApi->IsKeyDown(EditorScriptKeyCodeQ)) {\r\n"
		<< "\t\ttransform.rotation.y -= state.rotateSpeed * deltaTime;  // 旧来のキー直書き入力も必要なら併用できる。\r\n"
		<< "\t}\r\n"
		<< "\r\n"
		<< "\tif (runtimeApi->IsKeyDown(EditorScriptKeyCodeE)) {\r\n"
		<< "\t\ttransform.rotation.y += state.rotateSpeed * deltaTime;\r\n"
		<< "\t}\r\n"
		<< "\r\n"
		<< "\truntimeApi->SetTransform(gameObjectId, &transform);  // 計算後の Transform を Editor 側へ戻す。\r\n"
		<< "}\r\n"
		<< "\r\n"
		<< "extern \"C\" __declspec(dllexport) void EditorScript_FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) {\r\n"
		<< "\tif (runtimeApi == nullptr) {\r\n"
		<< "\t\treturn;\r\n"
		<< "\t}\r\n"
		<< "\r\n"
		<< "\t(void)gameObjectId;  // 今は未使用だが、AddForce や SetVelocity を書く時に使う。\r\n"
		<< "\t(void)fixedDeltaTime;\r\n"
		<< "}\r\n"
		<< "\r\n"
		<< "extern \"C\" __declspec(dllexport) void EditorScript_OnPhysicsEvent(int32_t gameObjectId, const EditorScriptPhysicsEvent* physicsEvent) {\r\n"
		<< "\tif (runtimeApi == nullptr || physicsEvent == nullptr) {\r\n"
		<< "\t\treturn;\r\n"
		<< "\t}\r\n"
		<< "\r\n"
		<< "\tif (physicsEvent->type == EditorScriptPhysicsEventTypeCollisionEnter) {\r\n"
		<< "\t\tstd::string logText = \"" << scriptName << "::CollisionEnter self=\" + std::to_string(gameObjectId);\r\n"
		<< "\t\truntimeApi->Log(logText.c_str());\r\n"
		<< "\t}\r\n"
		<< "}\r\n"
		<< "\r\n"
		<< "extern \"C\" __declspec(dllexport) void EditorScript_Stop(int32_t gameObjectId) {\r\n"
		<< "\tscriptStates.erase(gameObjectId);  // Play 停止や DLL 差し替え時に、この GameObject の一時状態を破棄する。\r\n"
		<< "\t(void)gameObjectId;\r\n"
		<< "}\r\n";
	return sourceText.str();
}

std::string EditorNativeScriptAssetManager::MakeBuildScriptText(const std::string& scriptName, bool isDebug) {
	std::ostringstream buildScriptText;
	const char* configurationDirectory = isDebug ? "Debug" : "Release";
	const char* runtimeOption = isDebug ? "/MDd" : "/MD";
	const char* optimizationOption = isDebug ? "/Od /Zi" : "/O2";

	buildScriptText
		<< "@echo off\r\n"
		<< "setlocal\r\n"
		<< "\r\n"
		<< "set SCRIPT_DIR=%~dp0\r\n"
		<< "set VSWHERE=%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe\r\n"
		<< "for /f \"usebackq delims=\" %%i in (`\"%VSWHERE%\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSINSTALL=%%i\r\n"
		<< "if \"%VSINSTALL%\"==\"\" exit /b 1\r\n"
		<< "call \"%VSINSTALL%\\Common7\\Tools\\VsDevCmd.bat\" -arch=x64\r\n"
		<< "if errorlevel 1 exit /b 1\r\n"
		<< "\r\n"
		<< "if not exist \"%SCRIPT_DIR%x64\\" << configurationDirectory << "\" mkdir \"%SCRIPT_DIR%x64\\" << configurationDirectory << "\"\r\n"
		<< "\r\n"
		<< "cl /nologo /std:c++20 /EHsc " << runtimeOption << " " << optimizationOption
		<< " /LD /I \"%SCRIPT_DIR%..\\..\\..\" \"%SCRIPT_DIR%" << scriptName << ".cpp\" "
		<< "/Fe:\"%SCRIPT_DIR%x64\\" << configurationDirectory << "\\" << scriptName << ".dll\"\r\n";

	return buildScriptText.str();
}

bool EditorNativeScriptAssetManager::WriteUtf8BomFile(const std::string& filePath, const std::string& text) {
	std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}

	file.write(reinterpret_cast<const char*>(kUtf8Bom.data()), static_cast<std::streamsize>(kUtf8Bom.size()));
	file.write(text.data(), static_cast<std::streamsize>(text.size()));
	return file.good();
}
