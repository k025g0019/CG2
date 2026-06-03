#pragma warning(push, 0)
#include <Windows.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
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
#include "externals/DirectXTex/DirectXTex.h"
#pragma warning(push)
#pragma warning(disable : 5045)
#include "externals/DirectXTex/d3dx12.h"
#pragma warning(pop)
#pragma warning(disable : 4820)
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <numbers>
#include <sdkddkver.h>
#include <sstream>
#include <string>
#include <vector>
#include <wrl.h>
#pragma warning(pop)
#pragma warning(disable : 4820)
#pragma warning(disable : 5045)

#include "ApplicationWindow.h"
#include "CrashHandler.h"
#include "EditorScene.h"
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

using Microsoft::WRL::ComPtr;


#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#include "ImGuizmo.h"
#pragma warning(pop)
#endif

namespace {
	// ================================
	// [Large] Core data structures
	// ================================
	struct Vector4 {
		float x;
		float y;
		float z;
		float w;
	};

	struct Vector2 {
		float x;
		float y;
	};

	struct Transforms {
		Vector3 scale;
		Vector3 rotate;
		Vector3 translate;
	};

	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};

	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float padding[3];
		Matrix4x4 uvTransform;
	};

	struct DirectionalLight {
		Vector4 color;
		Vector3 direction;
		float intensity;
	};

	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

	struct Sprite {
		Vector2 position;
		Vector2 size;
	};

	struct MaterialData {
		std::string textureFilePath;
	};

	struct ModelData {
		std::vector<VertexData> vertices;
		MaterialData material;
	};

	// ================================
	// [Large] Sound data structures
	// ================================
	struct ChunkHeader {
		char id[4];
		int32_t size;
	};

	struct RiffHeader {
		ChunkHeader chunk;
		char type[4];
	};

	struct FormatChunk {
		ChunkHeader chunk;
		WAVEFORMATEX format;
	};

	struct SoundData {
		WAVEFORMATEX wfex;
		BYTE* pBuffer;
		uint32_t bufferSize;
	};

	// [Middle] File and model helpers
	ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes);
	MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
	ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

	// [Middle] Sound helpers
	SoundData SoundLoadWave(const char* filePath);
	void SoundUnload(SoundData* soundData);

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap, UINT descriptorSize, UINT index) {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += descriptorSize * index;
		return handle;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap, UINT descriptorSize, UINT index) {
		D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += descriptorSize * index;
		return handle;
	}

	DirectX::ScratchImage LoadTexture(const std::wstring& filePath) {
		DirectX::TexMetadata metadata{};
		DirectX::ScratchImage image{};
		HRESULT hr = DirectX::LoadFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, &metadata, image);
		assert(SUCCEEDED(hr));

		DirectX::ScratchImage mipImages{};
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

	ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Width = static_cast<UINT>(metadata.width);
		resourceDesc.Height = static_cast<UINT>(metadata.height);
		resourceDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
		resourceDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
		resourceDesc.Format = metadata.format;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		ID3D12Resource* resource = nullptr;
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

	ID3D12Resource* UploadTextureData(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* commandList,
		ID3D12Resource* texture,
		const DirectX::ScratchImage& mipImages) {
		std::vector<D3D12_SUBRESOURCE_DATA> subresources;
		subresources.reserve(mipImages.GetImageCount());
		const DirectX::Image* images = mipImages.GetImages();
		for (size_t index = 0; index < mipImages.GetImageCount(); ++index) {
			D3D12_SUBRESOURCE_DATA subresource{};
			subresource.pData = images[index].pixels;
			subresource.RowPitch = static_cast<LONG_PTR>(images[index].rowPitch);
			subresource.SlicePitch = static_cast<LONG_PTR>(images[index].slicePitch);
			subresources.push_back(subresource);
		}

		UINT64 intermediateSize = GetRequiredIntermediateSize(texture, 0, static_cast<UINT>(subresources.size()));
		ID3D12Resource* intermediateResource = CreateBufferResource(device, intermediateSize);

		UpdateSubresources(
			commandList,
			texture,
			intermediateResource,
			0,
			0,
			static_cast<UINT>(subresources.size()),
			subresources.data());

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texture;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
		commandList->ResourceBarrier(1, &barrier);

		return intermediateResource;
	}

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

	ComPtr<IDxcBlob> CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile,
		IDxcUtils* dxcUtils,
		IDxcCompiler3* dxcCompiler,
		IDxcIncludeHandler* includeHandler,
		std::ofstream& logStream) {
		Log(logStream, std::format("Begin CompileShader, path:{}, profile:{}",
		                           ConvertString(filePath), ConvertString(std::wstring{profile})));

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
		                           ConvertString(filePath), ConvertString(std::wstring{profile})));

		return shaderBlob;
	}

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

	void SoundUnload(SoundData* soundData) {
		// [Small] Free wave memory buffer
		assert(soundData != nullptr);
		delete[] soundData->pBuffer;
		soundData->pBuffer = nullptr;
		soundData->bufferSize = 0u;
	}
}

#pragma warning(push)
#pragma warning(disable : 5045)
// ================================
// [Large] WinMain entry
// ================================
int WINAPI WinMain(_In_ HINSTANCE instanceHandle, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	InstallCrashHandler();

	// [Middle] Create log file
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

	HWND windowHandle = CreateMainWindow(instanceHandle, logStream);
	if (windowHandle == nullptr) {
		return 1;
	}

	HRESULT hr = S_OK;
	IDirectInput8* directInput = nullptr;
	hr = DirectInput8Create(
		instanceHandle, DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast<void**>(&directInput), nullptr);
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || directInput == nullptr) {
		return 1;
	}

	IDirectInputDevice8* keyboardDevice = nullptr;
	hr = directInput->CreateDevice(GUID_SysKeyboard, &keyboardDevice, nullptr);
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || keyboardDevice == nullptr) {
		directInput->Release();
		return 1;
	}

	hr = keyboardDevice->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));
	hr = keyboardDevice->SetCooperativeLevel(
		windowHandle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));

	BYTE key[256] = {};
	BYTE preKey[256] = {};

#ifdef _DEBUG
	// [Middle] Enable D3D12 debug layer
	ComPtr<ID3D12Debug1> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	MSG message{};
	Log(logStream, "main loop started");

	// ================================
	// [Large] Audio setup and playback
	// ================================
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

	SoundData soundData = SoundLoadWave("resources/sound/maou_19_12345.wav");
	IXAudio2SourceVoice* sourceVoice = nullptr;
	hr = xAudio2->CreateSourceVoice(&sourceVoice, &soundData.wfex);
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || sourceVoice == nullptr) {
		SoundUnload(&soundData);
		masterVoice->DestroyVoice();
		xAudio2->Release();
		return 1;
	}

	XAUDIO2_BUFFER soundBuffer{};
	soundBuffer.pAudioData = soundData.pBuffer;
	soundBuffer.AudioBytes = soundData.bufferSize;
	soundBuffer.Flags = XAUDIO2_END_OF_STREAM;
	hr = sourceVoice->SubmitSourceBuffer(&soundBuffer);
	assert(SUCCEEDED(hr));

	// ================================
	// [Large] DirectX12 setup
	// ================================

	ComPtr<IDXGIFactory7> dxgiFactory;
	hr = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
	assert(SUCCEEDED(hr));

	ComPtr<IDXGIAdapter4> useAdapter;
	for (UINT adapterIndex = 0;; ++adapterIndex) {
		ComPtr<IDXGIAdapter4> candidateAdapter;
		if (dxgiFactory->EnumAdapterByGpuPreference(
			adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(candidateAdapter.GetAddressOf())) == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = candidateAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
			continue;
		}

		useAdapter = candidateAdapter;
		Log(logStream, std::format("Use Adapter:{}", ConvertString(std::wstring{adapterDesc.Description})));
		break;
	}
	assert(useAdapter != nullptr);

	ComPtr<ID3D12Device> device;
	hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(device.GetAddressOf()));
	if (SUCCEEDED(hr)) {
		Log(logStream, "FeatureLevel:12.2");
	}
	else {
		hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(device.GetAddressOf()));
		if (SUCCEEDED(hr)) {
			Log(logStream, "FeatureLevel:12.1");
		}
		else {
			hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.GetAddressOf()));
			if (SUCCEEDED(hr)) {
				Log(logStream, "FeatureLevel:12.0");
			}
		}
	}
	assert(device != nullptr);
	Log(logStream, "Complete create D3D12Device!!!");

#ifdef _DEBUG
	ComPtr<ID3D12InfoQueue> infoQueue;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(infoQueue.GetAddressOf())))) {
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
		};
		D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO};
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		infoQueue->PushStorageFilter(&filter);
	}
#endif

	ComPtr<ID3D12CommandQueue> commandQueue;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || commandQueue == nullptr) {
		return 1;
	}


	ComPtr<ID3D12CommandAllocator> commandAllocator;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf()));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || commandAllocator == nullptr) {
		return 1;
	}

	ComPtr<ID3D12GraphicsCommandList> commandList;
	hr = device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf()));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || commandList == nullptr) {
		return 1;
	}
	hr = commandList->Close();
	assert(SUCCEEDED(hr));

	RECT clientRect{};
	GetClientRect(windowHandle, &clientRect);
	uint32_t renderWidth = (std::max)(1u, static_cast<uint32_t>(clientRect.right - clientRect.left));
	uint32_t renderHeight = (std::max)(1u, static_cast<uint32_t>(clientRect.bottom - clientRect.top));

	ComPtr<IDXGISwapChain4> swapChain;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = renderWidth;
	swapChainDesc.Height = renderHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	hr = dxgiFactory->CreateSwapChainForHwnd(
		commandQueue.Get(), windowHandle, &swapChainDesc, nullptr, nullptr,
		reinterpret_cast<IDXGISwapChain1**>(swapChain.GetAddressOf()));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || swapChain == nullptr) {
		return 1;
	}

	ID3D12DescriptorHeap* rtvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);
	ID3D12DescriptorHeap* srvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);
	ID3D12DescriptorHeap* dsvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	ID3D12Resource* swapChainResources[2] = {nullptr};
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];
	rtvHandles[0] = GetCPUDescriptorHandle(
		rtvDescriptorHeap, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), 0);
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);
	rtvHandles[1] = GetCPUDescriptorHandle(
		rtvDescriptorHeap, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), 1);
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	auto createDepthStencilResource = [&](uint32_t width, uint32_t height) -> ID3D12Resource* {
		D3D12_RESOURCE_DESC depthStencilResourceDesc{};
		depthStencilResourceDesc.Width = width;
		depthStencilResourceDesc.Height = height;
		depthStencilResourceDesc.MipLevels = 1;
		depthStencilResourceDesc.DepthOrArraySize = 1;
		depthStencilResourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilResourceDesc.SampleDesc.Count = 1;
		depthStencilResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthStencilResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		D3D12_HEAP_PROPERTIES depthStencilHeapProperties{};
		depthStencilHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		ID3D12Resource* newDepthStencilResource = nullptr;
		HRESULT createDepthResult = device->CreateCommittedResource(
			&depthStencilHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilResourceDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthClearValue,
			IID_PPV_ARGS(&newDepthStencilResource));
		assert(SUCCEEDED(createDepthResult));

		return newDepthStencilResource;
	};

	ID3D12Resource* depthStencilResource = createDepthStencilResource(renderWidth, renderHeight);
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandle);

	ComPtr<IDxcUtils> dxcUtils;
	ComPtr<IDxcCompiler3> dxcCompiler;
	ComPtr<IDxcIncludeHandler> includeHandler;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxcUtils.GetAddressOf()));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxcCompiler.GetAddressOf()));
	assert(SUCCEEDED(hr));
	hr = dxcUtils->CreateDefaultIncludeHandler(includeHandler.GetAddressOf());
	assert(SUCCEEDED(hr));

	ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(
		L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(
		L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[0].Descriptor.RegisterSpace = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;
	rootParameters[1].Descriptor.RegisterSpace = 0;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].Descriptor.ShaderRegister = 1;
	rootParameters[2].Descriptor.RegisterSpace = 0;

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

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

	ID3D12Resource* spriteMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));
	Material* spriteMaterialData = nullptr;
	spriteMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&spriteMaterialData));
	spriteMaterialData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	spriteMaterialData->enableLighting = FALSE;
	spriteMaterialData->padding[0] = 0.0f;
	spriteMaterialData->padding[1] = 0.0f;
	spriteMaterialData->padding[2] = 0.0f;
	spriteMaterialData->uvTransform = MakeIdentity4x4();

	ID3D12Resource* sphereMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));
	Material* sphereMaterialData = nullptr;
	sphereMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&sphereMaterialData));
	sphereMaterialData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	sphereMaterialData->enableLighting = TRUE;
	sphereMaterialData->padding[0] = 0.0f;
	sphereMaterialData->padding[1] = 0.0f;
	sphereMaterialData->padding[2] = 0.0f;
	sphereMaterialData->uvTransform = MakeIdentity4x4();

	ID3D12Resource* directionalLightResource = CreateBufferResource(device.Get(), sizeof(DirectionalLight));
	DirectionalLight* directionalLightData = nullptr;
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	directionalLightData->direction = {0.0f, -1.0f, 0.0f};
	directionalLightData->intensity = 1.0f;


	ID3D12Resource* spriteTransformationMatrixResource = CreateBufferResource(
		device.Get(), sizeof(TransformationMatrix));
	TransformationMatrix* spriteTransformationMatrixData = nullptr;
	spriteTransformationMatrixResource->Map(
		0, nullptr, reinterpret_cast<void**>(&spriteTransformationMatrixData));
	spriteTransformationMatrixData->WVP = MakeIdentity4x4();
	spriteTransformationMatrixData->World = MakeIdentity4x4();
	ID3D12Resource* sphereTransformationMatrixResource = CreateBufferResource(
		device.Get(), sizeof(TransformationMatrix));
	TransformationMatrix* sphereTransformationMatrixData = nullptr;
	sphereTransformationMatrixResource->Map(
		0, nullptr, reinterpret_cast<void**>(&sphereTransformationMatrixData));
	sphereTransformationMatrixData->WVP = MakeIdentity4x4();
	sphereTransformationMatrixData->World = MakeIdentity4x4();

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
	assert(SUCCEEDED(hr));

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

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.DepthClipEnable = TRUE;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
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

	ModelData modelData = LoadObjFile("resources", "plane.obj");

	constexpr uint32_t kSubdivision = 64;
	constexpr float kLonEvery = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kSubdivision);
	constexpr float kLatEvery = std::numbers::pi_v<float> / static_cast<float>(kSubdivision);

	std::vector<VertexData> vertices;
	vertices.reserve(kSubdivision * kSubdivision * 6);
	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		float lat = -std::numbers::pi_v<float> / 2.0f + kLatEvery * static_cast<float>(latIndex);
		float latNext = lat + kLatEvery;

		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			float lon = kLonEvery * static_cast<float>(lonIndex) + std::numbers::pi_v<float>;
			float lonNext = lon + kLonEvery;

			float u0 = static_cast<float>(lonIndex) / static_cast<float>(kSubdivision);
			float u1 = static_cast<float>(lonIndex + 1) / static_cast<float>(kSubdivision);
			float v0 = 1.0f - static_cast<float>(latIndex) / static_cast<float>(kSubdivision);
			float v1 = 1.0f - static_cast<float>(latIndex + 1) / static_cast<float>(kSubdivision);

			VertexData a{
				{std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon), 1.0f},
				{u0, v0},
				Normalize({std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)})
			};
			VertexData b{
				{std::cos(latNext) * std::cos(lon), std::sin(latNext), std::cos(latNext) * std::sin(lon), 1.0f},
				{u0, v1},
				Normalize({std::cos(latNext) * std::cos(lon), std::sin(latNext), std::cos(latNext) * std::sin(lon)})
			};
			VertexData c{
				{std::cos(lat) * std::cos(lonNext), std::sin(lat), std::cos(lat) * std::sin(lonNext), 1.0f},
				{u1, v0},
				Normalize({std::cos(lat) * std::cos(lonNext), std::sin(lat), std::cos(lat) * std::sin(lonNext)})
			};
			VertexData d{
				{std::cos(latNext) * std::cos(lonNext), std::sin(latNext), std::cos(latNext) * std::sin(lonNext), 1.0f},
				{u1, v1},
				Normalize(
					{std::cos(latNext) * std::cos(lonNext), std::sin(latNext), std::cos(latNext) * std::sin(lonNext)})
			};

			vertices.push_back(a);
			vertices.push_back(b);
			vertices.push_back(c);
			vertices.push_back(c);
			vertices.push_back(b);
			vertices.push_back(d);
		}
	}

	Sprite sprite{
		.position = {128.0f, 128.0f},
		.size = {256.0f, 256.0f}
	};
	VertexData spriteVertices[] = {
		{{-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
		{{-0.5f, 0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
		{{0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
		{{0.5f, 0.5f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
	};
	uint32_t spriteIndices[] = {
		0, 1, 2,
		2, 1, 3,
	};

	Transforms transform{
		.scale = {0.55f, 0.55f, 0.55f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, 0.0f}
	};
	Transforms spriteTransform{
		.scale = {sprite.size.x, sprite.size.y, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {sprite.position.x, sprite.position.y, 0.0f}
	};
	Transforms cameraTransform{
		.scale = {1.0f, 1.0f, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, -5.0f}
	};
	Transforms uvTransform{
		.scale = {1.0f, 1.0f, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, 0.0f}
	};
	ID3D12Resource* vertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * vertices.size());

	VertexData* mappedVertexData = nullptr;
	hr = vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedVertexData, vertices.data(), sizeof(VertexData) * vertices.size());

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	ID3D12Resource* modelVertexResource = CreateBufferResource(device.Get(),
	                                                           sizeof(VertexData) * modelData.vertices.size());
	VertexData* mappedModelVertexData = nullptr;
	hr = modelVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedModelVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedModelVertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());

	D3D12_VERTEX_BUFFER_VIEW modelVertexBufferView{};
	modelVertexBufferView.BufferLocation = modelVertexResource->GetGPUVirtualAddress();
	modelVertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelData.vertices.size());
	modelVertexBufferView.StrideInBytes = sizeof(VertexData);

	ID3D12Resource* spriteVertexResource = CreateBufferResource(device.Get(), sizeof(spriteVertices));
	VertexData* mappedSpriteVertexData = nullptr;
	hr = spriteVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedSpriteVertexData, spriteVertices, sizeof(spriteVertices));

	D3D12_VERTEX_BUFFER_VIEW spriteVertexBufferView{};
	spriteVertexBufferView.BufferLocation = spriteVertexResource->GetGPUVirtualAddress();
	spriteVertexBufferView.SizeInBytes = sizeof(spriteVertices);
	spriteVertexBufferView.StrideInBytes = sizeof(VertexData);

	ID3D12Resource* spriteIndexResource = CreateBufferResource(device.Get(), sizeof(spriteIndices));
	uint32_t* mappedSpriteIndexData = nullptr;
	hr = spriteIndexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteIndexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedSpriteIndexData, spriteIndices, sizeof(spriteIndices));

	D3D12_INDEX_BUFFER_VIEW spriteIndexBufferView{};
	spriteIndexBufferView.BufferLocation = spriteIndexResource->GetGPUVirtualAddress();
	spriteIndexBufferView.SizeInBytes = sizeof(spriteIndices);
	spriteIndexBufferView.Format = DXGI_FORMAT_R32_UINT;

	constexpr float editorMenuHeight = 20.0f;
	constexpr float editorSceneHeaderHeight = 24.0f;
	float editorWindowWidth = static_cast<float>(renderWidth);
	float editorWindowHeight = static_cast<float>(renderHeight);
	float editorLeftWidth = 250.0f;
	float editorRightWidth = 320.0f;
	float editorBottomHeight = 190.0f;
	float editorSceneX = editorLeftWidth;
	float editorSceneY = editorMenuHeight + editorSceneHeaderHeight;
	float editorSceneWidth =
		editorWindowWidth - editorLeftWidth - editorRightWidth;
	float editorSceneHeight =
		editorWindowHeight - editorSceneY - editorBottomHeight;
	auto updateEditorLayout = [&]() {
		editorLeftWidth = (std::clamp)(editorLeftWidth, 160.0f, 420.0f);
		editorRightWidth = (std::clamp)(editorRightWidth, 220.0f, 520.0f);
		editorBottomHeight = (std::clamp)(editorBottomHeight, 120.0f, 320.0f);
		editorSceneX = editorLeftWidth;
		editorSceneY = editorMenuHeight + editorSceneHeaderHeight;
		editorSceneWidth = editorWindowWidth - editorLeftWidth - editorRightWidth;
		editorSceneHeight = editorWindowHeight - editorSceneY - editorBottomHeight;
		editorSceneWidth = (std::max)(editorSceneWidth, 240.0f);
		editorSceneHeight = (std::max)(editorSceneHeight, 180.0f);
	};
	updateEditorLayout();

	D3D12_VIEWPORT viewport{};
	viewport.TopLeftX = editorSceneX;
	viewport.TopLeftY = editorSceneY;
	viewport.Width = editorWindowWidth;
	viewport.Height = editorWindowHeight;
	viewport.MaxDepth = 1.0f;
	viewport.Width = editorSceneWidth;
	viewport.Height = editorSceneHeight;

	D3D12_RECT scissorRect{};
	scissorRect.left = static_cast<LONG>(editorSceneX);
	scissorRect.top = static_cast<LONG>(editorSceneY);
	scissorRect.right = static_cast<LONG>(editorSceneX + editorSceneWidth);
	scissorRect.bottom = static_cast<LONG>(editorSceneY + editorSceneHeight);

	std::wstring textureFilePaths[] = {
		L"resources/uvChecker.png",
		L"resources/monsterBall.png",
		ConvertString(modelData.material.textureFilePath),
		L"resources/ball.png",
	};
	std::string textureFilePathStrings[_countof(textureFilePaths)];
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		textureFilePathStrings[textureIndex] = ConvertString(textureFilePaths[textureIndex]);
	}
	DirectX::ScratchImage mipImages[_countof(textureFilePaths)];
	DirectX::TexMetadata textureMetadatas[_countof(textureFilePaths)];
	ID3D12Resource* textureResources[_countof(textureFilePaths)] = {nullptr};
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		mipImages[textureIndex] = LoadTexture(textureFilePaths[textureIndex]);
		textureMetadatas[textureIndex] = mipImages[textureIndex].GetMetadata();
		textureResources[textureIndex] = CreateTextureResource(device.Get(), textureMetadatas[textureIndex]);
	}

	Matrix4x4 cameraMatrix = MakeAffineMatrix(
		cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
	Matrix4x4 viewMatrix = Inverse(cameraMatrix);
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(
		0.45f,
		editorSceneWidth / editorSceneHeight,
		0.1f,
		100.0f);
	Matrix4x4 spriteProjectionMatrix = MakeOrthographicMatrix(
		0.0f,
		0.0f,
		editorWindowWidth,
		editorWindowHeight,
		0.0f,
		100.0f);
	float editorCameraMoveSpeed = 0.12f;
	float editorCameraRotateSpeed = 0.006f;
	float editorCameraWheelMoveSpeed = 0.5f;
	float editorCameraPanSpeed = 0.01f;
	float editorCameraFastRate = 4.0f;
	float sceneClearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};
	bool isSceneGizmoVisible = true;
	bool isLightGizmoVisible = true;
	bool isCameraGizmoVisible = true;
	Vector3 directionalLightIconPosition = {-1.8f, 1.4f, 0.0f};

	//============================================================
	// エディタ用シーンオブジェクト
	//============================================================

	enum class EditorSceneObjectType {
		Model,
		Sprite,
	};

	struct EditorSceneObject {
		EditorSceneObjectType type;
		int32_t gameObjectId;
		int32_t textureIndex;
		Transforms transform;
		std::string name;
		ID3D12Resource* transformationResource;
		TransformationMatrix* transformationData;
	};

	std::vector<EditorSceneObject> editorSceneObjects;
	int32_t selectedPlacedSceneObjectIndex = -1;

	auto createEditorSceneObject = [&](EditorSceneObjectType type, int32_t textureIndex,
	                                   const Transforms& initialTransform,
	                                   const std::string& name) -> int32_t {
		EditorSceneObject editorSceneObject{};
		editorSceneObject.type = type;
		editorSceneObject.gameObjectId = -1;
		editorSceneObject.textureIndex = textureIndex;
		editorSceneObject.transform = initialTransform;
		editorSceneObject.name = name;
		editorSceneObject.transformationResource = CreateBufferResource(device.Get(), sizeof(TransformationMatrix));
		editorSceneObject.transformationResource->Map(
			0, nullptr, reinterpret_cast<void**>(&editorSceneObject.transformationData));
		editorSceneObject.transformationData->WVP = MakeIdentity4x4();
		editorSceneObject.transformationData->World = MakeIdentity4x4();

		editorSceneObjects.push_back(editorSceneObject);
		return static_cast<int32_t>(editorSceneObjects.size() - 1);
	};

	ComPtr<ID3D12Fence> fence;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
	assert(SUCCEEDED(hr));

	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent != nullptr);
	if (fenceEvent == nullptr) {
		return 1;
	}

	auto waitForGpu = [&]() {
		fenceValue++;
		HRESULT signalResult = commandQueue->Signal(fence.Get(), fenceValue);
		assert(SUCCEEDED(signalResult));

		if (fence->GetCompletedValue() < fenceValue) {
			HRESULT eventResult = fence->SetEventOnCompletion(fenceValue, fenceEvent);
			assert(SUCCEEDED(eventResult));
			WaitForSingleObject(fenceEvent, INFINITE);
		}
	};

	auto resizeRenderTargets = [&](uint32_t width, uint32_t height) {
		if (renderWidth == width && renderHeight == height) {
			return;
		}

		waitForGpu();

		for (ID3D12Resource*& swapChainResource : swapChainResources) {
			if (swapChainResource != nullptr) {
				swapChainResource->Release();
				swapChainResource = nullptr;
			}
		}

		if (depthStencilResource != nullptr) {
			depthStencilResource->Release();
			depthStencilResource = nullptr;
		}

		renderWidth = width;
		renderHeight = height;

		HRESULT resizeResult = swapChain->ResizeBuffers(
			swapChainDesc.BufferCount,
			renderWidth,
			renderHeight,
			swapChainDesc.Format,
			0);
		assert(SUCCEEDED(resizeResult));

		for (uint32_t bufferIndex = 0; bufferIndex < swapChainDesc.BufferCount; ++bufferIndex) {
			HRESULT getBufferResult =
				swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapChainResources[bufferIndex]));
			assert(SUCCEEDED(getBufferResult));
			device->CreateRenderTargetView(swapChainResources[bufferIndex], &rtvDesc, rtvHandles[bufferIndex]);
		}

		depthStencilResource = createDepthStencilResource(renderWidth, renderHeight);
		device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandle);
	};

	hr = commandAllocator->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr);
	assert(SUCCEEDED(hr));

	ID3D12Resource* intermediateResources[_countof(textureFilePaths)] = {nullptr};
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		intermediateResources[textureIndex] = UploadTextureData(
			device.Get(), commandList.Get(), textureResources[textureIndex], mipImages[textureIndex]);
	}

	UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandlesCPU[_countof(textureFilePaths)];
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandlesGPU[_countof(textureFilePaths)];
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = textureMetadatas[textureIndex].format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(textureMetadatas[textureIndex].mipLevels);

		textureSrvHandlesCPU[textureIndex] = GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize,
		                                                            textureIndex + 1);
		textureSrvHandlesGPU[textureIndex] = GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize,
		                                                            textureIndex + 1);
		device->CreateShaderResourceView(textureResources[textureIndex], &srvDesc, textureSrvHandlesCPU[textureIndex]);
	}

	hr = commandList->Close();
	assert(SUCCEEDED(hr));
	ID3D12CommandList* uploadCommandLists[] = {commandList.Get()};
	commandQueue->ExecuteCommandLists(1, uploadCommandLists);

	fenceValue++;
	hr = commandQueue->Signal(fence.Get(), fenceValue);
	assert(SUCCEEDED(hr));
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		assert(SUCCEEDED(hr));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

#ifdef USE_IMGUI

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(windowHandle);
	ImGui_ImplDX12_Init(
		device.Get(),
		static_cast<int>(swapChainDesc.BufferCount),
		rtvDesc.Format,
		srvDescriptorHeap,
		GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 0),
		GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 0));
	ImGuiIO& io = ImGui::GetIO();
	if (std::filesystem::exists("C:/Windows/Fonts/meiryo.ttc")) {
		io.Fonts->AddFontFromFileTTF(
			"C:/Windows/Fonts/meiryo.ttc", 16.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	}
	io.Fonts->Build();
#endif
	while (message.message != WM_QUIT) {
		if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
			TranslateMessage(&message);
			DispatchMessage(&message);
		}
		else {
			memcpy(preKey, key, sizeof(key));
			hr = keyboardDevice->Acquire();
			hr = keyboardDevice->GetDeviceState(sizeof(key), key);
			if (FAILED(hr)) {
				keyboardDevice->Acquire();
				hr = keyboardDevice->GetDeviceState(sizeof(key), key);
			}

			if (key[DIK_ESCAPE]) {
				PostQuitMessage(0);
			}

			float cameraPitchCos = std::cos(cameraTransform.rotate.x);
			Vector3 cameraForward{
				std::sin(cameraTransform.rotate.y) * cameraPitchCos,
				-std::sin(cameraTransform.rotate.x),
				std::cos(cameraTransform.rotate.y) * cameraPitchCos
			};
			Vector3 cameraRight{
				std::cos(cameraTransform.rotate.y),
				0.0f,
				-std::sin(cameraTransform.rotate.y)
			};
			float currentCameraMoveSpeed = editorCameraMoveSpeed;
			bool isCameraFastMove = key[DIK_LSHIFT] != 0 || key[DIK_RSHIFT] != 0;
			if (isCameraFastMove) {
				currentCameraMoveSpeed *= editorCameraFastRate;
			}

			if (key[DIK_LEFT]) {
				cameraTransform.rotate.y -= editorCameraRotateSpeed * 3.0f;
			}
			if (key[DIK_RIGHT]) {
				cameraTransform.rotate.y += editorCameraRotateSpeed * 3.0f;
			}
			if (key[DIK_UP]) {
				cameraTransform.rotate.x -= editorCameraRotateSpeed * 3.0f;
			}
			if (key[DIK_DOWN]) {
				cameraTransform.rotate.x += editorCameraRotateSpeed * 3.0f;
			}
			cameraTransform.rotate.x = (std::clamp)(cameraTransform.rotate.x, -1.5f, 1.5f);

			if (key[DIK_A]) {
				cameraTransform.translate.x -= cameraRight.x * currentCameraMoveSpeed;
				cameraTransform.translate.z -= cameraRight.z * currentCameraMoveSpeed;
			}
			if (key[DIK_D]) {
				cameraTransform.translate.x += cameraRight.x * currentCameraMoveSpeed;
				cameraTransform.translate.z += cameraRight.z * currentCameraMoveSpeed;
			}
			if (key[DIK_Q]) {
				cameraTransform.translate.y += currentCameraMoveSpeed;
			}
			if (key[DIK_E]) {
				cameraTransform.translate.y -= currentCameraMoveSpeed;
			}
			if (key[DIK_W]) {
				cameraTransform.translate.x += cameraForward.x * currentCameraMoveSpeed;
				cameraTransform.translate.y += cameraForward.y * currentCameraMoveSpeed;
				cameraTransform.translate.z += cameraForward.z * currentCameraMoveSpeed;
			}
			if (key[DIK_S]) {
				cameraTransform.translate.x -= cameraForward.x * currentCameraMoveSpeed;
				cameraTransform.translate.y -= cameraForward.y * currentCameraMoveSpeed;
				cameraTransform.translate.z -= cameraForward.z * currentCameraMoveSpeed;
			}

			bool isReturnTrigger =
				((key[DIK_RETURN] != 0) && (preKey[DIK_RETURN] == 0));
			if (isReturnTrigger) {
				uvTransform.translate = {0.0f, 0.0f, 0.0f};
				cameraTransform.rotate = {0.0f, 0.0f, 0.0f};
				cameraTransform.translate = {0.0f, 0.0f, -5.0f};
			}

			RECT currentClientRect{};
			GetClientRect(windowHandle, &currentClientRect);
			uint32_t nextRenderWidth =
				(std::max)(1u, static_cast<uint32_t>(currentClientRect.right - currentClientRect.left));
			uint32_t nextRenderHeight =
				(std::max)(1u, static_cast<uint32_t>(currentClientRect.bottom - currentClientRect.top));
			resizeRenderTargets(nextRenderWidth, nextRenderHeight);
			editorWindowWidth = static_cast<float>(renderWidth);
			editorWindowHeight = static_cast<float>(renderHeight);

#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			static int selectedSceneObject = 0;
			static int activeEditorTool = 1;
			static int editorViewportTabIndex = 0;
			static std::string selectedAssetPath = "resources/uvChecker.png";
			static char hierarchyFilter[64] = {};
			static char assetFilter[64] = {};
			static bool isConsoleCleared = false;
			static bool isSceneRangeSelecting = false;
			static bool isSceneMiddleCameraDragging = false;
			static bool isSceneRightCameraDragging = false;
			static bool isGizmoLocalMode = true;
			static bool isGizmoSnapEnabled = false;
			static bool isSceneAssistVisible = true;
			static float gizmoSnapValues[3] = {0.5f, 0.5f, 0.5f};
			static auto sceneRangeStart = ImVec2(0.0f, 0.0f);
			static auto sceneRangeEnd = ImVec2(0.0f, 0.0f);
			static EditorScene editorScene;
			static bool isEditorSceneInitialized = false;
			static int32_t selectedEditorGameObjectId = -1;
			static int32_t previousSelectedEditorGameObjectId = -1;
			static char selectedGameObjectName[128] = {};
			static int32_t selectedAddComponentIndex = 0;

			if (!isEditorSceneInitialized) {
				editorScene.InitializeDefaultScene();
				if (!editorScene.GetGameObjects().empty()) {
					selectedEditorGameObjectId = editorScene.GetGameObjects()[0].id;
				}
				isEditorSceneInitialized = true;
			}

			auto syncLegacySelection = [&]() {
				const EditorGameObject* gameObject = editorScene.FindGameObject(selectedEditorGameObjectId);
				if (gameObject == nullptr) {
					return;
				}

				selectedPlacedSceneObjectIndex = -1;
				for (int32_t sceneObjectIndex = 0;
				     sceneObjectIndex < static_cast<int32_t>(editorSceneObjects.size());
				     ++sceneObjectIndex) {
					const EditorSceneObject& sceneObject = editorSceneObjects[static_cast<size_t>(sceneObjectIndex)];
					if (sceneObject.gameObjectId == gameObject->id) {
						selectedPlacedSceneObjectIndex = sceneObjectIndex;
						selectedSceneObject = sceneObject.type == EditorSceneObjectType::Model ? 0 : 1;
						return;
					}
				}

				if (editorScene.HasComponent(gameObject->id, EditorComponentType::ModelRenderer)) {
					selectedSceneObject = 0;
				}
				else if (editorScene.HasComponent(gameObject->id, EditorComponentType::SpriteRenderer)) {
					selectedSceneObject = 1;
				}
				else if (editorScene.HasComponent(gameObject->id, EditorComponentType::Light)) {
					selectedSceneObject = 2;
				}
				else if (editorScene.HasComponent(gameObject->id, EditorComponentType::Camera)) {
					selectedSceneObject = 3;
				}
			};

			auto getTextureIndex = [&](const std::string& path) -> int32_t {
				for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
					if (textureFilePathStrings[textureIndex] == path) {
						return static_cast<int32_t>(textureIndex);
					}
				}
				return -1;
			};

			auto getFilename = [](const std::string& path) -> std::string {
				size_t separator = path.find_last_of("/\\");
				if (separator == std::string::npos) {
					return path;
				}
				return path.substr(separator + 1);
			};

			auto hasExtension = [](const std::string& path, const char* extension) -> bool {
				size_t extensionLength = std::strlen(extension);
				if (path.size() < extensionLength) {
					return false;
				}
				return path.compare(path.size() - extensionLength, extensionLength, extension) == 0;
			};

			auto hasFilterText = [](const char* filterText) -> bool {
				return filterText != nullptr && filterText[0] != '\0';
			};

			auto matchesFilter = [](const std::string& text, const char* filterText) -> bool {
				if (filterText == nullptr || filterText[0] == '\0') {
					return true;
				}
				return text.find(filterText) != std::string::npos;
			};

			constexpr ImGuiWindowFlags fixedWindowFlags =
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoCollapse;

			if (ImGui::BeginMainMenuBar()) {
				if (ImGui::BeginMenu("ファイル")) {
					ImGui::MenuItem("保存", nullptr, false, false);
					ImGui::MenuItem("終了", nullptr, false, false);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("編集")) {
					ImGui::MenuItem("元に戻す", nullptr, false, false);
					ImGui::MenuItem("やり直し", nullptr, false, false);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("アセット")) {
					ImGui::MenuItem("再読み込み", nullptr, false, false);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("ゲームオブジェクト")) {
					ImGui::MenuItem("空のオブジェクト", nullptr, false, false);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("コンポーネント")) {
					ImGui::MenuItem("Mesh Renderer", nullptr, false, false);
					ImGui::MenuItem("Directional Light", nullptr, false, false);
					ImGui::EndMenu();
				}
				if (ImGui::BeginMenu("ウィンドウ")) {
					ImGui::MenuItem("レイアウト初期化", nullptr, false, false);
					ImGui::EndMenu();
				}
				ImGui::EndMainMenuBar();
			}

			updateEditorLayout();
			constexpr ImGuiWindowFlags splitterFlags =
				ImGuiWindowFlags_NoDecoration |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoBackground;
			constexpr float splitterHitWidth = 12.0f;

			updateEditorLayout();
			viewport.TopLeftX = editorSceneX;
			viewport.TopLeftY = editorSceneY;
			viewport.Width = editorSceneWidth;
			viewport.Height = editorSceneHeight;
			scissorRect.left = static_cast<LONG>(editorSceneX);
			scissorRect.top = static_cast<LONG>(editorSceneY);
			scissorRect.right = static_cast<LONG>(editorSceneX + editorSceneWidth);
			scissorRect.bottom = static_cast<LONG>(editorSceneY + editorSceneHeight);
			projectionMatrix = MakePerspectiveFovMatrix(0.45f, editorSceneWidth / editorSceneHeight, 0.1f, 100.0f);
			spriteProjectionMatrix = MakeOrthographicMatrix(
				0.0f,
				0.0f,
				editorWindowWidth,
				editorWindowHeight,
				0.0f,
				100.0f);
			cameraMatrix = MakeAffineMatrix(
				cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			viewMatrix = Inverse(cameraMatrix);

			ImDrawList* foregroundDrawList = ImGui::GetForegroundDrawList();
			ImVec2 sceneHeaderMin{editorSceneX, editorMenuHeight};
			ImVec2 sceneHeaderMax{editorSceneX + editorSceneWidth, editorSceneY};
			ImVec2 sceneViewMax{editorSceneX + editorSceneWidth, editorSceneY + editorSceneHeight};
			foregroundDrawList->AddRectFilled(sceneHeaderMin, sceneHeaderMax, IM_COL32(37, 37, 37, 255));
			foregroundDrawList->AddRect(sceneHeaderMin, sceneViewMax, IM_COL32(75, 75, 75, 255));
			foregroundDrawList->AddText(
				ImVec2(sceneHeaderMax.x - 115.0f, sceneHeaderMin.y + 5.0f), IM_COL32(180, 220, 255, 255),
				"Perspective");

			constexpr ImGuiWindowFlags tabHostFlags =
				ImGuiWindowFlags_NoDecoration |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoScrollbar |
				ImGuiWindowFlags_NoScrollWithMouse |
				ImGuiWindowFlags_NoBackground;
			ImGui::SetNextWindowPos(sceneHeaderMin, ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(editorSceneWidth - 120.0f, editorSceneHeaderHeight), ImGuiCond_Always);
			ImGui::Begin("ViewportTabsHost", nullptr, tabHostFlags);
			if (ImGui::BeginTabBar("ViewportTabs", ImGuiTabBarFlags_Reorderable)) {
				if (ImGui::BeginTabItem("Scene")) {
					editorViewportTabIndex = 0;
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Game")) {
					editorViewportTabIndex = 1;
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Asset Store")) {
					editorViewportTabIndex = 2;
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			ImGui::End();

			bool isSceneTabActive = editorViewportTabIndex == 0;

			constexpr ImGuiWindowFlags sceneOverlayFlags =
				ImGuiWindowFlags_NoDecoration |
				ImGuiWindowFlags_NoMove |
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoBackground;
			ImGui::SetNextWindowPos(ImVec2(editorSceneX, editorSceneY), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(editorSceneWidth, editorSceneHeight), ImGuiCond_Always);
			ImGui::Begin("SceneOverlay", nullptr, sceneOverlayFlags);

			auto getModelDropPosition = [&]() -> Vector3 {
				ImVec2 mousePosition = ImGui::GetIO().MousePos;
				float sceneRateX = (mousePosition.x - editorSceneX) / editorSceneWidth;
				float sceneRateY = (mousePosition.y - editorSceneY) / editorSceneHeight;
				return Vector3{
					(sceneRateX - 0.5f) * 4.0f,
					(0.5f - sceneRateY) * 3.0f,
					0.0f
				};
			};

			auto getSpriteDropPosition = [&]() -> Vector3 {
				ImVec2 mousePosition = ImGui::GetIO().MousePos;
				float sceneRateX = (mousePosition.x - editorSceneX) / editorSceneWidth;
				float sceneRateY = (mousePosition.y - editorSceneY) / editorSceneHeight;
				return Vector3{
					sceneRateX * editorWindowWidth,
					sceneRateY * editorWindowHeight,
					0.0f
				};
			};

			ImVec2 sceneInteractionMin{editorSceneX + 42.0f, editorSceneY};
			ImVec2 sceneInteractionMax{editorSceneX + editorSceneWidth, editorSceneY + editorSceneHeight};
			ImGui::SetCursorScreenPos(sceneInteractionMin);
			ImGui::InvisibleButton(
				"SceneDropTarget",
				ImVec2(editorSceneWidth - 42.0f, editorSceneHeight),
				ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_AllowOverlap);
			bool isSceneHovered = isSceneTabActive && ImGui::IsMouseHoveringRect(sceneInteractionMin, sceneInteractionMax);
			if (isSceneTabActive && ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
					std::string droppedAsset(
						static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize - 1));
					selectedAssetPath = droppedAsset;
					int32_t droppedTextureIndex = getTextureIndex(droppedAsset);
					if (droppedTextureIndex >= 0) {
						Transforms droppedSpriteTransform{
							.scale = {128.0f, 128.0f, 1.0f},
							.rotate = {0.0f, 0.0f, 0.0f},
							.translate = getSpriteDropPosition()
						};
						selectedPlacedSceneObjectIndex = createEditorSceneObject(
							EditorSceneObjectType::Sprite,
							droppedTextureIndex,
							droppedSpriteTransform,
							getFilename(droppedAsset));
						selectedSceneObject = 1;

						editorScene.PushUndo();
						selectedEditorGameObjectId = editorScene.CreateGameObject(getFilename(droppedAsset));
						editorSceneObjects[static_cast<size_t>(selectedPlacedSceneObjectIndex)].gameObjectId =
							selectedEditorGameObjectId;
						editorScene.AddComponent(selectedEditorGameObjectId, EditorComponentType::SpriteRenderer);
						if (EditorGameObject* gameObject = editorScene.FindGameObject(selectedEditorGameObjectId)) {
							gameObject->translate = droppedSpriteTransform.translate;
							gameObject->rotate = droppedSpriteTransform.rotate;
							gameObject->scale = droppedSpriteTransform.scale;
							for (EditorComponent& component : gameObject->components) {
								if (component.type == EditorComponentType::SpriteRenderer) {
									component.assetPath = droppedAsset;
								}
							}
						}
						previousSelectedEditorGameObjectId = -1;
					}
					else if (hasExtension(droppedAsset, ".obj")) {
						Transforms droppedModelTransform{
							.scale = {0.55f, 0.55f, 0.55f},
							.rotate = {0.0f, 0.0f, 0.0f},
							.translate = getModelDropPosition()
						};
						selectedPlacedSceneObjectIndex = createEditorSceneObject(
							EditorSceneObjectType::Model,
							2,
							droppedModelTransform,
							getFilename(droppedAsset));
						selectedSceneObject = 0;

						editorScene.PushUndo();
						selectedEditorGameObjectId = editorScene.CreateGameObject(getFilename(droppedAsset));
						editorSceneObjects[static_cast<size_t>(selectedPlacedSceneObjectIndex)].gameObjectId =
							selectedEditorGameObjectId;
						editorScene.AddComponent(selectedEditorGameObjectId, EditorComponentType::ModelRenderer);
						if (EditorGameObject* gameObject = editorScene.FindGameObject(selectedEditorGameObjectId)) {
							gameObject->translate = droppedModelTransform.translate;
							gameObject->rotate = droppedModelTransform.rotate;
							gameObject->scale = droppedModelTransform.scale;
							for (EditorComponent& component : gameObject->components) {
								if (component.type == EditorComponentType::ModelRenderer) {
									component.assetPath = droppedAsset;
								}
							}
						}
						previousSelectedEditorGameObjectId = -1;
					}
				}
				ImGui::EndDragDropTarget();
			}

			//============================================================
			// シーン操作
			//============================================================

			if (isSceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
				isSceneMiddleCameraDragging = true;
			}

			if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
				isSceneMiddleCameraDragging = false;
			}

			if (isSceneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				isSceneRightCameraDragging = true;
			}

			if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
				isSceneRightCameraDragging = false;
			}

			Vector3 cameraUp{
				cameraForward.y * cameraRight.z - cameraForward.z * cameraRight.y,
				cameraForward.z * cameraRight.x - cameraForward.x * cameraRight.z,
				cameraForward.x * cameraRight.y - cameraForward.y * cameraRight.x
			};

			if (isSceneMiddleCameraDragging) {
				ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
				cameraTransform.translate.x -= cameraRight.x * mouseDelta.x * editorCameraPanSpeed;
				cameraTransform.translate.y -= cameraRight.y * mouseDelta.x * editorCameraPanSpeed;
				cameraTransform.translate.z -= cameraRight.z * mouseDelta.x * editorCameraPanSpeed;
				cameraTransform.translate.x += cameraUp.x * mouseDelta.y * editorCameraPanSpeed;
				cameraTransform.translate.y += cameraUp.y * mouseDelta.y * editorCameraPanSpeed;
				cameraTransform.translate.z += cameraUp.z * mouseDelta.y * editorCameraPanSpeed;
			}

			if (isSceneRightCameraDragging) {
				ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;
				cameraTransform.rotate.y += mouseDelta.x * editorCameraRotateSpeed;
				cameraTransform.rotate.x += mouseDelta.y * editorCameraRotateSpeed;
				cameraTransform.rotate.x =
					(std::clamp)(cameraTransform.rotate.x, -1.5f, 1.5f);
			}

			if (isSceneHovered && ImGui::GetIO().MouseWheel != 0.0f) {
				float pitchCos = std::cos(cameraTransform.rotate.x);
				Vector3 wheelCameraForward{
					std::sin(cameraTransform.rotate.y) * pitchCos,
					-std::sin(cameraTransform.rotate.x),
					std::cos(cameraTransform.rotate.y) * pitchCos
				};
				cameraTransform.translate.x +=
					wheelCameraForward.x * ImGui::GetIO().MouseWheel * editorCameraWheelMoveSpeed;
				cameraTransform.translate.y +=
					wheelCameraForward.y * ImGui::GetIO().MouseWheel * editorCameraWheelMoveSpeed;
				cameraTransform.translate.z +=
					wheelCameraForward.z * ImGui::GetIO().MouseWheel * editorCameraWheelMoveSpeed;
			}

			auto projectWorldPosition = [&](const Vector3& worldPosition) -> ImVec2 {
				Matrix4x4 viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);
				Vector3 ndcPosition = Transform(worldPosition, viewProjectionMatrix);
				return ImVec2(
					editorSceneX + (ndcPosition.x + 1.0f) * 0.5f * editorSceneWidth,
					editorSceneY + (1.0f - ndcPosition.y) * 0.5f * editorSceneHeight);
			};

			auto projectSpritePosition = [&](const Vector3& spritePosition) -> ImVec2 {
				return ImVec2(
					editorSceneX + spritePosition.x / editorWindowWidth * editorSceneWidth,
					editorSceneY + spritePosition.y / editorWindowHeight * editorSceneHeight);
			};

			ImDrawList* sceneDrawList = ImGui::GetWindowDrawList();
			if (!isSceneTabActive) {
				const char* tabMessage = editorViewportTabIndex == 1
					                         ? "Game View は Scene と同じ描画結果を確認するためのタブです"
					                         : "Asset Store は今後アセット取得をまとめるためのタブです";
				ImVec2 messageSize = ImGui::CalcTextSize(tabMessage);
				ImVec2 messagePosition{
					editorSceneX + (editorSceneWidth - messageSize.x) * 0.5f,
					editorSceneY + editorSceneHeight * 0.5f
				};
				sceneDrawList->AddRectFilled(
					ImVec2(messagePosition.x - 16.0f, messagePosition.y - 12.0f),
					ImVec2(messagePosition.x + messageSize.x + 16.0f, messagePosition.y + messageSize.y + 12.0f),
					IM_COL32(20, 26, 34, 220),
					6.0f);
				sceneDrawList->AddText(messagePosition, IM_COL32(230, 235, 240, 255), tabMessage);
			}
			bool isGizmoHovered = false;
			bool isGizmoActive = false;
			Transforms* selectedModelTransform = &transform;
			bool hasSelectedModelTransform = selectedSceneObject == 0;

			if (selectedPlacedSceneObjectIndex >= 0 &&
				selectedPlacedSceneObjectIndex < static_cast<int32_t>(editorSceneObjects.size())) {
				EditorSceneObject& selectedPlacedSceneObject =
					editorSceneObjects[static_cast<size_t>(selectedPlacedSceneObjectIndex)];
				if (selectedPlacedSceneObject.type == EditorSceneObjectType::Model) {
					selectedModelTransform = &selectedPlacedSceneObject.transform;
					hasSelectedModelTransform = true;
				}
				else {
					hasSelectedModelTransform = false;
				}
			}

			auto syncSelectedPlacedObjectToGameObject = [&]() {
				if (selectedPlacedSceneObjectIndex < 0 ||
					selectedPlacedSceneObjectIndex >= static_cast<int32_t>(editorSceneObjects.size())) {
					return;
				}

				const EditorSceneObject& sceneObject =
					editorSceneObjects[static_cast<size_t>(selectedPlacedSceneObjectIndex)];
				EditorGameObject* gameObject = editorScene.FindGameObject(sceneObject.gameObjectId);
				if (gameObject == nullptr) {
					return;
				}

				gameObject->translate = sceneObject.transform.translate;
				gameObject->rotate = sceneObject.transform.rotate;
				gameObject->scale = sceneObject.transform.scale;
			};

			auto updateGizmoState = [&]() {
				isGizmoHovered = isGizmoHovered || ImGui::IsItemHovered();
				isGizmoActive = isGizmoActive || ImGui::IsItemActive();
			};

			auto getActiveEditorToolName = [&]() -> const char* {
				if (activeEditorTool == 1) {
					return "移動";
				}

				if (activeEditorTool == 2) {
					return "回転";
				}

				if (activeEditorTool == 3) {
					return "拡縮";
				}

				return "統合";
			};

			auto getActiveGizmoOperation = [&]() -> ImGuizmo::OPERATION {
				if (activeEditorTool == 2) {
					return ImGuizmo::ROTATE;
				}

				if (activeEditorTool == 3) {
					return ImGuizmo::SCALE;
				}

				if (activeEditorTool == 4) {
					return ImGuizmo::UNIVERSAL;
				}

				return ImGuizmo::TRANSLATE;
			};

			if (isSceneTabActive) {
				ImGuizmo::SetOrthographic(false);
				ImGuizmo::SetDrawlist(sceneDrawList);
				ImGuizmo::SetRect(editorSceneX, editorSceneY, editorSceneWidth, editorSceneHeight);

				if (hasSelectedModelTransform) {
					ImGuizmo::OPERATION gizmoOperation = getActiveGizmoOperation();
					Matrix4x4 selectedWorldMatrix = MakeAffineMatrix(
						selectedModelTransform->scale,
						selectedModelTransform->rotate,
						selectedModelTransform->translate);
					bool isManipulated = ImGuizmo::Manipulate(
						&viewMatrix.matrix[0][0],
						&projectionMatrix.matrix[0][0],
						gizmoOperation,
						isGizmoLocalMode ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
						&selectedWorldMatrix.matrix[0][0],
						nullptr,
						isGizmoSnapEnabled ? gizmoSnapValues : nullptr);
					isGizmoHovered = isGizmoHovered || ImGuizmo::IsOver(gizmoOperation);
					isGizmoActive = isGizmoActive || ImGuizmo::IsUsing();

					if (isManipulated) {
						float gizmoTranslation[3] = {};
						float gizmoRotationDegrees[3] = {};
						float gizmoScale[3] = {};
						ImGuizmo::DecomposeMatrixToComponents(
							&selectedWorldMatrix.matrix[0][0],
							gizmoTranslation,
							gizmoRotationDegrees,
							gizmoScale);
						constexpr float degreeToRadian = std::numbers::pi_v<float> / 180.0f;
						selectedModelTransform->translate = {
							gizmoTranslation[0],
							gizmoTranslation[1],
							gizmoTranslation[2]
						};
						selectedModelTransform->rotate = {
							gizmoRotationDegrees[0] * degreeToRadian,
							gizmoRotationDegrees[1] * degreeToRadian,
							gizmoRotationDegrees[2] * degreeToRadian
						};
						selectedModelTransform->scale = {
							(std::max)(0.01f, gizmoScale[0]),
							(std::max)(0.01f, gizmoScale[1]),
							(std::max)(0.01f, gizmoScale[2])
						};
						syncSelectedPlacedObjectToGameObject();
					}
				}

				Matrix4x4 viewManipulateModelMatrix = MakeIdentity4x4();
				Matrix4x4 viewManipulateViewMatrix = viewMatrix;
				ImGuizmo::ViewManipulate(
					&viewManipulateViewMatrix.matrix[0][0],
					&projectionMatrix.matrix[0][0],
					ImGuizmo::ROTATE,
					ImGuizmo::LOCAL,
					&viewManipulateModelMatrix.matrix[0][0],
					4.0f,
					ImVec2(editorSceneX + editorSceneWidth - 96.0f, editorSceneY + 12.0f),
					ImVec2(78.0f, 78.0f),
					IM_COL32(20, 26, 34, 190));

				isGizmoHovered = isGizmoHovered || ImGuizmo::IsViewManipulateHovered();
				isGizmoActive = isGizmoActive || ImGuizmo::IsUsingViewManipulate();

				if (ImGuizmo::IsUsingViewManipulate()) {
					Matrix4x4 viewManipulateCameraMatrix = Inverse(viewManipulateViewMatrix);
					float cameraTranslation[3] = {};
					float cameraRotationDegrees[3] = {};
					float cameraScale[3] = {};
					ImGuizmo::DecomposeMatrixToComponents(
						&viewManipulateCameraMatrix.matrix[0][0],
						cameraTranslation,
						cameraRotationDegrees,
						cameraScale);
					constexpr float degreeToRadian = std::numbers::pi_v<float> / 180.0f;
					cameraTransform.translate = {
						cameraTranslation[0],
						cameraTranslation[1],
						cameraTranslation[2]
					};
					cameraTransform.rotate = {
						cameraRotationDegrees[0] * degreeToRadian,
						cameraRotationDegrees[1] * degreeToRadian,
						cameraRotationDegrees[2] * degreeToRadian
					};
					cameraTransform.scale = {1.0f, 1.0f, 1.0f};
					cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
					viewMatrix = Inverse(cameraMatrix);
				}

				if (isSceneAssistVisible) {
					ImVec2 assistMin{editorSceneX + 52.0f, editorSceneY + 10.0f};
					ImVec2 assistMax{assistMin.x + 430.0f, assistMin.y + 66.0f};
					sceneDrawList->AddRectFilled(assistMin, assistMax, IM_COL32(16, 22, 30, 185), 6.0f);
					sceneDrawList->AddRect(assistMin, assistMax, IM_COL32(88, 105, 125, 180), 6.0f);
					sceneDrawList->AddText(
						ImVec2(assistMin.x + 10.0f, assistMin.y + 8.0f),
						IM_COL32(235, 240, 245, 255),
						"Scene操作: 右ドラッグ=回転 / 中ドラッグ=平行移動 / ホイール=前後");
					sceneDrawList->AddText(
						ImVec2(assistMin.x + 10.0f, assistMin.y + 30.0f),
						IM_COL32(170, 215, 255, 255),
						"オブジェクト: 左ドラッグ=範囲選択 / ギズモ軸=直接編集");
					char gizmoAssistText[128] = {};
					std::snprintf(
						gizmoAssistText,
						sizeof(gizmoAssistText),
						"Tool: %s / Mode: %s / Snap: %s",
						getActiveEditorToolName(),
						isGizmoLocalMode ? "Local" : "World",
						isGizmoSnapEnabled ? "On" : "Off");
					sceneDrawList->AddText(
						ImVec2(assistMin.x + 10.0f, assistMin.y + 50.0f),
						IM_COL32(200, 210, 220, 255),
						gizmoAssistText);
				}
			}

			//============================================================
			// 不可視オブジェクト用ギズモ
			//============================================================

			if (isSceneTabActive && isSceneGizmoVisible && isLightGizmoVisible) {
				ImVec2 lightIconPosition = projectWorldPosition(directionalLightIconPosition);
				ImU32 lightIconColor = IM_COL32(255, 220, 80, 255);
				sceneDrawList->AddCircleFilled(lightIconPosition, 8.0f, lightIconColor);
				for (int32_t rayIndex = 0; rayIndex < 8; ++rayIndex) {
					float angle = std::numbers::pi_v<float> * 2.0f *
						static_cast<float>(rayIndex) / 8.0f;
					ImVec2 rayStart{
						lightIconPosition.x + std::cos(angle) * 12.0f,
						lightIconPosition.y + std::sin(angle) * 12.0f
					};
					ImVec2 rayEnd{
						lightIconPosition.x + std::cos(angle) * 18.0f,
						lightIconPosition.y + std::sin(angle) * 18.0f
					};
					sceneDrawList->AddLine(rayStart, rayEnd, lightIconColor, 2.0f);
				}
				sceneDrawList->AddText(
					ImVec2(lightIconPosition.x + 14.0f, lightIconPosition.y - 8.0f),
					IM_COL32(255, 245, 180, 255),
					"Light");
				ImGui::SetCursorScreenPos(ImVec2(lightIconPosition.x - 20.0f, lightIconPosition.y - 20.0f));
				ImGui::InvisibleButton("DirectionalLightIcon", ImVec2(80.0f, 40.0f));
				updateGizmoState();
				if (ImGui::IsItemClicked()) {
					selectedPlacedSceneObjectIndex = -1;
					selectedSceneObject = 2;
				}
			}

			if (isSceneTabActive && isSceneGizmoVisible && isCameraGizmoVisible) {
				ImVec2 cameraIconPosition{
					editorSceneX + editorSceneWidth - 86.0f,
					editorSceneY + 104.0f
				};
				sceneDrawList->AddRect(
					ImVec2(cameraIconPosition.x, cameraIconPosition.y),
					ImVec2(cameraIconPosition.x + 30.0f, cameraIconPosition.y + 18.0f),
					IM_COL32(180, 220, 255, 255),
					2.0f,
					0,
					2.0f);
				sceneDrawList->AddTriangleFilled(
					ImVec2(cameraIconPosition.x + 30.0f, cameraIconPosition.y + 4.0f),
					ImVec2(cameraIconPosition.x + 42.0f, cameraIconPosition.y),
					ImVec2(cameraIconPosition.x + 42.0f, cameraIconPosition.y + 22.0f),
					IM_COL32(180, 220, 255, 210));
				sceneDrawList->AddText(
					ImVec2(cameraIconPosition.x - 6.0f, cameraIconPosition.y + 24.0f),
					IM_COL32(180, 220, 255, 255),
					"Camera");
				ImGui::SetCursorScreenPos(ImVec2(cameraIconPosition.x - 8.0f, cameraIconPosition.y - 8.0f));
				ImGui::InvisibleButton("DebugCameraIcon", ImVec2(68.0f, 54.0f));
				updateGizmoState();
				if (ImGui::IsItemClicked()) {
					selectedPlacedSceneObjectIndex = -1;
					selectedSceneObject = 3;
				}
			}

			//============================================================
			// 範囲選択
			//============================================================

			if (isSceneHovered && !isGizmoHovered && !isGizmoActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				isSceneRangeSelecting = true;
				sceneRangeStart = ImGui::GetIO().MousePos;
				sceneRangeEnd = sceneRangeStart;
			}

			if (isSceneRangeSelecting) {
				sceneRangeEnd = ImGui::GetIO().MousePos;
				ImVec2 rangeMin{
					(std::min)(sceneRangeStart.x, sceneRangeEnd.x),
					(std::min)(sceneRangeStart.y, sceneRangeEnd.y)
				};
				ImVec2 rangeMax{
					(std::max)(sceneRangeStart.x, sceneRangeEnd.x),
					(std::max)(sceneRangeStart.y, sceneRangeEnd.y)
				};
				sceneDrawList->AddRectFilled(rangeMin, rangeMax, IM_COL32(80, 150, 255, 40));
				sceneDrawList->AddRect(rangeMin, rangeMax, IM_COL32(80, 150, 255, 220), 0.0f, 0, 2.0f);

				if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
					float rangeWidth = rangeMax.x - rangeMin.x;
					float rangeHeight = rangeMax.y - rangeMin.y;
					bool hasRangeArea = rangeWidth >= 4.0f && rangeHeight >= 4.0f;
					ImVec2 modelScreenPosition = projectWorldPosition(transform.translate);
					bool isModelInRange =
						modelScreenPosition.x >= rangeMin.x &&
						modelScreenPosition.x <= rangeMax.x &&
						modelScreenPosition.y >= rangeMin.y &&
						modelScreenPosition.y <= rangeMax.y;

					if (hasRangeArea && isModelInRange) {
						selectedPlacedSceneObjectIndex = -1;
						selectedSceneObject = 0;
					}

					if (hasRangeArea && !isModelInRange) {
						for (int32_t sceneObjectIndex = 0;
						     sceneObjectIndex < static_cast<int32_t>(editorSceneObjects.size());
						     ++sceneObjectIndex) {
							EditorSceneObject& sceneObject = editorSceneObjects[static_cast<size_t>(sceneObjectIndex)];
							ImVec2 sceneObjectScreenPosition =
								sceneObject.type == EditorSceneObjectType::Model
									? projectWorldPosition(sceneObject.transform.translate)
									: projectSpritePosition(sceneObject.transform.translate);
							bool isSceneObjectInRange =
								sceneObjectScreenPosition.x >= rangeMin.x &&
								sceneObjectScreenPosition.x <= rangeMax.x &&
								sceneObjectScreenPosition.y >= rangeMin.y &&
								sceneObjectScreenPosition.y <= rangeMax.y;

							if (isSceneObjectInRange) {
								selectedPlacedSceneObjectIndex = sceneObjectIndex;
								selectedSceneObject = sceneObject.type == EditorSceneObjectType::Model ? 0 : 1;
								if (sceneObject.gameObjectId >= 0) {
									selectedEditorGameObjectId = sceneObject.gameObjectId;
									previousSelectedEditorGameObjectId = -1;
								}
								break;
							}
						}
					}

					isSceneRangeSelecting = false;
				}
			}
			ImGui::End();

			ImGui::SetNextWindowPos(ImVec2(0.0f, editorMenuHeight), ImGuiCond_Always);
			ImGui::SetNextWindowSize(
				ImVec2(editorLeftWidth, editorWindowHeight - editorMenuHeight - editorBottomHeight),
				ImGuiCond_Always);
			ImGui::Begin("ヒエラルキー###Hierarchy", nullptr, fixedWindowFlags);
			ImGui::InputText("検索", hierarchyFilter, _countof(hierarchyFilter));
			const char* objectNames[] = {
				"モデル",
				"スプライト",
				"ライト",
				"デバッグカメラ",
			};

			if (ImGui::Button("空のGameObject")) {
				editorScene.PushUndo();
				selectedEditorGameObjectId = editorScene.CreateGameObject("GameObject");
				selectedPlacedSceneObjectIndex = -1;
			}
			ImGui::SameLine();
			if (ImGui::Button("親解除")) {
				editorScene.PushUndo();
				editorScene.SetParent(selectedEditorGameObjectId, -1);
			}
			ImGui::Separator();

			std::function<void(int32_t, int32_t)> drawGameObjectNode =
				[&](int32_t gameObjectId, int32_t depth) {
				EditorGameObject* gameObject = editorScene.FindGameObject(gameObjectId);
				if (gameObject == nullptr) {
					return;
				}

				if (!matchesFilter(gameObject->name, hierarchyFilter) && depth == 0) {
					bool hasMatchedChild = false;
					for (int32_t childId : gameObject->children) {
						const EditorGameObject* child = editorScene.FindGameObject(childId);
						hasMatchedChild = hasMatchedChild ||
							(child != nullptr && matchesFilter(child->name, hierarchyFilter));
					}

					if (!hasMatchedChild) {
						return;
					}
				}

				ImGui::Indent(static_cast<float>(depth) * 14.0f);
				std::string label = gameObject->children.empty() ? "  " : "v ";
				label += gameObject->name;
				bool isSelected = selectedEditorGameObjectId == gameObject->id;
				if (ImGui::Selectable(label.c_str(), isSelected)) {
					selectedEditorGameObjectId = gameObject->id;
					selectedPlacedSceneObjectIndex = -1;
					syncLegacySelection();
				}
				if (ImGui::BeginDragDropSource()) {
					ImGui::SetDragDropPayload("GAME_OBJECT_ID", &gameObject->id, sizeof(gameObject->id));
					ImGui::Text("%s", gameObject->name.c_str());
					ImGui::EndDragDropSource();
				}
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GAME_OBJECT_ID")) {
						int32_t childId = *static_cast<const int32_t*>(payload->Data);
						editorScene.PushUndo();
						editorScene.SetParent(childId, gameObject->id);
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::Unindent(static_cast<float>(depth) * 14.0f);

				for (int32_t childId : gameObject->children) {
					drawGameObjectNode(childId, depth + 1);
				}
			};

			for (const EditorGameObject& gameObject : editorScene.GetGameObjects()) {
				if (gameObject.parentId == -1) {
					drawGameObjectNode(gameObject.id, 0);
				}
			}
			ImGui::End();

			ImGui::SetNextWindowPos(
				ImVec2(editorSceneX + editorSceneWidth, editorMenuHeight),
				ImGuiCond_Always);
			ImGui::SetNextWindowSize(
				ImVec2(editorRightWidth, editorWindowHeight - editorMenuHeight),
				ImGuiCond_Always);
			ImGui::Begin("インスペクター###Inspector", nullptr, fixedWindowFlags);
			const char* selectedObjectLabel = objectNames[selectedSceneObject];
			Transforms* inspectedModelTransform = &transform;
			Transforms* inspectedSpriteTransform = &spriteTransform;

			if (selectedPlacedSceneObjectIndex >= 0 &&
				selectedPlacedSceneObjectIndex < static_cast<int32_t>(editorSceneObjects.size())) {
				EditorSceneObject& selectedPlacedSceneObject =
					editorSceneObjects[static_cast<size_t>(selectedPlacedSceneObjectIndex)];
				selectedObjectLabel = selectedPlacedSceneObject.name.c_str();

				if (selectedPlacedSceneObject.type == EditorSceneObjectType::Model) {
					inspectedModelTransform = &selectedPlacedSceneObject.transform;
				}
				else {
					inspectedSpriteTransform = &selectedPlacedSceneObject.transform;
				}
			}

			EditorGameObject* selectedEditorGameObject = editorScene.FindGameObject(selectedEditorGameObjectId);
			if (selectedEditorGameObject != nullptr) {
				selectedObjectLabel = selectedEditorGameObject->name.c_str();

				if (previousSelectedEditorGameObjectId != selectedEditorGameObjectId) {
					strncpy_s(
						selectedGameObjectName,
						sizeof(selectedGameObjectName),
						selectedEditorGameObject->name.c_str(),
						_TRUNCATE);
					selectedGameObjectName[sizeof(selectedGameObjectName) - 1] = '\0';
					previousSelectedEditorGameObjectId = selectedEditorGameObjectId;
				}
			}

			ImGui::Text("選択: %s", selectedObjectLabel);
			ImGui::Separator();
			if (selectedEditorGameObject != nullptr) {
				constexpr auto kEditorScenePath = "resources/editorScene.scene";
				constexpr auto kEditorPrefabPath = "resources/editorPrefab.prefab";

				if (ImGui::CollapsingHeader("GameObject", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("有効", &selectedEditorGameObject->isActive);
					ImGui::InputText("名前", selectedGameObjectName, _countof(selectedGameObjectName));

					if (ImGui::IsItemDeactivatedAfterEdit()) {
						editorScene.PushUndo();
						editorScene.RenameGameObject(selectedEditorGameObject->id, selectedGameObjectName);
					}

					bool isGameObjectTransformChanged = false;
					isGameObjectTransformChanged |= ImGui::DragFloat3("位置", &selectedEditorGameObject->translate.x,
					                                                  0.01f);
					isGameObjectTransformChanged |= ImGui::DragFloat3("回転", &selectedEditorGameObject->rotate.x, 0.01f);
					isGameObjectTransformChanged |= ImGui::DragFloat3("拡大", &selectedEditorGameObject->scale.x, 0.01f,
					                                                  0.01f, 100.0f);
					if (isGameObjectTransformChanged &&
						selectedPlacedSceneObjectIndex >= 0 &&
						selectedPlacedSceneObjectIndex < static_cast<int32_t>(editorSceneObjects.size())) {
						EditorSceneObject& sceneObject =
							editorSceneObjects[static_cast<size_t>(selectedPlacedSceneObjectIndex)];
						if (sceneObject.gameObjectId == selectedEditorGameObject->id) {
							sceneObject.transform.translate = selectedEditorGameObject->translate;
							sceneObject.transform.rotate = selectedEditorGameObject->rotate;
							sceneObject.transform.scale = selectedEditorGameObject->scale;
						}
					}

					if (ImGui::Button("複製")) {
						editorScene.PushUndo();
						selectedEditorGameObjectId = editorScene.DuplicateGameObject(selectedEditorGameObject->id);
						previousSelectedEditorGameObjectId = -1;
						syncLegacySelection();
					}
					ImGui::SameLine();
					if (ImGui::Button("削除")) {
						ImGui::OpenPopup("GameObject削除確認");
					}
					ImGui::SameLine();
					if (ImGui::Button("Undo")) {
						editorScene.Undo();
						syncLegacySelection();
					}
					ImGui::SameLine();
					if (ImGui::Button("Redo")) {
						editorScene.Redo();
						syncLegacySelection();
					}

					if (ImGui::BeginPopupModal("GameObject削除確認", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
						ImGui::Text("選択中のGameObjectを削除しますか？");
						if (ImGui::Button("削除する")) {
							editorScene.PushUndo();
							editorScene.DeleteGameObject(selectedEditorGameObjectId);
							selectedEditorGameObjectId = editorScene.GetGameObjects().empty()
								                             ? -1
								                             : editorScene.GetGameObjects()[0].id;
							previousSelectedEditorGameObjectId = -1;
							selectedEditorGameObject = editorScene.FindGameObject(selectedEditorGameObjectId);
							ImGui::CloseCurrentPopup();
						}
						ImGui::SameLine();
						if (ImGui::Button("キャンセル")) {
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}
				}

				if (ImGui::CollapsingHeader("Scene / Prefab", ImGuiTreeNodeFlags_DefaultOpen)) {
					if (ImGui::Button("Scene保存")) {
						editorScene.SaveScene(kEditorScenePath);
					}
					ImGui::SameLine();
					if (ImGui::Button("Scene読込")) {
						if (editorScene.LoadScene(kEditorScenePath) && !editorScene.GetGameObjects().empty()) {
							selectedEditorGameObjectId = editorScene.GetGameObjects()[0].id;
							previousSelectedEditorGameObjectId = -1;
							syncLegacySelection();
						}
					}

					if (ImGui::Button("Prefab保存")) {
						editorScene.SavePrefab(selectedEditorGameObject->id, kEditorPrefabPath);
					}
					ImGui::SameLine();
					if (ImGui::Button("Prefab生成")) {
						editorScene.PushUndo();
						int32_t prefabId = editorScene.InstantiatePrefab(kEditorPrefabPath);
						if (prefabId >= 0) {
							selectedEditorGameObjectId = prefabId;
							previousSelectedEditorGameObjectId = -1;
						}
					}
				}

				if (ImGui::CollapsingHeader("Components", ImGuiTreeNodeFlags_DefaultOpen)) {
					const char* componentNames[] = {
						"ModelRenderer",
						"SpriteRenderer",
						"Light",
						"Camera",
						"AudioSource",
					};
					ImGui::Combo("追加するComponent", &selectedAddComponentIndex, componentNames, _countof(componentNames));
					if (ImGui::Button("Component追加")) {
						editorScene.PushUndo();
						editorScene.AddComponent(selectedEditorGameObject->id,
						                         ComponentTypeFromIndex(selectedAddComponentIndex));
						syncLegacySelection();
					}

					int32_t removeComponentIndex = -1;
					for (int32_t componentIndex = 0;
					     componentIndex < static_cast<int32_t>(selectedEditorGameObject->components.size());
					     ++componentIndex) {
						EditorComponent& component = selectedEditorGameObject->components[static_cast<size_t>(
							componentIndex)];
						ImGui::PushID(componentIndex);
						if (ImGui::TreeNodeEx(ToString(component.type).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
							ImGui::Checkbox("有効", &component.isActive);
							if (!component.assetPath.empty()) {
								ImGui::TextWrapped("Asset: %s", component.assetPath.c_str());
							}
							ImGui::ColorEdit3("色", &component.color.x);
							ImGui::DragFloat("強さ", &component.intensity, 0.01f, 0.0f, 10.0f);
							if (component.type != EditorComponentType::Transform && ImGui::Button("Component削除")) {
								removeComponentIndex = componentIndex;
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
					}

					if (removeComponentIndex >= 0) {
						editorScene.PushUndo();
						EditorComponentType removeType = selectedEditorGameObject->components[static_cast<size_t>(
							removeComponentIndex)].type;
						editorScene.RemoveComponent(selectedEditorGameObject->id, removeType);
						syncLegacySelection();
					}
				}
			}
			if (ImGui::CollapsingHeader("環境 / 背景", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::ColorEdit4("背景色", sceneClearColor);
				ImGui::Checkbox("ギズモ表示", &isSceneGizmoVisible);
				ImGui::Checkbox("ライトアイコン", &isLightGizmoVisible);
				ImGui::Checkbox("カメラアイコン", &isCameraGizmoVisible);
			}
			if (ImGui::CollapsingHeader("モデル / マテリアル", ImGuiTreeNodeFlags_DefaultOpen)) {
				bool isLighting = sphereMaterialData->enableLighting != FALSE;
				ImGui::Checkbox("ライティング", &isLighting);
				ImGui::ColorEdit4("マテリアル色", &sphereMaterialData->color.x);
				sphereMaterialData->enableLighting = isLighting ? TRUE : FALSE;
			}
			if (ImGui::CollapsingHeader("操作ツール", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Checkbox("ローカル座標", &isGizmoLocalMode);
				ImGui::Checkbox("スナップ", &isGizmoSnapEnabled);
				ImGui::DragFloat3("スナップ値", gizmoSnapValues, 0.01f, 0.01f, 10.0f);
				ImGui::RadioButton("移動", &activeEditorTool, 1);
				ImGui::SameLine();
				ImGui::RadioButton("回転", &activeEditorTool, 2);
				ImGui::SameLine();
				ImGui::RadioButton("拡縮", &activeEditorTool, 3);
				ImGui::SameLine();
				ImGui::RadioButton("統合", &activeEditorTool, 4);
				ImGui::Checkbox("Scene操作ヘルプ", &isSceneAssistVisible);
				ImGui::TextDisabled("右上のViewCubeで視点方向を変更できます。");
			}
			if (ImGui::CollapsingHeader("シーンカメラ操作", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::DragFloat("移動速度", &editorCameraMoveSpeed, 0.01f, 0.01f, 2.0f);
				ImGui::DragFloat("回転感度", &editorCameraRotateSpeed, 0.001f, 0.001f, 0.05f);
				ImGui::DragFloat("ホイール速度", &editorCameraWheelMoveSpeed, 0.01f, 0.05f, 5.0f);
				ImGui::DragFloat("中ボタン移動", &editorCameraPanSpeed, 0.001f, 0.001f, 0.1f);
				ImGui::DragFloat("Shift倍率", &editorCameraFastRate, 0.1f, 1.0f, 10.0f);
				ImGui::TextDisabled("右ドラッグ: 回転 / 中ドラッグ: 平行移動 / ホイール: 前後");
				ImGui::TextDisabled("WASD: 視点基準移動 / Q,E: 上下 / Shift: 高速");
			}
			ImGui::Separator();
			if (selectedSceneObject == 0) {
				if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::DragFloat3("位置", &inspectedModelTransform->translate.x, 0.01f);
					ImGui::DragFloat3("回転", &inspectedModelTransform->rotate.x, 0.01f);
					ImGui::DragFloat3("拡縮", &inspectedModelTransform->scale.x, 0.01f, 0.01f, 10.0f);
				}
				if (ImGui::CollapsingHeader("UV Transform")) {
					ImGui::DragFloat2("UV拡縮", &uvTransform.scale.x, 0.01f, 0.1f, 4.0f);
					ImGui::DragFloat("UV回転", &uvTransform.rotate.z, 0.01f, -3.14f, 3.14f);
					ImGui::DragFloat2("UV移動", &uvTransform.translate.x, 0.01f, -2.0f, 2.0f);
				}
			}
			else if (selectedSceneObject == 1) {
				if (ImGui::CollapsingHeader("Sprite", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::DragFloat3("位置", &inspectedSpriteTransform->translate.x, 1.0f);
					ImGui::DragFloat3("回転", &inspectedSpriteTransform->rotate.x, 0.01f);
					ImGui::DragFloat3("拡縮", &inspectedSpriteTransform->scale.x, 1.0f, 1.0f, 1024.0f);
				}
				if (ImGui::CollapsingHeader("Sprite Material", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::ColorEdit4("色", &spriteMaterialData->color.x);
				}
			}
			else if (selectedSceneObject == 2) {
				if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::ColorEdit4("色", &directionalLightData->color.x);
					ImGui::DragFloat3("方向", &directionalLightData->direction.x, 0.01f, -1.0f, 1.0f);
					ImGui::DragFloat("強さ", &directionalLightData->intensity, 0.01f, 0.0f, 4.0f);
					ImGui::DragFloat3("アイコン位置", &directionalLightIconPosition.x, 0.01f);
				}
			}
			else {
				if (ImGui::CollapsingHeader("Debug Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::DragFloat3("位置", &cameraTransform.translate.x, 0.1f);
					ImGui::DragFloat3("回転", &cameraTransform.rotate.x, 0.01f);
					if (ImGui::Button("カメラを初期化")) {
						cameraTransform.rotate = {0.0f, 0.0f, 0.0f};
						cameraTransform.translate = {0.0f, 0.0f, -5.0f};
					}
				}
			}
			ImGui::Separator();
			ImGui::Text("選択アセット");
			if (!selectedAssetPath.empty()) {
				ImGui::TextWrapped("%s", selectedAssetPath.c_str());
				int32_t textureIndex = getTextureIndex(selectedAssetPath);
				if (textureIndex >= 0) {
					ImGui::Image(
						ImTextureRef(textureSrvHandlesGPU[textureIndex].ptr),
						ImVec2(160.0f, 160.0f));
				}
			}
			ImGui::End();

			float bottomY = editorWindowHeight - editorBottomHeight;
			float bottomPanelWidth = editorWindowWidth - editorRightWidth;

			ImGui::SetNextWindowPos(ImVec2(0.0f, bottomY), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(bottomPanelWidth, editorBottomHeight), ImGuiCond_Always);
			ImGui::Begin("下部パネル###BottomPanel", nullptr, fixedWindowFlags);
			if (ImGui::BeginTabBar("BottomPanelTabs", ImGuiTabBarFlags_Reorderable)) {
				if (ImGui::BeginTabItem("Project")) {
					ImGui::InputText("検索", assetFilter, _countof(assetFilter));
					ImGui::BeginChild("Folders", ImVec2(180.0f, 0.0f), ImGuiChildFlags_Borders);
					ImGui::SetNextItemOpen(true, ImGuiCond_Once);
					if (ImGui::TreeNode("resources")) {
						ImGui::TreeNodeEx("sound", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
						ImGui::TreePop();
					}
					ImGui::EndChild();
					ImGui::SameLine();
					ImGui::BeginChild("Assets", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
					if (ImGui::BeginTable("AssetGrid", 4, ImGuiTableFlags_SizingStretchSame)) {
						const char* assetPaths[] = {
							"resources/ball.png",
							"resources/monsterBall.png",
							"resources/uvChecker.png",
							"resources/plane.obj",
							"resources/plane.mtl",
							"resources/sound/maou_19_12345.wav",
						};
						for (const char* assetPath : assetPaths) {
							std::string relativePath = assetPath;
							if (!matchesFilter(relativePath, assetFilter)) {
								continue;
							}
							ImGui::TableNextColumn();
							ImGui::PushID(relativePath.c_str());
							bool isPng = hasExtension(relativePath, ".png") || hasExtension(relativePath, ".PNG");
							int32_t textureIndex = getTextureIndex(relativePath);
							if (isPng && textureIndex >= 0) {
								ImGui::Image(
									ImTextureRef(textureSrvHandlesGPU[textureIndex].ptr),
									ImVec2(48.0f, 48.0f));
								if (ImGui::IsItemClicked()) {
									selectedAssetPath = relativePath;
								}
								if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
									ImGui::SetDragDropPayload("ASSET_PATH", relativePath.c_str(), relativePath.size() + 1);
									ImGui::Text("%s", relativePath.c_str());
									ImGui::EndDragDropSource();
								}
								std::string filename = getFilename(relativePath);
								ImGui::TextWrapped("%s", filename.c_str());
							}
							else {
								bool isSelected = selectedAssetPath == relativePath;
								auto assetIcon = "FILE";
								if (hasExtension(relativePath, ".wav")) {
									assetIcon = "WAV";
								}
								else if (hasExtension(relativePath, ".obj")) {
									assetIcon = "OBJ";
								}
								else if (hasExtension(relativePath, ".mtl")) {
									assetIcon = "MTL";
								}
								if (ImGui::Button(assetIcon, ImVec2(58.0f, 48.0f))) {
									selectedAssetPath = relativePath;
								}
								if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
									ImGui::SetDragDropPayload("ASSET_PATH", relativePath.c_str(), relativePath.size() + 1);
									ImGui::Text("%s", relativePath.c_str());
									ImGui::EndDragDropSource();
								}
								if (isSelected) {
									ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s",
									                   getFilename(relativePath).c_str());
								}
								else {
									ImGui::TextWrapped("%s", getFilename(relativePath).c_str());
								}
							}
							ImGui::PopID();
						}
						ImGui::EndTable();
					}
					if (hasFilterText(assetFilter)) {
						ImGui::TextDisabled("検索中: %s", assetFilter);
					}
					ImGui::EndChild();
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem("Console")) {
					if (ImGui::Button("ログ消去")) {
						isConsoleCleared = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("ログ表示")) {
						isConsoleCleared = false;
					}
					ImGui::Separator();
					if (!isConsoleCleared) {
						ImGui::Text("DirectX12: 初期化済み");
						ImGui::Text("入力: DirectInput キーボード");
						ImGui::Text("音声: XAudio2 wav待機");
						ImGui::Text("Scene: %.0f x %.0f", editorSceneWidth, editorSceneHeight);
					}
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
			ImGui::End();

			ImGui::SetNextWindowPos(ImVec2(editorLeftWidth - splitterHitWidth * 0.5f, editorMenuHeight),
			                        ImGuiCond_Always);
			ImGui::SetNextWindowSize(
				ImVec2(splitterHitWidth, editorWindowHeight - editorMenuHeight), ImGuiCond_Always);
			ImGui::Begin("LeftSplitter", nullptr, splitterFlags);
			ImGui::GetWindowDrawList()->AddRectFilled(
				ImVec2(editorLeftWidth - 1.0f, editorMenuHeight),
				ImVec2(editorLeftWidth + 1.0f, editorWindowHeight),
				IM_COL32(95, 115, 140, 255));
			ImGui::InvisibleButton("LeftSplitterButton", ImGui::GetContentRegionAvail());
			if (ImGui::IsItemActive()) {
				editorLeftWidth += ImGui::GetIO().MouseDelta.x;
			}
			ImGui::End();

			float rightSplitterX = editorWindowWidth - editorRightWidth;
			ImGui::SetNextWindowPos(ImVec2(rightSplitterX - splitterHitWidth * 0.5f, editorMenuHeight),
			                        ImGuiCond_Always);
			ImGui::SetNextWindowSize(
				ImVec2(splitterHitWidth, editorWindowHeight - editorMenuHeight), ImGuiCond_Always);
			ImGui::Begin("RightSplitter", nullptr, splitterFlags);
			ImGui::GetWindowDrawList()->AddRectFilled(
				ImVec2(rightSplitterX - 1.0f, editorMenuHeight),
				ImVec2(rightSplitterX + 1.0f, editorWindowHeight),
				IM_COL32(95, 115, 140, 255));
			ImGui::InvisibleButton("RightSplitterButton", ImGui::GetContentRegionAvail());
			if (ImGui::IsItemActive()) {
				editorRightWidth -= ImGui::GetIO().MouseDelta.x;
			}
			ImGui::End();

			float bottomSplitterY = editorWindowHeight - editorBottomHeight;
			ImGui::SetNextWindowPos(ImVec2(0.0f, bottomSplitterY - splitterHitWidth * 0.5f), ImGuiCond_Always);
			ImGui::SetNextWindowSize(
				ImVec2(editorWindowWidth - editorRightWidth, splitterHitWidth), ImGuiCond_Always);
			ImGui::Begin("BottomSplitter", nullptr, splitterFlags);
			ImGui::GetWindowDrawList()->AddRectFilled(
				ImVec2(0.0f, bottomSplitterY - 1.0f),
				ImVec2(editorWindowWidth - editorRightWidth, bottomSplitterY + 1.0f),
				IM_COL32(95, 115, 140, 255));
			ImGui::InvisibleButton("BottomSplitterButton", ImGui::GetContentRegionAvail());
			if (ImGui::IsItemActive()) {
				editorBottomHeight -= ImGui::GetIO().MouseDelta.y;
			}
			ImGui::End();
			ImGui::Render();
#endif

			cameraMatrix = MakeAffineMatrix(
				cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
			viewMatrix = Inverse(cameraMatrix);
			Matrix4x4 spriteWorldMatrix = MakeAffineMatrix(
				spriteTransform.scale, spriteTransform.rotate, spriteTransform.translate);
			Matrix4x4 spriteWorldViewProjectionMatrix = Multiply(spriteWorldMatrix, spriteProjectionMatrix);
			Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
			Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
			Matrix4x4 uvTransformMatrix = MakeAffineMatrix(
				uvTransform.scale, uvTransform.rotate, uvTransform.translate);
			spriteTransformationMatrixData->WVP = spriteWorldViewProjectionMatrix;
			spriteTransformationMatrixData->World = spriteWorldMatrix;
			sphereTransformationMatrixData->WVP = worldViewProjectionMatrix;
			sphereTransformationMatrixData->World = worldMatrix;
			spriteMaterialData->uvTransform = uvTransformMatrix;
			sphereMaterialData->uvTransform = uvTransformMatrix;
			for (EditorSceneObject& sceneObject : editorSceneObjects) {
				Matrix4x4 sceneObjectWorldMatrix = MakeAffineMatrix(
					sceneObject.transform.scale,
					sceneObject.transform.rotate,
					sceneObject.transform.translate);
				Matrix4x4 sceneObjectProjectionMatrix =
					sceneObject.type == EditorSceneObjectType::Sprite
						? spriteProjectionMatrix
						: Multiply(viewMatrix, projectionMatrix);
				sceneObject.transformationData->WVP = Multiply(sceneObjectWorldMatrix, sceneObjectProjectionMatrix);
				sceneObject.transformationData->World = sceneObjectWorldMatrix;
			}
			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator.Get(), graphicsPipelineState.Get());
			assert(SUCCEEDED(hr));

			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = swapChainResources[backBufferIndex];
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			commandList->ResourceBarrier(1, &barrier);

			commandList->RSSetViewports(1, &viewport);
			commandList->RSSetScissorRects(1, &scissorRect);

			commandList->SetGraphicsRootSignature(rootSignature.Get());
			commandList->SetPipelineState(graphicsPipelineState.Get());
			commandList->SetGraphicsRootConstantBufferView(2, directionalLightResource->GetGPUVirtualAddress());
			ID3D12DescriptorHeap* descriptorHeaps[] = {srvDescriptorHeap};
			commandList->SetDescriptorHeaps(1, descriptorHeaps);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], FALSE, &dsvHandle);

			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], sceneClearColor, 0, nullptr);
			commandList->ClearDepthStencilView(
				dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);


			commandList->SetGraphicsRootConstantBufferView(0, sphereMaterialResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(
				1, sphereTransformationMatrixResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandlesGPU[2]);
			commandList->IASetVertexBuffers(0, 1, &modelVertexBufferView);
			commandList->DrawInstanced(static_cast<UINT>(modelData.vertices.size()), 1, 0, 0);

			for (const EditorSceneObject& sceneObject : editorSceneObjects) {
				if (sceneObject.type == EditorSceneObjectType::Sprite) {
					int32_t textureIndex =
						(std::clamp)(sceneObject.textureIndex, 0, static_cast<int32_t>(_countof(textureFilePaths)) - 1);
					commandList->SetGraphicsRootConstantBufferView(
						0, spriteMaterialResource->GetGPUVirtualAddress());
					commandList->SetGraphicsRootConstantBufferView(
						1, sceneObject.transformationResource->GetGPUVirtualAddress());
					commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandlesGPU[textureIndex]);
					commandList->IASetVertexBuffers(0, 1, &spriteVertexBufferView);
					commandList->IASetIndexBuffer(&spriteIndexBufferView);
					commandList->DrawIndexedInstanced(_countof(spriteIndices), 1, 0, 0, 0);
				}
				else {
					commandList->SetGraphicsRootConstantBufferView(
						0, sphereMaterialResource->GetGPUVirtualAddress());
					commandList->SetGraphicsRootConstantBufferView(
						1, sceneObject.transformationResource->GetGPUVirtualAddress());
					commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandlesGPU[2]);
					commandList->IASetVertexBuffers(0, 1, &modelVertexBufferView);
					commandList->DrawInstanced(static_cast<UINT>(modelData.vertices.size()), 1, 0, 0);
				}
			}
#ifdef USE_IMGUI
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.Get());

#endif

			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			commandList->ResourceBarrier(1, &barrier);

			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			ID3D12CommandList* commandLists[] = {commandList.Get()};
			commandQueue->ExecuteCommandLists(1, commandLists);

			hr = swapChain->Present(1, 0);
			assert(SUCCEEDED(hr));

			fenceValue++;
			hr = commandQueue->Signal(fence.Get(), fenceValue);
			assert(SUCCEEDED(hr));

			if (fence->GetCompletedValue() < fenceValue) {
				hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
				assert(SUCCEEDED(hr));
				if (fenceEvent != nullptr) {
					WaitForSingleObject(fenceEvent, INFINITE);
				}
			}
		}
	}

	if (sourceVoice != nullptr) {
		sourceVoice->Stop(0);
		sourceVoice->FlushSourceBuffers();
		sourceVoice->DestroyVoice();
	}
	SoundUnload(&soundData);

	if (masterVoice != nullptr) {
		masterVoice->DestroyVoice();
	}
	if (xAudio2 != nullptr) {
		xAudio2->Release();
		xAudio2 = nullptr;
	}

	if (keyboardDevice != nullptr) {
		keyboardDevice->Unacquire();
		keyboardDevice->Release();
		keyboardDevice = nullptr;
	}
	if (directInput != nullptr) {
		directInput->Release();
		directInput = nullptr;
	}

	Log(logStream, "application finished");

#ifdef USE_IMGUI
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif

	if (fenceEvent != nullptr) {
		CloseHandle(fenceEvent);
	}

	for (ID3D12Resource* intermediateResource : intermediateResources) {
		intermediateResource->Release();
	}
	for (ID3D12Resource* textureResource : textureResources) {
		textureResource->Release();
	}
	for (EditorSceneObject& sceneObject : editorSceneObjects) {
		if (sceneObject.transformationResource != nullptr) {
			sceneObject.transformationResource->Release();
			sceneObject.transformationResource = nullptr;
			sceneObject.transformationData = nullptr;
		}
	}
	spriteMaterialResource->Release();
	sphereMaterialResource->Release();
	directionalLightResource->Release();
	spriteTransformationMatrixResource->Release();
	sphereTransformationMatrixResource->Release();
	spriteIndexResource->Release();
	spriteVertexResource->Release();
	modelVertexResource->Release();
	vertexResource->Release();
	srvDescriptorHeap->Release();
	dsvDescriptorHeap->Release();
	rtvDescriptorHeap->Release();
	depthStencilResource->Release();
	swapChainResources[0]->Release();
	swapChainResources[1]->Release();

#ifdef _DEBUG
	ComPtr<IDXGIDebug1> debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(debug.GetAddressOf())))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
	}
#endif

	return static_cast<int>(message.wParam);
}
#pragma warning(pop)
