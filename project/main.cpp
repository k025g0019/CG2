#pragma warning(push, 0)
#include <Windows.h>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <d3d12.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <xaudio2.h>
#pragma warning(disable : 4820)
#include <filesystem>
#include <format>
#include <fstream>
#include <sdkddkver.h>
#include <sstream>
#include <string>
#include <vector>
#include <wrl.h>
#pragma warning(pop)
#pragma warning(disable : 4820)

#include <execution>

#include "2d/SpriteCommon.h"
#include "3d/ParticleEffect.h"
#include "Application/GameScene.h"
#include "base/CrashHandler.h"
#include "base/DirectXCommon.h"
#include "base/TextureManager.h"
#include "base/WinApp.h"
#include "debug/Imgui.h"
#include "debug/Log.h"
#include "input/Input.h"
#include "math/Matrix.h"
#include "math/Vector&Matrix.h"
#include "math/Vector.h"
#include "motion/StringUtility.h"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "xaudio2.lib")

using Microsoft::WRL::ComPtr;
# pragma once
constexpr double PI = 3.14159265358979323846;

#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#pragma warning(pop)
#endif

namespace {
	//========================================
	// 描画データ定義
	//========================================


	//------------------------------
	// 頂点・マテリアルデータ
	//------------------------------

	// 変換行列データ
	struct Transforms {
		Vector3 scale;
		Vector3 rotate;
		Vector3 translate;
	};

	// 頂点データ
	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};

	// マテリアルデータ
	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float padding[3];
		Matrix4x4 uvTransform;
	};

	// ライトデータ
	struct DirectionalLight {
		Vector4 color;
		Vector3 direction;
		float intensity;
	};

	//------------------------------
	// モデル読み込みデータ
	//------------------------------
	struct MaterialData {
		std::string textureFilePath;
	};

	struct ModelData {
		std::vector<VertexData> vertices;
		MaterialData material;
	};

	struct CameraForGPU {
		Vector3 worldPosition;
	};

	//========================================
	// 音声データ定義
	//========================================


	// WAVEファイル構造体定義

	struct ChunkHeader {
		char id[4];
		int32_t size;
	};

	// RIFFヘッダ構造体定義
	struct RiffHeader {
		ChunkHeader chunk;
		char type[4];
	};

	// fmtチャンク構造体定義
	struct FormatChunk {
		ChunkHeader chunk;
		WAVEFORMATEX format;
	};

	// dataチャンク構造体定義
	struct SoundData {
		WAVEFORMATEX wfex;
		BYTE* pBuffer;
		uint32_t bufferSize;
	};

	//========================================
	// 補助関数宣言
	//========================================

	//------------------------------
	// GPUリソース・モデル読み込み
	//------------------------------
	ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes);
	MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
	ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

	//------------------------------
	// 音声読み込み
	//------------------------------
	SoundData SoundLoadWave(const char* filePath);
	void SoundUnload(SoundData* soundData);

	//------------------------------
	// 汎用バッファ生成
	//------------------------------
	ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
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
		HRESULT hr = device->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&resource));
		assert(SUCCEEDED(hr));

		return resource;
	}

	//========================================
	// シェーダーコンパイル処理
	//========================================
	ComPtr<IDxcBlob> CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile,
		IDxcUtils* dxcUtils,
		IDxcCompiler3* dxcCompiler,
		IDxcIncludeHandler* includeHandler,
		std::ofstream& logStream) {
		Log(logStream, std::format("Begin CompileShader, path:{}, profile:{}",
		                           StringUtility::ConvertString(filePath),
		                           StringUtility::ConvertString(std::wstring{profile})));

		ComPtr<IDxcBlobEncoding> shaderSource;
		HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, shaderSource.GetAddressOf());
		assert(SUCCEEDED(hr));

		DxcBuffer shaderSourceBuffer{};
		shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
		shaderSourceBuffer.Size = shaderSource->GetBufferSize();
		shaderSourceBuffer.Encoding = DXC_CP_UTF8;

		LPCWSTR arguments[] = {
			filePath.c_str(),
			L"-E", L"main",
			L"-T", profile,
			L"-Zi", L"-Qembed_debug",
			L"-Od",
			L"-Zpr"
		};

		ComPtr<IDxcResult> shaderResult;
		hr = dxcCompiler->Compile(
			&shaderSourceBuffer,
			arguments,
			_countof(arguments),
			includeHandler,
			IID_PPV_ARGS(shaderResult.GetAddressOf()));
		assert(SUCCEEDED(hr));


		ComPtr<IDxcBlobUtf8> shaderError;
		shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(shaderError.GetAddressOf()), nullptr);
		if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
			Log(shaderError->GetStringPointer());
			assert(false);
		}

		ComPtr<IDxcBlob> shaderBlob;
		hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shaderBlob.GetAddressOf()), nullptr);
		assert(SUCCEEDED(hr));

		Log(logStream, std::format("Compile Succeeded, path:{}, profile:{}",
		                           StringUtility::ConvertString(filePath),
		                           StringUtility::ConvertString(std::wstring{profile})));

		return shaderBlob;
	}

	//========================================
	// モデル読み込み処理
	//========================================

	//------------------------------
	// MTL読み込み
	//------------------------------
	MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
		MaterialData materialData{};

		std::ifstream file(directoryPath + "/" + filename);
		assert(file.is_open());

		std::string line;
		while (std::getline(file, line)) {
			std::string identifier;
			std::istringstream lineStream(line);
			lineStream >> identifier;

			if (identifier == "map_Kd") {
				lineStream >> materialData.textureFilePath;
				materialData.textureFilePath = directoryPath + "/" + materialData.textureFilePath;
			}
		}

		return materialData;
	}

	//------------------------------
	// OBJ読み込み
	//------------------------------
	ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
		ModelData modelData{};
		std::ifstream file(directoryPath + "/" + filename);
		assert(file.is_open());

		std::vector<Vector4> positions;
		std::vector<Vector2> texcoords;
		std::vector<Vector3> normals;
		std::string line;

		while (std::getline(file, line)) {
			std::string identifier;
			std::istringstream lineStream(line);
			lineStream >> identifier;

			if (identifier == "v") {
				Vector4 position{};
				lineStream >> position.x >> position.y >> position.z;
				position.x *= -1.0f;
				position.w = 1.0f;
				positions.push_back(position);
			}
			else if (identifier == "vt") {
				Vector2 texcoord{};
				lineStream >> texcoord.x >> texcoord.y;
				texcoord.y = 1.0f - texcoord.y;
				texcoords.push_back(texcoord);
			}
			else if (identifier == "vn") {
				Vector3 normal{};
				lineStream >> normal.x >> normal.y >> normal.z;
				normal.x *= -1.0f;
				normals.push_back(normal);
			}
			else if (identifier == "f") {
				VertexData triangle[3]{};
				for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
					std::string vertexDefinition;
					lineStream >> vertexDefinition;

					std::istringstream vertexStream(vertexDefinition);
					std::string positionIndexString;
					std::string texcoordIndexString;
					std::string normalIndexString;
					std::getline(vertexStream, positionIndexString, '/');
					std::getline(vertexStream, texcoordIndexString, '/');
					std::getline(vertexStream, normalIndexString, '/');

					uint32_t positionIndex = static_cast<uint32_t>(std::stoi(positionIndexString)) - 1;
					uint32_t texcoordIndex = static_cast<uint32_t>(std::stoi(texcoordIndexString)) - 1;
					uint32_t normalIndex = static_cast<uint32_t>(std::stoi(normalIndexString)) - 1;

					triangle[faceVertex].position = positions[positionIndex];
					triangle[faceVertex].texcoord = texcoords[texcoordIndex];
					triangle[faceVertex].normal = normals[normalIndex];
				}

				modelData.vertices.push_back(triangle[2]);
				modelData.vertices.push_back(triangle[1]);
				modelData.vertices.push_back(triangle[0]);
			}
			else if (identifier == "mtllib") {
				std::string materialFilename;
				lineStream >> materialFilename;
				modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
			}
		}

		return modelData;
	}


	//========================================
	// WAV音声管理処理
	//========================================

	//------------------------------
	// WAV読み込み
	//------------------------------
	// RIFF/WAVEの各チャンクを読み、XAudio2へ渡せる音声データを作る。
	SoundData SoundLoadWave(const char* filePath) {
		SoundData soundData{};

		std::ifstream file(filePath, std::ios_base::binary);
		assert(file.is_open());

		RiffHeader riff{};
		file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
		assert(std::strncmp(riff.chunk.id, "RIFF", 4) == 0);
		assert(std::strncmp(riff.type, "WAVE", 4) == 0);

		FormatChunk format{};
		file.read(reinterpret_cast<char*>(&format), sizeof(ChunkHeader));
		assert(std::strncmp(format.chunk.id, "fmt ", 4) == 0);
		assert(format.chunk.size >= 0);
		assert(static_cast<size_t>(format.chunk.size) <= sizeof(format.format));
		file.read(reinterpret_cast<char*>(&format.format), format.chunk.size);

		ChunkHeader data{};
		file.read(reinterpret_cast<char*>(&data), sizeof(data));
		while (std::strncmp(data.id, "data", 4) != 0) {
			file.seekg(data.size, std::ios_base::cur);
			file.read(reinterpret_cast<char*>(&data), sizeof(data));
		}

		assert(data.size >= 0);
		uint32_t dataSize = static_cast<uint32_t>(data.size);
		auto pBuffer = new char[dataSize];
		file.read(pBuffer, dataSize);

		soundData.wfex = format.format;
		soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
		soundData.bufferSize = dataSize;
		return soundData;
	}

	//------------------------------
	// WAVメモリ解放
	//------------------------------
	void SoundUnload(SoundData* soundData) {
		// SoundLoadWaveで確保した波形データの所有権をここで解放する。
		assert(soundData != nullptr);
		delete[] soundData->pBuffer;
		soundData->pBuffer = nullptr;
		soundData->bufferSize = 0u;
	}
}

#pragma warning(push)
#pragma warning(disable : 5045)
//========================================
// アプリケーションエントリーポイント
//========================================
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	// WinMainの全体像
	// 1. アプリケーション、入力、音声を初期化する
	// 2. DirectX 12のデバイスと描画設定を作る
	// 3. モデル、テクスチャ、GPUバッファを準備する
	// 4. メインループで「入力 → 座標更新 → 描画」を繰り返す
	// 5. 終了時に外部APIのリソースを解放する

	//========================================
	// 初期化処理
	//========================================

	//------------------------------
	// アプリケーション・入力初期化
	//------------------------------
	InstallCrashHandler();

	// 実行ごとに時刻付きログを作り、同名ファイルによる上書きを避ける。
	std::filesystem::create_directory("logs");
	std::time_t now = std::time(nullptr);
	std::tm localTime{};
	localtime_s(&localTime, &now);

	std::string dateString = std::format(
		"{:04}{:02}{:02}_{:02}{:02}{:02}",
		localTime.tm_year + 1900,
		localTime.tm_mon + 1,
		localTime.tm_mday,
		localTime.tm_hour,
		localTime.tm_min,
		localTime.tm_sec);
	auto logFilePath = std::string("logs/" + dateString + ".Log");
	std::ofstream logStream(logFilePath);
	if (!logStream) {
		return 1;
	}
	WinApp* winApp = nullptr;

	winApp = new WinApp();
	winApp->Initialize();
	HWND hwnd = winApp->GetHwnd();
	if (hwnd == nullptr) {
		return 1;
	}
	DirectXCommon* directXCommon = nullptr;
	directXCommon = new DirectXCommon();
	directXCommon->Initialize(winApp);

	ID3D12Device* device =
		directXCommon->GetDevice();

	ID3D12GraphicsCommandList* commandList =
		directXCommon->GetCommandList();

	ID3D12DescriptorHeap* srvDescriptorHeap =
		directXCommon->GetSRVDescriptorHeap();

	uint32_t srvDescriptorSize =
		directXCommon->GetSRVDescriptorSize();
	HRESULT hr = S_OK;
	//ポインタ
	Input* input = nullptr;
	// 入力初期化
	input = new Input();
	input->Initialize(winApp);


	Log(logStream, "main loop started");

	//------------------------------
	// 音声初期化・再生開始
	//------------------------------
	IXAudio2* xAudio2 = nullptr;
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || xAudio2 == nullptr) {
		return 1;
	}

	IXAudio2MasteringVoice* masterVoice = nullptr;
	hr = xAudio2->CreateMasteringVoice(&masterVoice);
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || masterVoice == nullptr) {
		xAudio2->Release();
		return 1;
	}

	//SoundData soundData = SoundLoadWave("resources/sound/maou_19_12345.wav");
	//IXAudio2SourceVoice* sourceVoice = nullptr;
	//hr = xAudio2->CreateSourceVoice(&sourceVoice, &soundData.wfex);
	//assert(SUCCEEDED(hr));
	//if (FAILED(hr) || sourceVoice == nullptr) {
	//	SoundUnload(&soundData);
	//	masterVoice->DestroyVoice();
	//	xAudio2->Release();
	//	return 1;
	//}

	//XAUDIO2_BUFFER soundBuffer{};
	//soundBuffer.pAudioData = soundData.pBuffer;
	//soundBuffer.AudioBytes = soundData.bufferSize;
	//soundBuffer.Flags = XAUDIO2_END_OF_STREAM;
	//hr = sourceVoice->SubmitSourceBuffer(&soundBuffer);
	//assert(SUCCEEDED(hr));
	//hr = sourceVoice->Start(0);
	//assert(SUCCEEDED(hr));


	ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(
		L"resources/shaders/Particle.VS.hlsl",
		L"vs_6_0",
		directXCommon->GetDXCUtils(),
		directXCommon->GetDXCCompiler(),
		directXCommon->GetIncludeHandler(),
		logStream);

	ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(
		L"resources/shaders/Particle.PS.hlsl",
		L"ps_6_0",
		directXCommon->GetDXCUtils(),
		directXCommon->GetDXCCompiler(),
		directXCommon->GetIncludeHandler(),
		logStream);

	//------------------------------
	// ルートパラメータ初期化
	//------------------------------
	//------------------------------
	// インスタンシング用SRV設定
	//------------------------------

	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1]{};
	descriptorRangeForInstancing[0].BaseShaderRegister = 0;
	descriptorRangeForInstancing[0].NumDescriptors = 1;
	descriptorRangeForInstancing[0].RangeType =
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;


	//------------------------------
	// テクスチャ用SRV設定
	//------------------------------

	D3D12_DESCRIPTOR_RANGE descriptorRangeForTexture[1]{};
	descriptorRangeForTexture[0].BaseShaderRegister = 0;
	descriptorRangeForTexture[0].NumDescriptors = 1;
	descriptorRangeForTexture[0].RangeType =
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRangeForTexture[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;


	//------------------------------
	// ルートパラメータ設定
	//------------------------------

	D3D12_ROOT_PARAMETER rootParameters[5]{};

	rootParameters[0].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[0].Descriptor.RegisterSpace = 0;

	rootParameters[1].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].DescriptorTable.pDescriptorRanges =
		descriptorRangeForInstancing;
	rootParameters[1].DescriptorTable.NumDescriptorRanges =
		_countof(descriptorRangeForInstancing);

	rootParameters[2].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[2].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].Descriptor.ShaderRegister = 1;
	rootParameters[2].Descriptor.RegisterSpace = 0;

	rootParameters[3].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[3].ShaderVisibility =
		D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].DescriptorTable.pDescriptorRanges =
		descriptorRangeForTexture;
	rootParameters[3].DescriptorTable.NumDescriptorRanges =
		_countof(descriptorRangeForTexture);

	// 花火の全粒子で共有するカメラ軸と投影行列。
	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[4].Constants.ShaderRegister = 2;
	rootParameters[4].Constants.RegisterSpace = 0;
	rootParameters[4].Constants.Num32BitValues = sizeof(ParticleCameraData) / sizeof(UINT);


	//------------------------------
	// ルートシグネチャ設定
	//------------------------------

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters =
		_countof(rootParameters);


	D3D12_STATIC_SAMPLER_DESC staticSampler{};
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
	staticSampler.ShaderRegister = 0;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	descriptionRootSignature.pStaticSamplers = &staticSampler;
	descriptionRootSignature.NumStaticSamplers = 1;

	ComPtr<ID3DBlob> signatureBlob;
	ComPtr<ID3DBlob> errorBlob;

	//------------------------------
	// マテリアル・ライト初期化
	//------------------------------
	ComPtr<ID3D12Resource> particleMaterialResource;
	particleMaterialResource.Attach(CreateBufferResource(device, sizeof(Material)));
	Material* particleMaterialData = nullptr;
	particleMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&particleMaterialData));
	*particleMaterialData = {
		.color = {1.0f, 1.0f, 1.0f, 1.0f},
		.enableLighting = TRUE,
		.padding = {0.0f, 0.0f, 0.0f},
		.uvTransform = MakeIdentity4x4()
	};

	ComPtr<ID3D12Resource> directionalLightResource;
	directionalLightResource.Attach(CreateBufferResource(device, sizeof(DirectionalLight)));
	DirectionalLight* directionalLightData = nullptr;
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	directionalLightData->direction = {0.0f, -1.0f, 0.0f};
	directionalLightData->intensity = 1.0f;

	// 0はImGui、4はパーティクル用。テクスチャ管理には5以降を明示的に予約する。
	constexpr UINT kParticleSrvIndex = 4;
	constexpr UINT kFirstTextureSrvIndex = kParticleSrvIndex + 1;
	TextureManager textureManager;
	textureManager.Initialize(directXCommon, kFirstTextureSrvIndex,
		DirectXCommon::kMaxSRVCount - kFirstTextureSrvIndex);
	SpriteCommon spriteCommon;
	spriteCommon.Initialize(directXCommon);

	GameScene gameScene;
	if (!gameScene.Initialize(device, srvDescriptorHeap, srvDescriptorSize, kParticleSrvIndex, xAudio2)) {
		return 1;
	}
	gameScene.InitializeSprites(&spriteCommon, &textureManager);

	//------------------------------
	// ルートシグネチャ生成
	//------------------------------
	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.GetAddressOf(),
		errorBlob.GetAddressOf());
	if (FAILED(hr)) {
		if (errorBlob != nullptr) {
			Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	ComPtr<ID3D12RootSignature> rootSignature;
	hr = device->CreateRootSignature(
		0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(rootSignature.GetAddressOf()));
	Log(logStream, std::format(
		    "CreateRootSignature HRESULT: 0x{:08X}",
		    static_cast<uint32_t>(hr)));

	assert(SUCCEEDED(hr));

	//------------------------------
	// パイプラインステート初期化
	//------------------------------
	// 頂点レイアウト、ブレンド、ラスタライザ、深度判定をまとめてPSOへ登録する。
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.DepthClipEnable = TRUE;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();
	graphicsPipelineStateDesc.InputLayout.pInputElementDescs = inputElementDescs;
	graphicsPipelineStateDesc.InputLayout.NumElements = _countof(inputElementDescs);
	graphicsPipelineStateDesc.VS = {
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize()
	};
	graphicsPipelineStateDesc.PS = {
		pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize()
	};
	graphicsPipelineStateDesc.BlendState = blendDesc;
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	ComPtr<ID3D12PipelineState> graphicsPipelineState;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
	                                         IID_PPV_ARGS(graphicsPipelineState.GetAddressOf()));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || graphicsPipelineState == nullptr) {
		return 1;
	}

	//------------------------------
	// OBJモデル読み込み
	//------------------------------
	ModelData modelData = LoadObjFile("resources", "plane.obj");

	// 発射位置と開花位置を同時に見られるショー用の初期視点。
	constexpr Vector3 kShowCameraPosition{1.5f, 9.0f, -80.0f};
	Transforms cameraTransform{
		.scale = {1.0f, 1.0f, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = kShowCameraPosition
	};

	ComPtr<ID3D12Resource> modelVertexResource;
	modelVertexResource.Attach(CreateBufferResource(
		device, sizeof(VertexData) * modelData.vertices.size()));

	VertexData* mappedModelVertexData = nullptr;
	hr = modelVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedModelVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedModelVertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());

	D3D12_VERTEX_BUFFER_VIEW modelVertexBufferView{};
	modelVertexBufferView.BufferLocation = modelVertexResource->GetGPUVirtualAddress();
	modelVertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelData.vertices.size());
	modelVertexBufferView.StrideInBytes = sizeof(VertexData);

	//------------------------------
	// テクスチャリソース初期化
	//------------------------------
	const uint32_t particleTextureHandle = textureManager.LoadTexture("resources/hosi.png");

	//------------------------------
	// カメラ・GPU同期初期化
	//------------------------------
	Matrix4x4 cameraMatrix = MakeAffineMatrix(
		cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
	Matrix4x4 viewMatrix = Inverse(cameraMatrix);
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(
		0.65f,
		static_cast<float>(WinApp::kClientWidth) / static_cast<float>(WinApp::kClientHeight),
		0.1f,
		200.0f);
	constexpr float cameraMoveSpeed = 0.1f;
	constexpr float cameraRotateSpeed = 0.02f;


	const ParticleEffect::DrawContext particleDrawContext{
		.material = particleMaterialResource.Get(),
		.light = directionalLightResource.Get(),
		.texture = textureManager.GetSRVGPUDescriptorHandle(particleTextureHandle),
		.vertexBuffer = modelVertexBufferView,
		.vertexCount = static_cast<UINT>(modelData.vertices.size())
	};

	//========================================
	// メインループ
	//========================================
	while (true) {
		if (winApp->ProcessMessage()) {
			break;
		}

		//========================================
		// 更新処理
		//========================================

		//------------------------------
		// 入力更新
		//------------------------------
		input->Update();

#ifdef USE_IMGUI
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		if (input->TriggerKey(DIK_F1)) {
			gameScene.ToggleEditor();
		}
		const bool editorCapturesKeyboard = ImGui::GetIO().WantCaptureKeyboard;
#else
		constexpr bool editorCapturesKeyboard = false;
#endif

		if (!editorCapturesKeyboard) {
			if (input->TriggerKey(DIK_ESCAPE)) {
				PostQuitMessage(0);
			}
			if (input->TriggerKey(DIK_SPACE)) {
				gameScene.TogglePlayback();
			}
			if (input->TriggerKey(DIK_F2)) {
				gameScene.ToggleSprites();
			}

			if (input->PushKey(DIK_LEFT)) {
				cameraTransform.rotate.y -= cameraRotateSpeed;
			}
			if (input->PushKey(DIK_RIGHT)) {
				cameraTransform.rotate.y += cameraRotateSpeed;
			}
			if (input->PushKey(DIK_UP)) {
				cameraTransform.rotate.x -= cameraRotateSpeed;
			}
			if (input->PushKey(DIK_DOWN)) {
				cameraTransform.rotate.x += cameraRotateSpeed;
			}

			if (input->PushKey(DIK_A)) {
				cameraTransform.translate.x -= cameraMoveSpeed;
			}
			if (input->PushKey(DIK_D)) {
				cameraTransform.translate.x += cameraMoveSpeed;
			}
			if (input->PushKey(DIK_Q)) {
				cameraTransform.translate.y += cameraMoveSpeed;
			}
			if (input->PushKey(DIK_E)) {
				cameraTransform.translate.y -= cameraMoveSpeed;
			}
			if (input->PushKey(DIK_W)) {
				cameraTransform.translate.z += cameraMoveSpeed;
			}
			if (input->PushKey(DIK_S)) {
				cameraTransform.translate.z -= cameraMoveSpeed;
			}

			if (input->TriggerKey(DIK_RETURN)) {
				cameraTransform.rotate = {0.0f, 0.0f, 0.0f};
				cameraTransform.translate = kShowCameraPosition;
			}
		}

		//------------------------------
		// 座標・シェーダー入力更新
		//------------------------------
		cameraMatrix = MakeAffineMatrix(
			cameraTransform.scale,
			cameraTransform.rotate,
			cameraTransform.translate
		);

		viewMatrix = Inverse(cameraMatrix);

		const Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);
		gameScene.Update(cameraMatrix, viewProjectionMatrix);
#ifdef USE_IMGUI
		ImGui::Render();
#endif
		//========================================
		// 描画処理
		//========================================

		//------------------------------
		// 描画コマンド初期化
		//------------------------------
		directXCommon->PreDraw();
		commandList->SetGraphicsRootSignature(
			rootSignature.Get());

		commandList->SetPipelineState(
			graphicsPipelineState.Get());

		commandList->IASetPrimitiveTopology(
			D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gameScene.Draw(commandList, particleDrawContext);
#ifdef USE_IMGUI
		ImDrawData* drawData = ImGui::GetDrawData();
		if (drawData->DisplaySize.x > 0.0f && drawData->DisplaySize.y > 0.0f) {
			// ウィンドウを拡縮しても、UI と3Dプレビューを同じバックバッファに合わせる。
			drawData->FramebufferScale = {
				WinApp::kClientWidth / drawData->DisplaySize.x,
				WinApp::kClientHeight / drawData->DisplaySize.y
			};
			ImGui_ImplDX12_RenderDrawData(drawData, commandList);
		}
#endif

		directXCommon->PostDraw();
	}
	//========================================
	// 終了処理
	//========================================
	gameScene.Finalize();
	spriteCommon.Finalize();
	textureManager.Finalize();
	//if (sourceVoice != nullptr) {
	//	sourceVoice->Stop(0);
	//	sourceVoice->FlushSourceBuffers();
	//	sourceVoice->DestroyVoice();
	//}
	//SoundUnload(&soundData);

	if (masterVoice != nullptr) {
		masterVoice->DestroyVoice();
	}
	if (xAudio2 != nullptr) {
		xAudio2->Release();
		xAudio2 = nullptr;
	}

	// 入力解放
	delete input;
	input = nullptr;

	// DirectX基盤解放
	directXCommon->Finalize();
	delete directXCommon;
	directXCommon = nullptr;

	// ウィンドウ解放
	winApp->Finalize();
	delete winApp;
	winApp = nullptr;

	return 0;
}
#pragma warning(pop)
