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

#include "ApplicationWindow.h"
#include "CrashHandler.h"
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
	hr = sourceVoice->Start(0);
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

	ComPtr<IDXGISwapChain4> swapChain;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;
	swapChainDesc.Height = kClientHeight;
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

	D3D12_RESOURCE_DESC depthStencilResourceDesc{};
	depthStencilResourceDesc.Width = kClientWidth;
	depthStencilResourceDesc.Height = kClientHeight;
	depthStencilResourceDesc.MipLevels = 1;
	depthStencilResourceDesc.DepthOrArraySize = 1;
	depthStencilResourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilResourceDesc.SampleDesc.Count = 1;
	depthStencilResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_HEAP_PROPERTIES depthStencilHeapProperties{};
	depthStencilHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	ID3D12Resource* depthStencilResource = nullptr;
	hr = device->CreateCommittedResource(
		&depthStencilHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthStencilResourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&depthStencilResource));
	assert(SUCCEEDED(hr));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
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

	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(kClientWidth);
	viewport.Height = static_cast<float>(kClientHeight);
	viewport.MaxDepth = 1.0f;

	D3D12_RECT scissorRect{};
	scissorRect.right = kClientWidth;
	scissorRect.bottom = kClientHeight;

	std::wstring textureFilePaths[] = {
		L"resources/uvChecker.png",
		L"resources/monsterBall.png",
		ConvertString(modelData.material.textureFilePath),
	};
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
		static_cast<float>(kClientWidth) / static_cast<float>(kClientHeight),
		0.1f,
		100.0f);
	Matrix4x4 spriteProjectionMatrix = MakeOrthographicMatrix(
		0.0f,
		0.0f,
		static_cast<float>(kClientWidth),
		static_cast<float>(kClientHeight),
		0.0f,
		100.0f);
	constexpr float cameraMoveSpeed = 0.1f;
	constexpr float cameraRotateSpeed = 0.02f;

	ComPtr<ID3D12Fence> fence;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
	assert(SUCCEEDED(hr));

	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent != nullptr);
	if (fenceEvent == nullptr) {
		return 1;
	}

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

			if (key[DIK_LEFT]) {
				cameraTransform.rotate.y -= cameraRotateSpeed;
			}
			if (key[DIK_RIGHT]) {
				cameraTransform.rotate.y += cameraRotateSpeed;
			}
			if (key[DIK_UP]) {
				cameraTransform.rotate.x -= cameraRotateSpeed;
			}
			if (key[DIK_DOWN]) {
				cameraTransform.rotate.x += cameraRotateSpeed;
			}

			if (key[DIK_A]) {
				cameraTransform.translate.x -= cameraMoveSpeed;
			}
			if (key[DIK_D]) {
				cameraTransform.translate.x += cameraMoveSpeed;
			}
			if (key[DIK_Q]) {
				cameraTransform.translate.y += cameraMoveSpeed;
			}
			if (key[DIK_E]) {
				cameraTransform.translate.y -= cameraMoveSpeed;
			}
			if (key[DIK_W]) {
				cameraTransform.translate.z += cameraMoveSpeed;
			}
			if (key[DIK_S]) {
				cameraTransform.translate.z -= cameraMoveSpeed;
			}

			bool isReturnTrigger =
				((key[DIK_RETURN] != 0) && (preKey[DIK_RETURN] == 0));
			if (isReturnTrigger) {
				uvTransform.translate = {0.0f, 0.0f, 0.0f};
				cameraTransform.rotate = {0.0f, 0.0f, 0.0f};
				cameraTransform.translate = {0.0f, 0.0f, -5.0f};
			}

#ifdef USE_IMGUI
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(960.0f, 20.0f), ImGuiCond_Once);
			ImGui::SetNextWindowSize(ImVec2(300.0f, 260.0f), ImGuiCond_Once);
			ImGui::Begin("UVTransform");
			ImGui::Text("UV Transform");
			ImGui::SliderFloat2("Scale", &uvTransform.scale.x, 0.1f, 4.0f);
			ImGui::SliderFloat("RotateZ", &uvTransform.rotate.z, -3.14f, 3.14f);
			ImGui::SliderFloat2("Translate", &uvTransform.translate.x, -2.0f, 2.0f);
			ImGui::Separator();
			ImGui::Text("Material");
			bool isLighting = sphereMaterialData->enableLighting != FALSE;
			ImGui::Checkbox("EnableLighting", &isLighting);
			ImGui::ColorEdit4("MaterialColor", &sphereMaterialData->color.x);
			ImGui::Separator();
			ImGui::Text("Light");
			ImGui::ColorEdit4("Color", &directionalLightData->color.x);
			ImGui::SliderFloat3("Direction", &directionalLightData->direction.x, -1.0f, 1.0f);
			ImGui::SliderFloat("Intensity", &directionalLightData->intensity, 0.0f, 2.0f);
			ImGui::Separator();
			ImGui::Text("Object");
			ImGui::SliderFloat3("Rotate", &transform.rotate.x, -3.14f, 3.14f);
			ImGui::Separator();
			ImGui::Text("DebugCamera");
			ImGui::SliderFloat3("CamRotate", &cameraTransform.rotate.x, -3.14f, 3.14f);
			ImGui::SliderFloat3("CamTranslate", &cameraTransform.translate.x, -20.0f, 20.0f);
			sphereMaterialData->enableLighting = isLighting ? TRUE : FALSE;
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

			float clearColor[] = {0.1f, 0.25f, 0.5f, 1.0f};
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);
			commandList->ClearDepthStencilView(
				dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);


			commandList->SetGraphicsRootConstantBufferView(0, spriteMaterialResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(
				1, spriteTransformationMatrixResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandlesGPU[0]);
			commandList->IASetVertexBuffers(0, 1, &spriteVertexBufferView);
			commandList->IASetIndexBuffer(&spriteIndexBufferView);
			commandList->DrawIndexedInstanced(_countof(spriteIndices), 1, 0, 0, 0);

			commandList->SetGraphicsRootConstantBufferView(0, sphereMaterialResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootConstantBufferView(
				1, sphereTransformationMatrixResource->GetGPUVirtualAddress());
			commandList->SetGraphicsRootDescriptorTable(3, textureSrvHandlesGPU[2]);
			commandList->IASetVertexBuffers(0, 1, &modelVertexBufferView);
			commandList->DrawInstanced(static_cast<UINT>(modelData.vertices.size()), 1, 0, 0);
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
