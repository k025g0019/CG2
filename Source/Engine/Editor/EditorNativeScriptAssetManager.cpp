#include "EditorNativeScriptAssetManager.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
	constexpr std::array<unsigned char, 3> kUtf8Bom{{0xEFu, 0xBBu, 0xBFu}};  // 保存時に先頭へ付ける UTF-8 BOM。

	void ReplaceAll(std::string& text, const std::string& oldText, const std::string& newText) {
		size_t replacePosition = 0U;

		while ((replacePosition = text.find(oldText, replacePosition)) != std::string::npos) {
			text.replace(replacePosition, oldText.size(), newText);
			replacePosition += newText.size();
		}
	}
}

EditorNativeScriptAssetResult EditorNativeScriptAssetManager::CreateNativeScriptAsset(
	const std::string& requestedScriptName,
	bool isDebugBuild) {
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
		(scriptDirectoryPath /
		 "x64" /
		 (isDebugBuild ? "Debug" : "Release") /
		 (result.sanitizedScriptName + ".dll"))
			.generic_string();

	const bool isHeaderWritten = WriteUtf8BomFile(result.headerFilePath, MakeHeaderText(result.sanitizedScriptName));
	const bool isSourceWritten = WriteUtf8BomFile(result.sourceFilePath, MakeSourceText(result.sanitizedScriptName));
	const bool isDebugBuildFileWritten =
		WriteUtf8BomFile(result.buildDebugFilePath, MakeBuildScriptText(result.sanitizedScriptName, true));
	const bool isReleaseBuildFileWritten =
		WriteUtf8BomFile(result.buildReleaseFilePath, MakeBuildScriptText(result.sanitizedScriptName, false));

	if (!isHeaderWritten || !isSourceWritten || !isDebugBuildFileWritten || !isReleaseBuildFileWritten) {
		result.message = "C++ スクリプト雛形の保存に失敗しました。";
		return result;
	}

	result.isSucceeded = true;
	result.message = isDebugBuild
		? "C++ スクリプトを生成しました。build_debug.bat を実行すると DLL が作られます。"
		: "C++ スクリプトを生成しました。build_release.bat を実行すると DLL が作られます。";
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
	std::string headerText = R"SCRIPT(#pragma once

#include "EditorNativeScript.h"

#include <string>

//================================================================
// __SCRIPT_NAME__ - GameObject へ追加する C++ Component
//================================================================

class __SCRIPT_NAME__ final : public EditorNativeScript {
public:
	__SCRIPT_NAME__();  // 公開変数と Input Action 関数を登録する。

	void Start(int32_t gameObjectId) override;
	void Update(int32_t gameObjectId, float deltaTime) override;
	void FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) override;
	void OnCollisionEnter(const EditorScriptPhysicsEvent& physicsEvent) override;
	void OnTriggerEnter(const EditorScriptPhysicsEvent& physicsEvent) override;
	void Stop(int32_t gameObjectId) override;

private:
	float moveSpeed_ = 3.0f;  // Inspector から編集する移動速度。
	float jumpImpulse_ = 5.0f;  // Inspector から編集するジャンプの瞬間力。
	std::string startMessage_ = "__SCRIPT_NAME__::Start";  // Inspector から編集する開始ログ。
	EditorScriptVector2 moveInput_{};  // OnMove が受けた入力を Update まで保持する。

	void OnMove(const EditorScriptInputActionContext& inputContext);
	void OnJump(const EditorScriptInputActionContext& inputContext);
	void OnFire(const EditorScriptInputActionContext& inputContext);
};
)SCRIPT";
	ReplaceAll(headerText, "__SCRIPT_NAME__", scriptName);
	return headerText;
}

std::string EditorNativeScriptAssetManager::MakeSourceText(const std::string& scriptName) {
	std::string sourceText = R"SCRIPT(#include "__SCRIPT_NAME__.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace {
	const EditorScriptRuntimeApi* runtimeApi = nullptr;  // Editor 本体が渡す実行 API。
	std::unordered_map<int32_t, std::unique_ptr<__SCRIPT_NAME__>> scriptStates;  // GameObject ごとの C++ Component。

	__SCRIPT_NAME__& GetState(int32_t gameObjectId) {
		std::unique_ptr<__SCRIPT_NAME__>& scriptState = scriptStates[gameObjectId];

		if (scriptState == nullptr) {
			scriptState = std::make_unique<__SCRIPT_NAME__>();
		}

		return *scriptState;
	}

	__SCRIPT_NAME__& GetMetadataState() {
		static __SCRIPT_NAME__ metadataState;  // Play 前の Inspector が型情報だけを取得する。
		return metadataState;
	}
}

//================================================================
// ユーザーが編集する C++ Component 本体
//================================================================

__SCRIPT_NAME__::__SCRIPT_NAME__() {
	ExposeFloat("moveSpeed", "移動速度", moveSpeed_, 0.0f, 100.0f, 0.1f);
	ExposeFloat("jumpImpulse", "ジャンプ力", jumpImpulse_, 0.0f, 100.0f, 0.1f);
	ExposeString("startMessage", "開始メッセージ", startMessage_);

	BindAction("OnMove", [this](const EditorScriptInputActionContext& inputContext) { OnMove(inputContext); });
	BindAction("OnJump", [this](const EditorScriptInputActionContext& inputContext) { OnJump(inputContext); });
	BindAction("OnFire", [this](const EditorScriptInputActionContext& inputContext) { OnFire(inputContext); });
}

void __SCRIPT_NAME__::Start(int32_t gameObjectId) {
	(void)gameObjectId;

	if (runtimeApi != nullptr) {
		runtimeApi->Log(startMessage_.c_str());
	}
}

void __SCRIPT_NAME__::Update(int32_t gameObjectId, float deltaTime) {
	if (runtimeApi == nullptr) {
		return;
	}

	EditorScriptTransform transform = runtimeApi->GetTransform(gameObjectId);
	transform.position.x += moveInput_.x * moveSpeed_ * deltaTime;
	transform.position.z += moveInput_.y * moveSpeed_ * deltaTime;
	runtimeApi->SetTransform(gameObjectId, &transform);
}

void __SCRIPT_NAME__::FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) {
	(void)gameObjectId;
	(void)fixedDeltaTime;  // AddForce など周期を固定した物理処理を書く。
}

void __SCRIPT_NAME__::OnCollisionEnter(const EditorScriptPhysicsEvent& physicsEvent) {
	if (runtimeApi != nullptr) {
		const std::string message = "OnCollisionEnter: other=" + std::to_string(physicsEvent.otherGameObjectId);
		runtimeApi->Log(message.c_str());
	}
}

void __SCRIPT_NAME__::OnTriggerEnter(const EditorScriptPhysicsEvent& physicsEvent) {
	if (runtimeApi != nullptr) {
		const std::string message = "OnTriggerEnter: other=" + std::to_string(physicsEvent.otherGameObjectId);
		runtimeApi->Log(message.c_str());
	}
}

void __SCRIPT_NAME__::Stop(int32_t gameObjectId) {
	(void)gameObjectId;
	moveInput_ = {};
}

void __SCRIPT_NAME__::OnMove(const EditorScriptInputActionContext& inputContext) {
	moveInput_ = inputContext.phase == EditorScriptInputPhaseCanceled
		? EditorScriptVector2{}
		: inputContext.vector2Value;
}

void __SCRIPT_NAME__::OnJump(const EditorScriptInputActionContext& inputContext) {
	if (runtimeApi == nullptr || inputContext.phase != EditorScriptInputPhasePerformed) {
		return;
	}

	const EditorScriptVector3 jumpImpulse{0.0f, jumpImpulse_, 0.0f};
	runtimeApi->AddImpulse(inputContext.gameObjectId, &jumpImpulse);
}

void __SCRIPT_NAME__::OnFire(const EditorScriptInputActionContext& inputContext) {
	if (runtimeApi != nullptr && inputContext.phase == EditorScriptInputPhasePerformed) {
		runtimeApi->Log("OnFire");
	}
}

//================================================================
// Editor と C++ Component を接続する DLL ABI
//================================================================

extern "C" __declspec(dllexport) bool EditorScript_Load(
	uint32_t apiVersion,
	const EditorScriptRuntimeApi* api) {
	if (apiVersion != kEditorScriptApiVersion || api == nullptr) {
		return false;
	}

	runtimeApi = api;
	return true;
}

extern "C" __declspec(dllexport) void EditorScript_Unload() {
	scriptStates.clear();
	runtimeApi = nullptr;
}

extern "C" __declspec(dllexport) void EditorScript_Start(int32_t gameObjectId) {
	GetState(gameObjectId).Start(gameObjectId);
}

extern "C" __declspec(dllexport) void EditorScript_Update(int32_t gameObjectId, float deltaTime) {
	GetState(gameObjectId).Update(gameObjectId, deltaTime);
}

extern "C" __declspec(dllexport) void EditorScript_FixedUpdate(int32_t gameObjectId, float fixedDeltaTime) {
	GetState(gameObjectId).FixedUpdate(gameObjectId, fixedDeltaTime);
}

extern "C" __declspec(dllexport) void EditorScript_OnPhysicsEvent(
	int32_t gameObjectId,
	const EditorScriptPhysicsEvent* physicsEvent) {
	if (physicsEvent == nullptr) {
		return;
	}

	GetState(gameObjectId).DispatchPhysicsEvent(*physicsEvent);
}

extern "C" __declspec(dllexport) void EditorScript_Stop(int32_t gameObjectId) {
	const auto scriptStateIt = scriptStates.find(gameObjectId);

	if (scriptStateIt != scriptStates.end()) {
		scriptStateIt->second->Stop(gameObjectId);
		scriptStates.erase(scriptStateIt);
	}
}

extern "C" __declspec(dllexport) int32_t EditorScript_GetFieldCount() {
	return GetMetadataState().GetFieldCount();
}

extern "C" __declspec(dllexport) bool EditorScript_GetFieldDescriptor(
	int32_t fieldIndex,
	EditorScriptFieldDescriptor* fieldDescriptor) {
	return fieldDescriptor != nullptr && GetMetadataState().GetFieldDescriptor(fieldIndex, *fieldDescriptor);
}

extern "C" __declspec(dllexport) bool EditorScript_GetFieldValue(
	int32_t gameObjectId,
	const char* fieldName,
	EditorScriptFieldValue* fieldValue) {
	return fieldValue != nullptr && GetState(gameObjectId).GetFieldValue(fieldName, *fieldValue);
}

extern "C" __declspec(dllexport) bool EditorScript_SetFieldValue(
	int32_t gameObjectId,
	const char* fieldName,
	const EditorScriptFieldValue* fieldValue) {
	return fieldValue != nullptr && GetState(gameObjectId).SetFieldValue(fieldName, *fieldValue);
}

extern "C" __declspec(dllexport) bool EditorScript_InvokeAction(
	int32_t gameObjectId,
	const char* functionName,
	const EditorScriptInputActionContext* inputContext) {
	return inputContext != nullptr && GetState(gameObjectId).InvokeAction(functionName, *inputContext);
}
)SCRIPT";
	ReplaceAll(sourceText, "__SCRIPT_NAME__", scriptName);
	return sourceText;
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
		<< "pushd \"%~dp0\"\r\n"
		<< "set \"SCRIPT_DIR=%CD%\"\r\n"
		<< "set \"PROJECT_ROOT=%SCRIPT_DIR%\\..\\..\\..\"\r\n"
		<< "set \"VSWHERE=%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe\"\r\n"
		<< "for /f \"usebackq delims=\" %%i in (`\"%VSWHERE%\" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSINSTALL=%%i\r\n"
		<< "if \"%VSINSTALL%\"==\"\" exit /b 1\r\n"
		<< "call \"%VSINSTALL%\\Common7\\Tools\\VsDevCmd.bat\" -arch=x64\r\n"
		<< "if errorlevel 1 exit /b 1\r\n"
		<< "\r\n"
		<< "if not exist \"%SCRIPT_DIR%\\x64\\" << configurationDirectory
		<< "\" mkdir \"%SCRIPT_DIR%\\x64\\" << configurationDirectory << "\"\r\n"
		<< "\r\n"
		<< "cl /nologo /utf-8 /std:c++20 /EHsc " << runtimeOption << " " << optimizationOption
		<< " /LD /I \"%PROJECT_ROOT%\\Source\\Engine\\Core\" /I \"%PROJECT_ROOT%\" \"%SCRIPT_DIR%\\"
		<< scriptName << ".cpp\" /Fe:\"%SCRIPT_DIR%\\x64\\" << configurationDirectory << "\\"
		<< scriptName << ".dll\"\r\n"
		<< "popd\r\n";

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
