#pragma once

#pragma warning(push, 0)
#include <Windows.h>
#include <algorithm>
#include <array>
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
#include "ThirdParty/DirectXTex/DirectXTex.h"
#pragma warning(push)
#pragma warning(disable : 5045)
#include "ThirdParty/DirectXTex/d3dx12.h"
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
#include "EditorHrCheck.h"
#include "EditorHierarchyPanel.h"
#include "EditorInspectorPanel.h"
#include "EditorMainMenuBar.h"
#include "EditorRuntimeManager.h"
#include "EditorDepthHierarchyManager.h"
#include "EditorGpuCullingManager.h"
#include "EditorOceanFftManager.h"
#include "EditorGpuParticleManager.h"
#include "EditorVfxRenderer.h"
#include "EditorGBufferManager.h"
#include "EditorLightProbeManager.h"
#include "EditorPostProcessQualityManager.h"
#include "EditorTemporalRenderingManager.h"
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
#include "ThirdParty/FeelKitHaptics/FeelKit.h"
#include "ThirdParty/FeelKitHaptics/FeelKitHaptics.h"
using Microsoft::WRL::ComPtr;


#ifdef USE_IMGUI
#pragma warning(push, 0)
#include "ThirdParty/ImGuizmo-master/ImGuizmo-master/src/ImGuizmo.h"
#include "ThirdParty/imgui-docking/imgui-docking/imgui.h"
#include "ThirdParty/imgui-docking/imgui-docking/imgui_internal.h"
#include "ThirdParty/imgui-docking/imgui-docking/backends/imgui_impl_dx12.h"
#include "ThirdParty/imgui-docking/imgui-docking/backends/imgui_impl_win32.h"
#pragma warning(pop)
#endif

namespace EditorSharedState {
	// ================================
	// WAV 読み込み用のデータ構造
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

	inline ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes);
	inline MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
	inline ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

	inline SoundData SoundLoadWave(const char* filePath);
	inline void SoundUnload(SoundData* soundData);
	// 起動時Shaderを最後まで検査し、失敗した全Pathを1回の画面表示へまとめる。
	inline std::vector<std::string> g_shaderCompilationFailures;

	inline std::filesystem::path GetEngineDirectory() {
		std::wstring executablePath(32768U, L'\0');
		const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
		if (length == 0U || length >= executablePath.size()) return {};
		executablePath.resize(length);
		return std::filesystem::path(executablePath).parent_path();
	}

	// ProjectとEngineを別Folderへ置く配布版では、相対PathをまずProject側で探し、
	// Projectに無い内蔵ResourceだけCG2.exeと同じFolderから解決する。
	inline std::filesystem::path ResolveEngineOrProjectFilePath(const std::filesystem::path& requestedPath) {
		if (requestedPath.empty() || requestedPath.is_absolute()) return requestedPath;
		std::error_code fileError;
		if (std::filesystem::exists(requestedPath, fileError) && !fileError) return requestedPath;

		const std::filesystem::path engineDirectory = GetEngineDirectory();
		if (engineDirectory.empty()) return requestedPath;
		const std::filesystem::path enginePath = engineDirectory / requestedPath;
		fileError.clear();
		return std::filesystem::exists(enginePath, fileError) && !fileError ? enginePath : requestedPath;
	}

	inline D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap, UINT descriptorSize, UINT index) {
		// Heap 先頭から Descriptor のバイト幅だけ進め、指定番号の Handle を返す。
		D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += descriptorSize * index;
		return handle;
	}

	inline D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap, UINT descriptorSize, UINT index) {
		// CPU Handle と同じ規則で、Shader へ渡す GPU Handle を求める。
		D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
		handle.ptr += descriptorSize * index;
		return handle;
	}

	// forceSrgb/generateMipmaps/maxSizeはTexture Import Settings用の拡張引数。
	// 既定値(true/true/0)は変更前と同じ挙動になるため、既存の全呼び出し元は無変更で動く。
	inline DirectX::ScratchImage LoadTexture(
		const std::wstring& filePath,
		bool forceSrgb = true,
		bool generateMipmaps = true,
		int32_t maxSize = 0) {
		DirectX::ScratchImage emptyImage{};
		const std::filesystem::path resolvedFilePath = ResolveEngineOrProjectFilePath(filePath);
		if (filePath.empty() || !std::filesystem::exists(resolvedFilePath)) {
			return emptyImage;
		}

		DirectX::TexMetadata metadata{};

		DirectX::ScratchImage image{};

		std::filesystem::path path(resolvedFilePath);
		std::wstring extension = path.extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), towlower);

		HRESULT hr = E_FAIL;
		bool useSrgbMipFilter = true;
		if (extension == L".hdr") {
			hr = DirectX::LoadFromHDRFile(resolvedFilePath.c_str(), &metadata, image);
			useSrgbMipFilter = false;
		}
		else if (extension == L".dds") {
			hr = DirectX::LoadFromDDSFile(resolvedFilePath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
			useSrgbMipFilter = false;
		}
		else {
			hr = DirectX::LoadFromWICFile(
				resolvedFilePath.c_str(),
				forceSrgb ? DirectX::WIC_FLAGS_FORCE_SRGB : DirectX::WIC_FLAGS_FORCE_LINEAR,
				&metadata,
				image);
			useSrgbMipFilter = forceSrgb;
		}
		if (FAILED(hr) || image.GetImageCount() == 0u || image.GetImages() == nullptr) {
			return emptyImage;
		}

		// maxSizeが指定されていれば、Mipmap生成前に長辺がその値を超えないようDownscaleする。
		if (maxSize > 0 &&
			(metadata.width > static_cast<size_t>(maxSize) || metadata.height > static_cast<size_t>(maxSize))) {
			const float widthScale = static_cast<float>(maxSize) / static_cast<float>(metadata.width);
			const float heightScale = static_cast<float>(maxSize) / static_cast<float>(metadata.height);
			const float resizeScale = (std::min)(widthScale, heightScale);
			const size_t resizedWidth = (std::max)(static_cast<size_t>(static_cast<float>(metadata.width) * resizeScale), size_t{1});
			const size_t resizedHeight = (std::max)(static_cast<size_t>(static_cast<float>(metadata.height) * resizeScale), size_t{1});

			DirectX::ScratchImage resizedImage{};
			if (SUCCEEDED(DirectX::Resize(
					image.GetImages(),
					image.GetImageCount(),
					image.GetMetadata(),
					resizedWidth,
					resizedHeight,
					DirectX::TEX_FILTER_DEFAULT,
					resizedImage))) {
				image = std::move(resizedImage);
				metadata = image.GetMetadata();
			}
		}

		if (!generateMipmaps) {
			return image;
		}

		DirectX::ScratchImage mipImages{};

		const DirectX::TEX_FILTER_FLAGS mipFilter = useSrgbMipFilter
			? DirectX::TEX_FILTER_SRGB
			: DirectX::TEX_FILTER_DEFAULT;
		hr = DirectX::GenerateMipMaps(
			image.GetImages(),
			image.GetImageCount(),
			image.GetMetadata(),
			mipFilter,
			0,
			mipImages);
		if (FAILED(hr)) {
			return image;
		}

		return mipImages;
	}

	inline ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
		if (device == nullptr ||
			metadata.width == 0u ||
			metadata.height == 0u ||
			metadata.mipLevels == 0u ||
			metadata.arraySize == 0u ||
			metadata.format == DXGI_FORMAT_UNKNOWN) {
			return nullptr;
		}

		// UploadTextureData から転送するため、初期状態は COPY_DEST にする。
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
		if (FAILED(hr)) {
			return nullptr;
		}

		return resource;
	}

	inline ID3D12Resource* UploadTextureData(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* commandList,
		ID3D12Resource* texture,
		const DirectX::ScratchImage& mipImages) {
		if (device == nullptr ||
			commandList == nullptr ||
			texture == nullptr ||
			mipImages.GetImageCount() == 0u ||
			mipImages.GetImages() == nullptr) {
			return nullptr;
		}

		// DirectXTex の各 mip を D3D12 の Subresource 配列へ変換する。
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
		if (intermediateResource == nullptr) {
			return nullptr;
		}

		const UINT64 uploadedSize = UpdateSubresources(
			commandList,
			texture,
			intermediateResource,
			0,
			0,
			static_cast<UINT>(subresources.size()),
			subresources.data());
		if (uploadedSize == 0u) {
			intermediateResource->Release();
			return nullptr;
		}

		// 転送後は Texture を Shader から読み取れる状態へ遷移させる。
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
		if (device == nullptr || sizeInBytes == 0u) {
			return nullptr;
		}

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
		if (FAILED(hr)) {
			return nullptr;
		}

		return resource;
	}

	inline ComPtr<IDxcBlob> CompileShader(
		const std::wstring& filePath,
		const wchar_t* profile,
		IDxcUtils* dxcUtils,
		IDxcCompiler3* dxcCompiler,
		IDxcIncludeHandler* includeHandler,
		std::ofstream& logStream) {
		const std::filesystem::path resolvedFilePath = ResolveEngineOrProjectFilePath(filePath);
		const std::wstring resolvedFilePathText = resolvedFilePath.wstring();
		Log(logStream, std::format("Begin CompileShader, path:{}, profile:{}",
		                           ConvertString(resolvedFilePathText), ConvertString(std::wstring{profile})));

		ComPtr<IDxcBlobEncoding> shaderSourceSource;
		HRESULT hr = dxcUtils->LoadFile(resolvedFilePathText.c_str(), nullptr, shaderSourceSource.GetAddressOf());
		if (FAILED(hr) || shaderSourceSource == nullptr) {
			const std::string message = std::format("Failed to load shader file: {}", ConvertString(resolvedFilePathText));
			Log(logStream, message);
			g_shaderCompilationFailures.push_back(message);
			return nullptr;
		}
		
		ComPtr<IDxcBlobEncoding> shaderSource = shaderSourceSource;
		DxcBuffer shaderSourceBuffer{};
		shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
		shaderSourceBuffer.Size = shaderSource->GetBufferSize();
		shaderSourceBuffer.Encoding = DXC_CP_UTF8;

		std::vector<std::wstring> includeDirectories{};
		includeDirectories.push_back(L"Assets/Shaders");
		includeDirectories.push_back(L"Assets/Shaders/lygia");
		includeDirectories.push_back(L"Assets/Shaders/FidelityFX");
		includeDirectories.push_back(L"ThirdParty/Shader");
		includeDirectories.push_back(L"ThirdParty/Shader/lygia");
		includeDirectories.push_back(L"ThirdParty/Shader/NoiseShader-3.0.1");
		includeDirectories.push_back(L"ThirdParty/Shader/NoiseShader-3.0.1/Packages/jp.keijiro.noiseshader/Shader");
		includeDirectories.push_back(L"ThirdParty/Shader/FidelityFX-SDK-v1.1.4/bin/shaders");
		includeDirectories.push_back(L"ThirdParty/Shader/FidelityFX-SDK-v1.1.4/sdk/include");
		const std::filesystem::path engineDirectory = GetEngineDirectory();
		includeDirectories.push_back((engineDirectory / L"Assets/Shaders").wstring());
		includeDirectories.push_back((engineDirectory / L"Assets/Shaders/lygia").wstring());
		includeDirectories.push_back((engineDirectory / L"Assets/Shaders/FidelityFX").wstring());
		includeDirectories.push_back((engineDirectory / L"ThirdParty/Shader").wstring());
		includeDirectories.push_back((engineDirectory / L"ThirdParty/Shader/lygia").wstring());
		includeDirectories.push_back((engineDirectory / L"ThirdParty/Shader/NoiseShader-3.0.1").wstring());
		includeDirectories.push_back((engineDirectory / L"ThirdParty/Shader/NoiseShader-3.0.1/Packages/jp.keijiro.noiseshader/Shader").wstring());
		includeDirectories.push_back((engineDirectory / L"ThirdParty/Shader/FidelityFX-SDK-v1.1.4/bin/shaders").wstring());
		includeDirectories.push_back((engineDirectory / L"ThirdParty/Shader/FidelityFX-SDK-v1.1.4/sdk/include").wstring());

		std::vector<LPCWSTR> arguments{};
		arguments.push_back(resolvedFilePathText.c_str());
		arguments.push_back(L"-E");
		arguments.push_back(L"main");
		arguments.push_back(L"-T");
		arguments.push_back(profile);
		arguments.push_back(L"-Zi");
		arguments.push_back(L"-Qembed_debug");
		arguments.push_back(L"-Od");
		arguments.push_back(L"-Zpr");

		for (const std::wstring& includeDirectory : includeDirectories) {
			arguments.push_back(L"-I");
			arguments.push_back(includeDirectory.c_str());
		}

		ComPtr<IDxcResult> shaderResult;
		hr = dxcCompiler->Compile(
			&shaderSourceBuffer,
			arguments.data(),
			static_cast<uint32_t>(arguments.size()),
			includeHandler,
			IID_PPV_ARGS(shaderResult.GetAddressOf()));
		EDITOR_HR_VERIFY(hr);

		HRESULT compileStatus = S_OK;
		hr = shaderResult->GetStatus(&compileStatus);
		EDITOR_HR_VERIFY(hr);

		ComPtr<IDxcBlobUtf8> shaderError;
		shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(shaderError.GetAddressOf()), nullptr);
		if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
			Log(shaderError->GetStringPointer());
			Log(logStream, shaderError->GetStringPointer());
			if (FAILED(compileStatus)) {
				Log(logStream, std::format("Compile Error, path:{}, profile:{}",
				                           ConvertString(filePath), ConvertString(std::wstring{profile})));
				g_shaderCompilationFailures.push_back(
					ConvertString(filePath) + ": " + std::string(shaderError->GetStringPointer(), shaderError->GetStringLength()));
			}
		}

		if (FAILED(compileStatus)) {
			return nullptr;
		}

		ComPtr<IDxcBlob> shaderBlob;
		hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shaderBlob.GetAddressOf()), nullptr);
		EDITOR_HR_VERIFY(hr);

		Log(logStream, std::format("Compile Succeeded, path:{}, profile:{}",
		                           ConvertString(filePath), ConvertString(std::wstring{profile})));

		return shaderBlob;
	}

	inline MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
		MaterialData materialData{};

		std::ifstream file(directoryPath + "/" + filename);
		if (!file.is_open()) {
			Log(std::format("Material file is missing: {}/{}", directoryPath, filename));
			return materialData;
		}

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

	inline ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
		ModelData modelData{};

		const std::filesystem::path resolvedModelPath =
			ResolveEngineOrProjectFilePath(std::filesystem::path(directoryPath) / filename);
		std::ifstream file(resolvedModelPath); // Projectに無い内蔵OBJはEngine側から読む。
		if (!file.is_open()) {
			return modelData;
		}

		std::vector<Vector4> positions;
		std::vector<Vector2> texcoords;
		std::vector<Vector3> normals;

		std::string line;

		while (std::getline(file, line)) {
			std::string identifier;
			std::istringstream lineStream(line);
			lineStream >> identifier;

			// OBJ の右手座標系を Engine の左手座標系へ合わせる。
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

				// 座標系の変換後も表面の向きが維持されるよう、頂点順を反転する。
				modelData.vertices.push_back(triangle[2]);
				modelData.vertices.push_back(triangle[1]);
				modelData.vertices.push_back(triangle[0]);
			}
			else if (identifier == "mtllib") {
				std::string materialFilename;
				lineStream >> materialFilename;

				modelData.material = LoadMaterialTemplateFile(resolvedModelPath.parent_path().string(), materialFilename);
			}
		}

		return modelData;
	}

	inline SoundData SoundLoadWave(const char* filePath) {
		SoundData soundData{};

		if (filePath == nullptr) {
			return soundData;
		}

		const std::filesystem::path resolvedSoundPath = ResolveEngineOrProjectFilePath(filePath);
		std::ifstream file(resolvedSoundPath, std::ios_base::binary); // 内蔵SoundはEngine側へFallbackする。
		if (!file.is_open()) {
			return soundData;
		}

		RiffHeader riff{};
		file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
		if (!file ||
			std::strncmp(riff.chunk.id, "RIFF", 4) != 0 ||
			std::strncmp(riff.type, "WAVE", 4) != 0) {
			return soundData;
		}

		// fmt と data の間に未知の Chunk があっても読み飛ばせるよう、順番に走査する。
		FormatChunk format{};
		bool hasFormatChunk = false;
		ChunkHeader chunk{};
		while (file.read(reinterpret_cast<char*>(&chunk), sizeof(chunk))) {
			if (chunk.size < 0) {
				return soundData;
			}

			if (std::strncmp(chunk.id, "fmt ", 4) == 0) {
				format.chunk = chunk;
				const size_t formatReadSize =
					(std::min)(static_cast<size_t>(chunk.size), sizeof(format.format));
				file.read(reinterpret_cast<char*>(&format.format), static_cast<std::streamsize>(formatReadSize));
				if (!file) {
					return soundData;
				}

				if (static_cast<size_t>(chunk.size) > formatReadSize) {
					file.seekg(
						static_cast<std::streamoff>(static_cast<size_t>(chunk.size) - formatReadSize),
						std::ios_base::cur);
					if (!file) {
						return soundData;
					}
				}

				hasFormatChunk = true;
				break;
			}

			file.seekg(static_cast<std::streamoff>(chunk.size), std::ios_base::cur);
			if (!file) {
				return soundData;
			}
		}

		if (!hasFormatChunk ||
			format.format.nChannels == 0u ||
			format.format.nSamplesPerSec == 0u ||
			format.format.nBlockAlign == 0u ||
			format.format.wBitsPerSample == 0u ||
			format.format.nAvgBytesPerSec == 0u) {
			return soundData;
		}

		ChunkHeader data{};
		bool hasDataChunk = false;
		while (file.read(reinterpret_cast<char*>(&data), sizeof(data))) {
			if (data.size < 0) {
				return soundData;
			}

			if (std::strncmp(data.id, "data", 4) == 0) {
				hasDataChunk = true;
				break;
			}

			file.seekg(static_cast<std::streamoff>(data.size), std::ios_base::cur);
			if (!file) {
				return soundData;
			}
		}

		if (!hasDataChunk || data.size <= 0) {
			return soundData;
		}

		uint32_t dataSize = static_cast<uint32_t>(data.size);
		auto pBuffer = new char[static_cast<size_t>(dataSize)];
		file.read(pBuffer, static_cast<std::streamsize>(dataSize));
		if (!file) {
			delete[] pBuffer;
			return soundData;
		}

		soundData.wfex = format.format;
		soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
		soundData.bufferSize = dataSize;
		return soundData;
	}

	inline void SoundUnload(SoundData* soundData) {
		if (soundData == nullptr) {
			return;
		}

		delete[] soundData->pBuffer;
		soundData->pBuffer = nullptr;
		soundData->bufferSize = 0u;
		soundData->wfex = {};
	}
}

#pragma warning(push)
#pragma warning(disable : 4101 4189 4514 5045)

namespace EditorSharedState {
	constexpr uint32_t kRuntimeTextureCount = 4;
	constexpr uint32_t kRuntimeSwapChainBufferCount = 2;
	constexpr uint32_t kRuntimeSpriteIndexCount = 6;
	constexpr uint32_t kRuntimeShadowMapSize = 5120; // 5x5 atlas。Sun CSM とPoint Lightのキューブ影(6面)を同居させる。
	// 画像SRVを置く共通Descriptor Heapの総容量。普通のGame Engineと同じ桁へ合わせている。
	// D3D12の保証上限は1,000,000で、1個32B程度のため65536でも約2MB。足りなければここだけ増やす。
	constexpr uint32_t kRuntimeSrvDescriptorHeapCapacity = 65536;
	// 0からこの数までは描画機能・View別Temporal履歴・動的Fontの予約。これ以降を画像SRVへ使う。
	constexpr uint32_t kRuntimeReservedSrvDescriptorCount = 205;
	constexpr uint32_t kRuntimeShadowSrvDescriptorIndex = 15;
	// 1フレームで評価する通常ライト数。影の枚数は別途Shadow Atlas容量で制限する。
	constexpr uint32_t kMaxSceneLights = 16;
	constexpr uint32_t kShadowAtlasTiles = 5; // 5x5 grid = 25 タイル。各タイルは 1024x1024。
	// タイル予算: Sun cascade 4 + Point Light最大3灯 x 6面 = 22。25タイルなら収まる。
	// 16～30 番は従来の描画経路が固定利用するため、順番を変更しない。
	constexpr uint32_t kRuntimeHdrSrvDescriptorIndex = 16;
	constexpr uint32_t kRuntimeBloomSrvDescriptorIndexA = 17;
	constexpr uint32_t kRuntimeBloomSrvDescriptorIndexB = 18;
	constexpr uint32_t kRuntimePostProcessSrvDescriptorIndex = 19;
	constexpr uint32_t kRuntimeDepthSrvDescriptorIndex = 20;
	constexpr uint32_t kRuntimeSsaoSrvDescriptorIndexA = 21;
	constexpr uint32_t kRuntimeSsaoSrvDescriptorIndexB = 22;
	constexpr uint32_t kRuntimeHdrCompositeSrvDescriptorIndex = 23;
	constexpr uint32_t kRuntimeIblIrradianceSrvDescriptorIndex = 24;
	constexpr uint32_t kRuntimeIblPrefilterSrvDescriptorIndex = 25;
	constexpr uint32_t kRuntimeIblEnvironmentSrvDescriptorIndex = 26;
	constexpr uint32_t kRuntimeIblBrdfLutSrvDescriptorIndex = 27;
	constexpr uint32_t kRuntimeColorGradingLutSrvDescriptorIndex = 113u;
	constexpr uint32_t kRuntimeMaterialMaskSrvDescriptorIndex = 28;
	constexpr uint32_t kRuntimePlanarReflectionSrvDescriptorIndex = 29;
	constexpr uint32_t kRuntimeEnvironmentSrvDescriptorIndex = 30;
	constexpr uint32_t kRuntimeDepthPyramidDescriptorStartIndex = 31u; // 深度ピラミッドは SRV/UAV を交互に 31～54 番へ配置する。
	constexpr uint32_t kRuntimeReconstructedNormalSrvDescriptorIndex = 55u; // 深度から再構築したワールド法線の SRV。
	constexpr uint32_t kRuntimeReconstructedNormalUavDescriptorIndex = 56u; // ワールド法線を書き込む UAV。
	// Light Probe GI は 57-62 番を使う。SRV(57,58)とUAV(59,60)とキャプチャ(61,62)は
	// それぞれ連続していないとDescriptor Tableで束ねられないので、順番を変えないこと。
	constexpr uint32_t kRuntimeLightProbeDescriptorStartIndex = 57u;
	constexpr uint32_t kRuntimeLightProbeDescriptorCount = 6u;
	// SSGI の半解像度RT。63 = 現在フレーム、64/65 = 履歴のピンポン。
	constexpr uint32_t kRuntimeSsgiSrvDescriptorIndex = 63u;
	constexpr uint32_t kRuntimeSsgiHistorySrvDescriptorIndexA = 64u;
	constexpr uint32_t kRuntimeSsgiHistorySrvDescriptorIndexB = 65u;
	constexpr uint32_t kRuntimeOitAccumulationSrvDescriptorIndex = 120u;
	constexpr uint32_t kRuntimeOitRevealageSrvDescriptorIndex = 121u;
	constexpr uint32_t kRuntimeOitRevealageDuplicateSrvDescriptorIndex = 122u;
	constexpr uint32_t kRuntimeOpaqueDepthCopySrvDescriptorIndex = 114u;
	constexpr uint32_t kRuntimeRtvCount = 17; // +3: SSGI 半解像度(現在フレーム + 履歴2枚) // swap2 + HDR/Bloom/Post/SSAO/Composite/Mask/Planar + OIT 2枚
	// PlatformManager で生成し、RenderManager などから共有する Runtime 状態。
	inline HINSTANCE g_instanceHandle = nullptr;
	inline int g_exitCode = 0;
	inline bool g_isInitialized = false;
	inline bool g_isInitializationFailed = false;
	inline bool g_isEndRequested = false;
	inline bool g_isFinalized = false;
	inline bool g_isDrawRequested = false;
	inline std::ofstream g_logStream;
	inline HWND g_windowHandle = nullptr;
	inline HRESULT g_hr = S_OK;
	inline IDirectInput8* g_directInput = nullptr;
	inline IDirectInputDevice8* g_keyboardDevice = nullptr;
	inline IDirectInputDevice8* g_mouseDevice = nullptr;
	inline DIMOUSESTATE g_mouseState{};
	inline DIMOUSESTATE g_preMouseState{};  // Scriptの押した瞬間・離した瞬間判定に使う前フレーム状態。
	inline bool g_runtimeCursorLocked = false;  // Play中のCameraまたはScriptがカーソル固定を要求している。
	inline bool g_runtimeCursorVisible = true;  // Win32 ShowCursorの現在要求値。

	inline BYTE g_key[256] = {};

	inline BYTE g_preKey[256] = {};

	inline MSG g_message{};

	inline IXAudio2* g_xAudio2 = nullptr;
	inline IXAudio2MasteringVoice* g_masterVoice = nullptr;

	inline SoundData g_soundData{};

	inline IXAudio2SourceVoice* g_sourceVoice = nullptr;
	inline ComPtr<IDXGIFactory7> g_dxgiFactory;
	inline ComPtr<IDXGIAdapter4> g_useAdapter;
	inline ComPtr<ID3D12Device> g_device;
	inline ComPtr<ID3D12CommandQueue> g_commandQueue;
	inline ComPtr<ID3D12CommandAllocator> g_commandAllocator;
	inline ComPtr<ID3D12GraphicsCommandList> g_commandList;
	inline ComPtr<ID3D12QueryHeap> g_renderTimestampQueryHeap;
	inline ComPtr<ID3D12Resource> g_renderTimestampReadback;
	inline std::uint64_t g_renderTimestampFrequency = 0u;

	inline ComPtr<IDXGISwapChain4> g_swapChain;

	inline DXGI_SWAP_CHAIN_DESC1 g_swapChainDesc{};

	inline ID3D12DescriptorHeap* g_rtvDescriptorHeap = nullptr;
	inline ID3D12DescriptorHeap* g_srvDescriptorHeap = nullptr;
	inline ID3D12DescriptorHeap* g_dsvDescriptorHeap = nullptr;

	inline ID3D12Resource* g_swapChainResources[kRuntimeSwapChainBufferCount] = {nullptr, nullptr};

	inline D3D12_RENDER_TARGET_VIEW_DESC g_rtvDesc{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_rtvHandles[kRuntimeSwapChainBufferCount]{};

	inline D3D12_CLEAR_VALUE g_depthClearValue{};
	inline D3D12_DEPTH_STENCIL_VIEW_DESC g_dsvDesc{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_dsvHandle{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_shadowDsvHandle{};

	inline ID3D12Resource* g_depthStencilResource = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_depthSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_depthSrvHandleGPU{};
	inline ID3D12Resource* g_opaqueDepthCopyResource = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_opaqueDepthCopySrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_opaqueDepthCopySrvHandleGPU{};
	inline ID3D12Resource* g_shadowMapResource = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_shadowMapSrvCpuHandle{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_shadowMapSrvGpuHandle{};

	inline ID3D12Resource* g_hdrRenderTarget = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_hdrRtvHandle{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_hdrSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_hdrSrvHandleGPU{};

	inline ID3D12Resource* g_bloomRenderTargets[2] = {};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_bloomRtvHandles[2]{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_bloomSrvHandlesCPU[2]{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_bloomSrvHandlesGPU[2]{};
	inline ID3D12Resource* g_postProcessRenderTarget = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_postProcessRtvHandle{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_postProcessSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_postProcessSrvHandleGPU{};
	inline ID3D12Resource* g_ssaoRenderTargets[2] = {};
	// SSGI は半解像度で解き、Temporal で均してからフル解像度へ加算する。
	inline ID3D12Resource* g_ssgiRenderTarget = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_ssgiRtvHandle{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_ssgiSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_ssgiSrvHandleGPU{};
	inline ID3D12Resource* g_ssgiHistoryRenderTargets[2] = {};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_ssgiHistoryRtvHandles[2]{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_ssgiHistorySrvHandlesCPU[2]{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_ssgiHistorySrvHandlesGPU[2]{};
	inline uint32_t g_ssgiHistoryWriteIndex = 0u;
	inline bool g_isSsgiHistoryValid = false;
	inline uint32_t g_ssgiRenderWidth = 0u;
	inline uint32_t g_ssgiRenderHeight = 0u;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_ssaoRtvHandles[2]{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_ssaoSrvHandlesCPU[2]{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_ssaoSrvHandlesGPU[2]{};
	inline ID3D12Resource* g_hdrCompositeRenderTarget = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_hdrCompositeRtvHandle{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_hdrCompositeSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_hdrCompositeSrvHandleGPU{};

	inline ID3D12Resource* g_materialMaskRenderTarget = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_materialMaskRtvHandle{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_materialMaskSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_materialMaskSrvHandleGPU{};
	inline ID3D12Resource* g_planarReflectionRenderTarget = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_planarReflectionRtvHandle{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_planarReflectionSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_planarReflectionSrvHandleGPU{};
	inline ID3D12Resource* g_oitAccumulationRenderTarget = nullptr;
	inline ID3D12Resource* g_oitRevealageRenderTarget = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_oitRtvHandles[2]{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_oitSrvHandlesCPU[2]{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_oitSrvHandlesGPU[2]{};

	inline uint32_t g_renderWidth = 1u;
	inline uint32_t g_renderHeight = 1u;

	struct EditorRenderProfile {
		float frameMilliseconds = 0.0f;
		float frameRate = 0.0f;
		float gpuFrameMilliseconds = 0.0f;
		std::uint64_t localVideoMemoryUsage = 0u;
		std::uint64_t localVideoMemoryBudget = 0u;
		uint32_t sceneObjectCount = 0u;
		uint32_t instanceCount = 0u;
	};

	inline EditorRenderProfile g_renderProfile{};

	// 物理Body生成の失敗はConsoleへ出しても他のLogに埋もれて追えないため、
	// 直近の失敗内容と件数をここへ保持し、Log監視(RuntimeLog)から参照できるようにする。
	// Bodyが作られないObjectはRay/ShapeCastに一切引っかからない(弾がすり抜ける)ので、
	// 「気づけないまま放置される」ことが一番の問題になる。
	inline std::string g_lastPhysicsBodyFailure = "-";
	inline uint32_t g_physicsBodyFailureCount = 0u;

	inline ComPtr<IDxcUtils> g_dxcUtils;
	inline ComPtr<IDxcCompiler3> g_dxcCompiler;
	inline ComPtr<IDxcIncludeHandler> g_includeHandler;

	inline ComPtr<IDxcBlob> g_vertexShaderBlob;
	inline ComPtr<IDxcBlob> g_pixelShaderBlob;
	inline ComPtr<IDxcBlob> g_objectReflectionMaskPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_shadowVertexShaderBlob;
	inline ComPtr<IDxcBlob> g_alphaCutoutShadowPixelShaderBlob;

	// Post-process shader blobs
	inline ComPtr<IDxcBlob> g_fullscreenVertexShaderBlob;
	inline ComPtr<IDxcBlob> g_toneMappingPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_bloomExtractPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_bloomBlurPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_fxaaPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_ssaoPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_ssaoBlurPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_ssgiPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_ssgiTemporalPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_ssgiUpsamplePixelShaderBlob;
	inline ComPtr<IDxcBlob> g_volumetricLightShaftPixelShaderBlob;
	// Light Probe GI: Probe位置からのキャプチャとSH/可視性のBake。
	inline ComPtr<IDxcBlob> g_probeCaptureVertexShaderBlob;
	inline ComPtr<IDxcBlob> g_probeCapturePixelShaderBlob;
	inline ComPtr<IDxcBlob> g_probeShProjectionComputeShaderBlob;
	inline ComPtr<IDxcBlob> g_probeVisibilityComputeShaderBlob;
	inline ComPtr<IDxcBlob> g_skyboxPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_planarReflectionPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_sharpenPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_finalCompositePixelShaderBlob;
	inline ComPtr<IDxcBlob> g_depthOfFieldPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_motionBlurPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_passthroughPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_weightedOitPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_weightedOitCompositePixelShaderBlob;
	inline ComPtr<IDxcBlob> g_refractiveSurfacePixelShaderBlob;
	inline ComPtr<IDxcBlob> g_underwaterCausticsPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_skinnedMotionVectorVertexShaderBlob;


	inline ComPtr<ID3DBlob> g_signatureBlob;
	inline ComPtr<ID3DBlob> g_errorBlob;

	inline ComPtr<ID3D12RootSignature> g_rootSignature;
	inline ComPtr<ID3D12PipelineState> g_graphicsPipelineState;
	inline ComPtr<ID3D12PipelineState> g_planarScenePipelineState;
	inline ComPtr<ID3D12PipelineState> g_planarSurfacePipelineState;
	inline ComPtr<ID3D12PipelineState> g_objectReflectionMaskPipelineState;
	inline ComPtr<ID3D12PipelineState> g_cullFrontPipelineState;
	inline ComPtr<ID3D12PipelineState> g_cullNonePipelineState;
	inline ComPtr<ID3D12PipelineState> g_transparentPipelineState;
	inline ComPtr<ID3D12PipelineState> g_transparentCullNonePipelineState;
	inline ComPtr<ID3D12PipelineState> g_weightedOitPipelineState;
	inline ComPtr<ID3D12PipelineState> g_weightedOitCullNonePipelineState;
	inline ComPtr<ID3D12PipelineState> g_waterSurfacePipelineState;  // Opaque Color / Depth を読む水面専用パス
	inline ComPtr<ID3D12PipelineState> g_waterTessellationPipelineState;  // 画面Pixel密度からOceanをGPU細分化する専用パス
	inline ComPtr<ID3D12PipelineState> g_refractiveSurfacePipelineState;
	inline ComPtr<ID3D12PipelineState> g_refractiveSurfaceCullNonePipelineState;
	inline ComPtr<ID3D12PipelineState> g_shadowPipelineState;
	inline ComPtr<ID3D12PipelineState> g_shadowCullNonePipelineState;
	inline ComPtr<ID3D12PipelineState> g_alphaCutoutShadowPipelineState;
	inline ComPtr<ID3D12PipelineState> g_alphaCutoutShadowCullNonePipelineState;
	inline ComPtr<ID3D12PipelineState> g_batchedGraphicsPipelineState;
	inline ComPtr<ID3D12PipelineState> g_batchedCullFrontPipelineState;
	inline ComPtr<ID3D12PipelineState> g_batchedCullNonePipelineState;
	inline ComPtr<ID3D12PipelineState> g_batchedShadowPipelineState;
	inline ComPtr<ID3D12PipelineState> g_batchedShadowCullNonePipelineState;

	// Post-process root signature and pipeline states
	inline ComPtr<ID3D12RootSignature> g_postProcessRootSignature;
	inline ComPtr<ID3D12PipelineState> g_toneMappingPipelineState;
	inline ComPtr<ID3D12PipelineState> g_bloomExtractPipelineState;
	inline ComPtr<ID3D12PipelineState> g_bloomBlurPipelineState;
	inline ComPtr<ID3D12PipelineState> g_fxaaPipelineState;
	inline ComPtr<ID3D12PipelineState> g_ssaoPipelineState;
	inline ComPtr<ID3D12PipelineState> g_ssaoBlurPipelineState;
	inline ComPtr<ID3D12PipelineState> g_ssgiPipelineState;
	inline ComPtr<ID3D12PipelineState> g_ssgiTemporalPipelineState;
	inline ComPtr<ID3D12PipelineState> g_ssgiUpsamplePipelineState;
	// Volumetric Light Shaft(Sun Beams)専用。48値制約のあるpostProcessRootSignatureでは
	// Cascaded ShadowVPを渡すデータ量に足りないため、独立したRootSignatureを持つ。
	inline ComPtr<ID3D12RootSignature> g_volumetricLightShaftRootSignature;
	inline ComPtr<ID3D12PipelineState> g_volumetricLightShaftPipelineState;
	inline ComPtr<ID3D12PipelineState> g_skyboxPipelineState;
	inline ComPtr<ID3D12PipelineState> g_planarReflectionPipelineState;
	inline ComPtr<ID3D12PipelineState> g_sharpenPipelineState;
	inline ComPtr<ID3D12PipelineState> g_finalCompositePipelineState;
	inline ComPtr<ID3D12PipelineState> g_depthOfFieldPipelineState;
	inline ComPtr<ID3D12PipelineState> g_motionBlurPipelineState;
	inline ComPtr<ID3D12PipelineState> g_passthroughPipelineState;
	inline ComPtr<ID3D12PipelineState> g_weightedOitCompositePipelineState;
	inline ComPtr<ID3D12PipelineState> g_underwaterCausticsPipelineState;
	inline EditorDepthHierarchyManager g_depthHierarchyManager;
	inline EditorGBufferManager g_gBufferManager;
	inline EditorLightProbeManager g_lightProbeManager;
	inline EditorGpuCullingManager g_gpuCullingManager;
	inline EditorOceanFftManager g_oceanFftManager;
	inline EditorGpuParticleManager g_gpuParticleManager;
	inline EditorVfxRenderer g_vfxRenderer;  // Stage1 VFX(Billboard/Flipbook/Ribbon/Ring)専用の最小GPU描画担当。
	inline EditorPostProcessQualityManager g_postProcessQualityManager;
	inline EditorTemporalRenderingManager g_temporalRenderingManager;
	// IBL uses existing root signature with added descriptor ranges

	inline ID3D12Resource* g_spriteMaterialResource = nullptr;
	inline Material* g_spriteMaterialData = nullptr;

	inline ID3D12Resource* g_sphereMaterialResource = nullptr;
	inline Material* g_sphereMaterialData = nullptr;

	inline ID3D12Resource* g_directionalLightResource = nullptr;
	inline DirectionalLight* g_directionalLightData = nullptr;

	inline ID3D12Resource* g_emissiveLightResource = nullptr;
	inline EmissiveLightArray* g_emissiveLightData = nullptr;

	inline ID3D12Resource* g_spriteTransformationMatrixResource = nullptr;
	inline TransformationMatrix* g_spriteTransformationMatrixData = nullptr;
	inline ID3D12Resource* g_sphereTransformationMatrixResource = nullptr;
	inline TransformationMatrix* g_sphereTransformationMatrixData = nullptr;
	inline ID3D12Resource* g_identitySkinMatrixResource = nullptr;  // 非 Skin 頂点でも t16 / t17 を常に有効な SRV にする
	inline Matrix4x4* g_identitySkinMatrixData = nullptr;
	constexpr uint32_t kEditorBatchInstanceCapacity = 65536u;
	inline ID3D12Resource* g_batchInstanceResource = nullptr;
	inline EditorBatchInstanceData* g_batchInstanceData = nullptr;

	inline ModelData g_modelData{};
	constexpr size_t kEditorModelMeshTypeCount = static_cast<size_t>(EditorModelMeshType::Count);
	inline ModelData g_editorPrimitiveModelData[kEditorModelMeshTypeCount]{};
	inline ID3D12Resource* g_editorPrimitiveVertexResources[kEditorModelMeshTypeCount] = {};
	inline D3D12_VERTEX_BUFFER_VIEW g_editorPrimitiveVertexBufferViews[kEditorModelMeshTypeCount]{};
	inline uint32_t g_editorPrimitiveVertexCounts[kEditorModelMeshTypeCount] = {};

	inline std::vector<VertexData> g_vertices;

	inline Sprite g_sprite{};
	inline VertexData g_spriteVertices[4]{};
	inline uint32_t g_spriteIndices[kRuntimeSpriteIndexCount]{};

	inline Transforms g_transform{};
	inline Transforms g_spriteTransform{};
	inline Transforms g_cameraTransform{};

	inline Transforms g_uvTransform{};

	inline ID3D12Resource* g_vertexResource = nullptr;
	inline ID3D12Resource* g_modelVertexResource = nullptr;
	inline ID3D12Resource* g_spriteVertexResource = nullptr;
	inline ID3D12Resource* g_spriteIndexResource = nullptr;

	inline D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView{};
	inline D3D12_VERTEX_BUFFER_VIEW g_modelVertexBufferView{};
	inline D3D12_VERTEX_BUFFER_VIEW g_spriteVertexBufferView{};
	inline D3D12_INDEX_BUFFER_VIEW g_spriteIndexBufferView{};

	inline float g_editorWindowWidth = 0.0f;
	inline float g_editorWindowHeight = 0.0f;

	inline float g_editorLeftWidth = 250.0f;
	inline float g_editorRightWidth = 320.0f;
	inline float g_editorBottomHeight = 190.0f;

	inline float g_editorSceneX = 0.0f;
	inline float g_editorSceneY = 0.0f;
	inline float g_editorSceneWidth = 0.0f;
	inline float g_editorSceneHeight = 0.0f;

	inline float g_editorGameX = 0.0f;
	inline float g_editorGameY = 0.0f;
	inline float g_editorGameWidth = 0.0f;
	inline float g_editorGameHeight = 0.0f;

	// g_editorRenderOriginX / Y は ImGui 座標から back buffer 座標へ直すための原点。
	// ViewportsEnable が有効な間、ImGui の画面座標はデスクトップ基準になり、
	// Window の枠とタイトルバーの分だけ back buffer 座標より大きくなる。
	// この差を引かずに D3D12 の viewport へ渡すと、3D だけが右下へずれて描かれる。
	inline float g_editorRenderOriginX = 0.0f;
	inline float g_editorRenderOriginY = 0.0f;

	inline bool g_isSceneViewVisible = false;
	inline bool g_isGameViewVisible = false;
	inline bool g_isAnimationWindowVisible = false;  // true なら Docking 可能な Animation Window を表示する。
	inline bool g_isSplineEditorVisible = false;  // trueなら汎用Spline Editorを表示する。
	inline bool g_isGameplayTimelineWindowVisible = false;  // trueなら汎用Event Timelineを表示する。
	inline bool g_isStateGraphWindowVisible = false;  // trueなら汎用Threshold State Graphを表示する。
	inline bool g_isDiagnosticsWindowVisible = false;  // trueならProfilerとScene Validatorを表示する。
	inline bool g_isLogMonitorWindowVisible = false;  // trueなら汎用ログ・監視Windowを表示する。
	inline bool g_isTeamCollaborationWindowVisible = false;  // trueなら共同制作Server、接続、競合画面を表示する。
	inline bool g_isExternalFeatureWindowVisible = false;  // trueなら音声認識/画像認識/オンライン/HapticsのDebug Windowを表示する。
	inline bool g_isHookWireDebugWindowVisible = false;  // trueならHook構成とRuntime Wireの検査Windowを表示する。
	inline bool g_isHookWireSceneGizmoVisible = true;  // trueならSceneViewへHook→力伝達先の線とAnchorを重ねる。
	inline bool g_isGameViewUsingSceneCamera = true;

	inline D3D12_VIEWPORT g_viewport{};
	inline D3D12_RECT g_scissorRect{};

	inline Matrix4x4 g_cameraMatrix{};
	inline Matrix4x4 g_viewMatrix{};
	inline Matrix4x4 g_projectionMatrix{};

	inline Matrix4x4 g_gameCameraMatrix{};
	inline Matrix4x4 g_gameViewMatrix{};
	inline Matrix4x4 g_gameProjectionMatrix{};
	inline Vector3 g_gameCameraPosition{};
	inline bool g_runtimeGameCameraOverrideActive = false;  // CameraBlendがGame View姿勢を上書きしている間true。
	inline Transforms g_runtimeGameCameraOverrideTransform{};  // CameraBlendが計算したWorld姿勢。
	inline Vector3 g_runtimeGameCameraPositionOffset{};  // CameraShakeが加えるWorld位置差分。
	inline Vector3 g_runtimeGameCameraRotationOffset{};  // CameraShakeが加える回転差分rad。

#ifdef USE_IMGUI
	// Text/TextMeshProUGUIのtextFontIndexが選ぶFont候補。0番は既定Font(io.FontDefault)と同じものを指す。
	// EditorPlatformManager.cpp の初期化でAddFontFromFileTTFした結果をここへ格納する。
	inline constexpr int32_t kUiFontVariantCount = 5;
	inline std::array<ImFont*, kUiFontVariantCount> g_uiFontVariants{};
#endif

	inline Matrix4x4 g_spriteProjectionMatrix{};

	inline float g_editorCameraMoveSpeed = 0.12f;
	inline float g_editorCameraRotateSpeed = 0.006f;
	inline float g_editorCameraWheelMoveSpeed = 0.5f;
	inline float g_editorCameraPanSpeed = 0.01f;
	inline float g_editorCameraFastRate = 1000.0f;

	inline float g_sceneClearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};

	inline bool g_isSceneGizmoVisible = true;
	inline bool g_isLightGizmoVisible = false;
	inline bool g_isCameraGizmoVisible = false;

	inline Vector3 g_directionalLightIconPosition = {-1.8f, 1.4f, 0.0f};

	inline EditorSceneObjectManager g_editorSceneObjectManager;
	inline int32_t g_selectedPlacedSceneObjectIndex = -1;
	inline ComPtr<ID3D12Fence> g_fence;
	inline uint64_t g_fenceValue = 0;
	inline HANDLE g_fenceEvent = nullptr;

	inline std::wstring g_textureFilePaths[kRuntimeTextureCount];
	inline std::string g_textureFilePathStrings[kRuntimeTextureCount];
	inline std::vector<std::string> g_editorTextureFilePaths;

	inline DirectX::TexMetadata g_textureMetadatas[kRuntimeTextureCount]{};
	inline ID3D12Resource* g_textureResources[kRuntimeTextureCount] = {nullptr, nullptr, nullptr, nullptr};

	inline ID3D12Resource* g_intermediateResources[kRuntimeTextureCount] = {nullptr, nullptr, nullptr, nullptr};

	inline UINT g_srvDescriptorSize = 0;

	inline D3D12_CPU_DESCRIPTOR_HANDLE g_textureSrvHandlesCPU[kRuntimeTextureCount]{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_textureSrvHandlesGPU[kRuntimeTextureCount]{};
	inline ID3D12Resource* g_environmentTextureResource = nullptr;
	inline ID3D12Resource* g_environmentTextureUploadResource = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_environmentTextureSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_environmentTextureSrvHandleGPU{};
	inline std::string g_environmentTextureAssetPath;
	inline std::string g_loadedEnvironmentTextureAssetPath;
	inline bool g_isEnvironmentTextureDirty = false;
	inline ID3D12Resource* g_iblEnvironmentCube = nullptr;
	inline ID3D12Resource* g_iblIrradianceCube = nullptr;
	inline ID3D12Resource* g_iblPrefilterCube = nullptr;
	inline ID3D12Resource* g_iblBRDFLUT = nullptr;
	inline ID3D12Resource* g_colorGradingLut = nullptr;
	inline ID3D12Resource* g_customColorGradingLutResource = nullptr;
	inline ID3D12Resource* g_customColorGradingLutUploadResource = nullptr;
	inline std::string g_loadedColorGradingLutAssetPath;
	inline bool g_iblEnvironmentCubeLoaded = false;  // EnvironmentMapEffect 用の実キューブマップが読み込めたか。
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_iblIrradianceSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_iblIrradianceSrvHandleGPU{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_iblPrefilterSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_iblPrefilterSrvHandleGPU{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_iblEnvironmentSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_iblEnvironmentSrvHandleGPU{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_iblBrdfLutSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_iblBrdfLutSrvHandleGPU{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_colorGradingLutSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_colorGradingLutSrvHandleGPU{};
	inline uint32_t g_iblPrefilterMipCount = 0;

	inline EditorRuntimeManager g_editorRuntimeManager{};

	inline FeelKitHaptics g_feelKitHaptics{};

	inline EditorSceneCameraController g_editorSceneCameraController{};

	inline int g_selectedSceneObject = 0;
	inline int g_activeEditorTool = 1;
	inline int g_editorViewportTabIndex = 0;
	inline std::string g_selectedAssetPath;
	inline std::string g_currentScenePath;

	inline bool g_isStandaloneGame = false;  // 書き出した Player として起動中なら true。
	inline std::vector<std::string> g_gameBuildScenePaths;  // Player に含めた遷移可能 Scene 一覧。

	inline bool g_isPvShootModeActive = false;  // PV撮影モード中はGameViewだけを全画面表示し、他Windowを隠す。
	inline float g_pvShootManualTimeScale = 1.0f;  // PV撮影モード中にRuntimeへ乗算する再生速度。1で通常速度。

	inline RECT GetRuntimeCursorClipRect() {
		RECT cursorClipRect{};

		if (g_isGameViewVisible && g_editorGameWidth > 1.0f && g_editorGameHeight > 1.0f) {
			cursorClipRect.left = static_cast<LONG>(g_editorGameX);
			cursorClipRect.top = static_cast<LONG>(g_editorGameY);
			cursorClipRect.right = static_cast<LONG>(g_editorGameX + g_editorGameWidth);
			cursorClipRect.bottom = static_cast<LONG>(g_editorGameY + g_editorGameHeight);
			return cursorClipRect;
		}

		if (g_windowHandle != nullptr) {
			GetClientRect(g_windowHandle, &cursorClipRect);
			POINT clientOrigin{cursorClipRect.left, cursorClipRect.top};
			POINT clientEnd{cursorClipRect.right, cursorClipRect.bottom};
			ClientToScreen(g_windowHandle, &clientOrigin);
			ClientToScreen(g_windowHandle, &clientEnd);
			cursorClipRect = {clientOrigin.x, clientOrigin.y, clientEnd.x, clientEnd.y};
		}

		return cursorClipRect;
	}

	inline void ApplyRuntimeCursorLock(bool isLocked) {
		const bool wasLocked = g_runtimeCursorLocked;
		g_runtimeCursorLocked = isLocked;

		// Editor内でPlayしているだけの時にOSカーソルを実際にClipCursor/SetCursorPosすると、
		// GameView外(SceneView、Inspector、他Window)へマウスが一切出せなくなり、
		// 「視点操作を常時ONにしたScene」でギズモやWindow切り替えが完全に触れなくなる。
		// これは書き出し済みPlayer(g_isStandaloneGame)でだけ意味のある拘束なので、
		// Editor実行中はIsCursorLocked()が返す要求状態だけ更新し、実際のOS拘束は行わない。
		if (!g_isStandaloneGame) {
			return;
		}

		if (!isLocked) {
			ClipCursor(nullptr);
			return;
		}

		const RECT cursorClipRect = GetRuntimeCursorClipRect();
		ClipCursor(&cursorClipRect);

		// Lock開始時だけ中央へ戻す。毎フレーム呼ぶ関数なので、ここをwasLocked判定なしに
		// 呼び続けると、実カーソルが視点操作で動いた直後に強制的に中央へ引き戻され続け、
		// 「中央のカーソル」と「動かした先で点滅するカーソル」の二重表示に見えていた。
		if (!wasLocked) {
			SetCursorPos(
				(cursorClipRect.left + cursorClipRect.right) / 2,
				(cursorClipRect.top + cursorClipRect.bottom) / 2);
		}
	}

	inline void ApplyRuntimeCursorVisibility(bool isVisible) {
		if (g_runtimeCursorVisible == isVisible) {
			return;
		}

		g_runtimeCursorVisible = isVisible;

		// ロックと同じ理由。Editor内Playでカーソルを消すと、GameView外のWindowを
		// マウスで探すことすらできなくなるため、書き出し済みPlayerでだけ実際に隠す。
		if (!g_isStandaloneGame) {
			return;
		}

		if (isVisible) {
			while (ShowCursor(TRUE) < 0) {
			}
		}
		else {
			while (ShowCursor(FALSE) >= 0) {
			}
		}
	}

	inline char g_hierarchyFilter[128] = {};
	inline char g_assetFilter[128] = {};

	inline bool g_isConsoleCleared = false;
	inline bool g_isSceneRangeSelecting = false;
	inline bool g_isSceneMiddleCameraDragging = false;
	inline bool g_isSceneRightCameraDragging = false;

	inline bool g_isGizmoLocalMode = true;
	inline bool g_isGizmoSnapEnabled = false;
	inline bool g_isSceneAssistVisible = true;
	inline bool g_isLegacyPreviewVisible = false;

	inline float g_gizmoSnapValues[3] = {0.5f, 0.5f, 0.5f};

	inline auto g_sceneRangeStart = ImVec2(0.0f, 0.0f);
	inline auto g_sceneRangeEnd = ImVec2(0.0f, 0.0f);

	inline EditorScene g_editorScene;
	inline bool g_isEditorSceneInitialized = false;
	inline bool g_isEditorRuntimeInitialized = false;

	inline int32_t g_selectedEditorGameObjectId = -1;
	inline std::vector<int32_t> g_selectedEditorGameObjectIds;
	inline int32_t g_previousSelectedEditorGameObjectId = -1;

	inline bool IsGameObjectSelected(int32_t gameObjectId) {
		return std::find(
			g_selectedEditorGameObjectIds.begin(),
			g_selectedEditorGameObjectIds.end(),
			gameObjectId) != g_selectedEditorGameObjectIds.end();
	}

	inline void ClearSelectedGameObjects() {
		g_selectedEditorGameObjectIds.clear();
		g_selectedEditorGameObjectId = -1;
		g_previousSelectedEditorGameObjectId = -1;
		g_selectedPlacedSceneObjectIndex = -1;
		g_selectedSceneObject = 0;
	}

	inline void SetSelectedGameObjectIds(const std::vector<int32_t>& selectedGameObjectIds) {
		g_selectedEditorGameObjectIds.clear();

		for (int32_t gameObjectId : selectedGameObjectIds) {
			if (gameObjectId < 0 || IsGameObjectSelected(gameObjectId)) {
				continue;
			}

			g_selectedEditorGameObjectIds.push_back(gameObjectId);
		}

		g_selectedEditorGameObjectId =
			g_selectedEditorGameObjectIds.empty() ? -1 : g_selectedEditorGameObjectIds.front();
		g_previousSelectedEditorGameObjectId = -1;

		if (g_selectedEditorGameObjectIds.empty()) {
			g_selectedPlacedSceneObjectIndex = -1;
			g_selectedSceneObject = 0;
		}
	}

	inline void SetSingleSelectedGameObject(int32_t gameObjectId) {
		std::vector<int32_t> selectedGameObjectIds;
		if (gameObjectId >= 0) {
			selectedGameObjectIds.push_back(gameObjectId);
		}

		SetSelectedGameObjectIds(selectedGameObjectIds);
	}

	inline char g_selectedGameObjectName[128] = {};

	inline int32_t g_selectedAddComponentIndex = 0;

	inline std::vector<std::string> g_editorConsoleMessages = {
		"Editor: Ready",
		"Scene: Empty startup",
		"Runtime: Play physics waits for Play button",
	};

	inline EditorSelectionManager g_editorSelectionManager;
	inline EditorSceneSynchronizer g_editorSceneSynchronizer;
	inline EditorAssetFactory g_editorAssetFactory;
	inline EditorMainMenuBar g_editorMainMenuBar;
	inline EditorHierarchyPanel g_editorHierarchyPanel;
	inline EditorInspectorPanel g_editorInspectorPanel;
	inline EditorBottomPanel g_editorBottomPanel;
	inline bool g_isEditorManagerInitialized = false;
	inline bool g_isDockLayoutInitialized = false;

	inline ID3D12Resource* CreateRuntimeDepthStencilResource(uint32_t width, uint32_t height) {
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Width = width;
		resourceDesc.Height = height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		ID3D12Resource* resource = nullptr;

		HRESULT createResult = g_device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&g_depthClearValue,
			IID_PPV_ARGS(&resource));
		EDITOR_HR_VERIFY(createResult);
		return resource;
	}

	inline ID3D12Resource* CreateRuntimeOpaqueDepthCopyResource(uint32_t width, uint32_t height) {
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Width = width;
		resourceDesc.Height = height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		ID3D12Resource* resource = nullptr;
		const HRESULT createResult = g_device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&g_depthClearValue,
			IID_PPV_ARGS(&resource));
		EDITOR_HR_VERIFY(createResult);
		return resource;
	}

	inline void WaitForGpu() {
		g_fenceValue++;
		HRESULT signalResult = g_commandQueue->Signal(g_fence.Get(), g_fenceValue);
		EDITOR_HR_VERIFY(signalResult);

		if (g_fence->GetCompletedValue() < g_fenceValue) {
			signalResult = g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
			EDITOR_HR_VERIFY(signalResult);
			WaitForSingleObject(g_fenceEvent, INFINITE);
		}
	}

	inline void UpdateEditorLayout() {
		constexpr float editorMenuHeight = 20.0f;
		constexpr float editorSceneHeaderHeight = 24.0f;

		g_editorLeftWidth = (std::clamp)(g_editorLeftWidth, 160.0f, 420.0f);
		g_editorRightWidth = (std::clamp)(g_editorRightWidth, 220.0f, 520.0f);
		g_editorBottomHeight = (std::clamp)(g_editorBottomHeight, 120.0f, 320.0f);

		g_editorSceneX = g_editorLeftWidth;
		g_editorSceneY = editorMenuHeight + editorSceneHeaderHeight;

		g_editorSceneWidth = g_editorWindowWidth - g_editorLeftWidth - g_editorRightWidth;
		g_editorSceneHeight = g_editorWindowHeight - g_editorSceneY - g_editorBottomHeight;
		g_editorSceneWidth = (std::max)(g_editorSceneWidth, 240.0f);
		g_editorSceneHeight = (std::max)(g_editorSceneHeight, 180.0f);
	}

	inline void ResizeRenderTargets(uint32_t width, uint32_t height) {
		if (width == g_renderWidth && height == g_renderHeight) {
			return;
		}

		WaitForGpu();

		for (ID3D12Resource*& swapChainResource : g_swapChainResources) {
			if (swapChainResource != nullptr) {
				swapChainResource->Release();
				swapChainResource = nullptr;
			}
		}

		if (g_depthStencilResource != nullptr) {
			g_depthStencilResource->Release();
			g_depthStencilResource = nullptr;
		}

		if (g_opaqueDepthCopyResource != nullptr) {
			g_opaqueDepthCopyResource->Release();
			g_opaqueDepthCopyResource = nullptr;
		}

		g_renderWidth = width;
		g_renderHeight = height;

		HRESULT resizeResult = g_swapChain->ResizeBuffers(
			2,
			g_renderWidth,
			g_renderHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			0);
		EDITOR_HR_VERIFY(resizeResult);

		// 取得できた back buffer だけ RTV を張り直す。GetBuffer が失敗した Buffer は
		// nullptr のままなので、そのまま CreateRenderTargetView へ渡してはいけない。
		bool runtimeSwapChainBuffersReady = true;
		for (uint32_t bufferIndex = 0; bufferIndex < kRuntimeSwapChainBufferCount; bufferIndex++) {
			resizeResult = g_swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&g_swapChainResources[bufferIndex]));
			if (!EDITOR_HR_OK(resizeResult) || g_swapChainResources[bufferIndex] == nullptr) {
				runtimeSwapChainBuffersReady = false;
				continue;
			}
			g_device->CreateRenderTargetView(
				g_swapChainResources[bufferIndex],
				&g_rtvDesc,
				g_rtvHandles[bufferIndex]);
		}

		// back buffer が欠けたままでは Present も Barrier もできない。Depth の
		// 再生成へ進む前に終了要求を出し、null を参照する経路へ入らないようにする。
		if (!runtimeSwapChainBuffersReady) {
			g_isEndRequested = true;
			g_exitCode = 2; // 2 は起動後の復帰不能な描画失敗を表す。
			return;
		}

		g_depthStencilResource = CreateRuntimeDepthStencilResource(g_renderWidth, g_renderHeight);
		g_opaqueDepthCopyResource = CreateRuntimeOpaqueDepthCopyResource(g_renderWidth, g_renderHeight);

		// Depth の再生成が失敗すると DepthStencilView / SRV / Barrier がすべて
		// nullptr を参照する。描画を続けられないので、ここで終了要求を出す。
		if (g_depthStencilResource == nullptr || g_opaqueDepthCopyResource == nullptr) {
			g_isEndRequested = true;
			g_exitCode = 2; // 2 は起動後の復帰不能な描画失敗を表す。
			return;
		}

		g_device->CreateDepthStencilView(g_depthStencilResource, &g_dsvDesc, g_dsvHandle);

		{
			D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
			depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
			depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			depthSrvDesc.Texture2D.MipLevels = 1;
			g_depthSrvHandleCPU = GetCPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeDepthSrvDescriptorIndex);
			g_depthSrvHandleGPU = GetGPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeDepthSrvDescriptorIndex);
			g_device->CreateShaderResourceView(g_depthStencilResource, &depthSrvDesc, g_depthSrvHandleCPU);

			g_opaqueDepthCopySrvHandleCPU = GetCPUDescriptorHandle(
				g_srvDescriptorHeap,
				g_srvDescriptorSize,
				kRuntimeOpaqueDepthCopySrvDescriptorIndex);
			g_opaqueDepthCopySrvHandleGPU = GetGPUDescriptorHandle(
				g_srvDescriptorHeap,
				g_srvDescriptorSize,
				kRuntimeOpaqueDepthCopySrvDescriptorIndex);
			g_device->CreateShaderResourceView(
				g_opaqueDepthCopyResource,
				&depthSrvDesc,
				g_opaqueDepthCopySrvHandleCPU);
		}

		UINT rtvSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		auto recreateRenderTarget = [&](ID3D12Resource*& resource, uint32_t rtWidth, uint32_t rtHeight,
		                                 DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle) {
			if (resource != nullptr) {
				resource->Release();
				resource = nullptr;
			}
			D3D12_RESOURCE_DESC desc{};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			desc.Width = rtWidth;
			desc.Height = rtHeight;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.Format = format;
			desc.SampleDesc.Count = 1;
			desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_CLEAR_VALUE clearValue{};
			clearValue.Format = format;
			clearValue.Color[0] = 0.0f;
			clearValue.Color[1] = 0.0f;
			clearValue.Color[2] = 0.0f;
			clearValue.Color[3] = 0.0f;
			HRESULT hr = g_device->CreateCommittedResource(
				&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				&clearValue, IID_PPV_ARGS(&resource));
			EDITOR_HR_VERIFY(hr);
			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = format;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			g_device->CreateRenderTargetView(resource, &rtvDesc, rtvHandle);
		};

		// HDR RT (index 2)
		recreateRenderTarget(g_hdrRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 2));

		uint32_t bloomWidth = (std::max)(1u, g_renderWidth / 4);
		uint32_t bloomHeight = (std::max)(1u, g_renderHeight / 4);
		for (uint32_t i = 0; i < 2; i++) {
			recreateRenderTarget(g_bloomRenderTargets[i], bloomWidth, bloomHeight,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 3u + i));
		}

		recreateRenderTarget(g_postProcessRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 5u));

		for (uint32_t i = 0; i < 2; i++) {
			recreateRenderTarget(g_ssaoRenderTargets[i], g_renderWidth, g_renderHeight,
				DXGI_FORMAT_R8_UNORM,
				GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 6u + i));
		}

		// SSGI は半解像度。ノイズは後段のTemporalで均す。
		g_ssgiRenderWidth = (std::max)(1u, g_renderWidth / 2u);
		g_ssgiRenderHeight = (std::max)(1u, g_renderHeight / 2u);
		recreateRenderTarget(g_ssgiRenderTarget, g_ssgiRenderWidth, g_ssgiRenderHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 14u));

		for (uint32_t i = 0; i < 2; i++) {
			recreateRenderTarget(g_ssgiHistoryRenderTargets[i], g_ssgiRenderWidth, g_ssgiRenderHeight,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 15u + i));
		}

		// 解像度が変わると履歴の位置が合わないので作り直す。
		g_isSsgiHistoryValid = false;

		recreateRenderTarget(g_hdrCompositeRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 8u));


		recreateRenderTarget(g_materialMaskRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 10u));

		recreateRenderTarget(g_planarReflectionRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 11u));

		// Weighted Blended OIT は色の重み付き総和と透過率を別々に保持する。
		recreateRenderTarget(g_oitAccumulationRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 12u));
		recreateRenderTarget(g_oitRevealageRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R16_FLOAT,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 13u));

		// HDR SRV
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			g_hdrSrvHandleCPU = GetCPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeHdrSrvDescriptorIndex);
			g_hdrSrvHandleGPU = GetGPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeHdrSrvDescriptorIndex);
			g_device->CreateShaderResourceView(g_hdrRenderTarget, &srvDesc, g_hdrSrvHandleCPU);
		}

		// Bloom SRVs
		for (int i = 0; i < 2; i++) {
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			uint32_t srvIndex = (i == 0) ? kRuntimeBloomSrvDescriptorIndexA : kRuntimeBloomSrvDescriptorIndexB;
			g_bloomSrvHandlesCPU[i] = GetCPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, srvIndex);
			g_bloomSrvHandlesGPU[i] = GetGPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, srvIndex);
			g_device->CreateShaderResourceView(g_bloomRenderTargets[i], &srvDesc, g_bloomSrvHandlesCPU[i]);
		}

		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			g_postProcessRtvHandle = GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 5u);
			g_postProcessSrvHandleCPU = GetCPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimePostProcessSrvDescriptorIndex);
			g_postProcessSrvHandleGPU = GetGPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimePostProcessSrvDescriptorIndex);
			g_device->CreateShaderResourceView(g_postProcessRenderTarget, &srvDesc, g_postProcessSrvHandleCPU);
		}

		for (uint32_t i = 0; i < 2; i++) {
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R8_UNORM;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			uint32_t srvIndex = (i == 0u) ? kRuntimeSsaoSrvDescriptorIndexA : kRuntimeSsaoSrvDescriptorIndexB;
			g_ssaoRtvHandles[i] = GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 6u + i);
			g_ssaoSrvHandlesCPU[i] = GetCPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, srvIndex);
			g_ssaoSrvHandlesGPU[i] = GetGPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, srvIndex);
			g_device->CreateShaderResourceView(g_ssaoRenderTargets[i], &srvDesc, g_ssaoSrvHandlesCPU[i]);
		}

		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			g_ssgiRtvHandle = GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 14u);
			g_ssgiSrvHandleCPU = GetCPUDescriptorHandle(
				g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeSsgiSrvDescriptorIndex);
			g_ssgiSrvHandleGPU = GetGPUDescriptorHandle(
				g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeSsgiSrvDescriptorIndex);
			g_device->CreateShaderResourceView(g_ssgiRenderTarget, &srvDesc, g_ssgiSrvHandleCPU);

			for (uint32_t i = 0; i < 2; i++) {
				const uint32_t srvIndex = (i == 0u)
					? kRuntimeSsgiHistorySrvDescriptorIndexA
					: kRuntimeSsgiHistorySrvDescriptorIndexB;
				g_ssgiHistoryRtvHandles[i] =
					GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 15u + i);
				g_ssgiHistorySrvHandlesCPU[i] =
					GetCPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, srvIndex);
				g_ssgiHistorySrvHandlesGPU[i] =
					GetGPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, srvIndex);
				g_device->CreateShaderResourceView(
					g_ssgiHistoryRenderTargets[i], &srvDesc, g_ssgiHistorySrvHandlesCPU[i]);
			}
		}

		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			g_hdrCompositeRtvHandle = GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 8u);
			g_hdrCompositeSrvHandleCPU = GetCPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeHdrCompositeSrvDescriptorIndex);
			g_hdrCompositeSrvHandleGPU = GetGPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeHdrCompositeSrvDescriptorIndex);
			g_device->CreateShaderResourceView(g_hdrCompositeRenderTarget, &srvDesc, g_hdrCompositeSrvHandleCPU);
		}


		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			g_materialMaskRtvHandle = GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 10u);
			g_materialMaskSrvHandleCPU = GetCPUDescriptorHandle(
				g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeMaterialMaskSrvDescriptorIndex);
			g_materialMaskSrvHandleGPU = GetGPUDescriptorHandle(
				g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeMaterialMaskSrvDescriptorIndex);
			g_device->CreateShaderResourceView(g_materialMaskRenderTarget, &srvDesc, g_materialMaskSrvHandleCPU);
		}

		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			g_planarReflectionRtvHandle = GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 11u);
			g_planarReflectionSrvHandleCPU = GetCPUDescriptorHandle(
				g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimePlanarReflectionSrvDescriptorIndex);
			g_planarReflectionSrvHandleGPU = GetGPUDescriptorHandle(
				g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimePlanarReflectionSrvDescriptorIndex);
			g_device->CreateShaderResourceView(g_planarReflectionRenderTarget, &srvDesc, g_planarReflectionSrvHandleCPU);
		}

		// OIT の revealage は PostProcess RootSignature の t2/t3 連続テーブルへ載せる。
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC accumulationSrvDesc{};
			accumulationSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
			accumulationSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			accumulationSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			accumulationSrvDesc.Texture2D.MipLevels = 1;

			D3D12_SHADER_RESOURCE_VIEW_DESC revealageSrvDesc = accumulationSrvDesc;
			revealageSrvDesc.Format = DXGI_FORMAT_R16_FLOAT;

			g_oitRtvHandles[0] = GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 12u);
			g_oitRtvHandles[1] = GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 13u);

			g_oitSrvHandlesCPU[0] = GetCPUDescriptorHandle(
				g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeOitAccumulationSrvDescriptorIndex);
			g_oitSrvHandlesGPU[0] = GetGPUDescriptorHandle(
				g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeOitAccumulationSrvDescriptorIndex);
			g_oitSrvHandlesCPU[1] = GetCPUDescriptorHandle(
				g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeOitRevealageSrvDescriptorIndex);
			g_oitSrvHandlesGPU[1] = GetGPUDescriptorHandle(
				g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeOitRevealageSrvDescriptorIndex);

			g_device->CreateShaderResourceView(
				g_oitAccumulationRenderTarget, &accumulationSrvDesc, g_oitSrvHandlesCPU[0]);
			g_device->CreateShaderResourceView(
				g_oitRevealageRenderTarget, &revealageSrvDesc, g_oitSrvHandlesCPU[1]);

			const D3D12_CPU_DESCRIPTOR_HANDLE duplicateRevealageHandle = GetCPUDescriptorHandle(
				g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeOitRevealageDuplicateSrvDescriptorIndex);
			g_device->CreateShaderResourceView(
				g_oitRevealageRenderTarget, &revealageSrvDesc, duplicateRevealageHandle);
		}

		// 深度依存の Compute Texture も Scene 描画サイズへ追従させる。
		const bool isDepthHierarchyResized = g_depthHierarchyManager.Resize(g_renderWidth, g_renderHeight);
		assert(isDepthHierarchyResized);

		const bool isGBufferResized = g_gBufferManager.Resize(g_renderWidth, g_renderHeight);
		assert(isGBufferResized);

		const bool isTemporalRenderingResized = g_temporalRenderingManager.Resize(g_renderWidth, g_renderHeight);
		assert(isTemporalRenderingResized);

		const bool isPostProcessQualityResized = g_postProcessQualityManager.Resize(
			g_renderWidth,
			g_renderHeight);
		assert(isPostProcessQualityResized);
	}
}

#pragma warning(pop)
