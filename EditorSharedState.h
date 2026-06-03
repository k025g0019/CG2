#pragma once

#pragma warning(push, 0)
#include <Windows.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <d3d12.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <xaudio2.h>
#include "externals/DirectXTex/DirectXTex.h"
#pragma warning(push)
#pragma warning(disable : 5045)
#include "externals/DirectXTex/d3dx12.h"
#pragma warning(pop)
#pragma warning(disable : 4820)
#include <filesystem>
#include <format>
#include <fstream>
#include <numbers>
#include <sdkddkver.h>
#include <sstream>
#include <string>
#include <vector>
#include <wrl.h>
#pragma warning(pop)
#pragma warning(disable : 4820)
#pragma warning(disable : 4514)
#pragma warning(disable : 5045)

#include "ApplicationWindow.h"
#include "CrashHandler.h"
#include "EditorAssetFactory.h"
#include "EditorAssetUtility.h"
#include "EditorBottomPanel.h"
#include "EditorCommonTypes.h"
#include "EditorHierarchyPanel.h"
#include "EditorInspectorPanel.h"
#include "EditorMainMenuBar.h"
#include "EditorRuntimeManager.h"
#include "EditorScene.h"
#include "EditorSceneCameraController.h"
#include "EditorSceneObjectManager.h"
#include "EditorSceneSynchronizer.h"
#include "EditorSelectionManager.h"
#include "Imgui.h"
#include "Log.h"
#include "Matrix.h"
#include "StringUtility.h"
#include "Vector&Matrix.h"
#include "Vector.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxcompiler.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "xaudio2.lib")
#include "externals/FeelKitHaptics/FeelKit.h"
#include "externals/FeelKitHaptics/FeelKitHaptics.h"
using Microsoft::WRL::ComPtr;


#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "externals/ImGuizmo-master/ImGuizmo-master/src/ImGuizmo.h"
#include "externals/imgui-docking/imgui-docking/imgui.h"
#include "externals/imgui-docking/imgui-docking/imgui_internal.h"
#include "externals/imgui-docking/imgui-docking/backends/imgui_impl_dx12.h"
#include "externals/imgui-docking/imgui-docking/backends/imgui_impl_win32.h"
#pragma warning(pop)
#endif

namespace EditorSharedState {
	// ================================
	// WAV ファイル読み込み用のデータ構造
	// ================================
	struct ChunkHeader {
		char id[4];  // id は "RIFF"、"fmt "、"data" などの 4 文字チャンク名。
		int32_t size;  // size はこのチャンク本体のバイト数。
	};

	struct RiffHeader {
		ChunkHeader chunk;  // chunk は RIFF ファイル全体のチャンク情報。
		char type[4];  // type は WAV ファイルであることを表す "WAVE"。
	};

	struct FormatChunk {
		ChunkHeader chunk;  // chunk は "fmt " チャンクの ID とサイズ。
		WAVEFORMATEX format;  // format は サンプリングレートやチャンネル数などの WAV 形式情報。
	};

	struct SoundData {
		WAVEFORMATEX wfex;  // wfex は XAudio2 SourceVoice 作成に渡す WAV 形式情報。
		BYTE* pBuffer;  // pBuffer は WAV の PCM データ本体。
		uint32_t bufferSize;  // bufferSize は pBuffer のバイト数。
	};

	inline ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes);  // GPU UploadBuffer と OBJ 読み込みに使う helper 宣言。
	inline MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
	inline ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

	inline SoundData SoundLoadWave(const char* filePath);  // WAV 読み込みと解放に使う helper 宣言。
	inline void SoundUnload(SoundData* soundData);

	inline D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap, UINT descriptorSize, UINT index) {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();  // handle は Heap 先頭の CPU DescriptorHandle。
		handle.ptr += descriptorSize * index;  // descriptorSize * index だけ進めて、index 番目の CPU Handle を作る。
		return handle;
	}

	inline D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap, UINT descriptorSize, UINT index) {
		D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();  // handle は Heap 先頭の GPU DescriptorHandle。
		handle.ptr += descriptorSize * index;  // descriptorSize * index だけ進めて、index 番目の GPU Handle を作る。
		return handle;
	}

	inline DirectX::ScratchImage LoadTexture(const std::wstring& filePath) {
		// metadata は読み込んだ画像の幅・高さ・形式を受け取る。
		DirectX::TexMetadata metadata{};

		// image は WIC で読み込んだ元画像データ。
		DirectX::ScratchImage image{};

		HRESULT hr = DirectX::LoadFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, &metadata, image);  // LoadFromWICFile は png などの画像を sRGB 前提で読み込む。
		assert(SUCCEEDED(hr));

		// mipImages は GPU サンプリング用に mipmap を追加した画像データ。
		DirectX::ScratchImage mipImages{};

		// GenerateMipMaps は遠くの Texture がちらつかないよう mipmap 群を作る。
		hr = DirectX::GenerateMipMaps(
			image.GetImages(),
			image.GetImageCount(),
			image.GetMetadata(),
			DirectX::TEX_FILTER_SRGB,
			0,
			mipImages);
		assert(SUCCEEDED(hr));

		return mipImages;
	}

	inline ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
		// resourceDesc は metadata に合わせた 2D Texture Resource の設定。
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Width = static_cast<UINT>(metadata.width);
		resourceDesc.Height = static_cast<UINT>(metadata.height);
		resourceDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
		resourceDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
		resourceDesc.Format = metadata.format;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);

		// heapProperties は GPU 専用メモリ上に Texture を置く指定。
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		ID3D12Resource* resource = nullptr;  // resource は作成して返す Texture Resource。

		// 初期状態 COPY_DEST は、後で UploadTextureData からコピーするため。
		HRESULT hr = device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&resource));
		assert(SUCCEEDED(hr));

		return resource;
	}

	inline ID3D12Resource* UploadTextureData(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* commandList,
		ID3D12Resource* texture,
		const DirectX::ScratchImage& mipImages) {
		std::vector<D3D12_SUBRESOURCE_DATA> subresources;  // subresources は mipmap 各段を UpdateSubresources に渡すための配列。
		subresources.reserve(mipImages.GetImageCount());

		const DirectX::Image* images = mipImages.GetImages();  // images は DirectXTex が保持する mipmap 各段の画像ポインタ。
		for (size_t index = 0; index < mipImages.GetImageCount(); ++index) {
			// subresource は 1 mip 分のピクセルポインタと行/スライス幅。
			D3D12_SUBRESOURCE_DATA subresource{};
			subresource.pData = images[index].pixels;
			subresource.RowPitch = static_cast<LONG_PTR>(images[index].rowPitch);
			subresource.SlicePitch = static_cast<LONG_PTR>(images[index].slicePitch);
			subresources.push_back(subresource);
		}

		UINT64 intermediateSize = GetRequiredIntermediateSize(texture, 0, static_cast<UINT>(subresources.size()));  // intermediateSize は全 mip を GPU Texture へコピーするために必要な UploadBuffer サイズ。
		ID3D12Resource* intermediateResource = CreateBufferResource(device, intermediateSize);  // intermediateResource は texture へコピーするための一時 UploadBuffer。

		// UpdateSubresources は UploadBuffer から Default Heap Texture へコピー命令を積む。
		UpdateSubresources(
			commandList,
			texture,
			intermediateResource,
			0,
			0,
			static_cast<UINT>(subresources.size()),
			subresources.data());

		// barrier は Texture をコピー先状態から Shader 読み取り状態へ変える命令。
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texture;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
		commandList->ResourceBarrier(1, &barrier);

		return intermediateResource;
	}

	inline ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
		// uploadHeapProperties は CPU から Map して書き込める Upload Heap 指定。
		D3D12_HEAP_PROPERTIES uploadHeapProperties{};
		uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

		// resourceDesc は sizeInBytes 分の汎用 Buffer Resource 設定。
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width = sizeInBytes;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ID3D12Resource* resource = nullptr;  // resource は作成して返す Upload Buffer。

		// GENERIC_READ は CPU 書き込み後に GPU から読む定数/頂点 Buffer に使う。
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

	inline ComPtr<IDxcBlob> CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile,
		IDxcUtils* dxcUtils,
		IDxcCompiler3* dxcCompiler,
		IDxcIncludeHandler* includeHandler,
		std::ofstream& logStream) {
		Log(logStream, std::format("Begin CompileShader, path:{}, profile:{}",
		                           ConvertString(filePath), ConvertString(std::wstring{profile})));

		ComPtr<IDxcBlobEncoding> shaderSource;  // shaderSource は読み込んだ HLSL ファイル本文。
		HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, shaderSource.GetAddressOf());
		assert(SUCCEEDED(hr));

		// shaderSourceBuffer は DXC に渡すソースコードのポインタ・サイズ・文字コード。
		DxcBuffer shaderSourceBuffer{};
		shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
		shaderSourceBuffer.Size = shaderSource->GetBufferSize();
		shaderSourceBuffer.Encoding = DXC_CP_UTF8;

		// arguments は entry point、Shader Model、Debug 情報、最適化、行列配置の指定。
		LPCWSTR arguments[] = {
			filePath.c_str(),
			L"-E", L"main",
			L"-T", profile,
			L"-Zi", L"-Qembed_debug",
			L"-Od",
			L"-Zpr"
		};

		ComPtr<IDxcResult> shaderResult;  // shaderResult は DXC のコンパイル結果本体。
		hr = dxcCompiler->Compile(
			&shaderSourceBuffer,
			arguments,
			_countof(arguments),
			includeHandler,
			IID_PPV_ARGS(shaderResult.GetAddressOf()));
		assert(SUCCEEDED(hr));


		ComPtr<IDxcBlobUtf8> shaderError;  // shaderError は HLSL コンパイルエラーや警告の文字列。
		shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(shaderError.GetAddressOf()), nullptr);
		if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
			Log(shaderError->GetStringPointer());
			assert(false);
		}

		ComPtr<IDxcBlob> shaderBlob;  // shaderBlob は GPU に渡す最終的な DXIL バイトコード。
		hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shaderBlob.GetAddressOf()), nullptr);
		assert(SUCCEEDED(hr));

		Log(logStream, std::format("Compile Succeeded, path:{}, profile:{}",
		                           ConvertString(filePath), ConvertString(std::wstring{profile})));

		return shaderBlob;
	}

	inline MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
		// materialData は .mtl から読み取った Texture パスを返すための構造体。
		MaterialData materialData{};

		std::ifstream file(directoryPath + "/" + filename);  // file は directoryPath/filename の .mtl ファイル。
		assert(file.is_open());

		std::string line;  // line は .mtl を 1 行ずつ読むための文字列。
		while (std::getline(file, line)) {
			std::string identifier;  // identifier は map_Kd など、行先頭の命令名。
			std::istringstream lineStream(line);
			lineStream >> identifier;

			// map_Kd は Diffuse Texture ファイル名を示す .mtl 命令。
			if (identifier == "map_Kd") {
				lineStream >> materialData.textureFilePath;

				materialData.textureFilePath = directoryPath + "/" + materialData.textureFilePath;  // OBJ から見た相対パスを、読み込みに使える directoryPath 付きパスへ直す。
			}
		}

		return materialData;
	}

	inline ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
		// modelData は OBJ の頂点配列と Material 情報をまとめて返す。
		ModelData modelData{};

		std::ifstream file(directoryPath + "/" + filename);  // file は directoryPath/filename の .obj ファイル。
		assert(file.is_open());

		std::vector<Vector4> positions;  // positions / texcoords / normals は OBJ の v / vt / vn を一時保存する配列。
		std::vector<Vector2> texcoords;
		std::vector<Vector3> normals;

		std::string line;  // line は OBJ を 1 行ずつ読むための文字列。

		while (std::getline(file, line)) {
			std::string identifier;  // identifier は v、vt、vn、f、mtllib などの行種別。
			std::istringstream lineStream(line);
			lineStream >> identifier;

			// v は頂点位置。X を反転して右手/左手座標系の違いを合わせる。
			if (identifier == "v") {
				Vector4 position{};
				lineStream >> position.x >> position.y >> position.z;
				position.x *= -1.0f;
				position.w = 1.0f;
				positions.push_back(position);
			}
			else if (identifier == "vt") {
				// vt は Texture 座標。画像座標系に合わせるため Y を反転する。
				Vector2 texcoord{};
				lineStream >> texcoord.x >> texcoord.y;
				texcoord.y = 1.0f - texcoord.y;
				texcoords.push_back(texcoord);
			}
			else if (identifier == "vn") {
				// vn は法線。位置と同じ座標系に合わせるため X を反転する。
				Vector3 normal{};
				lineStream >> normal.x >> normal.y >> normal.z;
				normal.x *= -1.0f;
				normals.push_back(normal);
			}
			else if (identifier == "f") {
				// triangle は 1 面分の 3 頂点を一時保存する配列。
				VertexData triangle[3]{};
				for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
					std::string vertexDefinition;  // vertexDefinition は "位置/UV/法線" 形式の 1 頂点指定。
					lineStream >> vertexDefinition;

					std::istringstream vertexStream(vertexDefinition);  // vertexStream は '/' 区切りの index 文字列を分解するための stream。
					std::string positionIndexString;
					std::string texcoordIndexString;
					std::string normalIndexString;
					std::getline(vertexStream, positionIndexString, '/');
					std::getline(vertexStream, texcoordIndexString, '/');
					std::getline(vertexStream, normalIndexString, '/');

					uint32_t positionIndex = static_cast<uint32_t>(std::stoi(positionIndexString)) - 1;  // OBJ の index は 1 始まりなので、C++ 配列用に 0 始まりへ直す。
					uint32_t texcoordIndex = static_cast<uint32_t>(std::stoi(texcoordIndexString)) - 1;
					uint32_t normalIndex = static_cast<uint32_t>(std::stoi(normalIndexString)) - 1;

					triangle[faceVertex].position = positions[positionIndex];
					triangle[faceVertex].texcoord = texcoords[texcoordIndex];
					triangle[faceVertex].normal = normals[normalIndex];
				}

				modelData.vertices.push_back(triangle[2]);  // 頂点順を反転し、座標系変換後の面の向きを合わせる。
				modelData.vertices.push_back(triangle[1]);
				modelData.vertices.push_back(triangle[0]);
			}
			else if (identifier == "mtllib") {
				std::string materialFilename;  // materialFilename は OBJ が参照する .mtl ファイル名。
				lineStream >> materialFilename;

				modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);  // .mtl を読み込み、Texture パスを modelData に保持する。
			}
		}

		return modelData;
	}

	inline SoundData SoundLoadWave(const char* filePath) {
		// soundData は WAV から読み取った形式情報と PCM バッファを返す。
		SoundData soundData{};

		std::ifstream file(filePath, std::ios_base::binary);  // file は WAV をバイナリとして読む入力 stream。
		assert(file.is_open());

		// riff は WAV ファイル先頭の RIFF/WAVE ヘッダ。
		RiffHeader riff{};
		file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
		assert(std::strncmp(riff.chunk.id, "RIFF", 4) == 0);
		assert(std::strncmp(riff.type, "WAVE", 4) == 0);

		// format は "fmt " チャンク。XAudio2 の SourceVoice 作成に使う。
		FormatChunk format{};
		file.read(reinterpret_cast<char*>(&format), sizeof(ChunkHeader));
		assert(std::strncmp(format.chunk.id, "fmt ", 4) == 0);
		assert(format.chunk.size >= 0);
		assert(static_cast<size_t>(format.chunk.size) <= sizeof(format.format));
		file.read(reinterpret_cast<char*>(&format.format), format.chunk.size);

		// data は PCM 本体を持つ "data" チャンクを探すためのヘッダ。
		ChunkHeader data{};
		file.read(reinterpret_cast<char*>(&data), sizeof(data));
		while (std::strncmp(data.id, "data", 4) != 0) {
			file.seekg(data.size, std::ios_base::cur);  // data 以外のチャンクはサイズ分だけ読み飛ばして次のチャンクを見る。
			file.read(reinterpret_cast<char*>(&data), sizeof(data));
		}

		assert(data.size >= 0);

		uint32_t dataSize = static_cast<uint32_t>(data.size);  // dataSize は PCM バッファのバイト数。
		auto pBuffer = new char[dataSize];  // pBuffer は XAudio2 に渡す PCM データ。SoundUnload で解放する。
		file.read(pBuffer, dataSize);

		soundData.wfex = format.format;  // 読み込んだ形式情報と PCM バッファを SoundData に詰める。
		soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
		soundData.bufferSize = dataSize;
		return soundData;
	}

	inline void SoundUnload(SoundData* soundData) {
		assert(soundData != nullptr);  // soundData->pBuffer は SoundLoadWave で new[] した PCM バッファ。
		delete[] soundData->pBuffer;
		soundData->pBuffer = nullptr;
		soundData->bufferSize = 0u;
	}
}

#pragma warning(push)
#pragma warning(disable : 4101 4189 4514 5045)

namespace EditorSharedState {
	constexpr uint32_t kRuntimeTextureCount = 4;  // kRuntimeTextureCount は起動時に固定で確保する標準 Texture 数。
	constexpr uint32_t kRuntimeSwapChainBufferCount = 2;  // kRuntimeSwapChainBufferCount は SwapChain の back buffer 数。
	constexpr uint32_t kRuntimeSpriteIndexCount = 6;  // kRuntimeSpriteIndexCount は Sprite 四角形を 2 三角形で描く index 数。
	inline HINSTANCE g_instanceHandle = nullptr;  // g_instanceHandle は Win32 Window と DirectInput 初期化に使うアプリ実体。
	inline int g_exitCode = 0;  // g_exitCode は WinMain へ返す終了コード。
	inline bool g_isInitialized = false;  // g_isInitialized は PlatformManager の初期化が完了したかを表すフラグ。
	inline bool g_isInitializationFailed = false;  // g_isInitializationFailed は初期化失敗で後続 Manager を止めるためのフラグ。
	inline bool g_isEndRequested = false;  // g_isEndRequested は WinMain のメインループを抜けるための終了要求フラグ。
	inline bool g_isFinalized = false;  // g_isFinalized は Finalize 済みリソースへ二重アクセスしないためのフラグ。
	inline bool g_isDrawRequested = false;  // g_isDrawRequested は ImGui::Render 後に Renderer が GPU 描画してよいかを表すフラグ。
	inline std::ofstream g_logStream;  // g_logStream は実行ログと Shader compile ログの出力先。
	inline HWND g_windowHandle = nullptr;  // g_windowHandle は SwapChain、ImGui Win32 backend、DirectInput に使う HWND。
	inline HRESULT g_hr = S_OK;  // g_hr は直近の HRESULT を共有してデバッグ確認するための値。
	inline IDirectInput8* g_directInput = nullptr;  // g_directInput はキーボード・マウスデバイスを生成する DirectInput 本体。
	inline IDirectInputDevice8* g_keyboardDevice = nullptr;  // g_keyboardDevice は DIK_* の押下状態を読む入力デバイス。
	inline IDirectInputDevice8* g_mouseDevice = nullptr;  // g_mouseDevice はマウスの相対移動量・ボタン状態を読む入力デバイス。
	inline DIMOUSESTATE g_mouseState{};  // g_mouseState はマウスの各軸移動量とボタン押下状態。

	// g_key は今フレームの 256 キー押下状態。
	inline BYTE g_key[256] = {};

	// g_preKey は前フレームの 256 キー押下状態。
	inline BYTE g_preKey[256] = {};

	// g_message は Win32 メッセージループで処理中のメッセージ。
	inline MSG g_message{};

	inline IXAudio2* g_xAudio2 = nullptr;  // g_xAudio2 は音声再生エンジン本体。
	inline IXAudio2MasteringVoice* g_masterVoice = nullptr;  // g_masterVoice は最終出力先の XAudio2 Voice。

	// g_soundData は読み込んだ wav の PCM バッファとフォーマット。
	inline SoundData g_soundData{};

	inline IXAudio2SourceVoice* g_sourceVoice = nullptr;  // g_sourceVoice は g_soundData を再生する XAudio2 SourceVoice。
	inline ComPtr<IDXGIFactory7> g_dxgiFactory;  // g_dxgiFactory は Adapter と SwapChain を作る DXGI Factory。
	inline ComPtr<IDXGIAdapter4> g_useAdapter;  // g_useAdapter は D3D12Device を作成した物理 GPU。
	inline ComPtr<ID3D12Device> g_device;  // g_device は DirectX12 リソース生成と描画命令の中心。
	inline ComPtr<ID3D12CommandQueue> g_commandQueue;  // g_commandQueue / Allocator / List は GPU へ描画命令を送るための一式。
	inline ComPtr<ID3D12CommandAllocator> g_commandAllocator;
	inline ComPtr<ID3D12GraphicsCommandList> g_commandList;

	inline ComPtr<IDXGISwapChain4> g_swapChain;  // g_swapChain は Window に表示する back buffer 列。

	// g_swapChainDesc は back buffer 数・形式・サイズの設定。
	inline DXGI_SWAP_CHAIN_DESC1 g_swapChainDesc{};

	inline ID3D12DescriptorHeap* g_rtvDescriptorHeap = nullptr;  // g_*DescriptorHeap は RTV / SRV / DSV をまとめて保持する DescriptorHeap。
	inline ID3D12DescriptorHeap* g_srvDescriptorHeap = nullptr;
	inline ID3D12DescriptorHeap* g_dsvDescriptorHeap = nullptr;

	// g_swapChainResources は SwapChain の back buffer 実体。
	inline ID3D12Resource* g_swapChainResources[kRuntimeSwapChainBufferCount] = {nullptr, nullptr};

	// g_rtvDesc と g_rtvHandles は back buffer を RenderTarget として使う設定とハンドル。
	inline D3D12_RENDER_TARGET_VIEW_DESC g_rtvDesc{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_rtvHandles[kRuntimeSwapChainBufferCount]{};

	// g_depthClearValue / g_dsvDesc / g_dsvHandle は DepthStencil の Clear と View 作成に使う。
	inline D3D12_CLEAR_VALUE g_depthClearValue{};
	inline D3D12_DEPTH_STENCIL_VIEW_DESC g_dsvDesc{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_dsvHandle{};

	inline ID3D12Resource* g_depthStencilResource = nullptr;  // g_depthStencilResource は 3D 描画の前後関係を判定する Depth バッファ。
	inline uint32_t g_renderWidth = 1u;  // g_renderWidth / Height は SwapChain と DepthStencil の現在サイズ。
	inline uint32_t g_renderHeight = 1u;

	inline ComPtr<IDxcUtils> g_dxcUtils;  // g_dxc* は HLSL の読み込み・コンパイル・include 解決に使う DXC オブジェクト。
	inline ComPtr<IDxcCompiler3> g_dxcCompiler;
	inline ComPtr<IDxcIncludeHandler> g_includeHandler;

	inline ComPtr<IDxcBlob> g_vertexShaderBlob;  // g_vertexShaderBlob / g_pixelShaderBlob はコンパイル済み Shader バイトコード。
	inline ComPtr<IDxcBlob> g_pixelShaderBlob;

	inline ComPtr<ID3DBlob> g_signatureBlob;  // g_signatureBlob / g_errorBlob は RootSignature シリアライズ結果と失敗ログ。
	inline ComPtr<ID3DBlob> g_errorBlob;

	inline ComPtr<ID3D12RootSignature> g_rootSignature;  // g_rootSignature / g_graphicsPipelineState は Draw 時に使う固定 GPU Pipeline 設定。
	inline ComPtr<ID3D12PipelineState> g_graphicsPipelineState;

	inline ID3D12Resource* g_spriteMaterialResource = nullptr;  // g_spriteMaterialResource / Data は Sprite 描画用 Material 定数バッファと mapped ポインタ。
	inline Material* g_spriteMaterialData = nullptr;

	inline ID3D12Resource* g_sphereMaterialResource = nullptr;  // g_sphereMaterialResource / Data は 3D モデル描画用 Material 定数バッファと mapped ポインタ。
	inline Material* g_sphereMaterialData = nullptr;

	inline ID3D12Resource* g_directionalLightResource = nullptr;  // g_directionalLightResource / Data は PixelShader へ渡す平行光源定数バッファと mapped ポインタ。
	inline DirectionalLight* g_directionalLightData = nullptr;

	inline ID3D12Resource* g_spriteTransformationMatrixResource = nullptr;  // g_*TransformationMatrixResource / Data は Sprite と 3D プレビューの WVP/World 行列。
	inline TransformationMatrix* g_spriteTransformationMatrixData = nullptr;
	inline ID3D12Resource* g_sphereTransformationMatrixResource = nullptr;
	inline TransformationMatrix* g_sphereTransformationMatrixData = nullptr;

	// g_modelData は起動時に読み込む既定 OBJ モデル。
	inline ModelData g_modelData{};
	constexpr size_t kEditorModelMeshTypeCount = static_cast<size_t>(EditorModelMeshType::Count);  // 基本形メッシュ配列の要素数。
	inline ModelData g_editorPrimitiveModelData[kEditorModelMeshTypeCount]{};  // 基本形ごとの CPU 側頂点データ。
	inline ID3D12Resource* g_editorPrimitiveVertexResources[kEditorModelMeshTypeCount] = {};  // 基本形ごとの GPU 頂点 Buffer。
	inline D3D12_VERTEX_BUFFER_VIEW g_editorPrimitiveVertexBufferViews[kEditorModelMeshTypeCount]{};  // Draw 時に使う基本形の BufferView。
	inline uint32_t g_editorPrimitiveVertexCounts[kEditorModelMeshTypeCount] = {};  // DrawInstanced に渡す基本形の頂点数。

	inline std::vector<VertexData> g_vertices;  // g_vertices は旧球プレビュー用に生成した頂点配列。

	// g_sprite / g_spriteVertices / g_spriteIndices は旧 Sprite プレビューの四角形データ。
	inline Sprite g_sprite{};
	inline VertexData g_spriteVertices[4]{};
	inline uint32_t g_spriteIndices[kRuntimeSpriteIndexCount]{};

	// g_transform / g_spriteTransform は旧プレビュー用、g_cameraTransform は SceneView カメラ用。
	inline Transforms g_transform{};
	inline Transforms g_spriteTransform{};
	inline Transforms g_cameraTransform{};

	// g_uvTransform は Material の UV 行列を作るための Transform。
	inline Transforms g_uvTransform{};

	inline ID3D12Resource* g_vertexResource = nullptr;  // g_*Resource は旧プレビュー用の VertexBuffer / IndexBuffer 実体。
	inline ID3D12Resource* g_modelVertexResource = nullptr;
	inline ID3D12Resource* g_spriteVertexResource = nullptr;
	inline ID3D12Resource* g_spriteIndexResource = nullptr;

	// g_*BufferView は Draw 時に IASetVertexBuffers / IASetIndexBuffer へ渡す情報。
	inline D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView{};
	inline D3D12_VERTEX_BUFFER_VIEW g_modelVertexBufferView{};
	inline D3D12_VERTEX_BUFFER_VIEW g_spriteVertexBufferView{};
	inline D3D12_INDEX_BUFFER_VIEW g_spriteIndexBufferView{};

	inline float g_editorWindowWidth = 0.0f;  // g_editorWindowWidth / Height は ImGui と DirectX viewport が共有する Window サイズ。
	inline float g_editorWindowHeight = 0.0f;

	inline float g_editorLeftWidth = 250.0f;  // g_editorLeft/Right/Bottom は Docking 前の初期パネル幅・高さ。
	inline float g_editorRightWidth = 320.0f;
	inline float g_editorBottomHeight = 190.0f;

	inline float g_editorSceneX = 0.0f;  // g_editorScene* は SceneView の左上座標と幅・高さ。
	inline float g_editorSceneY = 0.0f;
	inline float g_editorSceneWidth = 0.0f;
	inline float g_editorSceneHeight = 0.0f;

	inline float g_editorGameX = 0.0f;  // g_editorGame* は GameView の左上座標と幅・高さ。
	inline float g_editorGameY = 0.0f;
	inline float g_editorGameWidth = 0.0f;
	inline float g_editorGameHeight = 0.0f;

	inline bool g_isGameViewVisible = false;  // g_isGameViewVisible は GameView へ描画する矩形が有効かどうか。
	inline bool g_isGameViewUsingSceneCamera = true;  // true なら Camera Component がないため Scene カメラを代用している。

	// g_viewport / g_scissorRect は DirectX が SceneView 内だけへ描くための矩形。
	inline D3D12_VIEWPORT g_viewport{};
	inline D3D12_RECT g_scissorRect{};

	// g_cameraMatrix / g_viewMatrix / g_projectionMatrix は SceneView 3D 表示用行列。
	inline Matrix4x4 g_cameraMatrix{};
	inline Matrix4x4 g_viewMatrix{};
	inline Matrix4x4 g_projectionMatrix{};

	// g_game*Matrix は GameView の Camera Component 出力に使う行列。
	inline Matrix4x4 g_gameCameraMatrix{};
	inline Matrix4x4 g_gameViewMatrix{};
	inline Matrix4x4 g_gameProjectionMatrix{};

	// g_spriteProjectionMatrix は Sprite を画面座標で表示するための正射影行列。
	inline Matrix4x4 g_spriteProjectionMatrix{};

	inline float g_editorCameraMoveSpeed = 0.12f;  // g_editorCamera*Speed は SceneView カメラ操作の速度設定。
	inline float g_editorCameraRotateSpeed = 0.006f;
	inline float g_editorCameraWheelMoveSpeed = 0.5f;
	inline float g_editorCameraPanSpeed = 0.01f;
	inline float g_editorCameraFastRate = 4.0f;

	// g_sceneClearColor は SceneView 背景色 RGBA。
	inline float g_sceneClearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};

	inline bool g_isSceneGizmoVisible = true;  // g_is*GizmoVisible は SceneView 補助表示の表示フラグ。
	inline bool g_isLightGizmoVisible = false;
	inline bool g_isCameraGizmoVisible = false;

	// g_directionalLightIconPosition はライトアイコンのワールド座標。
	inline Vector3 g_directionalLightIconPosition = {-1.8f, 1.4f, 0.0f};

	inline EditorSceneObjectManager g_editorSceneObjectManager;  // g_editorSceneObjectManager は GameObject と対応する DirectX 描画用 SceneObject を持つ。
	inline int32_t g_selectedPlacedSceneObjectIndex = -1;  // g_selectedPlacedSceneObjectIndex は選択中 SceneObject の配列 index。-1 は未選択。
	inline ComPtr<ID3D12Fence> g_fence;  // g_fence / g_fenceValue / g_fenceEvent は CPU と GPU の同期に使う。
	inline uint64_t g_fenceValue = 0;
	inline HANDLE g_fenceEvent = nullptr;

	inline std::wstring g_textureFilePaths[kRuntimeTextureCount];  // g_textureFilePaths は GPU に読み込む標準 Texture の UTF-16 パス。
	inline std::string g_textureFilePathStrings[kRuntimeTextureCount];  // g_textureFilePathStrings / g_editorTextureFilePaths は Project パネルで扱う UTF-8 パス。
	inline std::vector<std::string> g_editorTextureFilePaths;

	// g_textureMetadatas / Resources は Texture の情報と GPU 実体。
	inline DirectX::TexMetadata g_textureMetadatas[kRuntimeTextureCount]{};
	inline ID3D12Resource* g_textureResources[kRuntimeTextureCount] = {nullptr, nullptr, nullptr, nullptr};

	// g_intermediateResources は Texture upload 用の一時 Buffer。
	inline ID3D12Resource* g_intermediateResources[kRuntimeTextureCount] = {nullptr, nullptr, nullptr, nullptr};

	inline UINT g_srvDescriptorSize = 0;  // g_srvDescriptorSize は SRV Heap 内で次の Descriptor へ進む幅。

	// g_textureSrvHandlesCPU/GPU は Texture SRV の CPU 作成用・GPU 描画用ハンドル。
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_textureSrvHandlesCPU[kRuntimeTextureCount]{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_textureSrvHandlesGPU[kRuntimeTextureCount]{};

	// g_editorRuntimeManager は Play 中の Input / Physics を更新する Runtime。
	inline EditorRuntimeManager g_editorRuntimeManager{};

	// g_feelKitHaptics は XInput コントローラーの振動を管理する FeelKitHaptics インスタンス。
	inline FeelKitHaptics g_feelKitHaptics{};

	// g_editorSceneCameraController は SceneView のキーボード・マウスカメラ操作を扱う。
	inline EditorSceneCameraController g_editorSceneCameraController{};

	inline int g_selectedSceneObject = 0;  // g_selectedSceneObject は旧プレビュー選択番号。0:Model、1:Sprite、2:Light、3:Camera。
	inline int g_activeEditorTool = 1;  // g_activeEditorTool は Scene ギズモ操作種別。1:移動、2:回転、3:拡縮、4:統合。
	inline int g_editorViewportTabIndex = 0;  // g_editorViewportTabIndex は SceneView のタブ番号。0:Scene、1:Game、2:AssetStore。
	inline std::string g_selectedAssetPath;  // g_selectedAssetPath は Project パネルで選択中のアセットパス。
	inline std::string g_currentScenePath;  // g_currentScenePath は今開いている .scene の保存先。未保存シーンでは空文字。

	// g_hierarchyFilter / g_assetFilter は各検索欄の入力バッファ。
	inline char g_hierarchyFilter[128] = {};
	inline char g_assetFilter[128] = {};

	inline bool g_isConsoleCleared = false;  // g_isConsoleCleared は Console クリアボタン処理を反映するフラグ。
	inline bool g_isSceneRangeSelecting = false;  // g_isSceneRangeSelecting は SceneView の矩形選択ドラッグ中フラグ。
	inline bool g_isSceneMiddleCameraDragging = false;  // g_isScene*CameraDragging は中ボタン移動・右ボタン回転のドラッグ状態。
	inline bool g_isSceneRightCameraDragging = false;

	inline bool g_isGizmoLocalMode = true;  // g_isGizmoLocalMode はギズモを Local / World どちらの軸で動かすかのフラグ。
	inline bool g_isGizmoSnapEnabled = false;  // g_isGizmoSnapEnabled はギズモスナップを使うかどうかのフラグ。
	inline bool g_isSceneAssistVisible = true;  // g_isSceneAssistVisible は SceneView 操作説明の表示フラグ。
	inline bool g_isLegacyPreviewVisible = false;  // g_isLegacyPreviewVisible は旧プレビュー Model/Sprite を表示するかのフラグ。

	// g_gizmoSnapValues は X/Y/Z 各軸のスナップ量。
	inline float g_gizmoSnapValues[3] = {0.5f, 0.5f, 0.5f};

	inline ImVec2 g_sceneRangeStart = ImVec2(0.0f, 0.0f);  // g_sceneRangeStart / End は SceneView 矩形選択の開始点と現在点。
	inline ImVec2 g_sceneRangeEnd = ImVec2(0.0f, 0.0f);

	inline EditorScene g_editorScene;  // g_editorScene は GameObject / Component / Prefab を保持する編集 Scene。
	inline bool g_isEditorSceneInitialized = false;  // g_isEditorSceneInitialized / RuntimeInitialized は Scene と Play Runtime の初期化済みフラグ。
	inline bool g_isEditorRuntimeInitialized = false;

	inline int32_t g_selectedEditorGameObjectId = -1;  // g_selectedEditorGameObjectId は Inspector / Hierarchy で選択中の GameObject ID。
	inline int32_t g_previousSelectedEditorGameObjectId = -1;  // g_previousSelectedEditorGameObjectId は選択変更検出用の前回 GameObject ID。

	// g_selectedGameObjectName は Inspector の名前編集用 char バッファ。
	inline char g_selectedGameObjectName[128] = {};

	inline int32_t g_selectedAddComponentIndex = 0;  // g_selectedAddComponentIndex は Inspector の追加 Component コンボ選択番号。

	// g_editorConsoleMessages は Console パネルへ表示するログ文字列一覧。
	inline std::vector<std::string> g_editorConsoleMessages = {
		"Editor: Ready",
		"Scene: Empty startup",
		"Runtime: Play physics waits for Play button",
	};

	inline EditorSelectionManager g_editorSelectionManager;  // g_editorSelectionManager は GameObject 選択と SceneObject 選択を同期する。
	inline EditorSceneSynchronizer g_editorSceneSynchronizer;  // g_editorSceneSynchronizer は EditorScene から描画用 SceneObject を作る。
	inline EditorAssetFactory g_editorAssetFactory;  // g_editorAssetFactory は Project からの配置で GameObject と Component を作る。
	inline EditorMainMenuBar g_editorMainMenuBar;  // g_editorMainMenuBar は上部メニューと Play/Stop ボタンを描画する。
	inline EditorHierarchyPanel g_editorHierarchyPanel;  // g_editorHierarchyPanel は GameObject ツリーと親子付けを描画・処理する。
	inline EditorInspectorPanel g_editorInspectorPanel;  // g_editorInspectorPanel は選択対象の Transform と Component を編集する。
	inline EditorBottomPanel g_editorBottomPanel;  // g_editorBottomPanel は Project アセット一覧と Console を表示する。
	inline bool g_isEditorManagerInitialized = false;  // g_isEditorManagerInitialized は Panel / Manager の参照設定が済んだかを表す。
	inline bool g_isDockLayoutInitialized = false;  // g_isDockLayoutInitialized は DockBuilder 初期配置を一度だけ実行するためのフラグ。

	inline ID3D12Resource* CreateRuntimeDepthStencilResource(uint32_t width, uint32_t height) {
		// heapProperties は DepthStencil を GPU 専用メモリに置くための指定。
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		// resourceDesc は width / height に合わせた 2D DepthStencil Texture の設定。
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Width = width;
		resourceDesc.Height = height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		ID3D12Resource* resource = nullptr;  // resource は CreateCommittedResource が作成して返す DepthStencil 実体。

		// createResult は DepthStencil 作成 API の成否。
		HRESULT createResult = g_device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&g_depthClearValue,
			IID_PPV_ARGS(&resource));
		assert(SUCCEEDED(createResult));
		return resource;
	}

	inline void WaitForGpu() {
		g_fenceValue++;  // g_fenceValue を進めて、このフレームで待つ GPU 完了番号を作る。
		HRESULT signalResult = g_commandQueue->Signal(g_fence.Get(), g_fenceValue);  // Signal は CommandQueue へ、ここまでの GPU 作業番号を登録する。
		assert(SUCCEEDED(signalResult));

		// GPU がまだ g_fenceValue まで終わっていなければ、Event を使って CPU を待たせる。
		if (g_fence->GetCompletedValue() < g_fenceValue) {
			signalResult = g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
			assert(SUCCEEDED(signalResult));
			WaitForSingleObject(g_fenceEvent, INFINITE);
		}
	}

	inline void UpdateEditorLayout() {
		constexpr float editorMenuHeight = 20.0f;  // editorMenuHeight は上部メニュー、editorSceneHeaderHeight は SceneView タブの高さ。
		constexpr float editorSceneHeaderHeight = 24.0f;

		g_editorLeftWidth = (std::clamp)(g_editorLeftWidth, 160.0f, 420.0f);  // 各パネル幅・高さを操作可能な範囲に制限し、SceneView が潰れないようにする。
		g_editorRightWidth = (std::clamp)(g_editorRightWidth, 220.0f, 520.0f);
		g_editorBottomHeight = (std::clamp)(g_editorBottomHeight, 120.0f, 320.0f);

		g_editorSceneX = g_editorLeftWidth;  // SceneView 左上は左パネル幅とメニュー・タブ高さから決める。
		g_editorSceneY = editorMenuHeight + editorSceneHeaderHeight;

		g_editorSceneWidth = g_editorWindowWidth - g_editorLeftWidth - g_editorRightWidth;  // SceneView 幅は Window 幅から左・右パネルを引いた残り。
		g_editorSceneHeight = g_editorWindowHeight - g_editorSceneY - g_editorBottomHeight;  // SceneView 高さは Window 高さから上部領域と下部パネルを引いた残り。
		g_editorSceneWidth = (std::max)(g_editorSceneWidth, 240.0f);  // DirectX viewport の 0 サイズを避けるため、最低幅・高さを確保する。
		g_editorSceneHeight = (std::max)(g_editorSceneHeight, 180.0f);
	}

	inline void ResizeRenderTargets(uint32_t width, uint32_t height) {
		// サイズが変わっていない場合は SwapChain と DepthStencil を作り直さない。
		if (width == g_renderWidth && height == g_renderHeight) {
			return;
		}

		WaitForGpu();  // 古い back buffer を GPU が使い終わるまで待ってから Release する。

		for (ID3D12Resource*& swapChainResource : g_swapChainResources) {
			// ResizeBuffers 前に全 back buffer 参照を解放する。
			if (swapChainResource != nullptr) {
				swapChainResource->Release();
				swapChainResource = nullptr;
			}
		}

		// DepthStencil も描画サイズに依存するため古いものを解放する。
		if (g_depthStencilResource != nullptr) {
			g_depthStencilResource->Release();
			g_depthStencilResource = nullptr;
		}

		g_renderWidth = width;  // g_renderWidth / Height は新しく作る back buffer サイズ。
		g_renderHeight = height;

		// SwapChain の back buffer を新しいサイズで再確保する。
		HRESULT resizeResult = g_swapChain->ResizeBuffers(
			2,
			g_renderWidth,
			g_renderHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			0);
		assert(SUCCEEDED(resizeResult));

		for (uint32_t bufferIndex = 0; bufferIndex < kRuntimeSwapChainBufferCount; bufferIndex++) {
			resizeResult = g_swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&g_swapChainResources[bufferIndex]));  // Resize 後の back buffer を取得して RTV を再作成する。
			assert(SUCCEEDED(resizeResult));
			g_device->CreateRenderTargetView(
				g_swapChainResources[bufferIndex],
				&g_rtvDesc,
				g_rtvHandles[bufferIndex]);
		}

		g_depthStencilResource = CreateRuntimeDepthStencilResource(g_renderWidth, g_renderHeight);  // DepthStencil も新しい描画サイズに合わせて再作成する。
		g_device->CreateDepthStencilView(g_depthStencilResource, &g_dsvDesc, g_dsvHandle);
	}
}

#pragma warning(pop)
