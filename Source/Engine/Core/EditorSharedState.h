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
	// WAV �t�@�C���ǂݍ��ݗp�̃f�[�^�\��
	// ================================
	struct ChunkHeader {
		char id[4]; // id �� "RIFF"�A"fmt "�A"data" �Ȃǂ� 4 �����`�����N���B
		int32_t size; // size �͂��̃`�����N�{�̂̃o�C�g���B
	};

	struct RiffHeader {
		ChunkHeader chunk; // chunk �� RIFF �t�@�C���S�̂̃`�����N���B
		char type[4]; // type �� WAV �t�@�C���ł��邱�Ƃ�\�� "WAVE"�B
	};

	struct FormatChunk {
		ChunkHeader chunk; // chunk �� "fmt " �`�����N�� ID �ƃT�C�Y�B
		WAVEFORMATEX format; // format �� �T���v�����O���[�g��`�����l�����Ȃǂ� WAV �`�����B
	};

	struct SoundData {
		WAVEFORMATEX wfex; // wfex �� XAudio2 SourceVoice �쐬�ɓn�� WAV �`�����B
		BYTE* pBuffer; // pBuffer �� WAV �� PCM �f�[�^�{�́B
		uint32_t bufferSize; // bufferSize �� pBuffer �̃o�C�g���B
	};

	inline ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes);
	// GPU UploadBuffer �� OBJ �ǂݍ��݂Ɏg�� helper �錾�B
	inline MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
	inline ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

	inline SoundData SoundLoadWave(const char* filePath); // WAV �ǂݍ��݂Ɖ���Ɏg�� helper �錾�B
	inline void SoundUnload(SoundData* soundData);

	inline D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap, UINT descriptorSize, UINT index) {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
		// handle �� Heap �擪�� CPU DescriptorHandle�B
		handle.ptr += descriptorSize * index; // descriptorSize * index �����i�߂āAindex �Ԗڂ� CPU Handle �����B
		return handle;
	}

	inline D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
		ID3D12DescriptorHeap* descriptorHeap, UINT descriptorSize, UINT index) {
		D3D12_GPU_DESCRIPTOR_HANDLE handle = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
		// handle �� Heap �擪�� GPU DescriptorHandle�B
		handle.ptr += descriptorSize * index; // descriptorSize * index �����i�߂āAindex �Ԗڂ� GPU Handle �����B
		return handle;
	}

	inline DirectX::ScratchImage LoadTexture(const std::wstring& filePath) {
		// metadata �͓ǂݍ��񂾉摜�̕��E�����E�`�����󂯎��B
		DirectX::TexMetadata metadata{};

		// image �� WIC / HDR / DDS ����ǂݍ��񂾌��摜�f�[�^�B
		DirectX::ScratchImage image{};

		std::filesystem::path path(filePath);
		std::wstring extension = path.extension().wstring();
		std::transform(extension.begin(), extension.end(), extension.begin(), towlower);

		HRESULT hr = E_FAIL;
		bool useSrgbMipFilter = true;
		if (extension == L".hdr") {
			hr = DirectX::LoadFromHDRFile(filePath.c_str(), &metadata, image);
			useSrgbMipFilter = false;
		}
		else if (extension == L".dds") {
			hr = DirectX::LoadFromDDSFile(filePath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
			useSrgbMipFilter = false;
		}
		else {
			hr = DirectX::LoadFromWICFile(filePath.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, &metadata, image);
			useSrgbMipFilter = true;
		}
		assert(SUCCEEDED(hr));

		// mipImages �� GPU �T���v�����O�p�� mipmap ��ǉ������摜�f�[�^�B
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
		assert(SUCCEEDED(hr));

		return mipImages;
	}

	inline ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata) {
		// resourceDesc �� metadata �ɍ��킹�� 2D Texture Resource �̐ݒ�B
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Width = static_cast<UINT>(metadata.width);
		resourceDesc.Height = static_cast<UINT>(metadata.height);
		resourceDesc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
		resourceDesc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
		resourceDesc.Format = metadata.format;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Dimension = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);

		// heapProperties �� GPU ��p��������� Texture ��u���w��B
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		ID3D12Resource* resource = nullptr; // resource �͍쐬���ĕԂ� Texture Resource�B

		// ������� COPY_DEST �́A��� UploadTextureData ����R�s�[���邽�߁B
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
		std::vector<D3D12_SUBRESOURCE_DATA> subresources; // subresources �� mipmap �e�i�� UpdateSubresources �ɓn�����߂̔z��B
		subresources.reserve(mipImages.GetImageCount());

		const DirectX::Image* images = mipImages.GetImages(); // images �� DirectXTex ���ێ����� mipmap �e�i�̉摜�|�C���^�B
		for (size_t index = 0; index < mipImages.GetImageCount(); ++index) {
			// subresource �� 1 mip ���̃s�N�Z���|�C���^�ƍs/�X���C�X���B
			D3D12_SUBRESOURCE_DATA subresource{};
			subresource.pData = images[index].pixels;
			subresource.RowPitch = static_cast<LONG_PTR>(images[index].rowPitch);
			subresource.SlicePitch = static_cast<LONG_PTR>(images[index].slicePitch);
			subresources.push_back(subresource);
		}

		UINT64 intermediateSize = GetRequiredIntermediateSize(texture, 0, static_cast<UINT>(subresources.size()));
		// intermediateSize �͑S mip �� GPU Texture �փR�s�[���邽�߂ɕK�v�� UploadBuffer �T�C�Y�B
		ID3D12Resource* intermediateResource = CreateBufferResource(device, intermediateSize);
		// intermediateResource �� texture �փR�s�[���邽�߂̈ꎞ UploadBuffer�B

		// UpdateSubresources �� UploadBuffer ���� Default Heap Texture �փR�s�[���߂�ςށB
		UpdateSubresources(
			commandList,
			texture,
			intermediateResource,
			0,
			0,
			static_cast<UINT>(subresources.size()),
			subresources.data());

		// barrier �� Texture ���R�s�[���Ԃ��� Shader �ǂݎ���Ԃ֕ς��閽�߁B
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
		// uploadHeapProperties �� CPU ���� Map ���ď������߂� Upload Heap �w��B
		D3D12_HEAP_PROPERTIES uploadHeapProperties{};
		uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

		// resourceDesc �� sizeInBytes ���̔ėp Buffer Resource �ݒ�B
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Width = sizeInBytes;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ID3D12Resource* resource = nullptr; // resource �͍쐬���ĕԂ� Upload Buffer�B

		// GENERIC_READ �� CPU �������݌�� GPU ����ǂޒ萔/���_ Buffer �Ɏg���B
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

		ComPtr<IDxcBlobEncoding> shaderSource; // shaderSource �͓ǂݍ��� HLSL �t�@�C���{���B
		HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, shaderSource.GetAddressOf());
		assert(SUCCEEDED(hr));

		// shaderSourceBuffer �� DXC �ɓn���\�[�X�R�[�h�̃|�C���^�E�T�C�Y�E�����R�[�h�B
		DxcBuffer shaderSourceBuffer{};
		shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
		shaderSourceBuffer.Size = shaderSource->GetBufferSize();
		shaderSourceBuffer.Encoding = DXC_CP_UTF8;

		// arguments �� entry point�AShader Model�ADebug ���A�œK���A�s��z�u�̎w��B
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

		std::vector<LPCWSTR> arguments{};
		arguments.push_back(filePath.c_str());
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

		ComPtr<IDxcResult> shaderResult; // shaderResult �� DXC �̃R���p�C�����ʖ{�́B
		hr = dxcCompiler->Compile(
			&shaderSourceBuffer,
			arguments.data(),
			static_cast<uint32_t>(arguments.size()),
			includeHandler,
			IID_PPV_ARGS(shaderResult.GetAddressOf()));
		assert(SUCCEEDED(hr));

		HRESULT compileStatus = S_OK; // compileStatus �� DXC ���Ԃ����ŏI�I�ȃR���p�C�����ہB
		hr = shaderResult->GetStatus(&compileStatus);
		assert(SUCCEEDED(hr));

		ComPtr<IDxcBlobUtf8> shaderError; // shaderError �� HLSL �R���p�C���G���[��x���̕�����B
		shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(shaderError.GetAddressOf()), nullptr);
		if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
			Log(shaderError->GetStringPointer());
			Log(logStream, std::format("Compile Error, path:{}, profile:{}",
			                           ConvertString(filePath), ConvertString(std::wstring{profile})));
		}

		if (FAILED(compileStatus)) {
			return nullptr;
		}

		ComPtr<IDxcBlob> shaderBlob; // shaderBlob �� GPU �ɓn���ŏI�I�� DXIL �o�C�g�R�[�h�B
		hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(shaderBlob.GetAddressOf()), nullptr);
		assert(SUCCEEDED(hr));

		Log(logStream, std::format("Compile Succeeded, path:{}, profile:{}",
		                           ConvertString(filePath), ConvertString(std::wstring{profile})));

		return shaderBlob;
	}

	inline MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
		// materialData �� .mtl ����ǂݎ���� Texture �p�X��Ԃ����߂̍\���́B
		MaterialData materialData{};

		std::ifstream file(directoryPath + "/" + filename); // file �� directoryPath/filename �� .mtl �t�@�C���B
		assert(file.is_open());

		std::string line; // line �� .mtl �� 1 �s���ǂނ��߂̕�����B
		while (std::getline(file, line)) {
			std::string identifier; // identifier �� map_Kd �ȂǁA�s�擪�̖��ߖ��B
			std::istringstream lineStream(line);
			lineStream >> identifier;

			// map_Kd �� Diffuse Texture �t�@�C���������� .mtl ���߁B
			if (identifier == "map_Kd") {
				lineStream >> materialData.textureFilePath;

				materialData.textureFilePath = directoryPath + "/" + materialData.textureFilePath;
				// OBJ ���猩�����΃p�X���A�ǂݍ��݂Ɏg���� directoryPath �t���p�X�֒����B
			}
		}

		return materialData;
	}

	inline ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename) {
		// modelData �� OBJ �̒��_�z��� Material �����܂Ƃ߂ĕԂ��B
		ModelData modelData{};

		std::ifstream file(directoryPath + "/" + filename); // file �� directoryPath/filename �� .obj �t�@�C���B
		assert(file.is_open());

		std::vector<Vector4> positions; // positions / texcoords / normals �� OBJ �� v / vt / vn ���ꎞ�ۑ�����z��B
		std::vector<Vector2> texcoords;
		std::vector<Vector3> normals;

		std::string line; // line �� OBJ �� 1 �s���ǂނ��߂̕�����B

		while (std::getline(file, line)) {
			std::string identifier; // identifier �� v�Avt�Avn�Af�Amtllib �Ȃǂ̍s��ʁB
			std::istringstream lineStream(line);
			lineStream >> identifier;

			// v �͒��_�ʒu�BX �𔽓]���ĉE��/������W�n�̈Ⴂ�����킹��B
			if (identifier == "v") {
				Vector4 position{};
				lineStream >> position.x >> position.y >> position.z;
				position.x *= -1.0f;
				position.w = 1.0f;
				positions.push_back(position);
			}
			else if (identifier == "vt") {
				// vt �� Texture ���W�B�摜���W�n�ɍ��킹�邽�� Y �𔽓]����B
				Vector2 texcoord{};
				lineStream >> texcoord.x >> texcoord.y;
				texcoord.y = 1.0f - texcoord.y;
				texcoords.push_back(texcoord);
			}
			else if (identifier == "vn") {
				// vn �͖@���B�ʒu�Ɠ������W�n�ɍ��킹�邽�� X �𔽓]����B
				Vector3 normal{};
				lineStream >> normal.x >> normal.y >> normal.z;
				normal.x *= -1.0f;
				normals.push_back(normal);
			}
			else if (identifier == "f") {
				// triangle �� 1 �ʕ��� 3 ���_���ꎞ�ۑ�����z��B
				VertexData triangle[3]{};
				for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
					std::string vertexDefinition; // vertexDefinition �� "�ʒu/UV/�@��" �`���� 1 ���_�w��B
					lineStream >> vertexDefinition;

					std::istringstream vertexStream(vertexDefinition);
					// vertexStream �� '/' ��؂�� index ������𕪉����邽�߂� stream�B
					std::string positionIndexString;
					std::string texcoordIndexString;
					std::string normalIndexString;
					std::getline(vertexStream, positionIndexString, '/');
					std::getline(vertexStream, texcoordIndexString, '/');
					std::getline(vertexStream, normalIndexString, '/');

					uint32_t positionIndex = static_cast<uint32_t>(std::stoi(positionIndexString)) - 1;
					// OBJ �� index �� 1 �n�܂�Ȃ̂ŁAC++ �z��p�� 0 �n�܂�֒����B
					uint32_t texcoordIndex = static_cast<uint32_t>(std::stoi(texcoordIndexString)) - 1;
					uint32_t normalIndex = static_cast<uint32_t>(std::stoi(normalIndexString)) - 1;

					triangle[faceVertex].position = positions[positionIndex];
					triangle[faceVertex].texcoord = texcoords[texcoordIndex];
					triangle[faceVertex].normal = normals[normalIndex];
				}

				modelData.vertices.push_back(triangle[2]); // ���_���𔽓]���A���W�n�ϊ���̖ʂ̌��������킹��B
				modelData.vertices.push_back(triangle[1]);
				modelData.vertices.push_back(triangle[0]);
			}
			else if (identifier == "mtllib") {
				std::string materialFilename; // materialFilename �� OBJ ���Q�Ƃ��� .mtl �t�@�C�����B
				lineStream >> materialFilename;

				modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
				// .mtl ��ǂݍ��݁ATexture �p�X�� modelData �ɕێ�����B
			}
		}

		return modelData;
	}

	inline SoundData SoundLoadWave(const char* filePath) {
		// soundData �� WAV ����ǂݎ�����`������ PCM �o�b�t�@��Ԃ��B
		SoundData soundData{};

		std::ifstream file(filePath, std::ios_base::binary); // file �� WAV ���o�C�i���Ƃ��ēǂޓ��� stream�B
		assert(file.is_open());

		// riff �� WAV �t�@�C���擪�� RIFF/WAVE �w�b�_�B
		RiffHeader riff{};
		file.read(reinterpret_cast<char*>(&riff), sizeof(riff));
		assert(std::strncmp(riff.chunk.id, "RIFF", 4) == 0);
		assert(std::strncmp(riff.type, "WAVE", 4) == 0);

		// format �� "fmt " �`�����N�BXAudio2 �� SourceVoice �쐬�Ɏg���B
		FormatChunk format{};
		file.read(reinterpret_cast<char*>(&format), sizeof(ChunkHeader));
		assert(std::strncmp(format.chunk.id, "fmt ", 4) == 0);
		assert(format.chunk.size >= 0);
		assert(static_cast<size_t>(format.chunk.size) <= sizeof(format.format));
		file.read(reinterpret_cast<char*>(&format.format), format.chunk.size);

		// data �� PCM �{�̂����� "data" �`�����N��T�����߂̃w�b�_�B
		ChunkHeader data{};
		file.read(reinterpret_cast<char*>(&data), sizeof(data));
		while (std::strncmp(data.id, "data", 4) != 0) {
			file.seekg(data.size, std::ios_base::cur); // data �ȊO�̃`�����N�̓T�C�Y�������ǂݔ�΂��Ď��̃`�����N������B
			file.read(reinterpret_cast<char*>(&data), sizeof(data));
		}

		assert(data.size >= 0);

		uint32_t dataSize = static_cast<uint32_t>(data.size); // dataSize �� PCM �o�b�t�@�̃o�C�g���B
		auto pBuffer = new char[dataSize]; // pBuffer �� XAudio2 �ɓn�� PCM �f�[�^�BSoundUnload �ŉ������B
		file.read(pBuffer, dataSize);

		soundData.wfex = format.format; // �ǂݍ��񂾌`������ PCM �o�b�t�@�� SoundData �ɋl�߂�B
		soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer);
		soundData.bufferSize = dataSize;
		return soundData;
	}

	inline void SoundUnload(SoundData* soundData) {
		assert(soundData != nullptr); // soundData->pBuffer �� SoundLoadWave �� new[] ���� PCM �o�b�t�@�B
		delete[] soundData->pBuffer;
		soundData->pBuffer = nullptr;
		soundData->bufferSize = 0u;
	}
}

#pragma warning(push)
#pragma warning(disable : 4101 4189 4514 5045)

namespace EditorSharedState {
	constexpr uint32_t kRuntimeTextureCount = 4; // kRuntimeTextureCount �͋N�����ɌŒ�Ŋm�ۂ���W�� Texture ���B
	constexpr uint32_t kRuntimeSwapChainBufferCount = 2; // kRuntimeSwapChainBufferCount �� SwapChain �� back buffer ���B
	constexpr uint32_t kRuntimeSpriteIndexCount = 6; // kRuntimeSpriteIndexCount �� Sprite �l�p�`�� 2 �O�p�`�ŕ`�� index ���B
	constexpr uint32_t kRuntimeShadowMapSize = 2048; // �e�p DepthTexture �̉𑜓x�B
	constexpr uint32_t kRuntimeShadowSrvDescriptorIndex = 15; // 0 �� ImGui�A1-4 �͌Œ� Texture�A16 �ȍ~�͌� Texture�B
	constexpr uint32_t kRuntimeHdrSrvDescriptorIndex = 16; // HDR RT �� SRV �� DescriptorHeap �� 16 �ԖځB
	constexpr uint32_t kRuntimeBloomSrvDescriptorIndexA = 17; // Bloom A �� SRV �� 17 �ԖځB
	constexpr uint32_t kRuntimeBloomSrvDescriptorIndexB = 18; // Bloom B �� SRV �� 18 �ԖځB
	constexpr uint32_t kRuntimePostProcessSrvDescriptorIndex = 19; // ToneMap ��� LDR RT �� FXAA �̓��͂Ƃ��� 19 �ԖځB
	constexpr uint32_t kRuntimeDepthSrvDescriptorIndex = 20; // Scene Depth �� SSAO �œǂނ��߂� SRV�B
	constexpr uint32_t kRuntimeSsaoSrvDescriptorIndexA = 21; // SSAO ���ڂ������ʂ� SRV�B
	constexpr uint32_t kRuntimeSsaoSrvDescriptorIndexB = 22; // SSAO �ڂ������ʂ� SRV�B
	constexpr uint32_t kRuntimeHdrCompositeSrvDescriptorIndex = 23; // ���ˍ����� HDR �� SRV�B
	constexpr uint32_t kRuntimeIblIrradianceSrvDescriptorIndex = 24; // IBL �g�U irradiance cube �� SRV�B
	constexpr uint32_t kRuntimeIblPrefilterSrvDescriptorIndex = 25; // IBL ���O�t�B���^�[�ς� cube �� SRV�B
	constexpr uint32_t kRuntimeIblEnvironmentSrvDescriptorIndex = 26; // IBL �� cube �� SRV�B
	constexpr uint32_t kRuntimeIblBrdfLutSrvDescriptorIndex = 27; // IBL BRDF LUT �� SRV�B
	constexpr uint32_t kRuntimeMaterialMaskSrvDescriptorIndex = 28; // Object3d �̋��ʗ� / �e���}�X�N SRV�B
	constexpr uint32_t kRuntimePlanarReflectionSrvDescriptorIndex = 29; // ���ʔ��˗p�ɕʃJ�����ŕ`���� HDR RT �� SRV�B
	constexpr uint32_t kRuntimeEnvironmentSrvDescriptorIndex = 30; // ���摜 / HDRI �� SRV�B
	constexpr uint32_t kRuntimeRtvCount = 12; // RTV Heap �T�C�Y: swap2 + HDR + bloom2 + post + SSAO2 + HDR���� + �ގ��}�X�N + ���ʔ���
	inline HINSTANCE g_instanceHandle = nullptr; // g_instanceHandle �� Win32 Window �� DirectInput �������Ɏg���A�v�����́B
	inline int g_exitCode = 0; // g_exitCode �� WinMain �֕Ԃ��I���R�[�h�B
	inline bool g_isInitialized = false; // g_isInitialized �� PlatformManager �̏�������������������\���t���O�B
	inline bool g_isInitializationFailed = false; // g_isInitializationFailed �͏��������s�Ō㑱 Manager ���~�߂邽�߂̃t���O�B
	inline bool g_isEndRequested = false; // g_isEndRequested �� WinMain �̃��C�����[�v�𔲂��邽�߂̏I���v���t���O�B
	inline bool g_isFinalized = false; // g_isFinalized �� Finalize �ς݃��\�[�X�֓�d�A�N�Z�X���Ȃ����߂̃t���O�B
	inline bool g_isDrawRequested = false; // g_isDrawRequested �� ImGui::Render ��� Renderer �� GPU �`�悵�Ă悢����\���t���O�B
	inline std::ofstream g_logStream; // g_logStream �͎��s���O�� Shader compile ���O�̏o�͐�B
	inline HWND g_windowHandle = nullptr; // g_windowHandle �� SwapChain�AImGui Win32 backend�ADirectInput �Ɏg�� HWND�B
	inline HRESULT g_hr = S_OK; // g_hr �͒��߂� HRESULT �����L���ăf�o�b�O�m�F���邽�߂̒l�B
	inline IDirectInput8* g_directInput = nullptr; // g_directInput �̓L�[�{�[�h�E�}�E�X�f�o�C�X�𐶐����� DirectInput �{�́B
	inline IDirectInputDevice8* g_keyboardDevice = nullptr; // g_keyboardDevice �� DIK_* �̉�����Ԃ�ǂޓ��̓f�o�C�X�B
	inline IDirectInputDevice8* g_mouseDevice = nullptr; // g_mouseDevice �̓}�E�X�̑��Έړ��ʁE�{�^����Ԃ�ǂޓ��̓f�o�C�X�B
	inline DIMOUSESTATE g_mouseState{}; // g_mouseState �̓}�E�X�̊e���ړ��ʂƃ{�^��������ԁB

	// g_key �͍��t���[���� 256 �L�[������ԁB
	inline BYTE g_key[256] = {};

	// g_preKey �͑O�t���[���� 256 �L�[������ԁB
	inline BYTE g_preKey[256] = {};

	// g_message �� Win32 ���b�Z�[�W���[�v�ŏ������̃��b�Z�[�W�B
	inline MSG g_message{};

	inline IXAudio2* g_xAudio2 = nullptr; // g_xAudio2 �͉����Đ��G���W���{�́B
	inline IXAudio2MasteringVoice* g_masterVoice = nullptr; // g_masterVoice �͍ŏI�o�͐�� XAudio2 Voice�B

	// g_soundData �͓ǂݍ��� wav �� PCM �o�b�t�@�ƃt�H�[�}�b�g�B
	inline SoundData g_soundData{};

	inline IXAudio2SourceVoice* g_sourceVoice = nullptr; // g_sourceVoice �� g_soundData ���Đ����� XAudio2 SourceVoice�B
	inline ComPtr<IDXGIFactory7> g_dxgiFactory; // g_dxgiFactory �� Adapter �� SwapChain ����� DXGI Factory�B
	inline ComPtr<IDXGIAdapter4> g_useAdapter; // g_useAdapter �� D3D12Device ���쐬�������� GPU�B
	inline ComPtr<ID3D12Device> g_device; // g_device �� DirectX12 ���\�[�X�����ƕ`�施�߂̒��S�B
	inline ComPtr<ID3D12CommandQueue> g_commandQueue; // g_commandQueue / Allocator / List �� GPU �֕`�施�߂𑗂邽�߂̈ꎮ�B
	inline ComPtr<ID3D12CommandAllocator> g_commandAllocator;
	inline ComPtr<ID3D12GraphicsCommandList> g_commandList;

	inline ComPtr<IDXGISwapChain4> g_swapChain; // g_swapChain �� Window �ɕ\������ back buffer ��B

	// g_swapChainDesc �� back buffer ���E�`���E�T�C�Y�̐ݒ�B
	inline DXGI_SWAP_CHAIN_DESC1 g_swapChainDesc{};

	inline ID3D12DescriptorHeap* g_rtvDescriptorHeap = nullptr;
	// g_*DescriptorHeap �� RTV / SRV / DSV ���܂Ƃ߂ĕێ����� DescriptorHeap�B
	inline ID3D12DescriptorHeap* g_srvDescriptorHeap = nullptr;
	inline ID3D12DescriptorHeap* g_dsvDescriptorHeap = nullptr;

	// g_swapChainResources �� SwapChain �� back buffer ���́B
	inline ID3D12Resource* g_swapChainResources[kRuntimeSwapChainBufferCount] = {nullptr, nullptr};

	// g_rtvDesc �� g_rtvHandles �� back buffer �� RenderTarget �Ƃ��Ďg���ݒ�ƃn���h���B
	inline D3D12_RENDER_TARGET_VIEW_DESC g_rtvDesc{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_rtvHandles[kRuntimeSwapChainBufferCount]{};

	// g_depthClearValue / g_dsvDesc / g_dsvHandle �� DepthStencil �� Clear �� View �쐬�Ɏg���B
	inline D3D12_CLEAR_VALUE g_depthClearValue{};
	inline D3D12_DEPTH_STENCIL_VIEW_DESC g_dsvDesc{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_dsvHandle{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_shadowDsvHandle{};

	inline ID3D12Resource* g_depthStencilResource = nullptr; // g_depthStencilResource �� 3D �`��̑O��֌W�𔻒肷�� Depth �o�b�t�@�B
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_depthSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_depthSrvHandleGPU{};
	inline ID3D12Resource* g_shadowMapResource = nullptr; // ���s�������猩���[�x���������މe�p DepthTexture�B
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_shadowMapSrvCpuHandle{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_shadowMapSrvGpuHandle{};

	// HDR render target (R16G16B16A16_FLOAT) ? �V�[���� HDR ��Ԃŕ`�����߂� RT
	inline ID3D12Resource* g_hdrRenderTarget = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_hdrRtvHandle{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_hdrSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_hdrSrvHandleGPU{};

	// Bloom ping-pong render targets (1/4 �𑜓x)
	inline ID3D12Resource* g_bloomRenderTargets[2] = {};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_bloomRtvHandles[2]{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_bloomSrvHandlesCPU[2]{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_bloomSrvHandlesGPU[2]{};
	inline ID3D12Resource* g_postProcessRenderTarget = nullptr;
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_postProcessRtvHandle{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_postProcessSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_postProcessSrvHandleGPU{};
	inline ID3D12Resource* g_ssaoRenderTargets[2] = {};
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

	inline uint32_t g_renderWidth = 1u; // g_renderWidth / Height �� SwapChain �� DepthStencil �̌��݃T�C�Y�B
	inline uint32_t g_renderHeight = 1u;

	inline ComPtr<IDxcUtils> g_dxcUtils; // g_dxc* �� HLSL �̓ǂݍ��݁E�R���p�C���Einclude �����Ɏg�� DXC �I�u�W�F�N�g�B
	inline ComPtr<IDxcCompiler3> g_dxcCompiler;
	inline ComPtr<IDxcIncludeHandler> g_includeHandler;

	inline ComPtr<IDxcBlob> g_vertexShaderBlob; // g_vertexShaderBlob / g_pixelShaderBlob �̓R���p�C���ς� Shader �o�C�g�R�[�h�B
	inline ComPtr<IDxcBlob> g_pixelShaderBlob;
	inline ComPtr<IDxcBlob> g_objectReflectionMaskPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_shadowVertexShaderBlob;

	// Post-process shader blobs
	inline ComPtr<IDxcBlob> g_fullscreenVertexShaderBlob;
	inline ComPtr<IDxcBlob> g_toneMappingPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_bloomExtractPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_bloomBlurPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_fxaaPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_ssaoPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_ssaoBlurPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_skyboxPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_planarReflectionPixelShaderBlob;
	inline ComPtr<IDxcBlob> g_sharpenPixelShaderBlob;


	inline ComPtr<ID3DBlob> g_signatureBlob; // g_signatureBlob / g_errorBlob �� RootSignature �V���A���C�Y���ʂƎ��s���O�B
	inline ComPtr<ID3DBlob> g_errorBlob;

	inline ComPtr<ID3D12RootSignature> g_rootSignature;
	// g_rootSignature / g_graphicsPipelineState �� Draw ���Ɏg���Œ� GPU Pipeline �ݒ�B
	inline ComPtr<ID3D12PipelineState> g_graphicsPipelineState;
	inline ComPtr<ID3D12PipelineState> g_planarScenePipelineState;
	inline ComPtr<ID3D12PipelineState> g_objectReflectionMaskPipelineState;
	inline ComPtr<ID3D12PipelineState> g_cullFrontPipelineState;
	inline ComPtr<ID3D12PipelineState> g_shadowPipelineState;

	// Post-process root signature and pipeline states
	inline ComPtr<ID3D12RootSignature> g_postProcessRootSignature;
	inline ComPtr<ID3D12PipelineState> g_toneMappingPipelineState;
	inline ComPtr<ID3D12PipelineState> g_bloomExtractPipelineState;
	inline ComPtr<ID3D12PipelineState> g_bloomBlurPipelineState;
	inline ComPtr<ID3D12PipelineState> g_fxaaPipelineState;
	inline ComPtr<ID3D12PipelineState> g_ssaoPipelineState;
	inline ComPtr<ID3D12PipelineState> g_ssaoBlurPipelineState;
	inline ComPtr<ID3D12PipelineState> g_skyboxPipelineState;
	inline ComPtr<ID3D12PipelineState> g_planarReflectionPipelineState;
	inline ComPtr<ID3D12PipelineState> g_sharpenPipelineState;
	// IBL uses existing root signature with added descriptor ranges

	inline ID3D12Resource* g_spriteMaterialResource = nullptr;
	// g_spriteMaterialResource / Data �� Sprite �`��p Material �萔�o�b�t�@�� mapped �|�C���^�B
	inline Material* g_spriteMaterialData = nullptr;

	inline ID3D12Resource* g_sphereMaterialResource = nullptr;
	// g_sphereMaterialResource / Data �� 3D ���f���`��p Material �萔�o�b�t�@�� mapped �|�C���^�B
	inline Material* g_sphereMaterialData = nullptr;

	inline ID3D12Resource* g_directionalLightResource = nullptr;
	// g_directionalLightResource / Data �� PixelShader �֓n�����s�����萔�o�b�t�@�� mapped �|�C���^�B
	inline DirectionalLight* g_directionalLightData = nullptr;

	inline ID3D12Resource* g_emissiveLightResource = nullptr;
	// g_emissiveLightResource / Data �� PixelShader �֓n�����ˌ����萔�o�b�t�@�� mapped �|�C���^�B
	inline EmissiveLightArray* g_emissiveLightData = nullptr;

	inline ID3D12Resource* g_spriteTransformationMatrixResource = nullptr;
	// g_*TransformationMatrixResource / Data �� Sprite �� 3D �v���r���[�� WVP/World �s��B
	inline TransformationMatrix* g_spriteTransformationMatrixData = nullptr;
	inline ID3D12Resource* g_sphereTransformationMatrixResource = nullptr;
	inline TransformationMatrix* g_sphereTransformationMatrixData = nullptr;

	// g_modelData �͋N�����ɓǂݍ��ފ��� OBJ ���f���B
	inline ModelData g_modelData{};
	constexpr size_t kEditorModelMeshTypeCount = static_cast<size_t>(EditorModelMeshType::Count); // ��{�`���b�V���z��̗v�f���B
	inline ModelData g_editorPrimitiveModelData[kEditorModelMeshTypeCount]{}; // ��{�`���Ƃ� CPU �����_�f�[�^�B
	inline ID3D12Resource* g_editorPrimitiveVertexResources[kEditorModelMeshTypeCount] = {}; // ��{�`���Ƃ� GPU ���_ Buffer�B
	inline D3D12_VERTEX_BUFFER_VIEW g_editorPrimitiveVertexBufferViews[kEditorModelMeshTypeCount]{};
	// Draw ���Ɏg����{�`�� BufferView�B
	inline uint32_t g_editorPrimitiveVertexCounts[kEditorModelMeshTypeCount] = {}; // DrawInstanced �ɓn����{�`�̒��_���B

	inline std::vector<VertexData> g_vertices; // g_vertices �͋����v���r���[�p�ɐ����������_�z��B

	// g_sprite / g_spriteVertices / g_spriteIndices �͋� Sprite �v���r���[�̎l�p�`�f�[�^�B
	inline Sprite g_sprite{};
	inline VertexData g_spriteVertices[4]{};
	inline uint32_t g_spriteIndices[kRuntimeSpriteIndexCount]{};

	// g_transform / g_spriteTransform �͋��v���r���[�p�Ag_cameraTransform �� SceneView �J�����p�B
	inline Transforms g_transform{};
	inline Transforms g_spriteTransform{};
	inline Transforms g_cameraTransform{};

	// g_uvTransform �� Material �� UV �s�����邽�߂� Transform�B
	inline Transforms g_uvTransform{};

	inline ID3D12Resource* g_vertexResource = nullptr; // g_*Resource �͋��v���r���[�p�� VertexBuffer / IndexBuffer ���́B
	inline ID3D12Resource* g_modelVertexResource = nullptr;
	inline ID3D12Resource* g_spriteVertexResource = nullptr;
	inline ID3D12Resource* g_spriteIndexResource = nullptr;

	// g_*BufferView �� Draw ���� IASetVertexBuffers / IASetIndexBuffer �֓n�����B
	inline D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView{};
	inline D3D12_VERTEX_BUFFER_VIEW g_modelVertexBufferView{};
	inline D3D12_VERTEX_BUFFER_VIEW g_spriteVertexBufferView{};
	inline D3D12_INDEX_BUFFER_VIEW g_spriteIndexBufferView{};

	inline float g_editorWindowWidth = 0.0f;
	// g_editorWindowWidth / Height �� ImGui �� DirectX viewport �����L���� Window �T�C�Y�B
	inline float g_editorWindowHeight = 0.0f;

	inline float g_editorLeftWidth = 250.0f; // g_editorLeft/Right/Bottom �� Docking �O�̏����p�l�����E�����B
	inline float g_editorRightWidth = 320.0f;
	inline float g_editorBottomHeight = 190.0f;

	inline float g_editorSceneX = 0.0f; // g_editorScene* �� SceneView �̍�����W�ƕ��E�����B
	inline float g_editorSceneY = 0.0f;
	inline float g_editorSceneWidth = 0.0f;
	inline float g_editorSceneHeight = 0.0f;

	inline float g_editorGameX = 0.0f; // g_editorGame* �� GameView �̍�����W�ƕ��E�����B
	inline float g_editorGameY = 0.0f;
	inline float g_editorGameWidth = 0.0f;
	inline float g_editorGameHeight = 0.0f;

	inline bool g_isSceneViewVisible = false; // g_isSceneViewVisible �� SceneView �֕`�悷���`�����t���[���L�����ǂ����B
	inline bool g_isGameViewVisible = false; // g_isGameViewVisible �� GameView �֕`�悷���`���L�����ǂ����B
	inline bool g_isGameViewUsingSceneCamera = true; // true �Ȃ� Camera Component ���Ȃ����� Scene �J�������p���Ă���B

	// g_viewport / g_scissorRect �� DirectX �� SceneView �������֕`�����߂̋�`�B
	inline D3D12_VIEWPORT g_viewport{};
	inline D3D12_RECT g_scissorRect{};

	// g_cameraMatrix / g_viewMatrix / g_projectionMatrix �� SceneView 3D �\���p�s��B
	inline Matrix4x4 g_cameraMatrix{};
	inline Matrix4x4 g_viewMatrix{};
	inline Matrix4x4 g_projectionMatrix{};

	// g_game*Matrix �� GameView �� Camera Component �o�͂Ɏg���s��B
	inline Matrix4x4 g_gameCameraMatrix{};
	inline Matrix4x4 g_gameViewMatrix{};
	inline Matrix4x4 g_gameProjectionMatrix{};
	inline Vector3 g_gameCameraPosition{};  // GameView �� PBR ���ˌv�Z�Ɏg�� Camera Component �̃��[���h�ʒu�B

	// g_spriteProjectionMatrix �� Sprite ����ʍ��W�ŕ\�����邽�߂̐��ˉe�s��B
	inline Matrix4x4 g_spriteProjectionMatrix{};

	inline float g_editorCameraMoveSpeed = 0.12f; // g_editorCamera*Speed �� SceneView �J��������̑��x�ݒ�B
	inline float g_editorCameraRotateSpeed = 0.006f;
	inline float g_editorCameraWheelMoveSpeed = 0.5f;
	inline float g_editorCameraPanSpeed = 0.01f;
	inline float g_editorCameraFastRate = 1000.0f;

	// g_sceneClearColor �� SceneView �w�i�F RGBA�B
	inline float g_sceneClearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};

	inline bool g_isSceneGizmoVisible = true; // g_is*GizmoVisible �� SceneView �⏕�\���̕\���t���O�B
	inline bool g_isLightGizmoVisible = false;
	inline bool g_isCameraGizmoVisible = false;

	// g_directionalLightIconPosition �̓��C�g�A�C�R���̃��[���h���W�B
	inline Vector3 g_directionalLightIconPosition = {-1.8f, 1.4f, 0.0f};

	inline EditorSceneObjectManager g_editorSceneObjectManager;
	// g_editorSceneObjectManager �� GameObject �ƑΉ����� DirectX �`��p SceneObject �����B
	inline int32_t g_selectedPlacedSceneObjectIndex = -1;
	// g_selectedPlacedSceneObjectIndex �͑I�� SceneObject �̔z�� index�B-1 �͖��I���B
	inline ComPtr<ID3D12Fence> g_fence; // g_fence / g_fenceValue / g_fenceEvent �� CPU �� GPU �̓����Ɏg���B
	inline uint64_t g_fenceValue = 0;
	inline HANDLE g_fenceEvent = nullptr;

	inline std::wstring g_textureFilePaths[kRuntimeTextureCount];
	// g_textureFilePaths �� GPU �ɓǂݍ��ޕW�� Texture �� UTF-16 �p�X�B
	inline std::string g_textureFilePathStrings[kRuntimeTextureCount];
	// g_textureFilePathStrings / g_editorTextureFilePaths �� Project �p�l���ň��� UTF-8 �p�X�B
	inline std::vector<std::string> g_editorTextureFilePaths;

	// g_textureMetadatas / Resources �� Texture �̏��� GPU ���́B
	inline DirectX::TexMetadata g_textureMetadatas[kRuntimeTextureCount]{};
	inline ID3D12Resource* g_textureResources[kRuntimeTextureCount] = {nullptr, nullptr, nullptr, nullptr};

	// g_intermediateResources �� Texture upload �p�̈ꎞ Buffer�B
	inline ID3D12Resource* g_intermediateResources[kRuntimeTextureCount] = {nullptr, nullptr, nullptr, nullptr};

	inline UINT g_srvDescriptorSize = 0; // g_srvDescriptorSize �� SRV Heap ���Ŏ��� Descriptor �֐i�ޕ��B

	// g_textureSrvHandlesCPU/GPU �� Texture SRV �� CPU �쐬�p�EGPU �`��p�n���h���B
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
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_iblIrradianceSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_iblIrradianceSrvHandleGPU{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_iblPrefilterSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_iblPrefilterSrvHandleGPU{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_iblEnvironmentSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_iblEnvironmentSrvHandleGPU{};
	inline D3D12_CPU_DESCRIPTOR_HANDLE g_iblBrdfLutSrvHandleCPU{};
	inline D3D12_GPU_DESCRIPTOR_HANDLE g_iblBrdfLutSrvHandleGPU{};
	inline uint32_t g_iblPrefilterMipCount = 0;

	// g_editorRuntimeManager �� Play ���� Input / Physics ���X�V���� Runtime�B
	inline EditorRuntimeManager g_editorRuntimeManager{};

	// g_feelKitHaptics �� XInput �R���g���[���[�̐U�����Ǘ����� FeelKitHaptics �C���X�^���X�B
	inline FeelKitHaptics g_feelKitHaptics{};

	// g_editorSceneCameraController �� SceneView �̃L�[�{�[�h�E�}�E�X�J��������������B
	inline EditorSceneCameraController g_editorSceneCameraController{};

	inline int g_selectedSceneObject = 0; // g_selectedSceneObject �͋��v���r���[�I��ԍ��B0:Model�A1:Sprite�A2:Light�A3:Camera�B
	inline int g_activeEditorTool = 1; // g_activeEditorTool �� Scene �M�Y�������ʁB1:�ړ��A2:��]�A3:�g�k�A4:�����B
	inline int g_editorViewportTabIndex = 0; // g_editorViewportTabIndex �� SceneView �̃^�u�ԍ��B0:Scene�A1:Game�A2:AssetStore�B
	inline std::string g_selectedAssetPath; // g_selectedAssetPath �� Project �p�l���őI�𒆂̃A�Z�b�g�p�X�B
	inline std::string g_currentScenePath; // g_currentScenePath �͍��J���Ă��� .scene �̕ۑ���B���ۑ��V�[���ł͋󕶎��B

	// g_hierarchyFilter / g_assetFilter �͊e�������̓��̓o�b�t�@�B
	inline char g_hierarchyFilter[128] = {};
	inline char g_assetFilter[128] = {};

	inline bool g_isConsoleCleared = false; // g_isConsoleCleared �� Console �N���A�{�^�������𔽉f����t���O�B
	inline bool g_isSceneRangeSelecting = false; // g_isSceneRangeSelecting �� SceneView �̋�`�I���h���b�O���t���O�B
	inline bool g_isSceneMiddleCameraDragging = false; // g_isScene*CameraDragging �͒��{�^���ړ��E�E�{�^����]�̃h���b�O��ԁB
	inline bool g_isSceneRightCameraDragging = false;

	inline bool g_isGizmoLocalMode = true; // g_isGizmoLocalMode �̓M�Y���� Local / World �ǂ���̎��œ��������̃t���O�B
	inline bool g_isGizmoSnapEnabled = false; // g_isGizmoSnapEnabled �̓M�Y���X�i�b�v���g�����ǂ����̃t���O�B
	inline bool g_isSceneAssistVisible = true; // g_isSceneAssistVisible �� SceneView ��������̕\���t���O�B
	inline bool g_isLegacyPreviewVisible = false; // g_isLegacyPreviewVisible �͋��v���r���[ Model/Sprite ��\�����邩�̃t���O�B

	// g_gizmoSnapValues �� X/Y/Z �e���̃X�i�b�v�ʁB
	inline float g_gizmoSnapValues[3] = {0.5f, 0.5f, 0.5f};

	inline auto g_sceneRangeStart = ImVec2(0.0f, 0.0f); // g_sceneRangeStart / End �� SceneView ��`�I���̊J�n�_�ƌ��ݓ_�B
	inline auto g_sceneRangeEnd = ImVec2(0.0f, 0.0f);

	inline EditorScene g_editorScene; // g_editorScene �� GameObject / Component / Prefab ��ێ�����ҏW Scene�B
	inline bool g_isEditorSceneInitialized = false;
	// g_isEditorSceneInitialized / RuntimeInitialized �� Scene �� Play Runtime �̏������ς݃t���O�B
	inline bool g_isEditorRuntimeInitialized = false;

	inline int32_t g_selectedEditorGameObjectId = -1;
	// g_selectedEditorGameObjectId �� Inspector / Hierarchy �őI�𒆂� GameObject ID�B
	inline std::vector<int32_t> g_selectedEditorGameObjectIds;
	// g_selectedEditorGameObjectIds �͔͈͑I���╡���ҏW�Ŏg���I�� GameObject ID �ꗗ�B
	inline int32_t g_previousSelectedEditorGameObjectId = -1;
	// g_previousSelectedEditorGameObjectId �͑I��ύX���o�p�̑O�� GameObject ID�B

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

	// g_selectedGameObjectName �� Inspector �̖��O�ҏW�p char �o�b�t�@�B
	inline char g_selectedGameObjectName[128] = {};

	inline int32_t g_selectedAddComponentIndex = 0; // g_selectedAddComponentIndex �� Inspector �̒ǉ� Component �R���{�I��ԍ��B

	// g_editorConsoleMessages �� Console �p�l���֕\�����郍�O������ꗗ�B
	inline std::vector<std::string> g_editorConsoleMessages = {
		"Editor: Ready",
		"Scene: Empty startup",
		"Runtime: Play physics waits for Play button",
	};

	inline EditorSelectionManager g_editorSelectionManager;
	// g_editorSelectionManager �� GameObject �I���� SceneObject �I���𓯊�����B
	inline EditorSceneSynchronizer g_editorSceneSynchronizer;
	// g_editorSceneSynchronizer �� EditorScene ����`��p SceneObject �����B
	inline EditorAssetFactory g_editorAssetFactory; // g_editorAssetFactory �� Project ����̔z�u�� GameObject �� Component �����B
	inline EditorMainMenuBar g_editorMainMenuBar; // g_editorMainMenuBar �͏㕔���j���[�� Play/Stop �{�^����`�悷��B
	inline EditorHierarchyPanel g_editorHierarchyPanel; // g_editorHierarchyPanel �� GameObject �c���[�Ɛe�q�t����`��E��������B
	inline EditorInspectorPanel g_editorInspectorPanel; // g_editorInspectorPanel �͑I��Ώۂ� Transform �� Component ��ҏW����B
	inline EditorBottomPanel g_editorBottomPanel; // g_editorBottomPanel �� Project �A�Z�b�g�ꗗ�� Console ��\������B
	inline bool g_isEditorManagerInitialized = false; // g_isEditorManagerInitialized �� Panel / Manager �̎Q�Ɛݒ肪�ς񂾂���\���B
	inline bool g_isDockLayoutInitialized = false; // g_isDockLayoutInitialized �� DockBuilder �����z�u����x�������s���邽�߂̃t���O�B

	inline ID3D12Resource* CreateRuntimeDepthStencilResource(uint32_t width, uint32_t height) {
		// heapProperties �� DepthStencil �� GPU ��p�������ɒu�����߂̎w��B
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		// resourceDesc �� width / height �ɍ��킹�� 2D DepthStencil Texture �̐ݒ�B
		D3D12_RESOURCE_DESC resourceDesc{};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDesc.Width = width;
		resourceDesc.Height = height;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		ID3D12Resource* resource = nullptr; // resource �� CreateCommittedResource ���쐬���ĕԂ� DepthStencil ���́B

		// createResult �� DepthStencil �쐬 API �̐��ہB
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
		g_fenceValue++; // g_fenceValue ��i�߂āA���̃t���[���ő҂� GPU �����ԍ������B
		HRESULT signalResult = g_commandQueue->Signal(g_fence.Get(), g_fenceValue);
		// Signal �� CommandQueue �ցA�����܂ł� GPU ��Ɣԍ���o�^����B
		assert(SUCCEEDED(signalResult));

		// GPU ���܂� g_fenceValue �܂ŏI����Ă��Ȃ���΁AEvent ���g���� CPU ��҂�����B
		if (g_fence->GetCompletedValue() < g_fenceValue) {
			signalResult = g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
			assert(SUCCEEDED(signalResult));
			WaitForSingleObject(g_fenceEvent, INFINITE);
		}
	}

	inline void UpdateEditorLayout() {
		constexpr float editorMenuHeight = 20.0f; // editorMenuHeight �͏㕔���j���[�AeditorSceneHeaderHeight �� SceneView �^�u�̍����B
		constexpr float editorSceneHeaderHeight = 24.0f;

		g_editorLeftWidth = (std::clamp)(g_editorLeftWidth, 160.0f, 420.0f);
		// �e�p�l�����E�����𑀍�\�Ȕ͈͂ɐ������ASceneView ���ׂ�Ȃ��悤�ɂ���B
		g_editorRightWidth = (std::clamp)(g_editorRightWidth, 220.0f, 520.0f);
		g_editorBottomHeight = (std::clamp)(g_editorBottomHeight, 120.0f, 320.0f);

		g_editorSceneX = g_editorLeftWidth; // SceneView ����͍��p�l�����ƃ��j���[�E�^�u�������猈�߂�B
		g_editorSceneY = editorMenuHeight + editorSceneHeaderHeight;

		g_editorSceneWidth = g_editorWindowWidth - g_editorLeftWidth - g_editorRightWidth;
		// SceneView ���� Window �����獶�E�E�p�l�����������c��B
		g_editorSceneHeight = g_editorWindowHeight - g_editorSceneY - g_editorBottomHeight;
		// SceneView ������ Window ��������㕔�̈�Ɖ����p�l�����������c��B
		g_editorSceneWidth = (std::max)(g_editorSceneWidth, 240.0f); // DirectX viewport �� 0 �T�C�Y������邽�߁A�Œᕝ�E�������m�ۂ���B
		g_editorSceneHeight = (std::max)(g_editorSceneHeight, 180.0f);
	}

	inline void ResizeRenderTargets(uint32_t width, uint32_t height) {
		// �T�C�Y���ς���Ă��Ȃ��ꍇ�� SwapChain �� DepthStencil ����蒼���Ȃ��B
		if (width == g_renderWidth && height == g_renderHeight) {
			return;
		}

		WaitForGpu(); // �Â� back buffer �� GPU ���g���I���܂ő҂��Ă��� Release ����B

		for (ID3D12Resource*& swapChainResource : g_swapChainResources) {
			// ResizeBuffers �O�ɑS back buffer �Q�Ƃ��������B
			if (swapChainResource != nullptr) {
				swapChainResource->Release();
				swapChainResource = nullptr;
			}
		}

		// DepthStencil ���`��T�C�Y�Ɉˑ����邽�ߌÂ����̂��������B
		if (g_depthStencilResource != nullptr) {
			g_depthStencilResource->Release();
			g_depthStencilResource = nullptr;
		}

		g_renderWidth = width; // g_renderWidth / Height �͐V������� back buffer �T�C�Y�B
		g_renderHeight = height;

		// SwapChain �� back buffer ��V�����T�C�Y�ōĊm�ۂ���B
		HRESULT resizeResult = g_swapChain->ResizeBuffers(
			2,
			g_renderWidth,
			g_renderHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			0);
		assert(SUCCEEDED(resizeResult));

		for (uint32_t bufferIndex = 0; bufferIndex < kRuntimeSwapChainBufferCount; bufferIndex++) {
			resizeResult = g_swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&g_swapChainResources[bufferIndex]));
			// Resize ��� back buffer ���擾���� RTV ���č쐬����B
			assert(SUCCEEDED(resizeResult));
			g_device->CreateRenderTargetView(
				g_swapChainResources[bufferIndex],
				&g_rtvDesc,
				g_rtvHandles[bufferIndex]);
		}

		g_depthStencilResource = CreateRuntimeDepthStencilResource(g_renderWidth, g_renderHeight);
		// DepthStencil ���V�����`��T�C�Y�ɍ��킹�čč쐬����B
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
		}

		// HDR RT �� Bloom RTs ���č쐬����
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
			assert(SUCCEEDED(hr));
			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = format;
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
			g_device->CreateRenderTargetView(resource, &rtvDesc, rtvHandle);
		};

		// HDR RT (index 2)
		recreateRenderTarget(g_hdrRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 2));

		// Bloom RTs (indices 3, 4) ? 1/4 �𑜓x
		uint32_t bloomWidth = (std::max)(1u, g_renderWidth / 4);
		uint32_t bloomHeight = (std::max)(1u, g_renderHeight / 4);
		for (uint32_t i = 0; i < 2; i++) {
			recreateRenderTarget(g_bloomRenderTargets[i], bloomWidth, bloomHeight,
				DXGI_FORMAT_R16G16B16A16_FLOAT,
				GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 3u + i));
		}

		// ToneMap ��� LDR RT (index 5) �� FXAA ���ǂނ��߁A��ʃT�C�Y�Ɠ����傫���ō��B
		recreateRenderTarget(g_postProcessRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 5u));

		for (uint32_t i = 0; i < 2; i++) {
			recreateRenderTarget(g_ssaoRenderTargets[i], g_renderWidth, g_renderHeight,
				DXGI_FORMAT_R8_UNORM,
				GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 6u + i));
		}

		recreateRenderTarget(g_hdrCompositeRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 8u));


		recreateRenderTarget(g_materialMaskRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 10u));

		recreateRenderTarget(g_planarReflectionRenderTarget, g_renderWidth, g_renderHeight,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 11u));

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

		// ToneMap �ς݂� LDR RT �� FXAA �œǂނ��߂� SRV�B
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
			g_hdrCompositeRtvHandle = GetCPUDescriptorHandle(g_rtvDescriptorHeap, rtvSize, 8u);
			g_hdrCompositeSrvHandleCPU = GetCPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeHdrCompositeSrvDescriptorIndex);
			g_hdrCompositeSrvHandleGPU = GetGPUDescriptorHandle(g_srvDescriptorHeap, g_srvDescriptorSize, kRuntimeHdrCompositeSrvDescriptorIndex);
			g_device->CreateShaderResourceView(g_hdrCompositeRenderTarget, &srvDesc, g_hdrCompositeSrvHandleCPU);
		}


		// �ގ��}�X�N SRV �� SSR �������ɋ��ʗ��Ƒe�������邽�߂Ɏg���B
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

		// ���ʔ��� SRV �͔��˃J�����ŕ`�������ʂ����ʂ֓\�鎞�Ɏg���B
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
	}
}

#pragma warning(pop)
