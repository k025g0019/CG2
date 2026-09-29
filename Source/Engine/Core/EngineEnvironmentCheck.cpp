#include "EngineEnvironmentCheck.h"

#include "EngineVersion.h"

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <sstream>

namespace {
	void AddDllCheck(EnvironmentCheckReport& report, const std::filesystem::path& root,
		const wchar_t* fileName, const char* label, bool required) {
		const std::filesystem::path path = root / fileName;
		std::error_code error;
		const bool exists = std::filesystem::exists(path, error);
		report.items.push_back({label, exists, required,
			exists ? path.generic_string() : "不足: " + path.generic_string()});
	}

	void AddEnginePathCheck(EnvironmentCheckReport& report, const std::filesystem::path& root,
		const std::filesystem::path& relativePath, const char* label, bool directory = false) {
		const std::filesystem::path path = root / relativePath;
		std::error_code error;
		const bool exists = directory
			? std::filesystem::is_directory(path, error)
			: std::filesystem::is_regular_file(path, error);
		report.items.push_back({label, exists, true,
			exists ? path.generic_string() : "不足: " + path.generic_string()});
	}

	bool CanLoadSystemLibrary(const wchar_t* name) {
		HMODULE module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		if (module == nullptr) return false;
		FreeLibrary(module);
		return true;
	}

	std::filesystem::path FindMSBuild() {
		wchar_t pathBuffer[32768]{};
		const DWORD length = SearchPathW(nullptr, L"MSBuild.exe", nullptr,
			_countof(pathBuffer), pathBuffer, nullptr);
		if (length > 0U && length < _countof(pathBuffer)) return pathBuffer;

		// MSBuild is normally installed with Visual Studio and is not added to PATH.
		for (const wchar_t* edition : {L"Community", L"Professional", L"Enterprise", L"BuildTools"}) {
			const std::filesystem::path candidate =
				std::filesystem::path(L"C:/Program Files/Microsoft Visual Studio/2022") /
				edition / L"MSBuild/Current/Bin/MSBuild.exe";
			std::error_code error;
			if (std::filesystem::exists(candidate, error)) return candidate;
		}
		return {};
	}
}

bool EnvironmentCheckReport::HasRequiredFailure() const {
	for (const EnvironmentCheckItem& item : items) if (item.required && !item.succeeded) return true;
	return false;
}

std::string EnvironmentCheckReport::ToText() const {
	std::ostringstream text;
	for (const EnvironmentCheckItem& item : items) {
		text << (item.succeeded ? "[正常] " : item.required ? "[エラー] " : "[警告] ")
			<< item.name << ": " << item.detail << '\n';
	}
	return text.str();
}

EnvironmentCheckReport EngineEnvironmentCheck::Run(const std::filesystem::path& engineDirectory,
	EnvironmentCheckMode mode) {
	EnvironmentCheckReport report{};
	OSVERSIONINFOW versionInfo{};
	versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
	using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	auto rtlGetVersion = reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion"));
	const bool versionRead = rtlGetVersion != nullptr && rtlGetVersion(&versionInfo) == 0;
	report.items.push_back({"Windowsバージョン", versionRead && versionInfo.dwMajorVersion >= 10U, true,
		versionRead ? std::to_string(versionInfo.dwMajorVersion) + "." + std::to_string(versionInfo.dwMinorVersion) +
			" ビルド " + std::to_string(versionInfo.dwBuildNumber) : "取得できません"});

	const bool vcRuntime = CanLoadSystemLibrary(L"vcruntime140.dll") && CanLoadSystemLibrary(L"msvcp140.dll");
	report.items.push_back({"Visual C++ランタイム", vcRuntime, true, vcRuntime ? "vcruntime140/msvcp140" : "VC++ 2015-2022 x64ランタイムが必要です"});

	bool directX12 = false;
	HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (d3d12 != nullptr) {
		using CreateDeviceFunction = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
		auto createDevice = reinterpret_cast<CreateDeviceFunction>(GetProcAddress(d3d12, "D3D12CreateDevice"));
		if (createDevice != nullptr) {
			// 作れるかどうかを見るだけの Device。ComPtr が抜けるときに手放す。
			Microsoft::WRL::ComPtr<ID3D12Device> device;
			directX12 = SUCCEEDED(createDevice(
				nullptr,
				D3D_FEATURE_LEVEL_11_0,
				__uuidof(ID3D12Device),
				reinterpret_cast<void**>(device.GetAddressOf())));
		}
		FreeLibrary(d3d12);
	}
	report.items.push_back({"DirectX 12", directX12, true, directX12 ? "D3D12デバイス作成成功" : "対応GPU・ドライバーを確認してください"});

	AddDllCheck(report, engineDirectory, L"dxcompiler.dll", "DXコンパイラーDLL", true);
	AddDllCheck(report, engineDirectory, L"dxil.dll", "DXIL DLL", true);
	AddDllCheck(report, engineDirectory, L"libfbxsdk.dll", "FBXランタイムDLL", true);
	AddDllCheck(report, engineDirectory, L"onnxruntime.dll", "ONNXランタイムDLL", true);
	for (const wchar_t* dll : {L"PhysX_64.dll", L"PhysXCommon_64.dll", L"PhysXFoundation_64.dll", L"PhysXCooking_64.dll"})
		AddDllCheck(report, engineDirectory, dll, "物理演算DLL", true);
	AddDllCheck(report, engineDirectory, L"NvBlast.dll", "BlastランタイムDLL", true);
	for (const wchar_t* dll : {L"behaviortree_cpp.dll", L"minitrace.dll", L"NvBlastExtAuthoring.dll",
		L"NvBlastGlobals.dll", L"PhysXGpu_64.dll", L"tinyxml2.dll"})
		AddDllCheck(report, engineDirectory, dll, "Engine追加ランタイムDLL", true);
	AddEnginePathCheck(report, engineDirectory, L"CG2TeamServer.exe", "共同制作Server");
	AddEnginePathCheck(report, engineDirectory, L"Assets/Shaders", "Engine Shader一式", true);
	AddEnginePathCheck(report, engineDirectory, L"Assets/Shaders/Object3d.VS.hlsl", "基本描画Shader");
	AddEnginePathCheck(report, engineDirectory, L"Assets/Shaders/PostProcess/Sharpen.PS.hlsl", "Sharpen Shader");
	AddEnginePathCheck(report, engineDirectory, L"Assets/Shaders/lygia", "Lygia Shader Library", true);
	AddEnginePathCheck(report, engineDirectory, L"Assets/Shaders/FidelityFX", "FidelityFX Shader Library", true);
	AddEnginePathCheck(report, engineDirectory, L"resources/editorDefault", "Editor標準Resource", true);
	AddEnginePathCheck(report, engineDirectory, L"resources/editorDefault/uvChecker.png", "Editor標準Texture");
	report.items.push_back({"スクリプトAPIバージョン", GetCG2ScriptApiVersion() > 0U, true,
		std::to_string(GetCG2ScriptApiVersion())});

	if (mode == EnvironmentCheckMode::EngineDeveloper) {
		const std::filesystem::path msBuild = FindMSBuild();
		report.items.push_back({"MSBuild", !msBuild.empty(), true,
			!msBuild.empty() ? msBuild.generic_string() : "Visual Studio Build Toolsが必要です"});
		const std::filesystem::path root = engineDirectory.parent_path().parent_path();
		const bool solutionExists = std::filesystem::exists(root / "CG2.sln");
		report.items.push_back({"エンジンソース", solutionExists, true, solutionExists ? root.generic_string() : "CG2.slnがありません"});
		const bool physicsSetup = std::filesystem::exists(root / "Build/PhysicsSdk/PhysicsSdk.props");
		report.items.push_back({"物理SDKセットアップ", physicsSetup, true,
			physicsSetup ? "PhysicsSdk.props検出" : "Build/PhysicsSdk/Setup.ps1を実行してください"});
		const bool fbxSdk = std::filesystem::exists(L"C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.9/include/fbxsdk.h");
		report.items.push_back({"FBX SDK 2020.3.9", fbxSdk, true, fbxSdk ? "検出" : "開発用SDKがありません"});
	}
	return report;
}
