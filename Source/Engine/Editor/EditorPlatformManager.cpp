#pragma warning(disable : 4189 4514)

#include "EditorPlatformManager.h"
#include <onnxruntime_cxx_api.h>
#include <xaudio2.h>
#include <xaudio2fx.h>
#include "EditorSharedState.h"
using namespace EditorSharedState;

namespace {
	//================================================================
	// 初期化失敗時の終亁E��汁E
	//================================================================

	void RequestInitializationFailure() {
		g_isInitializationFailed = true; // g_isInitializationFailed は GameScene が後綁EManager 初期化を止めるためのフラグ、E
		g_isEndRequested = true; // g_isEndRequested は WinMain のループへ入らなぁE��ぁE��する終亁E��求フラグ、E
		g_exitCode = 1; // 1 は初期化失敗を表す終亁E��ード、E
	}

	VertexData MakePrimitiveVertex(float x, float y, float z, float u, float v, const Vector3& normal) {
		// 基本形メチE��ュの 1 頂点を作る。position はローカル座標、texcoord は checker 表示用、E
		return VertexData{
			{x, y, z, 1.0f},
			{u, v},
			normal
		};
	}

	void AddPrimitiveTriangle(
		std::vector<VertexData>& vertices,
		const VertexData& a,
		const VertexData& b,
		const VertexData& c) {
		// DrawInstanced は triangle list なので、三角形単位で頂点を追加する、E
		vertices.push_back(a);
		vertices.push_back(b);
		vertices.push_back(c);
	}

	void AddPrimitiveQuad(
		std::vector<VertexData>& vertices,
		const VertexData& a,
		const VertexData& b,
		const VertexData& c,
		const VertexData& d) {
		// 四角形は 2 枚�E三角形に刁E��て追加する、E
		AddPrimitiveTriangle(vertices, a, b, c);
		AddPrimitiveTriangle(vertices, c, b, d);
	}

	std::vector<VertexData> CreateBoxVertices(const Vector3& halfSize) {
		std::vector<VertexData> vertices; // vertices は Box 6 面刁E�E三角形頂点、E
		vertices.reserve(36);

		const float x = halfSize.x;
		const float y = halfSize.y;
		const float z = halfSize.z;

		AddPrimitiveQuad(
			vertices,
			MakePrimitiveVertex(-x, -y, -z, 0.0f, 1.0f, {0.0f, 0.0f, -1.0f}),
			MakePrimitiveVertex(-x, y, -z, 0.0f, 0.0f, {0.0f, 0.0f, -1.0f}),
			MakePrimitiveVertex(x, -y, -z, 1.0f, 1.0f, {0.0f, 0.0f, -1.0f}),
			MakePrimitiveVertex(x, y, -z, 1.0f, 0.0f, {0.0f, 0.0f, -1.0f}));
		AddPrimitiveQuad(
			vertices,
			MakePrimitiveVertex(x, -y, z, 0.0f, 1.0f, {0.0f, 0.0f, 1.0f}),
			MakePrimitiveVertex(x, y, z, 0.0f, 0.0f, {0.0f, 0.0f, 1.0f}),
			MakePrimitiveVertex(-x, -y, z, 1.0f, 1.0f, {0.0f, 0.0f, 1.0f}),
			MakePrimitiveVertex(-x, y, z, 1.0f, 0.0f, {0.0f, 0.0f, 1.0f}));
		AddPrimitiveQuad(
			vertices,
			MakePrimitiveVertex(-x, -y, z, 0.0f, 1.0f, {-1.0f, 0.0f, 0.0f}),
			MakePrimitiveVertex(-x, y, z, 0.0f, 0.0f, {-1.0f, 0.0f, 0.0f}),
			MakePrimitiveVertex(-x, -y, -z, 1.0f, 1.0f, {-1.0f, 0.0f, 0.0f}),
			MakePrimitiveVertex(-x, y, -z, 1.0f, 0.0f, {-1.0f, 0.0f, 0.0f}));
		AddPrimitiveQuad(
			vertices,
			MakePrimitiveVertex(x, -y, -z, 0.0f, 1.0f, {1.0f, 0.0f, 0.0f}),
			MakePrimitiveVertex(x, y, -z, 0.0f, 0.0f, {1.0f, 0.0f, 0.0f}),
			MakePrimitiveVertex(x, -y, z, 1.0f, 1.0f, {1.0f, 0.0f, 0.0f}),
			MakePrimitiveVertex(x, y, z, 1.0f, 0.0f, {1.0f, 0.0f, 0.0f}));
		AddPrimitiveQuad(
			vertices,
			MakePrimitiveVertex(-x, y, -z, 0.0f, 1.0f, {0.0f, 1.0f, 0.0f}),
			MakePrimitiveVertex(-x, y, z, 0.0f, 0.0f, {0.0f, 1.0f, 0.0f}),
			MakePrimitiveVertex(x, y, -z, 1.0f, 1.0f, {0.0f, 1.0f, 0.0f}),
			MakePrimitiveVertex(x, y, z, 1.0f, 0.0f, {0.0f, 1.0f, 0.0f}));
		AddPrimitiveQuad(
			vertices,
			MakePrimitiveVertex(-x, -y, z, 0.0f, 1.0f, {0.0f, -1.0f, 0.0f}),
			MakePrimitiveVertex(-x, -y, -z, 0.0f, 0.0f, {0.0f, -1.0f, 0.0f}),
			MakePrimitiveVertex(x, -y, z, 1.0f, 1.0f, {0.0f, -1.0f, 0.0f}),
			MakePrimitiveVertex(x, -y, -z, 1.0f, 0.0f, {0.0f, -1.0f, 0.0f}));

		return vertices;
	}

	std::vector<VertexData> CreateCylinderVertices(uint32_t segmentCount) {
		std::vector<VertexData> vertices; // vertices は側面、上面、下面を持つ冁E��メチE��ュ、E
		vertices.reserve(static_cast<size_t>(segmentCount) * 12u);

		for (uint32_t segmentIndex = 0; segmentIndex < segmentCount; segmentIndex++) {
			float rate0 = static_cast<float>(segmentIndex) / static_cast<float>(segmentCount);
			float rate1 = static_cast<float>(segmentIndex + 1u) / static_cast<float>(segmentCount);
			float angle0 = rate0 * std::numbers::pi_v<float> * 2.0f;
			float angle1 = rate1 * std::numbers::pi_v<float> * 2.0f;
			float x0 = std::cos(angle0) * 0.5f;
			float z0 = std::sin(angle0) * 0.5f;
			float x1 = std::cos(angle1) * 0.5f;
			float z1 = std::sin(angle1) * 0.5f;
			Vector3 normal0 = Normalize(Vector3{x0, 0.0f, z0});
			Vector3 normal1 = Normalize(Vector3{x1, 0.0f, z1});

			AddPrimitiveQuad(
				vertices,
				MakePrimitiveVertex(x0, -0.5f, z0, rate0, 1.0f, normal0),
				MakePrimitiveVertex(x0, 0.5f, z0, rate0, 0.0f, normal0),
				MakePrimitiveVertex(x1, -0.5f, z1, rate1, 1.0f, normal1),
				MakePrimitiveVertex(x1, 0.5f, z1, rate1, 0.0f, normal1));
			AddPrimitiveTriangle(
				vertices,
				MakePrimitiveVertex(0.0f, 0.5f, 0.0f, 0.5f, 0.5f, {0.0f, 1.0f, 0.0f}),
				MakePrimitiveVertex(x0, 0.5f, z0, rate0, 0.0f, {0.0f, 1.0f, 0.0f}),
				MakePrimitiveVertex(x1, 0.5f, z1, rate1, 0.0f, {0.0f, 1.0f, 0.0f}));
			AddPrimitiveTriangle(
				vertices,
				MakePrimitiveVertex(0.0f, -0.5f, 0.0f, 0.5f, 0.5f, {0.0f, -1.0f, 0.0f}),
				MakePrimitiveVertex(x1, -0.5f, z1, rate1, 1.0f, {0.0f, -1.0f, 0.0f}),
				MakePrimitiveVertex(x0, -0.5f, z0, rate0, 1.0f, {0.0f, -1.0f, 0.0f}));
		}

		return vertices;
	}

	std::vector<VertexData> CreateConeVertices(uint32_t segmentCount) {
		std::vector<VertexData> vertices; // vertices は側面と底面を持つ冁E��メチE��ュ、E
		vertices.reserve(static_cast<size_t>(segmentCount) * 6u);

		for (uint32_t segmentIndex = 0; segmentIndex < segmentCount; segmentIndex++) {
			float rate0 = static_cast<float>(segmentIndex) / static_cast<float>(segmentCount);
			float rate1 = static_cast<float>(segmentIndex + 1u) / static_cast<float>(segmentCount);
			float angle0 = rate0 * std::numbers::pi_v<float> * 2.0f;
			float angle1 = rate1 * std::numbers::pi_v<float> * 2.0f;
			float x0 = std::cos(angle0) * 0.5f;
			float z0 = std::sin(angle0) * 0.5f;
			float x1 = std::cos(angle1) * 0.5f;
			float z1 = std::sin(angle1) * 0.5f;
			Vector3 sideNormal = Normalize(Cross(
				Vector3{x1 - x0, 0.0f, z1 - z0},
				Vector3{-x0, 1.0f, -z0}));

			AddPrimitiveTriangle(
				vertices,
				MakePrimitiveVertex(0.0f, 0.5f, 0.0f, 0.5f, 0.0f, sideNormal),
				MakePrimitiveVertex(x0, -0.5f, z0, rate0, 1.0f, sideNormal),
				MakePrimitiveVertex(x1, -0.5f, z1, rate1, 1.0f, sideNormal));
			AddPrimitiveTriangle(
				vertices,
				MakePrimitiveVertex(0.0f, -0.5f, 0.0f, 0.5f, 0.5f, {0.0f, -1.0f, 0.0f}),
				MakePrimitiveVertex(x1, -0.5f, z1, rate1, 1.0f, {0.0f, -1.0f, 0.0f}),
				MakePrimitiveVertex(x0, -0.5f, z0, rate0, 1.0f, {0.0f, -1.0f, 0.0f}));
		}

		return vertices;
	}

	std::vector<VertexData> CreateTorusVertices(uint32_t majorSegmentCount, uint32_t minorSegmentCount) {
		std::vector<VertexData> vertices; // vertices はド�Eナツ形状のト�EラスメチE��ュ、E
		vertices.reserve(static_cast<size_t>(majorSegmentCount) * static_cast<size_t>(minorSegmentCount) * 6u);

		for (uint32_t majorIndex = 0; majorIndex < majorSegmentCount; majorIndex++) {
			float majorRate0 = static_cast<float>(majorIndex) / static_cast<float>(majorSegmentCount);
			float majorRate1 = static_cast<float>(majorIndex + 1u) / static_cast<float>(majorSegmentCount);
			float majorAngle0 = majorRate0 * std::numbers::pi_v<float> * 2.0f;
			float majorAngle1 = majorRate1 * std::numbers::pi_v<float> * 2.0f;

			for (uint32_t minorIndex = 0; minorIndex < minorSegmentCount; minorIndex++) {
				float minorRate0 = static_cast<float>(minorIndex) / static_cast<float>(minorSegmentCount);
				float minorRate1 = static_cast<float>(minorIndex + 1u) / static_cast<float>(minorSegmentCount);
				float minorAngle0 = minorRate0 * std::numbers::pi_v<float> * 2.0f;
				float minorAngle1 = minorRate1 * std::numbers::pi_v<float> * 2.0f;
				constexpr float majorRadius = 0.38f;
				constexpr float minorRadius = 0.14f;

				auto makeTorusVertex = [&](float majorAngle, float minorAngle, float u, float v) {
					float ringRadius = majorRadius + minorRadius * std::cos(minorAngle);
					Vector3 normal = Normalize(Vector3{
						std::cos(majorAngle) * std::cos(minorAngle),
						std::sin(minorAngle),
						std::sin(majorAngle) * std::cos(minorAngle)
					});

					return MakePrimitiveVertex(
						std::cos(majorAngle) * ringRadius,
						minorRadius * std::sin(minorAngle),
						std::sin(majorAngle) * ringRadius,
						u,
						v,
						normal);
				};

				AddPrimitiveQuad(
					vertices,
					makeTorusVertex(majorAngle0, minorAngle0, majorRate0, minorRate0),
					makeTorusVertex(majorAngle0, minorAngle1, majorRate0, minorRate1),
					makeTorusVertex(majorAngle1, minorAngle0, majorRate1, minorRate0),
					makeTorusVertex(majorAngle1, minorAngle1, majorRate1, minorRate1));
			}
		}

		return vertices;
	}

	std::vector<VertexData> CreateIcoVertices() {
		std::vector<VertexData> vertices; // vertices は低�Eリゴン基本形として使ぁE�E面体メチE��ュ、E
		vertices.reserve(24);
		constexpr Vector3 top{0.0f, 0.6f, 0.0f};
		constexpr Vector3 bottom{0.0f, -0.6f, 0.0f};
		constexpr Vector3 front{0.0f, 0.0f, -0.6f};
		constexpr Vector3 right{0.6f, 0.0f, 0.0f};
		constexpr Vector3 back{0.0f, 0.0f, 0.6f};
		constexpr Vector3 left{-0.6f, 0.0f, 0.0f};

		auto addFace = [&](const Vector3& a, const Vector3& b, const Vector3& c) {
			Vector3 normal = Normalize(Cross(Subtract(b, a), Subtract(c, a)));
			// 面法線を計算してフラチE��シェーチE��ングにする、E
			AddPrimitiveTriangle(
				vertices,
				MakePrimitiveVertex(a.x, a.y, a.z, 0.5f, 0.0f, normal),
				MakePrimitiveVertex(b.x, b.y, b.z, 0.0f, 1.0f, normal),
				MakePrimitiveVertex(c.x, c.y, c.z, 1.0f, 1.0f, normal));
		};

		addFace(top, front, right);
		addFace(top, right, back);
		addFace(top, back, left);
		addFace(top, left, front);
		addFace(bottom, right, front);
		addFace(bottom, back, right);
		addFace(bottom, left, back);
		addFace(bottom, front, left);

		return vertices;
	}

	std::vector<VertexData> CreateSphereVertices(uint32_t subdivision, float radius) {
		std::vector<VertexData> vertices; // vertices は緯度経度で作る琁E�E三角形頂点、E
		vertices.reserve(static_cast<size_t>(subdivision) * static_cast<size_t>(subdivision) * 6u);

		const float lonEvery = 2.0f * std::numbers::pi_v<float> / static_cast<float>(subdivision);
		const float latEvery = std::numbers::pi_v<float> / static_cast<float>(subdivision);

		for (uint32_t latIndex = 0; latIndex < subdivision; ++latIndex) {
			float lat = -std::numbers::pi_v<float> / 2.0f + latEvery * static_cast<float>(latIndex);
			float latNext = lat + latEvery;

			for (uint32_t lonIndex = 0; lonIndex < subdivision; ++lonIndex) {
				float lon = lonEvery * static_cast<float>(lonIndex) + std::numbers::pi_v<float>;
				float lonNext = lon + lonEvery;

				float u0 = static_cast<float>(lonIndex) / static_cast<float>(subdivision);
				float u1 = static_cast<float>(lonIndex + 1u) / static_cast<float>(subdivision);
				float v0 = 1.0f - static_cast<float>(latIndex) / static_cast<float>(subdivision);
				float v1 = 1.0f - static_cast<float>(latIndex + 1u) / static_cast<float>(subdivision);

				auto makeSphereVertex = [radius](float latAngle, float lonAngle, float u, float v) {
					Vector3 normal = Normalize(Vector3{
						std::cos(latAngle) * std::cos(lonAngle),
						std::sin(latAngle),
						std::cos(latAngle) * std::sin(lonAngle)
					});

					return MakePrimitiveVertex(
						normal.x * radius,
						normal.y * radius,
						normal.z * radius,
						u,
						v,
						normal);
				};

				VertexData a = makeSphereVertex(lat, lon, u0, v0);
				VertexData b = makeSphereVertex(latNext, lon, u0, v1);
				VertexData c = makeSphereVertex(lat, lonNext, u1, v0);
				VertexData d = makeSphereVertex(latNext, lonNext, u1, v1);

				AddPrimitiveQuad(vertices, a, b, c, d);
			}
		}

		return vertices;
	}

	ModelData CreatePrimitiveModelData(EditorModelMeshType meshType, const ModelData& fallbackPlaneModelData) {
		ModelData modelData{}; // modelData は基本形ごとの頂点配�Eを�Eれる戻り値、E

		switch (meshType) {
		case EditorModelMeshType::Plane:
			return fallbackPlaneModelData;
		case EditorModelMeshType::Cube:
			modelData.vertices = CreateBoxVertices(Vector3{0.5f, 0.5f, 0.5f});
			break;
		case EditorModelMeshType::Box:
			modelData.vertices = CreateBoxVertices(Vector3{0.8f, 0.35f, 0.5f});
			break;
		case EditorModelMeshType::Cylinder:
			modelData.vertices = CreateCylinderVertices(32u);
			break;
		case EditorModelMeshType::Cone:
			modelData.vertices = CreateConeVertices(32u);
			break;
		case EditorModelMeshType::Torus:
			modelData.vertices = CreateTorusVertices(32u, 12u);
			break;
		case EditorModelMeshType::Ico:
			modelData.vertices = CreateIcoVertices();
			break;
		case EditorModelMeshType::Sphere:
			modelData.vertices = CreateSphereVertices(32u, 0.5f);
			break;
		case EditorModelMeshType::Count:
		default:
			return fallbackPlaneModelData;
		}

		modelData.material = fallbackPlaneModelData.material;
		return modelData;
	}

	void CreatePrimitiveMeshBuffers(
		ID3D12Device* device,
		const ModelData& fallbackPlaneModelData,
		ModelData* primitiveModelData,
		ID3D12Resource** primitiveVertexResources,
		D3D12_VERTEX_BUFFER_VIEW* primitiveVertexBufferViews,
		uint32_t* primitiveVertexCounts) {
		for (size_t meshTypeIndex = 0; meshTypeIndex < kEditorModelMeshTypeCount; meshTypeIndex++) {
			auto meshType = static_cast<EditorModelMeshType>(meshTypeIndex); // 配�E番号を基本形 enum として扱ぁE��E
			primitiveModelData[meshTypeIndex] = CreatePrimitiveModelData(meshType, fallbackPlaneModelData);
			primitiveVertexCounts[meshTypeIndex] =
				static_cast<uint32_t>(primitiveModelData[meshTypeIndex].vertices.size());

			if (primitiveVertexCounts[meshTypeIndex] == 0u) {
				continue;
			}

			size_t bufferSize = sizeof(VertexData) * primitiveModelData[meshTypeIndex].vertices.size();
			// 頂点配�Eを丸ごと UploadBuffer に置く、E
			primitiveVertexResources[meshTypeIndex] = CreateBufferResource(device, bufferSize);

			VertexData* mappedVertexData = nullptr;
			HRESULT mapResult = primitiveVertexResources[meshTypeIndex]->Map(
				0,
				nullptr,
				reinterpret_cast<void**>(&mappedVertexData));
			assert(SUCCEEDED(mapResult));
			std::memcpy(
				mappedVertexData,
				primitiveModelData[meshTypeIndex].vertices.data(),
				bufferSize);

			primitiveVertexBufferViews[meshTypeIndex].BufferLocation =
				primitiveVertexResources[meshTypeIndex]->GetGPUVirtualAddress();
			primitiveVertexBufferViews[meshTypeIndex].SizeInBytes = static_cast<UINT>(bufferSize);
			primitiveVertexBufferViews[meshTypeIndex].StrideInBytes = sizeof(VertexData);
		}
	}
}

void EditorPlatformManager::Initialize(_In_ HINSTANCE instanceHandle) {
	InstallCrashHandler(); // クラチE��ュ時に dump を残せるよぁE��最初に例外ハンドラを登録する、E

	//================================================================
	// ログファイルの作�E
	//================================================================

	std::filesystem::create_directory("logs"); // logs フォルダは実行ごとの .Log ファイルを保存する場所、E
	std::time_t now = std::time(nullptr); // now / localTime はログファイル名に使ぁE��在時刻、E
	std::tm localTime{};
	localtime_s(&localTime, &now);

	// dateString は yyyyMMdd_HHmmss 形式�Eログファイル名部刁E��E
	std::string dateString = std::format(
		"{:04}{:02}{:02}_{:02}{:02}{:02}",
		localTime.tm_year + 1900,
		localTime.tm_mon + 1,
		localTime.tm_mday,
		localTime.tm_hour,
		localTime.tm_min,
		localTime.tm_sec);

	auto logFilePath = std::string("logs/" + dateString + ".Log"); // logFilePath は今回の実行ログの保存�E、E
	std::ofstream logStream(logFilePath); // logStream は初期化ログとシェーダーコンパイルログを書き込む出力�E、E

	// ログが開けなぁE��合�E、以降�E初期化状況を記録できなぁE��め起動を止める、E
	if (!logStream) {
		RequestInitializationFailure();
		return;
	}

	HWND windowHandle = CreateMainWindow(instanceHandle, logStream);
	// windowHandle は DirectX SwapChain と DirectInput の協調レベル設定に使ぁEHWND、E

	// Window 作�E失敗時は DirectX / ImGui ぁEHWND を使えなぁE��め終亁E��る、E
	if (windowHandle == nullptr) {
		RequestInitializationFailure();
		return;
	}

	HRESULT hr = S_OK; // hr は Win32 / DirectX / XAudio2 API の成否を受け取る�E送EHRESULT、E
	IDirectInput8* directInput = nullptr; // directInput はキーボ�Eドデバイスを作る DirectInput 本体、E
	hr = DirectInput8Create(
		instanceHandle, DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast<void**>(&directInput), nullptr);
	assert(SUCCEEDED(hr));

	// directInput が作れなぁE��合、エチE��ター操作�E入力を取得できなぁE��め起動を止める、E
	if (FAILED(hr) || directInput == nullptr) {
		RequestInitializationFailure();
		return;
	}

	IDirectInputDevice8* keyboardDevice = nullptr; // keyboardDevice は DIK_* の押下状態を取得するため�Eキーボ�Eド�E力デバイス、E
	hr = directInput->CreateDevice(GUID_SysKeyboard, &keyboardDevice, nullptr);
	assert(SUCCEEDED(hr));

	// キーボ�Eドデバイス作�E失敗時は directInput を解放してから終亁E��る、E
	if (FAILED(hr) || keyboardDevice == nullptr) {
		directInput->Release();
		RequestInitializationFailure();
		return;
	}

	hr = keyboardDevice->SetDataFormat(&c_dfDIKeyboard);
	// c_dfDIKeyboard は DirectInput の 256 キー配�E形式で入力を受け取る持E��、E
	assert(SUCCEEDED(hr));

	// foreground / nonexclusive は他アプリと入力を奪ぁE��わなぁE��チE��ター向け設定、E
	hr = keyboardDevice->SetCooperativeLevel(
		windowHandle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));

	//================================================================
	// DirectInput マウスチE��イス作�E
	//================================================================

	IDirectInputDevice8* mouseDevice = nullptr;
	hr = directInput->CreateDevice(GUID_SysMouse, &mouseDevice, nullptr);
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || mouseDevice == nullptr) {
		directInput->Release();
		RequestInitializationFailure();
		return;
	}

	hr = mouseDevice->SetDataFormat(&c_dfDIMouse);
	assert(SUCCEEDED(hr));
	hr = mouseDevice->SetCooperativeLevel(
		windowHandle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(hr));

	// key / preKey は初期化直後�E今フレーム・前フレーム入力状態、E
	BYTE key[256] = {};
	BYTE preKey[256] = {};

#ifdef _DEBUG
	//================================================================
	// DirectX12 チE��チE��レイヤー
	//================================================================

	ComPtr<ID3D12Debug1> debugController;
	// debugController は DirectX12 のエラーめE��告を Visual Studio に出すため�E制御オブジェクト、E

	// Debug ビルドだぁEGPU 検証を有効にし、危険な API 使用を早めに検�Eする、E
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	// message は Win32 メチE��ージループで使ぁE��在メチE��ージ、E
	MSG message{};
	Log(logStream, "main loop started");

	//================================================================
	// XAudio2 の初期匁E
	//================================================================

	IXAudio2* xAudio2 = nullptr; // xAudio2 は音声再生エンジン本体、E
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(hr));

	// XAudio2 が作れなぁE��合、E��声リソースを保持できなぁE��め終亁E��る、E
	if (FAILED(hr) || xAudio2 == nullptr) {
		RequestInitializationFailure();
		return;
	}

	IXAudio2MasteringVoice* masterVoice = nullptr; // masterVoice は最終的にスピ�Eカーへ送る出劁EVoice、E
	hr = xAudio2->CreateMasteringVoice(&masterVoice);
	assert(SUCCEEDED(hr));

	// masterVoice 作�E失敗時は XAudio2 本体を解放して終亁E��る、E
	if (FAILED(hr) || masterVoice == nullptr) {
		xAudio2->Release();
		RequestInitializationFailure();
		return;
	}

	SoundData soundData = SoundLoadWave("resources/sound/maou_19_12345.wav");
	// soundData は起動確認用 wav の PCM チE�Eタとフォーマット情報、E
	IXAudio2SourceVoice* sourceVoice = nullptr; // sourceVoice は soundData を�E生キューへ積�E Voice、E
	hr = xAudio2->CreateSourceVoice(&sourceVoice, &soundData.wfex);
	assert(SUCCEEDED(hr));

	// sourceVoice 作�E失敗時は読み込んだ wav と Voice を解放して終亁E��る、E
	if (FAILED(hr) || sourceVoice == nullptr) {
		SoundUnload(&soundData);
		masterVoice->DestroyVoice();
		xAudio2->Release();
		RequestInitializationFailure();
		return;
	}

	// soundBuffer は sourceVoice に渡す�E生データの篁E��と終端フラグ、E
	XAUDIO2_BUFFER soundBuffer{};
	soundBuffer.pAudioData = soundData.pBuffer;
	soundBuffer.AudioBytes = soundData.bufferSize;
	soundBuffer.Flags = XAUDIO2_END_OF_STREAM;
	hr = sourceVoice->SubmitSourceBuffer(&soundBuffer);
	assert(SUCCEEDED(hr));

	//================================================================
	// DirectX12 の初期匁E
	//================================================================

	ComPtr<IDXGIFactory7> dxgiFactory; // dxgiFactory は GPU Adapter と SwapChain を作るための DXGI 入口、E
	hr = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
	assert(SUCCEEDED(hr));

	ComPtr<IDXGIAdapter4> useAdapter; // useAdapter は実際に D3D12Device を作る物琁EGPU、E
	for (UINT adapterIndex = 0;; ++adapterIndex) {
		ComPtr<IDXGIAdapter4> candidateAdapter; // candidateAdapter は性能優先頁E��列挙した GPU 候補、E
		if (dxgiFactory->EnumAdapterByGpuPreference(
			adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(candidateAdapter.GetAddressOf())) == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		// adapterDesc は GPU 名と Software Adapter 判定に使ぁE��報、E
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = candidateAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		// Software Adapter は WARP なので、ゲーム描画に使ぁE��EGPU 候補から外す、E
		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
			continue;
		}

		useAdapter = candidateAdapter; // 最初に見つかった宁EGPU を使用 Adapter として採用する、E
		Log(logStream, std::format("Use Adapter:{}", ConvertString(std::wstring{adapterDesc.Description})));
		break;
	}
	assert(useAdapter != nullptr);

	ComPtr<ID3D12Device> device; // device は DirectX12 リソース生�Eとコマンド発行�E中忁E��なめEGPU チE��イス、E
	hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(device.GetAddressOf()));
	// FeatureLevel は 12.2 から頁E��試し、PC が対応する一番高い機�Eレベルで作る、E
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
	ComPtr<ID3D12InfoQueue> infoQueue; // infoQueue は DirectX12 の重大エラーをデバッガ停止に変えるため�E診断キュー、E
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(infoQueue.GetAddressOf())))) {
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// 破損�Eエラー・警告をそ�E場で止め、原因箁E��を追ぁE��すくする、E
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// denyIds は既知のノイズ警告を Debug 出力から除外するため�E ID 一覧、E
		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
		};

		// severities は惁E��メチE��ージを抑制するための重大度一覧、E
		D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO};

		// filter は denyIds / severities めEInfoQueue に渡すため�E設定、E
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		infoQueue->PushStorageFilter(&filter);
	}
#endif

	ComPtr<ID3D12CommandQueue> commandQueue; // commandQueue は GPU に CommandList を送るキュー、E

	// commandQueueDesc は標準�E Direct CommandQueue を作るための設定、E
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// CommandQueue がなぁE��描画命令めEGPU に送れなぁE��め終亁E��る、E
	if (FAILED(hr) || commandQueue == nullptr) {
		RequestInitializationFailure();
		return;
	}


	ComPtr<ID3D12CommandAllocator> commandAllocator; // commandAllocator は CommandList が記録する命令メモリを管琁E��る、E
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// CommandAllocator がなぁE�� CommandList めEReset できなぁE��め終亁E��る、E
	if (FAILED(hr) || commandAllocator == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12GraphicsCommandList> commandList;
	// commandList は描画・コピ�E・ResourceBarrier の命令を記録するオブジェクト、E
	hr = device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// CommandList がなぁE��初期チE��スチャアチE�Eロードも描画もできなぁE��め終亁E��る、E
	if (FAILED(hr) || commandList == nullptr) {
		RequestInitializationFailure();
		return;
	}

	hr = commandList->Close(); // 作�E直後�E CommandList は開いてぁE��ため、一度 Close して通常の Reset 手頁E��合わせる、E
	assert(SUCCEEDED(hr));

	// clientRect は Window 冁E�E描画可能領域。SwapChain サイズの初期値に使ぁE��E
	RECT clientRect{};
	GetClientRect(windowHandle, &clientRect);

	uint32_t renderWidth = (std::max)(1u, static_cast<uint32_t>(clientRect.right - clientRect.left));
	// renderWidth / renderHeight は最小化などで 0 にならなぁE��ぁE1 以上にする、E
	uint32_t renderHeight = (std::max)(1u, static_cast<uint32_t>(clientRect.bottom - clientRect.top));

	ComPtr<IDXGISwapChain4> swapChain; // swapChain は Window に表示するバックバッファ列、E

	// swapChainDesc はバックバッファ数、形式、表示方式を持E��する設定、E
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

	// SwapChain がなぁE�� Window へ Present できなぁE��め終亁E��る、E
	if (FAILED(hr) || swapChain == nullptr) {
		RequestInitializationFailure();
		return;
	}

	// rtvDescriptorHeap は SwapChain バックバッファめERenderTarget として参�Eする Heap、E
	ID3D12DescriptorHeap* rtvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRuntimeRtvCount, false);

	// srvDescriptorHeap は Texture SRV と ImGui 用 SRV めEShader から参�Eする Heap、E
	ID3D12DescriptorHeap* srvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	// dsvDescriptorHeap は DepthStencil を参照する Heap、E
	ID3D12DescriptorHeap* dsvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);

	// swapChainResources は 2 枚�Eバックバッファ実体、E
	ID3D12Resource* swapChainResources[2] = {nullptr};
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	// rtvDesc はバックバッファめE2D RenderTarget として扱ぁE��定、E
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2]; // rtvHandles は吁E��チE��バッファに対応すめECPU 側 RTV ハンドル、E
	rtvHandles[0] = GetCPUDescriptorHandle(
		rtvDescriptorHeap, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), 0);
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);
	rtvHandles[1] = GetCPUDescriptorHandle(
		rtvDescriptorHeap, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), 1);
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

	// depthClearValue は DepthStencil めEClear する時�E初期値、E
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	// dsvDesc は DepthStencilResource めEDSV として使ぁE��め�E設定、E
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	// dsvHandle は DepthStencilView を作�Eする CPU 側ハンドル、E

	auto createDepthStencilResource = [&](uint32_t width, uint32_t height) -> ID3D12Resource* {
		// depthStencilResourceDesc は SceneView と同じサイズの Depth バッファ設定、E
		D3D12_RESOURCE_DESC depthStencilResourceDesc{};
		depthStencilResourceDesc.Width = width;
		depthStencilResourceDesc.Height = height;
		depthStencilResourceDesc.MipLevels = 1;
		depthStencilResourceDesc.DepthOrArraySize = 1;
		depthStencilResourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depthStencilResourceDesc.SampleDesc.Count = 1;
		depthStencilResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthStencilResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		// depthStencilHeapProperties は GPU 専用メモリ上に Depth バッファを置く指定、E
		D3D12_HEAP_PROPERTIES depthStencilHeapProperties{};
		depthStencilHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		ID3D12Resource* newDepthStencilResource = nullptr; // newDepthStencilResource は作�Eして返す DepthStencil 実体、E
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
	// depthStencilResource は現在の描画サイズに合わせた Depth バッファ、E
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandle);

	D3D12_CPU_DESCRIPTOR_HANDLE depthSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap,
		device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
		kRuntimeDepthSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap,
		device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
		kRuntimeDepthSrvDescriptorIndex);
	D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc{};
	depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthSrvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(depthStencilResource, &depthSrvDesc, depthSrvHandleCPU);

	D3D12_CLEAR_VALUE shadowClearValue{};
	shadowClearValue.Format = DXGI_FORMAT_D32_FLOAT;
	shadowClearValue.DepthStencil.Depth = 1.0f;
	shadowClearValue.DepthStencil.Stencil = 0;

	D3D12_RESOURCE_DESC shadowMapResourceDesc{};
	shadowMapResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	shadowMapResourceDesc.Width = kRuntimeShadowMapSize;
	shadowMapResourceDesc.Height = kRuntimeShadowMapSize;
	shadowMapResourceDesc.DepthOrArraySize = 1;
	shadowMapResourceDesc.MipLevels = 1;
	shadowMapResourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	shadowMapResourceDesc.SampleDesc.Count = 1;
	shadowMapResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_HEAP_PROPERTIES shadowMapHeapProperties{};
	shadowMapHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	ID3D12Resource* shadowMapResource = nullptr;
	hr = device->CreateCommittedResource(
		&shadowMapHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&shadowMapResourceDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&shadowClearValue,
		IID_PPV_ARGS(&shadowMapResource));
	assert(SUCCEEDED(hr));

	D3D12_DEPTH_STENCIL_VIEW_DESC shadowDsvDesc{};
	shadowDsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	shadowDsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	D3D12_CPU_DESCRIPTOR_HANDLE shadowDsvHandle = GetCPUDescriptorHandle(
		dsvDescriptorHeap,
		device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV),
		1);
	device->CreateDepthStencilView(shadowMapResource, &shadowDsvDesc, shadowDsvHandle);

	D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc{};
	shadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	shadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	shadowSrvDesc.Texture2D.MipLevels = 1;
	D3D12_CPU_DESCRIPTOR_HANDLE shadowMapSrvCpuHandle = GetCPUDescriptorHandle(
		srvDescriptorHeap,
		device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
		kRuntimeShadowSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE shadowMapSrvGpuHandle = GetGPUDescriptorHandle(
		srvDescriptorHeap,
		device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
		kRuntimeShadowSrvDescriptorIndex);
	device->CreateShaderResourceView(shadowMapResource, &shadowSrvDesc, shadowMapSrvCpuHandle);

	//================================================================
	// HDR RenderTarget and Bloom RenderTargets
	//================================================================

	auto createRenderTargetResource = [&](uint32_t rtWidth, uint32_t rtHeight, DXGI_FORMAT format) -> ID3D12Resource* {
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

		ID3D12Resource* resource = nullptr;
		HRESULT hr = device->CreateCommittedResource(
			&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&clearValue, IID_PPV_ARGS(&resource));
		assert(SUCCEEDED(hr));
		return resource;
	};

	UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	UINT srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// HDR RT (full resolution, R16G16B16A16_FLOAT) ? RTV index 2
	ID3D12Resource* hdrRenderTarget = createRenderTargetResource(renderWidth, renderHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
	D3D12_CPU_DESCRIPTOR_HANDLE hdrRtvHandle = GetCPUDescriptorHandle(rtvDescriptorHeap, rtvSize, 2);
	D3D12_RENDER_TARGET_VIEW_DESC hdrRtvDesc{};
	hdrRtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	hdrRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device->CreateRenderTargetView(hdrRenderTarget, &hdrRtvDesc, hdrRtvHandle);
	D3D12_CPU_DESCRIPTOR_HANDLE hdrSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap, srvSize, kRuntimeHdrSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE hdrSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap, srvSize, kRuntimeHdrSrvDescriptorIndex);
	D3D12_SHADER_RESOURCE_VIEW_DESC hdrSrvDesc{};
	hdrSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	hdrSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	hdrSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	hdrSrvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(hdrRenderTarget, &hdrSrvDesc, hdrSrvHandleCPU);

	// Bloom RTs (1/4 resolution, R16G16B16A16_FLOAT) ? RTV indices 3,4
	uint32_t bloomWidth = (std::max)(1u, renderWidth / 4);
	uint32_t bloomHeight = (std::max)(1u, renderHeight / 4);
	ID3D12Resource* bloomRenderTargets[2] = {};
	D3D12_CPU_DESCRIPTOR_HANDLE bloomRtvHandles[2]{};
	D3D12_CPU_DESCRIPTOR_HANDLE bloomSrvHandlesCPU[2]{};
	D3D12_GPU_DESCRIPTOR_HANDLE bloomSrvHandlesGPU[2]{};
	for (uint32_t i = 0; i < 2; i++) {
		bloomRenderTargets[i] = createRenderTargetResource(bloomWidth, bloomHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
		bloomRtvHandles[i] = GetCPUDescriptorHandle(rtvDescriptorHeap, rtvSize, 3u + i);
		device->CreateRenderTargetView(bloomRenderTargets[i], &hdrRtvDesc, bloomRtvHandles[i]);
		uint32_t srvIndex = (i == 0) ? kRuntimeBloomSrvDescriptorIndexA : kRuntimeBloomSrvDescriptorIndexB;
		bloomSrvHandlesCPU[i] = GetCPUDescriptorHandle(srvDescriptorHeap, srvSize, srvIndex);
		bloomSrvHandlesGPU[i] = GetGPUDescriptorHandle(srvDescriptorHeap, srvSize, srvIndex);
		device->CreateShaderResourceView(bloomRenderTargets[i], &hdrSrvDesc, bloomSrvHandlesCPU[i]);
	}

	// ToneMap ��� LDR RT �́AFXAA ���ŏI BackBuffer �֏o���O�ɓǂޒ��� RenderTexture�B
	ID3D12Resource* postProcessRenderTarget = createRenderTargetResource(renderWidth, renderHeight, DXGI_FORMAT_R8G8B8A8_UNORM);
	D3D12_CPU_DESCRIPTOR_HANDLE postProcessRtvHandle = GetCPUDescriptorHandle(rtvDescriptorHeap, rtvSize, 5u);
	D3D12_RENDER_TARGET_VIEW_DESC postProcessRtvDesc{};
	postProcessRtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	postProcessRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device->CreateRenderTargetView(postProcessRenderTarget, &postProcessRtvDesc, postProcessRtvHandle);
	D3D12_CPU_DESCRIPTOR_HANDLE postProcessSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap,
		srvSize,
		kRuntimePostProcessSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE postProcessSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap,
		srvSize,
		kRuntimePostProcessSrvDescriptorIndex);
	D3D12_SHADER_RESOURCE_VIEW_DESC postProcessSrvDesc{};
	postProcessSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	postProcessSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	postProcessSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	postProcessSrvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(postProcessRenderTarget, &postProcessSrvDesc, postProcessSrvHandleCPU);

	ID3D12Resource* ssaoRenderTargets[2] = {};
	D3D12_CPU_DESCRIPTOR_HANDLE ssaoRtvHandles[2]{};
	D3D12_CPU_DESCRIPTOR_HANDLE ssaoSrvHandlesCPU[2]{};
	D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrvHandlesGPU[2]{};
	D3D12_RENDER_TARGET_VIEW_DESC ssaoRtvDesc{};
	ssaoRtvDesc.Format = DXGI_FORMAT_R8_UNORM;
	ssaoRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	D3D12_SHADER_RESOURCE_VIEW_DESC ssaoSrvDesc{};
	ssaoSrvDesc.Format = DXGI_FORMAT_R8_UNORM;
	ssaoSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	ssaoSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	ssaoSrvDesc.Texture2D.MipLevels = 1;
	for (uint32_t i = 0; i < 2; i++) {
		ssaoRenderTargets[i] = createRenderTargetResource(renderWidth, renderHeight, DXGI_FORMAT_R8_UNORM);
		ssaoRtvHandles[i] = GetCPUDescriptorHandle(rtvDescriptorHeap, rtvSize, 6u + i);
		device->CreateRenderTargetView(ssaoRenderTargets[i], &ssaoRtvDesc, ssaoRtvHandles[i]);
		uint32_t srvIndex = (i == 0u) ? kRuntimeSsaoSrvDescriptorIndexA : kRuntimeSsaoSrvDescriptorIndexB;
		ssaoSrvHandlesCPU[i] = GetCPUDescriptorHandle(srvDescriptorHeap, srvSize, srvIndex);
		ssaoSrvHandlesGPU[i] = GetGPUDescriptorHandle(srvDescriptorHeap, srvSize, srvIndex);
		device->CreateShaderResourceView(ssaoRenderTargets[i], &ssaoSrvDesc, ssaoSrvHandlesCPU[i]);
	}

	ID3D12Resource* hdrCompositeRenderTarget = createRenderTargetResource(
		renderWidth,
		renderHeight,
		DXGI_FORMAT_R16G16B16A16_FLOAT);
	D3D12_CPU_DESCRIPTOR_HANDLE hdrCompositeRtvHandle = GetCPUDescriptorHandle(rtvDescriptorHeap, rtvSize, 8u);
	device->CreateRenderTargetView(hdrCompositeRenderTarget, &hdrRtvDesc, hdrCompositeRtvHandle);
	D3D12_CPU_DESCRIPTOR_HANDLE hdrCompositeSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap,
		srvSize,
		kRuntimeHdrCompositeSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE hdrCompositeSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap,
		srvSize,
		kRuntimeHdrCompositeSrvDescriptorIndex);
	device->CreateShaderResourceView(hdrCompositeRenderTarget, &hdrSrvDesc, hdrCompositeSrvHandleCPU);

	ID3D12Resource* materialMaskRenderTarget = createRenderTargetResource(
		renderWidth,
		renderHeight,
		DXGI_FORMAT_R16G16B16A16_FLOAT);
	D3D12_RENDER_TARGET_VIEW_DESC materialMaskRtvDesc{};
	materialMaskRtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	materialMaskRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	D3D12_CPU_DESCRIPTOR_HANDLE materialMaskRtvHandle = GetCPUDescriptorHandle(rtvDescriptorHeap, rtvSize, 10u);
	device->CreateRenderTargetView(materialMaskRenderTarget, &materialMaskRtvDesc, materialMaskRtvHandle);
	D3D12_SHADER_RESOURCE_VIEW_DESC materialMaskSrvDesc{};
	materialMaskSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	materialMaskSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	materialMaskSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	materialMaskSrvDesc.Texture2D.MipLevels = 1;
	D3D12_CPU_DESCRIPTOR_HANDLE materialMaskSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap,
		srvSize,
		kRuntimeMaterialMaskSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE materialMaskSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap,
		srvSize,
		kRuntimeMaterialMaskSrvDescriptorIndex);
	device->CreateShaderResourceView(materialMaskRenderTarget, &materialMaskSrvDesc, materialMaskSrvHandleCPU);

	ID3D12Resource* planarReflectionRenderTarget = createRenderTargetResource(
		renderWidth,
		renderHeight,
		DXGI_FORMAT_R16G16B16A16_FLOAT);
	D3D12_CPU_DESCRIPTOR_HANDLE planarReflectionRtvHandle = GetCPUDescriptorHandle(rtvDescriptorHeap, rtvSize, 11u);
	device->CreateRenderTargetView(planarReflectionRenderTarget, &hdrRtvDesc, planarReflectionRtvHandle);
	D3D12_CPU_DESCRIPTOR_HANDLE planarReflectionSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap,
		srvSize,
		kRuntimePlanarReflectionSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE planarReflectionSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap,
		srvSize,
		kRuntimePlanarReflectionSrvDescriptorIndex);
	device->CreateShaderResourceView(planarReflectionRenderTarget, &hdrSrvDesc, planarReflectionSrvHandleCPU);

	ComPtr<IDxcUtils> dxcUtils; // dxcUtils は HLSL ファイル読み込みと IncludeHandler 作�Eに使ぁEDXC 補助、E
	ComPtr<IDxcCompiler3> dxcCompiler; // dxcCompiler は HLSL めEDXIL へコンパイルする DXC コンパイラ、E
	ComPtr<IDxcIncludeHandler> includeHandler; // includeHandler は shader の #include を解決するための標準ハンドラ、E
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxcUtils.GetAddressOf()));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxcCompiler.GetAddressOf()));
	assert(SUCCEEDED(hr));
	hr = dxcUtils->CreateDefaultIncludeHandler(includeHandler.GetAddressOf());
	assert(SUCCEEDED(hr));

	// vertexShaderBlob は VS main のコンパイル済みバイトコード、E
	ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(
		L"Assets/Shaders/Object3d.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	// pixelShaderBlob は PS main のコンパイル済みバイトコード、E
	ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(
		L"Assets/Shaders/Object3d.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	ComPtr<IDxcBlob> objectReflectionMaskPixelShaderBlob = CompileShader(
		L"Assets/Shaders/Object3dReflectionMask.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	Log(std::string("Object3d VS size=") + std::to_string(vertexShaderBlob != nullptr ? vertexShaderBlob->GetBufferSize() : 0ull) +
	    " PS size=" + std::to_string(pixelShaderBlob != nullptr ? pixelShaderBlob->GetBufferSize() : 0ull));

	ComPtr<IDxcBlob> shadowVertexShaderBlob = CompileShader(
		L"Assets/Shaders/ShadowDepth.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	// Post-process shader compilation
	ComPtr<IDxcBlob> fullscreenVertexShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/FullScreen.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> toneMappingPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/ToneMapping.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> bloomExtractPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/BloomExtract.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> bloomBlurPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/BloomBlur.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> fxaaPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/FXAA.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssaoPixelShaderBlob = CompileShader(
		L"Assets/Shaders/AO/GTAO.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssaoBlurPixelShaderBlob = CompileShader(
		L"Assets/Shaders/Shadow/ContactShadow.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> skyboxPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/Skybox.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> planarReflectionPixelShaderBlob = CompileShader(
		L"Assets/Shaders/Reflection/PlanarReflection.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> sharpenPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/Sharpen.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	if (vertexShaderBlob == nullptr ||
		pixelShaderBlob == nullptr ||
		objectReflectionMaskPixelShaderBlob == nullptr ||
		shadowVertexShaderBlob == nullptr ||
		fullscreenVertexShaderBlob == nullptr ||
		toneMappingPixelShaderBlob == nullptr ||
		bloomExtractPixelShaderBlob == nullptr ||
		bloomBlurPixelShaderBlob == nullptr ||
		fxaaPixelShaderBlob == nullptr ||
		ssaoPixelShaderBlob == nullptr ||
		ssaoBlurPixelShaderBlob == nullptr ||
		skyboxPixelShaderBlob == nullptr ||
		planarReflectionPixelShaderBlob == nullptr ||
		sharpenPixelShaderBlob == nullptr) {
		RequestInitializationFailure();  // �K�{�V�F�[�_�[�� 1 �ł��������� PSO �쐬�֐i�߂Ȃ��B
		return;
	}

	Log(logStream, "Init Stage: shader compile completed");

	// descriptorRange は Texture SRV めERootSignature の DescriptorTable に渡す篁E��、E
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE shadowDescriptorRange[1] = {};
	shadowDescriptorRange[0].BaseShaderRegister = 1;
	shadowDescriptorRange[0].NumDescriptors = 1;
	shadowDescriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	shadowDescriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE environmentDescriptorRange[1] = {};
	environmentDescriptorRange[0].BaseShaderRegister = 2;
	environmentDescriptorRange[0].NumDescriptors = 1;
	environmentDescriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	environmentDescriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// IBL descriptor ranges: t3=irradiance, t4=prefilter, t5=environment cube, t6=BRDF LUT
	D3D12_DESCRIPTOR_RANGE iblIrradianceRange[1] = {};
	iblIrradianceRange[0].BaseShaderRegister = 3;
	iblIrradianceRange[0].NumDescriptors = 1;
	iblIrradianceRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	iblIrradianceRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE iblPrefilterRange[1] = {};
	iblPrefilterRange[0].BaseShaderRegister = 4;
	iblPrefilterRange[0].NumDescriptors = 1;
	iblPrefilterRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	iblPrefilterRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE iblEnvironmentRange[1] = {};
	iblEnvironmentRange[0].BaseShaderRegister = 5;
	iblEnvironmentRange[0].NumDescriptors = 1;
	iblEnvironmentRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	iblEnvironmentRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE iblBrdfLutRange[1] = {};
	iblBrdfLutRange[0].BaseShaderRegister = 6;
	iblBrdfLutRange[0].NumDescriptors = 1;
	iblBrdfLutRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	iblBrdfLutRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// descriptionRootSignature �� Shader �ւ� CBV / SRV / Sampler �̊��蓖�Ē�`�B
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// rootParameters �� 0:Material�A1:Transform�A2:DirectionalLight�A3:Texture SRV�A4:Shadow�A5:EmissiveLights�A6:Environment�A7-10:IBL
	D3D12_ROOT_PARAMETER rootParameters[11] = {};

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

	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[4].DescriptorTable.pDescriptorRanges = shadowDescriptorRange;
	rootParameters[4].DescriptorTable.NumDescriptorRanges = _countof(shadowDescriptorRange);

	rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[5].Descriptor.ShaderRegister = 2;
	rootParameters[5].Descriptor.RegisterSpace = 0;

	rootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[6].DescriptorTable.pDescriptorRanges = environmentDescriptorRange;
	rootParameters[6].DescriptorTable.NumDescriptorRanges = _countof(environmentDescriptorRange);

	rootParameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[7].DescriptorTable.pDescriptorRanges = iblIrradianceRange;
	rootParameters[7].DescriptorTable.NumDescriptorRanges = _countof(iblIrradianceRange);

	rootParameters[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[8].DescriptorTable.pDescriptorRanges = iblPrefilterRange;
	rootParameters[8].DescriptorTable.NumDescriptorRanges = _countof(iblPrefilterRange);

	rootParameters[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[9].DescriptorTable.pDescriptorRanges = iblEnvironmentRange;
	rootParameters[9].DescriptorTable.NumDescriptorRanges = _countof(iblEnvironmentRange);

	rootParameters[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[10].DescriptorTable.pDescriptorRanges = iblBrdfLutRange;
	rootParameters[10].DescriptorTable.NumDescriptorRanges = _countof(iblBrdfLutRange);

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	D3D12_STATIC_SAMPLER_DESC staticSamplers[3] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[1].ShaderRegister = 1;
	staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	staticSamplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSamplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[2].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[2].ShaderRegister = 2;
	staticSamplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	ComPtr<ID3DBlob> signatureBlob; // signatureBlob は RootSignature のシリアライズ結果、E
	ComPtr<ID3DBlob> errorBlob; // errorBlob は RootSignature シリアライズ失敗時のエラー斁E���E、E
	Log(logStream, "Init Stage: creating material and transform buffers");
	ID3D12Resource* spriteMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));
	// spriteMaterialResource は Sprite 描画用 Material 定数バッファ、E
	Material* spriteMaterialData = nullptr;
	// spriteMaterialData は CPU から直接書き込める Sprite Material の mapped ポインタ、E
	spriteMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&spriteMaterialData));
	spriteMaterialData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	spriteMaterialData->enableLighting = FALSE;
	spriteMaterialData->useTexture = TRUE;
	spriteMaterialData->metallic = 0.0f;
	spriteMaterialData->roughness = 0.5f;
	spriteMaterialData->reflectance = 0.0f;
	spriteMaterialData->ior = 1.0f;
	spriteMaterialData->emissionStrength = 0.0f;
	spriteMaterialData->reflectionMode = 0.0f;
	spriteMaterialData->reflectionProbeIntensity = 0.0f;
	spriteMaterialData->reflectionReserved = 0.0f;
	spriteMaterialData->uvTransform = MakeIdentity4x4();

	ID3D12Resource* sphereMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));
	// sphereMaterialResource は 3D モチE��描画用 Material 定数バッファ、E
	Material* sphereMaterialData = nullptr;
	// sphereMaterialData は CPU から直接書き込める 3D Material の mapped ポインタ、E
	sphereMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&sphereMaterialData));
	sphereMaterialData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	sphereMaterialData->enableLighting = TRUE;
	sphereMaterialData->useTexture = TRUE;
	sphereMaterialData->metallic = 0.0f;
	sphereMaterialData->roughness = 0.5f;
	sphereMaterialData->reflectance = 0.0f;
	sphereMaterialData->ior = 1.0f;
	sphereMaterialData->emissionStrength = 0.0f;
	sphereMaterialData->reflectionMode = 0.0f;
	sphereMaterialData->reflectionProbeIntensity = 0.0f;
	sphereMaterialData->reflectionReserved = 0.0f;
	sphereMaterialData->uvTransform = MakeIdentity4x4();

	ID3D12Resource* directionalLightResource = CreateBufferResource(device.Get(), sizeof(DirectionalLight));
	// directionalLightResource は PixelShader に渡す平行�E源定数バッファ、E
	DirectionalLight* directionalLightData = nullptr;
	// directionalLightData は Inspector から色・向き・強さを書き換える mapped ポインタ、E
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	directionalLightData->direction = {0.0f, -1.0f, 0.0f};
	directionalLightData->intensity = 1.0f;
	directionalLightData->position = {0.0f, 3.0f, -3.0f};
	directionalLightData->range = 15.0f;
	directionalLightData->skyUpperColor = {0.45f, 0.58f, 0.78f};
	directionalLightData->skyIntensity = 1.0f;
	directionalLightData->skyLowerColor = {0.12f, 0.14f, 0.18f};
	directionalLightData->skyEmission = 0.08f;
	directionalLightData->ambientIntensity = 0.35f;
	directionalLightData->horizonSharpness = 1.0f;
	directionalLightData->reflectionIntensity = 0.60f;
	directionalLightData->spotCosInner = std::cos(20.0f * 3.1415926f / 180.0f);
	directionalLightData->spotCosOuter = std::cos(35.0f * 3.1415926f / 180.0f);
	directionalLightData->lightType = 0;
	directionalLightData->areaRadius = 1.0f;
	directionalLightData->cameraPosition = {0.0f, 0.0f, -5.0f};
	directionalLightData->padding3 = 0.0f;
	directionalLightData->environmentTextureEnabled = 0.0f;
	directionalLightData->environmentTextureIntensity = 1.0f;
	directionalLightData->environmentTextureRotation = 0.0f;
	directionalLightData->environmentTextureMipBias = 0.0f;

	ID3D12Resource* emissiveLightResource = CreateBufferResource(device.Get(), sizeof(EmissiveLightArray));
	EmissiveLightArray* emissiveLightData = nullptr;
	emissiveLightResource->Map(0, nullptr, reinterpret_cast<void**>(&emissiveLightData));
	emissiveLightData->count = 0;


	// spriteTransformationMatrixResource は Sprite の WVP / World 行�E用定数バッファ、E
	ID3D12Resource* spriteTransformationMatrixResource = CreateBufferResource(
		device.Get(), sizeof(TransformationMatrix));

	TransformationMatrix* spriteTransformationMatrixData = nullptr;
	// spriteTransformationMatrixData は CPU から Sprite 行�Eを書き込む mapped ポインタ、E
	spriteTransformationMatrixResource->Map(
		0, nullptr, reinterpret_cast<void**>(&spriteTransformationMatrixData));
	spriteTransformationMatrixData->WVP = MakeIdentity4x4();
	spriteTransformationMatrixData->World = MakeIdentity4x4();
	spriteTransformationMatrixData->lightWVP = MakeIdentity4x4();

	// sphereTransformationMatrixResource は旧 3D プレビューの WVP / World 行�E用定数バッファ、E
	ID3D12Resource* sphereTransformationMatrixResource = CreateBufferResource(
		device.Get(), sizeof(TransformationMatrix));

	TransformationMatrix* sphereTransformationMatrixData = nullptr;
	// sphereTransformationMatrixData は CPU から 3D 行�Eを書き込む mapped ポインタ、E
	sphereTransformationMatrixResource->Map(
		0, nullptr, reinterpret_cast<void**>(&sphereTransformationMatrixData));
	sphereTransformationMatrixData->WVP = MakeIdentity4x4();
	sphereTransformationMatrixData->World = MakeIdentity4x4();
	sphereTransformationMatrixData->lightWVP = MakeIdentity4x4();
	Log(logStream, "Init Stage: material and transform buffers completed");

	// RootSignature は Shader がどの Resource をどのスロチE��で読むかを固定する、E
	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.GetAddressOf(),
		errorBlob.GetAddressOf());
	if (FAILED(hr)) {
		// errorBlob がある場合�E HLSL/RootSignature 側の具体的な失敗理由をログへ出す、E
		if (errorBlob != nullptr) {
			Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	Log(logStream, "Init Stage: object root signature serialized");

	ComPtr<ID3D12RootSignature> rootSignature; // rootSignature は PipelineState に設定すめEGPU 側 RootSignature 実体、E
	hr = device->CreateRootSignature(
		0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(rootSignature.GetAddressOf()));

	if (FAILED(hr) || rootSignature == nullptr) {
		Log(logStream, std::format("Object3d RootSignature Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: object root signature created");

	// inputElementDescs は VertexData の position / texcoord / normal めEShader 入力に対応させる、E
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

	// blendDesc は RenderTarget へ色を書き込む方法。現状は不透�E描画の標準設定、E
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// rasterizerDesc は三角形を塗りつぶし、裏表カリングをしなぁE��定、E
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	rasterizerDesc.DepthClipEnable = TRUE;

	// depthStencilDesc は手前の物体を優先して描くための深度チE��ト設定、E
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// graphicsPipelineStateDesc は Shader、�E力レイアウト、Blend、Depth をまとめた描画設定、E
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
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	ComPtr<ID3D12PipelineState> graphicsPipelineState;
	// graphicsPipelineState は Draw 時に CommandList へセチE��する PSO 実体、E
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
	                                         IID_PPV_ARGS(graphicsPipelineState.GetAddressOf()));

	// PSO がなぁE�� Shader と RenderState が確定せず描画できなぁE��め終亁E��る、E
	if (FAILED(hr) || graphicsPipelineState == nullptr) {
		Log(logStream, std::format("Object3d PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: object pso created");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC planarScenePipelineStateDesc = graphicsPipelineStateDesc;
	planarScenePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ComPtr<ID3D12PipelineState> planarScenePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&planarScenePipelineStateDesc,
		IID_PPV_ARGS(planarScenePipelineState.GetAddressOf()));

	if (FAILED(hr) || planarScenePipelineState == nullptr) {
		Log(logStream, std::format("Planar scene PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: planar scene pso created");

	// Planar surface PSO: 反射面オブジェクトを両面描画する (CullMode=NONE)
	D3D12_GRAPHICS_PIPELINE_STATE_DESC planarSurfacePipelineStateDesc = graphicsPipelineStateDesc;
	planarSurfacePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ComPtr<ID3D12PipelineState> planarSurfacePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&planarSurfacePipelineStateDesc,
		IID_PPV_ARGS(planarSurfacePipelineState.GetAddressOf()));

	if (FAILED(hr) || planarSurfacePipelineState == nullptr) {
		Log(logStream, std::format("Planar surface PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: planar surface pso created");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC objectReflectionMaskPipelineStateDesc = graphicsPipelineStateDesc;
	objectReflectionMaskPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	objectReflectionMaskPipelineStateDesc.PS = {
		objectReflectionMaskPixelShaderBlob->GetBufferPointer(),
		objectReflectionMaskPixelShaderBlob->GetBufferSize()
	};
	objectReflectionMaskPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	objectReflectionMaskPipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	objectReflectionMaskPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	ComPtr<ID3D12PipelineState> objectReflectionMaskPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&objectReflectionMaskPipelineStateDesc,
		IID_PPV_ARGS(objectReflectionMaskPipelineState.GetAddressOf()));

	if (FAILED(hr) || objectReflectionMaskPipelineState == nullptr) {
		Log(logStream, std::format("Object reflection mask PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: object reflection mask pso created");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC cullFrontPipelineStateDesc = graphicsPipelineStateDesc;
	cullFrontPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
	cullFrontPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	ComPtr<ID3D12PipelineState> cullFrontPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&cullFrontPipelineStateDesc,
		IID_PPV_ARGS(cullFrontPipelineState.GetAddressOf()));

	if (FAILED(hr) || cullFrontPipelineState == nullptr) {
		Log(logStream, std::format("CullFront PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: cull front pso created");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPipelineStateDesc{};
	shadowPipelineStateDesc.pRootSignature = rootSignature.Get();
	shadowPipelineStateDesc.InputLayout.pInputElementDescs = inputElementDescs;
	shadowPipelineStateDesc.InputLayout.NumElements = _countof(inputElementDescs);
	shadowPipelineStateDesc.VS = {
		shadowVertexShaderBlob->GetBufferPointer(),
		shadowVertexShaderBlob->GetBufferSize()
	};
	shadowPipelineStateDesc.PS = {};
	shadowPipelineStateDesc.BlendState = blendDesc;
	shadowPipelineStateDesc.RasterizerState = rasterizerDesc;
	shadowPipelineStateDesc.DepthStencilState = depthStencilDesc;
	shadowPipelineStateDesc.NumRenderTargets = 0;
	for (int renderTargetIndex = 0; renderTargetIndex < 8; renderTargetIndex++) {
		shadowPipelineStateDesc.RTVFormats[renderTargetIndex] = DXGI_FORMAT_UNKNOWN;
	}
	shadowPipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	shadowPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	shadowPipelineStateDesc.SampleDesc.Count = 1;
	shadowPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	shadowPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	shadowPipelineStateDesc.RasterizerState.DepthBias = 1200;
	shadowPipelineStateDesc.RasterizerState.SlopeScaledDepthBias = 1.5f;
	shadowPipelineStateDesc.RasterizerState.DepthBiasClamp = 0.01f;

	ComPtr<ID3D12PipelineState> shadowPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&shadowPipelineStateDesc,
		IID_PPV_ARGS(shadowPipelineState.GetAddressOf()));
	if (FAILED(hr) || shadowPipelineState == nullptr) {
		Log(logStream, std::format("Shadow PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: shadow pso created");

	//================================================================
	// Post-process RootSignature and PipelineStates
	//================================================================

	// Post-process descriptor ranges: t0 (input HDR/texture SRV), t1 (bloom SRV for composite)
	D3D12_DESCRIPTOR_RANGE postProcessDescriptorRange0[1] = {};
	postProcessDescriptorRange0[0].BaseShaderRegister = 0;
	postProcessDescriptorRange0[0].NumDescriptors = 1;
	postProcessDescriptorRange0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	postProcessDescriptorRange0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE postProcessDescriptorRange1[1] = {};
	postProcessDescriptorRange1[0].BaseShaderRegister = 1;
	postProcessDescriptorRange1[0].NumDescriptors = 1;
	postProcessDescriptorRange1[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	postProcessDescriptorRange1[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE postProcessDescriptorRange2[1] = {};
	postProcessDescriptorRange2[0].BaseShaderRegister = 2;
	postProcessDescriptorRange2[0].NumDescriptors = 2;
	postProcessDescriptorRange2[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	postProcessDescriptorRange2[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER postProcessRootParameters[4] = {};
	postProcessRootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	postProcessRootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	postProcessRootParameters[0].DescriptorTable.pDescriptorRanges = postProcessDescriptorRange0;
	postProcessRootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(postProcessDescriptorRange0);

	postProcessRootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	postProcessRootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	postProcessRootParameters[1].DescriptorTable.pDescriptorRanges = postProcessDescriptorRange1;
	postProcessRootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(postProcessDescriptorRange1);

	postProcessRootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	postProcessRootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	postProcessRootParameters[2].Constants.ShaderRegister = 0;
	postProcessRootParameters[2].Constants.Num32BitValues = 52;

	postProcessRootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	postProcessRootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	postProcessRootParameters[3].DescriptorTable.pDescriptorRanges = postProcessDescriptorRange2;
	postProcessRootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(postProcessDescriptorRange2);

	D3D12_STATIC_SAMPLER_DESC postProcessSampler{};
	postProcessSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	postProcessSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	postProcessSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	postProcessSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	postProcessSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	postProcessSampler.MaxLOD = D3D12_FLOAT32_MAX;
	postProcessSampler.ShaderRegister = 0;
	postProcessSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC postProcessRootSignatureDesc{};
	postProcessRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	postProcessRootSignatureDesc.pParameters = postProcessRootParameters;
	postProcessRootSignatureDesc.NumParameters = _countof(postProcessRootParameters);
	postProcessRootSignatureDesc.pStaticSamplers = &postProcessSampler;
	postProcessRootSignatureDesc.NumStaticSamplers = 1;

	ComPtr<ID3DBlob> postProcessSignatureBlob;
	ComPtr<ID3DBlob> postProcessErrorBlob;
	hr = D3D12SerializeRootSignature(
		&postProcessRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		postProcessSignatureBlob.GetAddressOf(), postProcessErrorBlob.GetAddressOf());
	if (FAILED(hr)) {
		if (postProcessErrorBlob != nullptr) {
			Log(reinterpret_cast<char*>(postProcessErrorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	ComPtr<ID3D12RootSignature> postProcessRootSignature;
	hr = device->CreateRootSignature(
		0, postProcessSignatureBlob->GetBufferPointer(), postProcessSignatureBlob->GetBufferSize(),
		IID_PPV_ARGS(postProcessRootSignature.GetAddressOf()));

	if (FAILED(hr) || postProcessRootSignature == nullptr) {
		Log(logStream, std::format("PostProcess RootSignature Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	// Post-process PSOs share common state (no depth, no culling, no vertex buffer)
	auto CreatePostProcessPSO = [&](const char* psoName, IDxcBlob* psBlob, DXGI_FORMAT rtvFormat) -> ComPtr<ID3D12PipelineState> {
		if (psBlob == nullptr || fullscreenVertexShaderBlob == nullptr) {
			Log(std::string("CreatePostProcessPSO skipped: ") + psoName + " shader blob is null");
			return nullptr;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
		desc.pRootSignature = postProcessRootSignature.Get();
		desc.VS = { fullscreenVertexShaderBlob->GetBufferPointer(), fullscreenVertexShaderBlob->GetBufferSize() };
		desc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
		D3D12_BLEND_DESC blendDesc{};
		blendDesc.RenderTarget[0].BlendEnable = FALSE;
		blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		desc.BlendState = blendDesc;
		D3D12_RASTERIZER_DESC rasterDesc{};
		rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
		rasterDesc.CullMode = D3D12_CULL_MODE_NONE;
		rasterDesc.DepthClipEnable = TRUE;
		desc.RasterizerState = rasterDesc;
		D3D12_DEPTH_STENCIL_DESC dsDesc{};
		dsDesc.DepthEnable = FALSE;
		dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		dsDesc.StencilEnable = FALSE;
		desc.DepthStencilState = dsDesc;
		desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = rtvFormat;
		for (int i = 1; i < 8; ++i) {
			desc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
		}
		desc.SampleDesc.Count = 1;
		desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.InputLayout.pInputElementDescs = nullptr;
		desc.InputLayout.NumElements = 0;
		ComPtr<ID3D12PipelineState> pso;
		HRESULT h = device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pso.GetAddressOf()));

		if (FAILED(h) || pso == nullptr) {
			Log(logStream, std::format("{} PSO Create failed. hr=0x{:08X}", psoName, static_cast<uint32_t>(h)));
		}

		return pso;
	};

	ComPtr<ID3D12PipelineState> toneMappingPipelineState = CreatePostProcessPSO(
		"ToneMapping", toneMappingPixelShaderBlob.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
	Log(std::string("ToneMap VS size=") + std::to_string(fullscreenVertexShaderBlob->GetBufferSize()) +
	     " PS size=" + std::to_string(toneMappingPixelShaderBlob->GetBufferSize()) +
	     " PSO=" + std::string(toneMappingPipelineState ? "non-null" : "NULL"));
	if (toneMappingPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> bloomExtractPipelineState = CreatePostProcessPSO(
		"BloomExtract", bloomExtractPixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (bloomExtractPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> bloomBlurPipelineState = CreatePostProcessPSO(
		"BloomBlur", bloomBlurPixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (bloomBlurPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> fxaaPipelineState = CreatePostProcessPSO(
		"FXAA", fxaaPixelShaderBlob.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
	if (fxaaPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> ssaoPipelineState = CreatePostProcessPSO(
		"GTAO", ssaoPixelShaderBlob.Get(), DXGI_FORMAT_R8_UNORM);
	if (ssaoPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> ssaoBlurPipelineState = CreatePostProcessPSO(
		"ContactShadow", ssaoBlurPixelShaderBlob.Get(), DXGI_FORMAT_R8_UNORM);
	if (ssaoBlurPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> skyboxPipelineState = CreatePostProcessPSO(
		"Skybox", skyboxPixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (skyboxPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> planarReflectionPipelineState = CreatePostProcessPSO(
		"PlanarReflectionComposite", planarReflectionPixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (planarReflectionPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> sharpenPipelineState = CreatePostProcessPSO(
		"Sharpen", sharpenPixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (sharpenPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ModelData modelData = LoadObjFile("resources", "plane.obj");
	// modelData は旧プレビューと既定アセチE��用に読み込む plane.obj の頂点チE�Eタ、E
	constexpr uint32_t kSubdivision = 64; // kSubdivision は旧琁E�Eレビューを作る緯度・経度刁E��数、E
	constexpr float kLonEvery = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kSubdivision);
	// kLonEvery / kLatEvery は琁E��チE��ュ 1 セグメントあたりの角度、E
	constexpr float kLatEvery = std::numbers::pi_v<float> / static_cast<float>(kSubdivision);

	std::vector<VertexData> vertices; // vertices は旧琁E�Eレビュー用に CPU で生�Eする三角形頂点列、E
	vertices.reserve(kSubdivision * kSubdivision * 6);
	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		float lat = -std::numbers::pi_v<float> / 2.0f + kLatEvery * static_cast<float>(latIndex);
		// lat / latNext は現在セルの下�E・上�Eの緯度角、E
		float latNext = lat + kLatEvery;

		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			float lon = kLonEvery * static_cast<float>(lonIndex) + std::numbers::pi_v<float>;
			// lon / lonNext は現在セルの左側・右側の経度角、E
			float lonNext = lon + kLonEvery;

			float u0 = static_cast<float>(lonIndex) / static_cast<float>(kSubdivision);
			// u0/u1/v0/v1 は琁E��に uvChecker を貼るため�E UV 篁E��、E
			float u1 = static_cast<float>(lonIndex + 1) / static_cast<float>(kSubdivision);
			float v0 = 1.0f - static_cast<float>(latIndex) / static_cast<float>(kSubdivision);
			float v1 = 1.0f - static_cast<float>(latIndex + 1) / static_cast<float>(kSubdivision);

			// a/b/c/d は琁E�� 1 セルの四隅。position と normal は単位球座標から作る、E
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

			vertices.push_back(a); // 四角形セルめE2 枚�E三角形に刁E��て VertexBuffer へ追加する、E
			vertices.push_back(b);
			vertices.push_back(c);
			vertices.push_back(c);
			vertices.push_back(b);
			vertices.push_back(d);
		}
	}

	// sprite は旧 Sprite プレビューの基準位置とサイズ、E
	Sprite sprite{
		.position = {128.0f, 128.0f},
		.size = {256.0f, 256.0f}
	};

	// spriteVertices は中忁E��点の四角形を表ぁE4 頂点、E
	VertexData spriteVertices[] = {
		{{-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
		{{-0.5f, 0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
		{{0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
		{{0.5f, 0.5f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
	};

	// spriteIndices は四角形めE2 三角形で描くための IndexBuffer、E
	uint32_t spriteIndices[] = {
		0, 1, 2,
		2, 1, 3,
	};

	// transform は旧 3D モチE��プレビューの初期 Transform、E
	Transforms transform{
		.scale = {0.55f, 0.55f, 0.55f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, 0.0f}
	};

	// spriteTransform は旧 Sprite プレビューの Transform。Sprite サイズめEscale に入れる、E
	Transforms spriteTransform{
		.scale = {sprite.size.x, sprite.size.y, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {sprite.position.x, sprite.position.y, 0.0f}
	};

	// cameraTransform は SceneView 用エチE��ターカメラの初期位置、E
	Transforms cameraTransform{
		.scale = {1.0f, 1.0f, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, -5.0f}
	};

	// uvTransform は Material の UV 変換行�Eを作るための初期 Transform、E
	Transforms uvTransform{
		.scale = {1.0f, 1.0f, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, 0.0f}
	};

	ID3D12Resource* vertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * vertices.size());
	// vertexResource は旧琁E�Eレビューの頂点めEGPU へ渡ぁEUpload Buffer、E
	VertexData* mappedVertexData = nullptr; // mappedVertexData は vertexResource に CPU から頂点を書き込むためのポインタ、E
	hr = vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedVertexData, vertices.data(), sizeof(VertexData) * vertices.size());

	// vertexBufferView は旧琁E�Eレビューの頂点 Buffer めEDraw に渡す情報、E
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// modelVertexResource は plane.obj の頂点めEGPU へ渡ぁEUpload Buffer、E
	ID3D12Resource* modelVertexResource = CreateBufferResource(device.Get(),
	                                                           sizeof(VertexData) * modelData.vertices.size());

	VertexData* mappedModelVertexData = nullptr;
	// mappedModelVertexData は modelVertexResource に CPU から頂点を書き込むためのポインタ、E
	hr = modelVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedModelVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedModelVertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());

	// modelVertexBufferView は plane.obj の頂点 Buffer めEDraw に渡す情報、E
	D3D12_VERTEX_BUFFER_VIEW modelVertexBufferView{};
	modelVertexBufferView.BufferLocation = modelVertexResource->GetGPUVirtualAddress();
	modelVertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelData.vertices.size());
	modelVertexBufferView.StrideInBytes = sizeof(VertexData);

	ModelData primitiveModelData[kEditorModelMeshTypeCount]{}; // primitiveModelData は基本形ごとの CPU 頂点チE�Eタ、E
	ID3D12Resource* primitiveVertexResources[kEditorModelMeshTypeCount] = {};
	// primitiveVertexResources は基本形ごとの GPU 頂点 Buffer、E
	D3D12_VERTEX_BUFFER_VIEW primitiveVertexBufferViews[kEditorModelMeshTypeCount]{};
	// primitiveVertexBufferViews は Draw 時に IA へ渡ぁEBufferView、E
	uint32_t primitiveVertexCounts[kEditorModelMeshTypeCount] = {}; // primitiveVertexCounts は DrawInstanced の頂点数、E
	CreatePrimitiveMeshBuffers(
		device.Get(),
		modelData,
		primitiveModelData,
		primitiveVertexResources,
		primitiveVertexBufferViews,
		primitiveVertexCounts);

	ID3D12Resource* spriteVertexResource = CreateBufferResource(device.Get(), sizeof(spriteVertices));
	// spriteVertexResource は Sprite 四角形の頂点めEGPU へ渡ぁEUpload Buffer、E
	VertexData* mappedSpriteVertexData = nullptr;
	// mappedSpriteVertexData は Sprite 頂点を書き込むための CPU mapped ポインタ、E
	hr = spriteVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedSpriteVertexData, spriteVertices, sizeof(spriteVertices));

	// spriteVertexBufferView は Sprite 頂点 Buffer めEDraw に渡す情報、E
	D3D12_VERTEX_BUFFER_VIEW spriteVertexBufferView{};
	spriteVertexBufferView.BufferLocation = spriteVertexResource->GetGPUVirtualAddress();
	spriteVertexBufferView.SizeInBytes = sizeof(spriteVertices);
	spriteVertexBufferView.StrideInBytes = sizeof(VertexData);

	ID3D12Resource* spriteIndexResource = CreateBufferResource(device.Get(), sizeof(spriteIndices));
	// spriteIndexResource は Sprite 四角形の IndexBuffer、E
	uint32_t* mappedSpriteIndexData = nullptr; // mappedSpriteIndexData は Sprite Index めECPU から書き込むためのポインタ、E
	hr = spriteIndexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteIndexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedSpriteIndexData, spriteIndices, sizeof(spriteIndices));

	// spriteIndexBufferView は Sprite IndexBuffer めEDrawIndexed に渡す情報、E
	D3D12_INDEX_BUFFER_VIEW spriteIndexBufferView{};
	spriteIndexBufferView.BufferLocation = spriteIndexResource->GetGPUVirtualAddress();
	spriteIndexBufferView.SizeInBytes = sizeof(spriteIndices);
	spriteIndexBufferView.Format = DXGI_FORMAT_R32_UINT;

	constexpr float editorMenuHeight = 20.0f;
	// editorMenuHeight / editorSceneHeaderHeight は初期 SceneView 矩形の Y 座標計算に使ぁE��定高さ、E
	constexpr float editorSceneHeaderHeight = 24.0f;

	float editorWindowWidth = static_cast<float>(renderWidth);
	// editorWindowWidth / Height は ImGui と SceneView 用に float で保持する Window サイズ、E
	float editorWindowHeight = static_cast<float>(renderHeight);

	float editorLeftWidth = 250.0f; // editorLeft/Right/Bottom は初期 Docking 前�E吁E��ネル幁E�E高さ、E
	float editorRightWidth = 320.0f;
	float editorBottomHeight = 190.0f;

	float editorSceneX = editorLeftWidth;
	// editorSceneX/Y/Width/Height は DirectX viewport めESceneView に合わせるための矩形、E
	float editorSceneY = editorMenuHeight + editorSceneHeaderHeight;
	float editorSceneWidth =
		editorWindowWidth - editorLeftWidth - editorRightWidth;
	float editorSceneHeight =
		editorWindowHeight - editorSceneY - editorBottomHeight;
	auto updateEditorLayout = [&]() {
		editorLeftWidth = (std::clamp)(editorLeftWidth, 160.0f, 420.0f);
		// パネル幁E�E高さに下限上限を持たせ、SceneView が極端に潰れなぁE��ぁE��する、E
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

	// viewport は DirectX が描画する画面上�E矩形、E
	D3D12_VIEWPORT viewport{};
	viewport.TopLeftX = editorSceneX;
	viewport.TopLeftY = editorSceneY;
	viewport.Width = editorWindowWidth;
	viewport.Height = editorWindowHeight;
	viewport.MaxDepth = 1.0f;
	viewport.Width = editorSceneWidth;
	viewport.Height = editorSceneHeight;

	// scissorRect は SceneView の外へ描画しなぁE��め�E刁E��取り矩形、E
	D3D12_RECT scissorRect{};
	scissorRect.left = static_cast<LONG>(editorSceneX);
	scissorRect.top = static_cast<LONG>(editorSceneY);
	scissorRect.right = static_cast<LONG>(editorSceneX + editorSceneWidth);
	scissorRect.bottom = static_cast<LONG>(editorSceneY + editorSceneHeight);

	// textureFilePaths は起動時に GPU へアチE�Eロードする標準テクスチャ一覧、E
	std::wstring textureFilePaths[] = {
		L"resources/uvChecker.png",
		L"resources/monsterBall.png",
		ConvertString(modelData.material.textureFilePath),
		L"resources/ball.png",
	};

	std::string textureFilePathStrings[_countof(textureFilePaths)];
	// textureFilePathStrings は Project パネルで扱ぁE��すい UTF-8 版パス、E
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		textureFilePathStrings[textureIndex] = ConvertString(textureFilePaths[textureIndex]);
	}
	std::vector<std::string> editorTextureFilePaths;
	editorTextureFilePaths.reserve(_countof(textureFilePaths));
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		editorTextureFilePaths.push_back(textureFilePathStrings[textureIndex]);
	}

	DirectX::ScratchImage mipImages[_countof(textureFilePaths)];
	// mipImages は DirectXTex が生成しぁEmipmap 付き画像データ、E
	DirectX::TexMetadata textureMetadatas[_countof(textureFilePaths)];
	// textureMetadatas は吁ETexture のサイズ、形式、mip 数、E

	// textureResources は GPU 上�E Texture 実体、E
	ID3D12Resource* textureResources[_countof(textureFilePaths)] = {nullptr};
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		mipImages[textureIndex] = LoadTexture(textureFilePaths[textureIndex]);
		textureMetadatas[textureIndex] = mipImages[textureIndex].GetMetadata();
		textureResources[textureIndex] = CreateTextureResource(device.Get(), textureMetadatas[textureIndex]);
	}

	// cameraMatrix はエチE��ターカメラ Transform から作るワールド行�E、E
	Matrix4x4 cameraMatrix = MakeAffineMatrix(
		cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);

	Matrix4x4 viewMatrix = Inverse(cameraMatrix); // viewMatrix は cameraMatrix の送E���E。ワールド座標をカメラ空間へ移す、E

	// projectionMatrix は SceneView の 3D 表示用透視投影行�E、E
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(
		0.45f,
		editorSceneWidth / editorSceneHeight,
		0.1f,
		100.0f);

	// spriteProjectionMatrix は 2D Sprite を画面座標で描くための正封E��行�E、E
	Matrix4x4 spriteProjectionMatrix = MakeOrthographicMatrix(
		0.0f,
		0.0f,
		editorWindowWidth,
		editorWindowHeight,
		0.0f,
		100.0f);

	float editorCameraMoveSpeed = 0.12f; // editorCamera*Speed は Inspector から調整できる Scene カメラ操作速度、E
	float editorCameraRotateSpeed = 0.006f;
	float editorCameraWheelMoveSpeed = 0.5f;
	float editorCameraPanSpeed = 0.01f;
	float editorCameraFastRate = 4.0f;

	// sceneClearColor は SceneView の背景色 RGBA、E
	float sceneClearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};

	bool isSceneGizmoVisible = true; // is*GizmoVisible は SceneView 上�E補助表示を�Eり替えるフラグ、E
	bool isLightGizmoVisible = false;
	bool isCameraGizmoVisible = false;

	// directionalLightIconPosition はライトアイコンめESceneView に表示するワールド座標、E
	Vector3 directionalLightIconPosition = {-1.8f, 1.4f, 0.0f};

	EditorSceneObjectManager editorSceneObjectManager;
	// editorSceneObjectManager は GameObject に対応すめEDirectX 描画用 SceneObject を保持する、E
	editorSceneObjectManager.Initialize(device.Get());

	std::vector<EditorSceneObject>& editorSceneObjects = editorSceneObjectManager.GetSceneObjects();
	// editorSceneObjects は SceneObjectManager 冁E��配�Eへの参�E、E
	int32_t selectedPlacedSceneObjectIndex = -1;
	// selectedPlacedSceneObjectIndex は SceneObject 配�Eの選択中 index、E1 は未選択、E
	ComPtr<ID3D12Fence> fence; // fence は CPU ぁEGPU 処琁E��亁E��征E��ための同期オブジェクト、E
	uint64_t fenceValue = 0; // fenceValue は Signal ごとに進める GPU 完亁E��認用カウンタ、E
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
	assert(SUCCEEDED(hr));

	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	// fenceEvent は GPU 完亁E��知めECPU ぁEWaitForSingleObject で征E��ための Win32 Event、E
	assert(fenceEvent != nullptr);

	// Event 作�Eに失敗すると GPU 征E��ができなぁE��め、描画開始前に終亁E��る、E
	if (fenceEvent == nullptr) {
		RequestInitializationFailure();
		return;
	}

	auto waitForGpu = [&]() {
		fenceValue++; // fenceValue を進め、今回征E�� GPU 作業番号を作る、E
		HRESULT signalResult = commandQueue->Signal(fence.Get(), fenceValue);
		// Signal は commandQueue に現在の fenceValue を完亁E��定として登録する、E
		assert(SUCCEEDED(signalResult));

		// GPU がまだ持E��値まで終わってぁE��ければ、Event を登録して CPU を征E��させる、E
		if (fence->GetCompletedValue() < fenceValue) {
			HRESULT eventResult = fence->SetEventOnCompletion(fenceValue, fenceEvent);
			assert(SUCCEEDED(eventResult));
			WaitForSingleObject(fenceEvent, INFINITE);
		}
	};

	auto resizeRenderTargets = [&](uint32_t width, uint32_t height) {
		// 既に同じサイズなめESwapChain と DepthStencil を作り直さなぁE��E
		if (renderWidth == width && renderHeight == height) {
			return;
		}

		waitForGpu(); // ResizeBuffers 前に GPU が古ぁEback buffer を使ぁE��わるまで征E��、E

		for (ID3D12Resource*& swapChainResource : swapChainResources) {
			// 古ぁESwapChain buffer は ResizeBuffers 前に忁E�� Release する、E
			if (swapChainResource != nullptr) {
				swapChainResource->Release();
				swapChainResource = nullptr;
			}
		}

		// 古ぁEDepthStencil も描画サイズが変わるため作り直す、E
		if (depthStencilResource != nullptr) {
			depthStencilResource->Release();
			depthStencilResource = nullptr;
		}

		renderWidth = width; // renderWidth / renderHeight は新しい SwapChain サイズ、E
		renderHeight = height;

		// ResizeBuffers は SwapChain の back buffer 実体を新しいサイズで再生成する、E
		HRESULT resizeResult = swapChain->ResizeBuffers(
			swapChainDesc.BufferCount,
			renderWidth,
			renderHeight,
			swapChainDesc.Format,
			0);
		assert(SUCCEEDED(resizeResult));

		for (uint32_t bufferIndex = 0; bufferIndex < swapChainDesc.BufferCount; ++bufferIndex) {
			// Resize 後�E新しい back buffer を取得して RTV を張り直す、E
			HRESULT getBufferResult =
				swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapChainResources[bufferIndex]));
			assert(SUCCEEDED(getBufferResult));
			device->CreateRenderTargetView(swapChainResources[bufferIndex], &rtvDesc, rtvHandles[bufferIndex]);
		}

		depthStencilResource = createDepthStencilResource(renderWidth, renderHeight);
		// DepthStencil も新しい renderWidth / renderHeight に合わせて再生成する、E
		device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandle);
	};

	hr = commandAllocator->Reset(); // チE��スチャアチE�Eロード用に CommandAllocator と CommandList を記録可能状態へ戻す、E
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr);
	assert(SUCCEEDED(hr));

	// intermediateResources は Texture upload のための一晁EUpload Buffer、E
	ID3D12Resource* intermediateResources[_countof(textureFilePaths)] = {nullptr};
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		// UploadTextureData は textureResources に mipImages をコピ�Eする命令めECommandList へ積�E、E
		intermediateResources[textureIndex] = UploadTextureData(
			device.Get(), commandList.Get(), textureResources[textureIndex], mipImages[textureIndex]);
	}

	UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// srvDescriptorSize は SRV Heap 冁E��次の Descriptor へ進むバイト幁E��E
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandlesCPU[_countof(textureFilePaths)];
	// textureSrvHandlesCPU は CreateShaderResourceView に渡ぁECPU 側 SRV ハンドル、E
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandlesGPU[_countof(textureFilePaths)];
	// textureSrvHandlesGPU は Draw 時に Shader へ渡ぁEGPU 側 SRV ハンドル、E
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		// srvDesc は 2D Texture SRV として mipmap 付き画像を Shader から読む設定、E
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = textureMetadatas[textureIndex].format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(textureMetadatas[textureIndex].mipLevels);

		// index + 1 にするのは 0 番めEImGui 用 SRV に空けるため、E
		textureSrvHandlesCPU[textureIndex] = GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize,
		                                                            textureIndex + 1);
		textureSrvHandlesGPU[textureIndex] = GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize,
		                                                            textureIndex + 1);
		device->CreateShaderResourceView(textureResources[textureIndex], &srvDesc, textureSrvHandlesCPU[textureIndex]);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE environmentTextureSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap,
		srvDescriptorSize,
		kRuntimeEnvironmentSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE environmentTextureSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap,
		srvDescriptorSize,
		kRuntimeEnvironmentSrvDescriptorIndex);

	hr = commandList->Close(); // Texture upload 用 CommandList を閉じて GPU に実行させる、E
	assert(SUCCEEDED(hr));

	// uploadCommandLists は ExecuteCommandLists に渡ぁECommandList 配�E、E
	ID3D12CommandList* uploadCommandLists[] = {commandList.Get()};
	commandQueue->ExecuteCommandLists(1, uploadCommandLists);

	fenceValue++; // 初期アチE�E��ードが完亁E��るまで征E��、以降�E描画で Texture を安�Eに参�Eする、E
	hr = commandQueue->Signal(fence.Get(), fenceValue);
	assert(SUCCEEDED(hr));
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		assert(SUCCEEDED(hr));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	commandAllocator->Reset();
	commandList->Reset(commandAllocator.Get(), nullptr);

	//================================================================
	// IBL (Image-Based Lighting) Texture 読み込み
	//================================================================

	D3D12_CPU_DESCRIPTOR_HANDLE iblIrradianceSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap, srvDescriptorSize, kRuntimeIblIrradianceSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE iblIrradianceSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap, srvDescriptorSize, kRuntimeIblIrradianceSrvDescriptorIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE iblPrefilterSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap, srvDescriptorSize, kRuntimeIblPrefilterSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE iblPrefilterSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap, srvDescriptorSize, kRuntimeIblPrefilterSrvDescriptorIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE iblEnvironmentSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap, srvDescriptorSize, kRuntimeIblEnvironmentSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE iblEnvironmentSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap, srvDescriptorSize, kRuntimeIblEnvironmentSrvDescriptorIndex);
	D3D12_CPU_DESCRIPTOR_HANDLE iblBrdfLutSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap, srvDescriptorSize, kRuntimeIblBrdfLutSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE iblBrdfLutSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap, srvDescriptorSize, kRuntimeIblBrdfLutSrvDescriptorIndex);

	ID3D12Resource* iblIrradianceCube = nullptr;
	ID3D12Resource* iblPrefilterCube = nullptr;
	ID3D12Resource* iblEnvironmentCube = nullptr;
	ID3D12Resource* iblBrdfLut = nullptr;
	uint32_t iblPrefilterMipCount = 0;

	std::wstring iblDir = L"Assets/Textures/IBL/Studio/";
	std::wstring iblFiles[] = {
		iblDir + L"irradiance_cube.dds",
		iblDir + L"prefilter_cube.dds",
		iblDir + L"environment_cube.dds",
		iblDir + L"brdf_lut.dds",
	};

	// DDS が存在すれば読み込む
	auto loadIblCube = [&](const std::wstring& path, ID3D12Resource*& outRes, D3D12_CPU_DESCRIPTOR_HANDLE srvCPU, uint32_t* mipCount) {
		if (!std::filesystem::exists(path)) return;
		DirectX::ScratchImage img = LoadTexture(path);
		const auto& meta = img.GetMetadata();
		outRes = CreateTextureResource(device.Get(), meta);
		UploadTextureData(device.Get(), commandList.Get(), outRes, img);
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = meta.format;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		bool isCube = (meta.arraySize >= 6);
		srv.ViewDimension = isCube ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
		if (isCube) srv.TextureCube.MipLevels = static_cast<UINT>(meta.mipLevels);
		else srv.Texture2D.MipLevels = static_cast<UINT>(meta.mipLevels);
		device->CreateShaderResourceView(outRes, &srv, srvCPU);
		if (mipCount) *mipCount = static_cast<uint32_t>(meta.mipLevels);
	};

	auto makePlaceholder = [&](uint32_t w, uint32_t h, uint32_t arraySize, DXGI_FORMAT fmt, bool isCube, ID3D12Resource*& outRes, D3D12_CPU_DESCRIPTOR_HANDLE srvCPU) {
		D3D12_RESOURCE_DESC d{};
		d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		d.Width = w; d.Height = h; d.MipLevels = 1;
		d.DepthOrArraySize = static_cast<UINT16>(arraySize);
		d.Format = fmt; d.SampleDesc.Count = 1;
		D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
		HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&outRes));
		if (FAILED(hr) || !outRes) { outRes = nullptr; return; }
		uint32_t bytesPerPixel = (fmt == DXGI_FORMAT_R16G16_FLOAT) ? 4 : 8;
		uint32_t pitch = w * bytesPerPixel;
		uint32_t sliceSize = pitch * h;
		uint32_t totalSize = sliceSize * arraySize;
		std::vector<uint8_t> data(totalSize);
		// Fill with neutral gray: (0.5,0.5,0.5,1.0) for RGBA or (1.0,0.0) for RG
		if (fmt == DXGI_FORMAT_R16G16_FLOAT) {
			const uint16_t half1 = 0x3C00; // 1.0 in half-float
			const uint16_t half0 = 0x0000; // 0.0 in half-float
			uint8_t pix[4];
			memcpy(pix + 0, &half1, 2);
			memcpy(pix + 2, &half0, 2);
			for (size_t j = 0; j < totalSize; j += 4)
				memcpy(&data[j], pix, 4);
		} else {
			const uint16_t half05 = 0x3800; // 0.5 in half-float
			const uint16_t half1  = 0x3C00; // 1.0 in half-float
			uint8_t pix[8];
			memcpy(pix + 0, &half05, 2);
			memcpy(pix + 2, &half05, 2);
			memcpy(pix + 4, &half05, 2);
			memcpy(pix + 6, &half1, 2);
			for (size_t j = 0; j < totalSize; j += 8)
				memcpy(&data[j], pix, 8);
		}
		UINT subResourceCount = arraySize;
		std::vector<D3D12_SUBRESOURCE_DATA> srd(subResourceCount);
		for (UINT i = 0; i < subResourceCount; ++i) {
			srd[i].pData = data.data();
			srd[i].RowPitch = pitch;
			srd[i].SlicePitch = sliceSize;
		}
		UINT64 uploadBufferSize = GetRequiredIntermediateSize(outRes, 0, subResourceCount);
		ID3D12Resource* uploadHeap = nullptr;
		D3D12_HEAP_PROPERTIES uploadHp{}; uploadHp.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bufDesc{};
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Width = uploadBufferSize;
		bufDesc.Height = 1;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.MipLevels = 1;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufDesc.SampleDesc.Count = 1;
		if (SUCCEEDED(device->CreateCommittedResource(&uploadHp, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadHeap))) && uploadHeap) {
			UpdateSubresources(commandList.Get(), outRes, uploadHeap, 0, 0, subResourceCount, srd.data());
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = outRes;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			commandList->ResourceBarrier(1, &barrier);
		}
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = fmt;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.ViewDimension = isCube ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
		if (isCube) srv.TextureCube.MipLevels = 1;
		else srv.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(outRes, &srv, srvCPU);
	};

	loadIblCube(iblFiles[0], iblIrradianceCube, iblIrradianceSrvHandleCPU, nullptr);
	if (iblIrradianceCube == nullptr) makePlaceholder(2, 2, 6, DXGI_FORMAT_R16G16B16A16_FLOAT, true, iblIrradianceCube, iblIrradianceSrvHandleCPU);

	loadIblCube(iblFiles[1], iblPrefilterCube, iblPrefilterSrvHandleCPU, &iblPrefilterMipCount);
	if (iblPrefilterCube == nullptr) makePlaceholder(2, 2, 6, DXGI_FORMAT_R16G16B16A16_FLOAT, true, iblPrefilterCube, iblPrefilterSrvHandleCPU);

	loadIblCube(iblFiles[2], iblEnvironmentCube, iblEnvironmentSrvHandleCPU, nullptr);
	if (iblEnvironmentCube == nullptr) makePlaceholder(2, 2, 6, DXGI_FORMAT_R16G16B16A16_FLOAT, true, iblEnvironmentCube, iblEnvironmentSrvHandleCPU);

	loadIblCube(iblFiles[3], iblBrdfLut, iblBrdfLutSrvHandleCPU, nullptr);
	if (iblBrdfLut == nullptr) makePlaceholder(2, 2, 1, DXGI_FORMAT_R16G16_FLOAT, false, iblBrdfLut, iblBrdfLutSrvHandleCPU);

	// IBL Upload を実行・同期（DDS があってもプレースホルダーでも Upload が発生している）
	{
		hr = commandList->Close();
		assert(SUCCEEDED(hr));
		ID3D12CommandList* uploadLists[] = { commandList.Get() };
		commandQueue->ExecuteCommandLists(1, uploadLists);
		fenceValue++;
		hr = commandQueue->Signal(fence.Get(), fenceValue);
		assert(SUCCEEDED(hr));
		if (fence->GetCompletedValue() < fenceValue) {
			hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
			assert(SUCCEEDED(hr));
			WaitForSingleObject(fenceEvent, INFINITE);
		}
		commandAllocator->Reset();
		commandList->Reset(commandAllocator.Get(), nullptr);
	}

#ifdef USE_IMGUI

	IMGUI_CHECKVERSION(); // ImGui のバ�Eジョン整合性を確認してから Context を作る、E
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO(); // io は Docking 有効化や Font 設定を行う ImGui の入出力設定、E
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // DockingEnable でウィンドウのドラチE��移動�Eドッキングを許可する、E
	io.ConfigDockingWithShift = false; // Shift なしで Docking できるようにして Unity 風の操作感にする、E
	ImGui::StyleColorsDark(); // 既孁EUI と合わせて Dark Style を基準にする、E
	ImGui_ImplWin32_Init(windowHandle); // Win32 backend は HWND からマウス・キーボ�Eド�E力を受け取る、E

	// DX12 backend は SRV Heap 0 番めEImGui Font Texture 用に使ぁE��E
	ImGui_ImplDX12_Init(
		device.Get(),
		static_cast<int>(swapChainDesc.BufferCount),
		rtvDesc.Format,
		srvDescriptorHeap,
		GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 0),
		GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 0));

	// meiryo.ttc があれ�E日本誁EUI が文字化けしなぁE��ぁE��読み込む、E
	if (std::filesystem::exists("C:/Windows/Fonts/meiryo.ttc")) {
		io.Fonts->AddFontFromFileTTF(
			"C:/Windows/Fonts/meiryo.ttc", 16.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	}

	io.Fonts->Build(); // Font Atlas をここで構築し、最初�Eフレームで日本語フォントを使える状態にする、E
#endif

	g_instanceHandle = instanceHandle; // ここから下�E、Initialize 冁E�Eローカル生�E物めEEditorSharedState の共有状態へ移す、E
	g_logStream = std::move(logStream);
	g_windowHandle = windowHandle;
	g_hr = hr;
	g_directInput = directInput;
	g_keyboardDevice = keyboardDevice;
	g_mouseDevice = mouseDevice;
	std::memcpy(g_key, key, sizeof(g_key));
	std::memcpy(g_preKey, preKey, sizeof(g_preKey));
	g_message = message;
	g_xAudio2 = xAudio2;
	g_masterVoice = masterVoice;
	g_soundData = soundData;
	g_sourceVoice = sourceVoice;
	g_dxgiFactory = dxgiFactory;
	g_useAdapter = useAdapter;
	g_device = device;
	g_commandQueue = commandQueue;
	g_commandAllocator = commandAllocator;
	g_commandList = commandList;
	g_swapChain = swapChain;
	g_swapChainDesc = swapChainDesc;
	g_rtvDescriptorHeap = rtvDescriptorHeap;
	g_srvDescriptorHeap = srvDescriptorHeap;
	g_dsvDescriptorHeap = dsvDescriptorHeap;
	for (uint32_t bufferIndex = 0; bufferIndex < kRuntimeSwapChainBufferCount; bufferIndex++) {
		g_swapChainResources[bufferIndex] = swapChainResources[bufferIndex];
		g_rtvHandles[bufferIndex] = rtvHandles[bufferIndex];
	}
	g_rtvDesc = rtvDesc;
	g_depthClearValue = depthClearValue;
	g_dsvDesc = dsvDesc;
	g_dsvHandle = dsvHandle;
	g_shadowDsvHandle = shadowDsvHandle;
	g_depthStencilResource = depthStencilResource;
	g_depthSrvHandleCPU = depthSrvHandleCPU;
	g_depthSrvHandleGPU = depthSrvHandleGPU;
	g_shadowMapResource = shadowMapResource;
	g_shadowMapSrvCpuHandle = shadowMapSrvCpuHandle;
	g_shadowMapSrvGpuHandle = shadowMapSrvGpuHandle;
	g_renderWidth = renderWidth;
	g_renderHeight = renderHeight;
	g_hdrRenderTarget = hdrRenderTarget;
	g_hdrRtvHandle = hdrRtvHandle;
	g_hdrSrvHandleCPU = hdrSrvHandleCPU;
	g_hdrSrvHandleGPU = hdrSrvHandleGPU;
	for (uint32_t i = 0; i < 2; i++) {
		g_bloomRenderTargets[i] = bloomRenderTargets[i];
		g_bloomRtvHandles[i] = bloomRtvHandles[i];
		g_bloomSrvHandlesCPU[i] = bloomSrvHandlesCPU[i];
		g_bloomSrvHandlesGPU[i] = bloomSrvHandlesGPU[i];
	}
	g_postProcessRenderTarget = postProcessRenderTarget;
	g_postProcessRtvHandle = postProcessRtvHandle;
	g_postProcessSrvHandleCPU = postProcessSrvHandleCPU;
	g_postProcessSrvHandleGPU = postProcessSrvHandleGPU;
	for (uint32_t i = 0; i < 2; i++) {
		g_ssaoRenderTargets[i] = ssaoRenderTargets[i];
		g_ssaoRtvHandles[i] = ssaoRtvHandles[i];
		g_ssaoSrvHandlesCPU[i] = ssaoSrvHandlesCPU[i];
		g_ssaoSrvHandlesGPU[i] = ssaoSrvHandlesGPU[i];
	}
	g_hdrCompositeRenderTarget = hdrCompositeRenderTarget;
	g_hdrCompositeRtvHandle = hdrCompositeRtvHandle;
	g_hdrCompositeSrvHandleCPU = hdrCompositeSrvHandleCPU;
	g_hdrCompositeSrvHandleGPU = hdrCompositeSrvHandleGPU;
	g_materialMaskRenderTarget = materialMaskRenderTarget;
	g_materialMaskRtvHandle = materialMaskRtvHandle;
	g_materialMaskSrvHandleCPU = materialMaskSrvHandleCPU;
	g_materialMaskSrvHandleGPU = materialMaskSrvHandleGPU;
	g_planarReflectionRenderTarget = planarReflectionRenderTarget;
	g_planarReflectionRtvHandle = planarReflectionRtvHandle;
	g_planarReflectionSrvHandleCPU = planarReflectionSrvHandleCPU;
	g_planarReflectionSrvHandleGPU = planarReflectionSrvHandleGPU;
	g_dxcUtils = dxcUtils;
	g_dxcCompiler = dxcCompiler;
	g_includeHandler = includeHandler;
	g_vertexShaderBlob = vertexShaderBlob;
	g_pixelShaderBlob = pixelShaderBlob;
	g_objectReflectionMaskPixelShaderBlob = objectReflectionMaskPixelShaderBlob;
	g_shadowVertexShaderBlob = shadowVertexShaderBlob;
	g_fullscreenVertexShaderBlob = fullscreenVertexShaderBlob;
	g_toneMappingPixelShaderBlob = toneMappingPixelShaderBlob;
	g_bloomExtractPixelShaderBlob = bloomExtractPixelShaderBlob;
	g_bloomBlurPixelShaderBlob = bloomBlurPixelShaderBlob;
	g_fxaaPixelShaderBlob = fxaaPixelShaderBlob;
	g_ssaoPixelShaderBlob = ssaoPixelShaderBlob;
	g_ssaoBlurPixelShaderBlob = ssaoBlurPixelShaderBlob;
	g_skyboxPixelShaderBlob = skyboxPixelShaderBlob;
	g_planarReflectionPixelShaderBlob = planarReflectionPixelShaderBlob;
	g_sharpenPixelShaderBlob = sharpenPixelShaderBlob;
	g_signatureBlob = signatureBlob;
	g_errorBlob = errorBlob;
	g_rootSignature = rootSignature;
	g_graphicsPipelineState = graphicsPipelineState;
	g_planarScenePipelineState = planarScenePipelineState;
	g_planarSurfacePipelineState = planarSurfacePipelineState;
	g_objectReflectionMaskPipelineState = objectReflectionMaskPipelineState;
	g_cullFrontPipelineState = cullFrontPipelineState;
	g_shadowPipelineState = shadowPipelineState;
	g_postProcessRootSignature = postProcessRootSignature;
	g_toneMappingPipelineState = toneMappingPipelineState;
	g_bloomExtractPipelineState = bloomExtractPipelineState;
	g_bloomBlurPipelineState = bloomBlurPipelineState;
	g_fxaaPipelineState = fxaaPipelineState;
	g_ssaoPipelineState = ssaoPipelineState;
	g_ssaoBlurPipelineState = ssaoBlurPipelineState;
	g_skyboxPipelineState = skyboxPipelineState;
	g_planarReflectionPipelineState = planarReflectionPipelineState;
	g_sharpenPipelineState = sharpenPipelineState;
	g_iblIrradianceCube = iblIrradianceCube;
	g_iblPrefilterCube = iblPrefilterCube;
	g_iblEnvironmentCube = iblEnvironmentCube;
	g_iblBRDFLUT = iblBrdfLut;
	g_iblIrradianceSrvHandleCPU = iblIrradianceSrvHandleCPU;
	g_iblIrradianceSrvHandleGPU = iblIrradianceSrvHandleGPU;
	g_iblPrefilterSrvHandleCPU = iblPrefilterSrvHandleCPU;
	g_iblPrefilterSrvHandleGPU = iblPrefilterSrvHandleGPU;
	g_iblEnvironmentSrvHandleCPU = iblEnvironmentSrvHandleCPU;
	g_iblEnvironmentSrvHandleGPU = iblEnvironmentSrvHandleGPU;
	g_iblBrdfLutSrvHandleCPU = iblBrdfLutSrvHandleCPU;
	g_iblBrdfLutSrvHandleGPU = iblBrdfLutSrvHandleGPU;
	g_iblPrefilterMipCount = iblPrefilterMipCount;
	g_spriteMaterialResource = spriteMaterialResource;
	g_spriteMaterialData = spriteMaterialData;
	g_sphereMaterialResource = sphereMaterialResource;
	g_sphereMaterialData = sphereMaterialData;
	g_directionalLightResource = directionalLightResource;
	g_directionalLightData = directionalLightData;
	g_emissiveLightResource = emissiveLightResource;
	g_emissiveLightData = emissiveLightData;
	g_spriteTransformationMatrixResource = spriteTransformationMatrixResource;
	g_spriteTransformationMatrixData = spriteTransformationMatrixData;
	g_sphereTransformationMatrixResource = sphereTransformationMatrixResource;
	g_sphereTransformationMatrixData = sphereTransformationMatrixData;
	g_modelData = std::move(modelData);
	for (size_t meshTypeIndex = 0; meshTypeIndex < kEditorModelMeshTypeCount; meshTypeIndex++) {
		g_editorPrimitiveModelData[meshTypeIndex] = std::move(primitiveModelData[meshTypeIndex]);
		g_editorPrimitiveVertexResources[meshTypeIndex] = primitiveVertexResources[meshTypeIndex];
		g_editorPrimitiveVertexBufferViews[meshTypeIndex] = primitiveVertexBufferViews[meshTypeIndex];
		g_editorPrimitiveVertexCounts[meshTypeIndex] = primitiveVertexCounts[meshTypeIndex];
	}
	g_vertices = std::move(vertices);
	g_sprite = sprite;
	for (uint32_t vertexIndex = 0; vertexIndex < _countof(g_spriteVertices); vertexIndex++) {
		g_spriteVertices[vertexIndex] = spriteVertices[vertexIndex];
	}
	for (uint32_t indexIndex = 0; indexIndex < kRuntimeSpriteIndexCount; indexIndex++) {
		g_spriteIndices[indexIndex] = spriteIndices[indexIndex];
	}
	g_transform = transform;
	g_spriteTransform = spriteTransform;
	g_cameraTransform = cameraTransform;
	g_uvTransform = uvTransform;
	g_vertexResource = vertexResource;
	g_modelVertexResource = modelVertexResource;
	g_spriteVertexResource = spriteVertexResource;
	g_spriteIndexResource = spriteIndexResource;
	g_vertexBufferView = vertexBufferView;
	g_modelVertexBufferView = modelVertexBufferView;
	g_spriteVertexBufferView = spriteVertexBufferView;
	g_spriteIndexBufferView = spriteIndexBufferView;
	g_editorWindowWidth = editorWindowWidth;
	g_editorWindowHeight = editorWindowHeight;
	g_editorLeftWidth = editorLeftWidth;
	g_editorRightWidth = editorRightWidth;
	g_editorBottomHeight = editorBottomHeight;
	g_editorSceneX = editorSceneX;
	g_editorSceneY = editorSceneY;
	g_editorSceneWidth = editorSceneWidth;
	g_editorSceneHeight = editorSceneHeight;
	g_viewport = viewport;
	g_scissorRect = scissorRect;
	g_cameraMatrix = cameraMatrix;
	g_viewMatrix = viewMatrix;
	g_projectionMatrix = projectionMatrix;
	g_spriteProjectionMatrix = spriteProjectionMatrix;
	g_editorCameraMoveSpeed = editorCameraMoveSpeed;
	g_editorCameraRotateSpeed = editorCameraRotateSpeed;
	g_editorCameraWheelMoveSpeed = editorCameraWheelMoveSpeed;
	g_editorCameraPanSpeed = editorCameraPanSpeed;
	g_editorCameraFastRate = editorCameraFastRate;
	std::memcpy(g_sceneClearColor, sceneClearColor, sizeof(g_sceneClearColor));
	g_isSceneGizmoVisible = isSceneGizmoVisible;
	g_isLightGizmoVisible = isLightGizmoVisible;
	g_isCameraGizmoVisible = isCameraGizmoVisible;
	g_directionalLightIconPosition = directionalLightIconPosition;
	g_editorSceneObjectManager = std::move(editorSceneObjectManager);
	g_selectedPlacedSceneObjectIndex = selectedPlacedSceneObjectIndex;
	g_fence = fence;
	g_fenceValue = fenceValue;
	g_fenceEvent = fenceEvent;
	for (uint32_t textureIndex = 0; textureIndex < kRuntimeTextureCount; textureIndex++) {
		g_textureFilePaths[textureIndex] = textureFilePaths[textureIndex];
		g_textureFilePathStrings[textureIndex] = textureFilePathStrings[textureIndex];
		g_textureMetadatas[textureIndex] = textureMetadatas[textureIndex];
		g_textureResources[textureIndex] = textureResources[textureIndex];
		g_intermediateResources[textureIndex] = intermediateResources[textureIndex];
		g_textureSrvHandlesCPU[textureIndex] = textureSrvHandlesCPU[textureIndex];
		g_textureSrvHandlesGPU[textureIndex] = textureSrvHandlesGPU[textureIndex];
	}
	g_editorTextureFilePaths = std::move(editorTextureFilePaths);
	g_srvDescriptorSize = srvDescriptorSize;
	g_environmentTextureSrvHandleCPU = environmentTextureSrvHandleCPU;
	g_environmentTextureSrvHandleGPU = environmentTextureSrvHandleGPU;
	g_environmentTextureAssetPath.clear();
	g_loadedEnvironmentTextureAssetPath.clear();
	g_isEnvironmentTextureDirty = false;
	g_feelKitHaptics.initialize({});
	g_isInitialized = true;
	g_isInitializationFailed = false;
	g_isEndRequested = false;
	Log(logStream, "EditorPlatformManager initialization completed");
}

void EditorPlatformManager::Update() {
	if (!g_isInitialized || g_isFinalized) {
		g_isEndRequested = true;
		return;
	}

	g_isDrawRequested = false;

	if (g_message.message == WM_QUIT) {
		g_exitCode = static_cast<int>(g_message.wParam);
		g_isEndRequested = true;
		return;
	}

	// ���T�C�Y��t���X�N���[���ؑւł� WM_SIZE / WM_PAINT ����C�ɗ��܂邽�߁A
	// 1 �t���[���ŃL���[����ɂ��āA�Â��E�B���h�E�T�C�Y�̂܂ܕ`�������Ȃ��悤�ɂ���B
	while (PeekMessage(&g_message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
		TranslateMessage(&g_message);
		DispatchMessage(&g_message);

		if (g_message.message == WM_QUIT) {
			g_exitCode = static_cast<int>(g_message.wParam);
			g_isEndRequested = true;
			break;
		}
	}
}

void EditorPlatformManager::Draw() {
}

bool EditorPlatformManager::IsEndRequested() const {
	return g_isEndRequested;
}

bool EditorPlatformManager::HasInitializationFailed() const {
	return g_isInitializationFailed;
}

int EditorPlatformManager::Finalize() {
	auto& hr = g_hr;
	auto& logStream = g_logStream;
	auto& windowHandle = g_windowHandle;
	auto& directInput = g_directInput;
	auto& keyboardDevice = g_keyboardDevice;
	auto& mouseDevice = g_mouseDevice;
	auto& key = g_key;
	auto& preKey = g_preKey;
	auto& message = g_message;
	auto& xAudio2 = g_xAudio2;
	auto& masterVoice = g_masterVoice;
	auto& soundData = g_soundData;
	auto& sourceVoice = g_sourceVoice;
	auto& dxgiFactory = g_dxgiFactory;
	auto& useAdapter = g_useAdapter;
	auto& device = g_device;
	auto& commandQueue = g_commandQueue;
	auto& commandAllocator = g_commandAllocator;
	auto& commandList = g_commandList;
	auto& swapChain = g_swapChain;
	auto& swapChainDesc = g_swapChainDesc;
	auto& rtvDescriptorHeap = g_rtvDescriptorHeap;
	auto& srvDescriptorHeap = g_srvDescriptorHeap;
	auto& dsvDescriptorHeap = g_dsvDescriptorHeap;
	auto& swapChainResources = g_swapChainResources;
	auto& rtvDesc = g_rtvDesc;
	auto& rtvHandles = g_rtvHandles;
	auto& depthClearValue = g_depthClearValue;
	auto& dsvDesc = g_dsvDesc;
	auto& dsvHandle = g_dsvHandle;
	auto& shadowDsvHandle = g_shadowDsvHandle;
	auto& depthStencilResource = g_depthStencilResource;
	auto& shadowMapResource = g_shadowMapResource;
	auto& shadowMapSrvCpuHandle = g_shadowMapSrvCpuHandle;
	auto& shadowMapSrvGpuHandle = g_shadowMapSrvGpuHandle;
	auto& renderWidth = g_renderWidth;
	auto& renderHeight = g_renderHeight;
	auto& hdrRenderTarget = g_hdrRenderTarget;
	auto& dxcUtils = g_dxcUtils;
	auto& dxcCompiler = g_dxcCompiler;
	auto& includeHandler = g_includeHandler;
	auto& vertexShaderBlob = g_vertexShaderBlob;
	auto& pixelShaderBlob = g_pixelShaderBlob;
	auto& shadowVertexShaderBlob = g_shadowVertexShaderBlob;
	auto& signatureBlob = g_signatureBlob;
	auto& errorBlob = g_errorBlob;
	auto& rootSignature = g_rootSignature;
	auto& graphicsPipelineState = g_graphicsPipelineState;
	auto& shadowPipelineState = g_shadowPipelineState;
	auto& spriteMaterialResource = g_spriteMaterialResource;
	auto& spriteMaterialData = g_spriteMaterialData;
	auto& sphereMaterialResource = g_sphereMaterialResource;
	auto& sphereMaterialData = g_sphereMaterialData;
	auto& directionalLightResource = g_directionalLightResource;
	auto& directionalLightData = g_directionalLightData;
	auto& spriteTransformationMatrixResource = g_spriteTransformationMatrixResource;
	auto& spriteTransformationMatrixData = g_spriteTransformationMatrixData;
	auto& sphereTransformationMatrixResource = g_sphereTransformationMatrixResource;
	auto& sphereTransformationMatrixData = g_sphereTransformationMatrixData;
	auto& modelData = g_modelData;
	auto& editorPrimitiveVertexResources = g_editorPrimitiveVertexResources;
	auto& vertices = g_vertices;
	auto& sprite = g_sprite;
	auto& spriteVertices = g_spriteVertices;
	auto& spriteIndices = g_spriteIndices;
	auto& transform = g_transform;
	auto& spriteTransform = g_spriteTransform;
	auto& cameraTransform = g_cameraTransform;
	auto& uvTransform = g_uvTransform;
	auto& vertexResource = g_vertexResource;
	auto& modelVertexResource = g_modelVertexResource;
	auto& spriteVertexResource = g_spriteVertexResource;
	auto& spriteIndexResource = g_spriteIndexResource;
	auto& vertexBufferView = g_vertexBufferView;
	auto& modelVertexBufferView = g_modelVertexBufferView;
	auto& spriteVertexBufferView = g_spriteVertexBufferView;
	auto& spriteIndexBufferView = g_spriteIndexBufferView;
	auto& editorWindowWidth = g_editorWindowWidth;
	auto& editorWindowHeight = g_editorWindowHeight;
	auto& editorLeftWidth = g_editorLeftWidth;
	auto& editorRightWidth = g_editorRightWidth;
	auto& editorBottomHeight = g_editorBottomHeight;
	auto& editorSceneX = g_editorSceneX;
	auto& editorSceneY = g_editorSceneY;
	auto& editorSceneWidth = g_editorSceneWidth;
	auto& editorSceneHeight = g_editorSceneHeight;
	auto& viewport = g_viewport;
	auto& scissorRect = g_scissorRect;
	auto& cameraMatrix = g_cameraMatrix;
	auto& viewMatrix = g_viewMatrix;
	auto& projectionMatrix = g_projectionMatrix;
	auto& spriteProjectionMatrix = g_spriteProjectionMatrix;
	auto& editorCameraMoveSpeed = g_editorCameraMoveSpeed;
	auto& editorCameraRotateSpeed = g_editorCameraRotateSpeed;
	auto& editorCameraWheelMoveSpeed = g_editorCameraWheelMoveSpeed;
	auto& editorCameraPanSpeed = g_editorCameraPanSpeed;
	auto& editorCameraFastRate = g_editorCameraFastRate;
	auto& sceneClearColor = g_sceneClearColor;
	auto& isSceneGizmoVisible = g_isSceneGizmoVisible;
	auto& isLightGizmoVisible = g_isLightGizmoVisible;
	auto& isCameraGizmoVisible = g_isCameraGizmoVisible;
	auto& directionalLightIconPosition = g_directionalLightIconPosition;
	auto& editorSceneObjectManager = g_editorSceneObjectManager;
	auto& editorSceneObjects = g_editorSceneObjectManager.GetSceneObjects();
	auto& selectedPlacedSceneObjectIndex = g_selectedPlacedSceneObjectIndex;
	auto& fence = g_fence;
	auto& fenceValue = g_fenceValue;
	auto& fenceEvent = g_fenceEvent;
	auto& textureFilePaths = g_textureFilePaths;
	auto& textureFilePathStrings = g_textureFilePathStrings;
	auto& editorTextureFilePaths = g_editorTextureFilePaths;
	auto& textureMetadatas = g_textureMetadatas;
	auto& textureResources = g_textureResources;
	auto& intermediateResources = g_intermediateResources;
	auto& srvDescriptorSize = g_srvDescriptorSize;
	auto& textureSrvHandlesCPU = g_textureSrvHandlesCPU;
	auto& textureSrvHandlesGPU = g_textureSrvHandlesGPU;
	auto& editorRuntimeManager = g_editorRuntimeManager;
	auto& editorSceneCameraController = g_editorSceneCameraController;
	auto& selectedSceneObject = g_selectedSceneObject;
	auto& activeEditorTool = g_activeEditorTool;
	auto& editorViewportTabIndex = g_editorViewportTabIndex;
	auto& selectedAssetPath = g_selectedAssetPath;
	auto& hierarchyFilter = g_hierarchyFilter;
	auto& assetFilter = g_assetFilter;
	auto& isConsoleCleared = g_isConsoleCleared;
	auto& isSceneRangeSelecting = g_isSceneRangeSelecting;
	auto& isSceneMiddleCameraDragging = g_isSceneMiddleCameraDragging;
	auto& isSceneRightCameraDragging = g_isSceneRightCameraDragging;
	auto& isGizmoLocalMode = g_isGizmoLocalMode;
	auto& isGizmoSnapEnabled = g_isGizmoSnapEnabled;
	auto& isSceneAssistVisible = g_isSceneAssistVisible;
	auto& isLegacyPreviewVisible = g_isLegacyPreviewVisible;
	auto& gizmoSnapValues = g_gizmoSnapValues;
	auto& sceneRangeStart = g_sceneRangeStart;
	auto& sceneRangeEnd = g_sceneRangeEnd;
	auto& editorScene = g_editorScene;
	auto& isEditorSceneInitialized = g_isEditorSceneInitialized;
	auto& isEditorRuntimeInitialized = g_isEditorRuntimeInitialized;
	auto& selectedEditorGameObjectId = g_selectedEditorGameObjectId;
	auto& previousSelectedEditorGameObjectId = g_previousSelectedEditorGameObjectId;
	auto& selectedGameObjectName = g_selectedGameObjectName;
	auto& selectedAddComponentIndex = g_selectedAddComponentIndex;
	auto& editorConsoleMessages = g_editorConsoleMessages;
	auto& editorSelectionManager = g_editorSelectionManager;
	auto& editorSceneSynchronizer = g_editorSceneSynchronizer;
	auto& editorAssetFactory = g_editorAssetFactory;
	auto& editorMainMenuBar = g_editorMainMenuBar;
	auto& editorHierarchyPanel = g_editorHierarchyPanel;
	auto& editorInspectorPanel = g_editorInspectorPanel;
	auto& editorBottomPanel = g_editorBottomPanel;
	auto& isEditorManagerInitialized = g_isEditorManagerInitialized;
	auto& isDockLayoutInitialized = g_isDockLayoutInitialized;

	if (!g_isInitialized || g_isFinalized) {
		return g_exitCode;
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

	g_feelKitHaptics.shutdown();
	if (keyboardDevice != nullptr) {
		keyboardDevice->Unacquire();
		keyboardDevice->Release();
		keyboardDevice = nullptr;
	}
	if (mouseDevice != nullptr) {
		mouseDevice->Unacquire();
		mouseDevice->Release();
		mouseDevice = nullptr;
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
	editorSceneObjectManager.ReleaseAll();
	spriteMaterialResource->Release();
	sphereMaterialResource->Release();
	directionalLightResource->Release();
	spriteTransformationMatrixResource->Release();
	sphereTransformationMatrixResource->Release();
	spriteIndexResource->Release();
	spriteVertexResource->Release();
	for (ID3D12Resource* primitiveVertexResource : editorPrimitiveVertexResources) {
		if (primitiveVertexResource != nullptr) {
			primitiveVertexResource->Release();
		}
	}
	modelVertexResource->Release();
	vertexResource->Release();
	srvDescriptorHeap->Release();
	dsvDescriptorHeap->Release();
	rtvDescriptorHeap->Release();
	if (shadowMapResource != nullptr) {
		shadowMapResource->Release();
		shadowMapResource = nullptr;
	}
	for (ID3D12Resource* bloomResource : g_bloomRenderTargets) {
		if (bloomResource != nullptr) {
			bloomResource->Release();
		}
	}
	if (g_postProcessRenderTarget != nullptr) {
		g_postProcessRenderTarget->Release();
		g_postProcessRenderTarget = nullptr;
	}
	for (ID3D12Resource*& ssaoResource : g_ssaoRenderTargets) {
		if (ssaoResource != nullptr) {
			ssaoResource->Release();
			ssaoResource = nullptr;
		}
	}
	if (g_hdrCompositeRenderTarget != nullptr) {
		g_hdrCompositeRenderTarget->Release();
		g_hdrCompositeRenderTarget = nullptr;
	}
	if (g_materialMaskRenderTarget != nullptr) {
		g_materialMaskRenderTarget->Release();
		g_materialMaskRenderTarget = nullptr;
	}

	if (g_planarReflectionRenderTarget != nullptr) {
		g_planarReflectionRenderTarget->Release();
		g_planarReflectionRenderTarget = nullptr;
	}
	if (g_environmentTextureUploadResource != nullptr) {
		g_environmentTextureUploadResource->Release();
		g_environmentTextureUploadResource = nullptr;
	}
if (g_environmentTextureResource != nullptr) {
		g_environmentTextureResource->Release();
		g_environmentTextureResource = nullptr;
	}
	if (g_iblIrradianceCube != nullptr) { g_iblIrradianceCube->Release(); g_iblIrradianceCube = nullptr; }
	if (g_iblPrefilterCube != nullptr) { g_iblPrefilterCube->Release(); g_iblPrefilterCube = nullptr; }
	if (g_iblEnvironmentCube != nullptr) { g_iblEnvironmentCube->Release(); g_iblEnvironmentCube = nullptr; }
	if (g_iblBRDFLUT != nullptr) { g_iblBRDFLUT->Release(); g_iblBRDFLUT = nullptr; }
	if (hdrRenderTarget != nullptr) {
		hdrRenderTarget->Release();
		hdrRenderTarget = nullptr;
	}
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


	g_exitCode = static_cast<int>(message.wParam);
	g_isFinalized = true;
	return g_exitCode;
}
