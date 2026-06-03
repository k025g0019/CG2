#pragma warning(disable : 4189 4514)

#include "EditorPlatformManager.h"


#include "EditorSharedState.h"
using namespace EditorSharedState;

namespace {
	//================================================================
	// 初期化失敗時の終了要求
	//================================================================

	void RequestInitializationFailure() {
		g_isInitializationFailed = true; // g_isInitializationFailed は GameScene が後続 Manager 初期化を止めるためのフラグ。
		g_isEndRequested = true; // g_isEndRequested は WinMain のループへ入らないようにする終了要求フラグ。
		g_exitCode = 1; // 1 は初期化失敗を表す終了コード。
	}

	VertexData MakePrimitiveVertex(float x, float y, float z, float u, float v, const Vector3& normal) {
		// 基本形メッシュの 1 頂点を作る。position はローカル座標、texcoord は checker 表示用。
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
		// DrawInstanced は triangle list なので、三角形単位で頂点を追加する。
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
		// 四角形は 2 枚の三角形に分けて追加する。
		AddPrimitiveTriangle(vertices, a, b, c);
		AddPrimitiveTriangle(vertices, c, b, d);
	}

	std::vector<VertexData> CreateBoxVertices(const Vector3& halfSize) {
		std::vector<VertexData> vertices; // vertices は Box 6 面分の三角形頂点。
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
		std::vector<VertexData> vertices; // vertices は側面、上面、下面を持つ円柱メッシュ。
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
		std::vector<VertexData> vertices; // vertices は側面と底面を持つ円錐メッシュ。
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
		std::vector<VertexData> vertices; // vertices はドーナツ形状のトーラスメッシュ。
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
		std::vector<VertexData> vertices; // vertices は低ポリゴン基本形として使う八面体メッシュ。
		vertices.reserve(24);
		constexpr Vector3 top{0.0f, 0.6f, 0.0f};
		constexpr Vector3 bottom{0.0f, -0.6f, 0.0f};
		constexpr Vector3 front{0.0f, 0.0f, -0.6f};
		constexpr Vector3 right{0.6f, 0.0f, 0.0f};
		constexpr Vector3 back{0.0f, 0.0f, 0.6f};
		constexpr Vector3 left{-0.6f, 0.0f, 0.0f};

		auto addFace = [&](const Vector3& a, const Vector3& b, const Vector3& c) {
			Vector3 normal = Normalize(Cross(Subtract(b, a), Subtract(c, a))); // 面法線を計算してフラットシェーディングにする。
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
		std::vector<VertexData> vertices; // vertices は緯度経度で作る球の三角形頂点。
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
		ModelData modelData{}; // modelData は基本形ごとの頂点配列を入れる戻り値。

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
			auto meshType = static_cast<EditorModelMeshType>(meshTypeIndex); // 配列番号を基本形 enum として扱う。
			primitiveModelData[meshTypeIndex] = CreatePrimitiveModelData(meshType, fallbackPlaneModelData);
			primitiveVertexCounts[meshTypeIndex] =
				static_cast<uint32_t>(primitiveModelData[meshTypeIndex].vertices.size());

			if (primitiveVertexCounts[meshTypeIndex] == 0u) {
				continue;
			}

			size_t bufferSize = sizeof(VertexData) * primitiveModelData[meshTypeIndex].vertices.size();
			// 頂点配列を丸ごと UploadBuffer に置く。
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
	InstallCrashHandler(); // クラッシュ時に dump を残せるよう、最初に例外ハンドラを登録する。

	//================================================================
	// ログファイルの作成
	//================================================================

	std::filesystem::create_directory("logs"); // logs フォルダは実行ごとの .Log ファイルを保存する場所。
	std::time_t now = std::time(nullptr); // now / localTime はログファイル名に使う現在時刻。
	std::tm localTime{};
	localtime_s(&localTime, &now);

	// dateString は yyyyMMdd_HHmmss 形式のログファイル名部分。
	std::string dateString = std::format(
		"{:04}{:02}{:02}_{:02}{:02}{:02}",
		localTime.tm_year + 1900,
		localTime.tm_mon + 1,
		localTime.tm_mday,
		localTime.tm_hour,
		localTime.tm_min,
		localTime.tm_sec);

	auto logFilePath = std::string("logs/" + dateString + ".Log"); // logFilePath は今回の実行ログの保存先。
	std::ofstream logStream(logFilePath); // logStream は初期化ログとシェーダーコンパイルログを書き込む出力先。

	// ログが開けない場合は、以降の初期化状況を記録できないため起動を止める。
	if (!logStream) {
		RequestInitializationFailure();
		return;
	}

	HWND windowHandle = CreateMainWindow(instanceHandle, logStream);
	// windowHandle は DirectX SwapChain と DirectInput の協調レベル設定に使う HWND。

	// Window 作成失敗時は DirectX / ImGui が HWND を使えないため終了する。
	if (windowHandle == nullptr) {
		RequestInitializationFailure();
		return;
	}

	HRESULT hr = S_OK; // hr は Win32 / DirectX / XAudio2 API の成否を受け取る共通 HRESULT。
	IDirectInput8* directInput = nullptr; // directInput はキーボードデバイスを作る DirectInput 本体。
	hr = DirectInput8Create(
		instanceHandle, DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast<void**>(&directInput), nullptr);
	assert(SUCCEEDED(hr));

	// directInput が作れない場合、エディター操作の入力を取得できないため起動を止める。
	if (FAILED(hr) || directInput == nullptr) {
		RequestInitializationFailure();
		return;
	}

	IDirectInputDevice8* keyboardDevice = nullptr; // keyboardDevice は DIK_* の押下状態を取得するためのキーボード入力デバイス。
	hr = directInput->CreateDevice(GUID_SysKeyboard, &keyboardDevice, nullptr);
	assert(SUCCEEDED(hr));

	// キーボードデバイス作成失敗時は directInput を解放してから終了する。
	if (FAILED(hr) || keyboardDevice == nullptr) {
		directInput->Release();
		RequestInitializationFailure();
		return;
	}

	hr = keyboardDevice->SetDataFormat(&c_dfDIKeyboard); // c_dfDIKeyboard は DirectInput の 256 キー配列形式で入力を受け取る指定。
	assert(SUCCEEDED(hr));

	// foreground / nonexclusive は他アプリと入力を奪い合わないエディター向け設定。
	hr = keyboardDevice->SetCooperativeLevel(
		windowHandle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));

	//================================================================
	// DirectInput マウスデバイス作成
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

	// key / preKey は初期化直後の今フレーム・前フレーム入力状態。
	BYTE key[256] = {};
	BYTE preKey[256] = {};

#ifdef _DEBUG
	//================================================================
	// DirectX12 デバッグレイヤー
	//================================================================

	ComPtr<ID3D12Debug1> debugController; // debugController は DirectX12 のエラーや警告を Visual Studio に出すための制御オブジェクト。

	// Debug ビルドだけ GPU 検証を有効にし、危険な API 使用を早めに検出する。
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	// message は Win32 メッセージループで使う現在メッセージ。
	MSG message{};
	Log(logStream, "main loop started");

	//================================================================
	// XAudio2 の初期化
	//================================================================

	IXAudio2* xAudio2 = nullptr; // xAudio2 は音声再生エンジン本体。
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(hr));

	// XAudio2 が作れない場合、音声リソースを保持できないため終了する。
	if (FAILED(hr) || xAudio2 == nullptr) {
		RequestInitializationFailure();
		return;
	}

	IXAudio2MasteringVoice* masterVoice = nullptr; // masterVoice は最終的にスピーカーへ送る出力 Voice。
	hr = xAudio2->CreateMasteringVoice(&masterVoice);
	assert(SUCCEEDED(hr));

	// masterVoice 作成失敗時は XAudio2 本体を解放して終了する。
	if (FAILED(hr) || masterVoice == nullptr) {
		xAudio2->Release();
		RequestInitializationFailure();
		return;
	}

	SoundData soundData = SoundLoadWave("resources/sound/maou_19_12345.wav");
	// soundData は起動確認用 wav の PCM データとフォーマット情報。
	IXAudio2SourceVoice* sourceVoice = nullptr; // sourceVoice は soundData を再生キューへ積む Voice。
	hr = xAudio2->CreateSourceVoice(&sourceVoice, &soundData.wfex);
	assert(SUCCEEDED(hr));

	// sourceVoice 作成失敗時は読み込んだ wav と Voice を解放して終了する。
	if (FAILED(hr) || sourceVoice == nullptr) {
		SoundUnload(&soundData);
		masterVoice->DestroyVoice();
		xAudio2->Release();
		RequestInitializationFailure();
		return;
	}

	// soundBuffer は sourceVoice に渡す再生データの範囲と終端フラグ。
	XAUDIO2_BUFFER soundBuffer{};
	soundBuffer.pAudioData = soundData.pBuffer;
	soundBuffer.AudioBytes = soundData.bufferSize;
	soundBuffer.Flags = XAUDIO2_END_OF_STREAM;
	hr = sourceVoice->SubmitSourceBuffer(&soundBuffer);
	assert(SUCCEEDED(hr));

	//================================================================
	// DirectX12 の初期化
	//================================================================

	ComPtr<IDXGIFactory7> dxgiFactory; // dxgiFactory は GPU Adapter と SwapChain を作るための DXGI 入口。
	hr = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
	assert(SUCCEEDED(hr));

	ComPtr<IDXGIAdapter4> useAdapter; // useAdapter は実際に D3D12Device を作る物理 GPU。
	for (UINT adapterIndex = 0;; ++adapterIndex) {
		ComPtr<IDXGIAdapter4> candidateAdapter; // candidateAdapter は性能優先順に列挙した GPU 候補。
		if (dxgiFactory->EnumAdapterByGpuPreference(
			adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(candidateAdapter.GetAddressOf())) == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		// adapterDesc は GPU 名と Software Adapter 判定に使う情報。
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = candidateAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		// Software Adapter は WARP なので、ゲーム描画に使う実 GPU 候補から外す。
		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
			continue;
		}

		useAdapter = candidateAdapter; // 最初に見つかった実 GPU を使用 Adapter として採用する。
		Log(logStream, std::format("Use Adapter:{}", ConvertString(std::wstring{adapterDesc.Description})));
		break;
	}
	assert(useAdapter != nullptr);

	ComPtr<ID3D12Device> device; // device は DirectX12 リソース生成とコマンド発行の中心になる GPU デバイス。
	hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(device.GetAddressOf()));
	// FeatureLevel は 12.2 から順に試し、PC が対応する一番高い機能レベルで作る。
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
	ComPtr<ID3D12InfoQueue> infoQueue; // infoQueue は DirectX12 の重大エラーをデバッガ停止に変えるための診断キュー。
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(infoQueue.GetAddressOf())))) {
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true); // 破損・エラー・警告をその場で止め、原因箇所を追いやすくする。
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// denyIds は既知のノイズ警告を Debug 出力から除外するための ID 一覧。
		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
		};

		// severities は情報メッセージを抑制するための重大度一覧。
		D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO};

		// filter は denyIds / severities を InfoQueue に渡すための設定。
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		infoQueue->PushStorageFilter(&filter);
	}
#endif

	ComPtr<ID3D12CommandQueue> commandQueue; // commandQueue は GPU に CommandList を送るキュー。

	// commandQueueDesc は標準の Direct CommandQueue を作るための設定。
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// CommandQueue がないと描画命令を GPU に送れないため終了する。
	if (FAILED(hr) || commandQueue == nullptr) {
		RequestInitializationFailure();
		return;
	}


	ComPtr<ID3D12CommandAllocator> commandAllocator; // commandAllocator は CommandList が記録する命令メモリを管理する。
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// CommandAllocator がないと CommandList を Reset できないため終了する。
	if (FAILED(hr) || commandAllocator == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12GraphicsCommandList> commandList; // commandList は描画・コピー・ResourceBarrier の命令を記録するオブジェクト。
	hr = device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// CommandList がないと初期テクスチャアップロードも描画もできないため終了する。
	if (FAILED(hr) || commandList == nullptr) {
		RequestInitializationFailure();
		return;
	}

	hr = commandList->Close(); // 作成直後の CommandList は開いているため、一度 Close して通常の Reset 手順に合わせる。
	assert(SUCCEEDED(hr));

	// clientRect は Window 内の描画可能領域。SwapChain サイズの初期値に使う。
	RECT clientRect{};
	GetClientRect(windowHandle, &clientRect);

	uint32_t renderWidth = (std::max)(1u, static_cast<uint32_t>(clientRect.right - clientRect.left));
	// renderWidth / renderHeight は最小化などで 0 にならないよう 1 以上にする。
	uint32_t renderHeight = (std::max)(1u, static_cast<uint32_t>(clientRect.bottom - clientRect.top));

	ComPtr<IDXGISwapChain4> swapChain; // swapChain は Window に表示するバックバッファ列。

	// swapChainDesc はバックバッファ数、形式、表示方式を指定する設定。
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

	// SwapChain がないと Window へ Present できないため終了する。
	if (FAILED(hr) || swapChain == nullptr) {
		RequestInitializationFailure();
		return;
	}

	// rtvDescriptorHeap は SwapChain バックバッファを RenderTarget として参照する Heap。
	ID3D12DescriptorHeap* rtvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	// srvDescriptorHeap は Texture SRV と ImGui 用 SRV を Shader から参照する Heap。
	ID3D12DescriptorHeap* srvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	// dsvDescriptorHeap は DepthStencil を参照する Heap。
	ID3D12DescriptorHeap* dsvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	// swapChainResources は 2 枚のバックバッファ実体。
	ID3D12Resource* swapChainResources[2] = {nullptr};
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	// rtvDesc はバックバッファを 2D RenderTarget として扱う設定。
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2]; // rtvHandles は各バックバッファに対応する CPU 側 RTV ハンドル。
	rtvHandles[0] = GetCPUDescriptorHandle(
		rtvDescriptorHeap, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), 0);
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);
	rtvHandles[1] = GetCPUDescriptorHandle(
		rtvDescriptorHeap, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), 1);
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

	// depthClearValue は DepthStencil を Clear する時の初期値。
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	// dsvDesc は DepthStencilResource を DSV として使うための設定。
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	// dsvHandle は DepthStencilView を作成する CPU 側ハンドル。

	auto createDepthStencilResource = [&](uint32_t width, uint32_t height) -> ID3D12Resource* {
		// depthStencilResourceDesc は SceneView と同じサイズの Depth バッファ設定。
		D3D12_RESOURCE_DESC depthStencilResourceDesc{};
		depthStencilResourceDesc.Width = width;
		depthStencilResourceDesc.Height = height;
		depthStencilResourceDesc.MipLevels = 1;
		depthStencilResourceDesc.DepthOrArraySize = 1;
		depthStencilResourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilResourceDesc.SampleDesc.Count = 1;
		depthStencilResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthStencilResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		// depthStencilHeapProperties は GPU 専用メモリ上に Depth バッファを置く指定。
		D3D12_HEAP_PROPERTIES depthStencilHeapProperties{};
		depthStencilHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		ID3D12Resource* newDepthStencilResource = nullptr; // newDepthStencilResource は作成して返す DepthStencil 実体。
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
	// depthStencilResource は現在の描画サイズに合わせた Depth バッファ。
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandle);

	ComPtr<IDxcUtils> dxcUtils; // dxcUtils は HLSL ファイル読み込みと IncludeHandler 作成に使う DXC 補助。
	ComPtr<IDxcCompiler3> dxcCompiler; // dxcCompiler は HLSL を DXIL へコンパイルする DXC コンパイラ。
	ComPtr<IDxcIncludeHandler> includeHandler; // includeHandler は shader の #include を解決するための標準ハンドラ。
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxcUtils.GetAddressOf()));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxcCompiler.GetAddressOf()));
	assert(SUCCEEDED(hr));
	hr = dxcUtils->CreateDefaultIncludeHandler(includeHandler.GetAddressOf());
	assert(SUCCEEDED(hr));

	// vertexShaderBlob は VS main のコンパイル済みバイトコード。
	ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(
		L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	// pixelShaderBlob は PS main のコンパイル済みバイトコード。
	ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(
		L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	// descriptorRange は Texture SRV を RootSignature の DescriptorTable に渡す範囲。
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// descriptionRootSignature は Shader へ渡す CBV / SRV / Sampler の入口定義。
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// rootParameters は 0:Material、1:Transform、2:DirectionalLight、3:Texture SRV。
	D3D12_ROOT_PARAMETER rootParameters[4] = {};

	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	// rootParameters[0] は PixelShader の b0 Material CBV。
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[0].Descriptor.RegisterSpace = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	// rootParameters[1] は VertexShader の b0 Transform CBV。
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;
	rootParameters[1].Descriptor.RegisterSpace = 0;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	// rootParameters[2] は PixelShader の b1 DirectionalLight CBV。
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].Descriptor.ShaderRegister = 1;
	rootParameters[2].Descriptor.RegisterSpace = 0;

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	// rootParameters[3] は PixelShader の t0 Texture SRV DescriptorTable。
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	// staticSampler は Texture を線形補間・Wrap でサンプリングする設定。
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

	ComPtr<ID3DBlob> signatureBlob; // signatureBlob は RootSignature のシリアライズ結果。
	ComPtr<ID3DBlob> errorBlob; // errorBlob は RootSignature シリアライズ失敗時のエラー文字列。
	ID3D12Resource* spriteMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));
	// spriteMaterialResource は Sprite 描画用 Material 定数バッファ。
	Material* spriteMaterialData = nullptr; // spriteMaterialData は CPU から直接書き込める Sprite Material の mapped ポインタ。
	spriteMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&spriteMaterialData));
	spriteMaterialData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	spriteMaterialData->enableLighting = FALSE;
	spriteMaterialData->useTexture = TRUE;
	spriteMaterialData->padding[0] = 0.0f;
	spriteMaterialData->padding[1] = 0.0f;
	spriteMaterialData->uvTransform = MakeIdentity4x4();

	ID3D12Resource* sphereMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));
	// sphereMaterialResource は 3D モデル描画用 Material 定数バッファ。
	Material* sphereMaterialData = nullptr; // sphereMaterialData は CPU から直接書き込める 3D Material の mapped ポインタ。
	sphereMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&sphereMaterialData));
	sphereMaterialData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	sphereMaterialData->enableLighting = TRUE;
	sphereMaterialData->useTexture = TRUE;
	sphereMaterialData->padding[0] = 0.0f;
	sphereMaterialData->padding[1] = 0.0f;
	sphereMaterialData->uvTransform = MakeIdentity4x4();

	ID3D12Resource* directionalLightResource = CreateBufferResource(device.Get(), sizeof(DirectionalLight));
	// directionalLightResource は PixelShader に渡す平行光源定数バッファ。
	DirectionalLight* directionalLightData = nullptr; // directionalLightData は Inspector から色・向き・強さを書き換える mapped ポインタ。
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	directionalLightData->direction = {0.0f, -1.0f, 0.0f};
	directionalLightData->intensity = 1.0f;


	// spriteTransformationMatrixResource は Sprite の WVP / World 行列用定数バッファ。
	ID3D12Resource* spriteTransformationMatrixResource = CreateBufferResource(
		device.Get(), sizeof(TransformationMatrix));

	TransformationMatrix* spriteTransformationMatrixData = nullptr;
	// spriteTransformationMatrixData は CPU から Sprite 行列を書き込む mapped ポインタ。
	spriteTransformationMatrixResource->Map(
		0, nullptr, reinterpret_cast<void**>(&spriteTransformationMatrixData));
	spriteTransformationMatrixData->WVP = MakeIdentity4x4();
	spriteTransformationMatrixData->World = MakeIdentity4x4();

	// sphereTransformationMatrixResource は旧 3D プレビューの WVP / World 行列用定数バッファ。
	ID3D12Resource* sphereTransformationMatrixResource = CreateBufferResource(
		device.Get(), sizeof(TransformationMatrix));

	TransformationMatrix* sphereTransformationMatrixData = nullptr;
	// sphereTransformationMatrixData は CPU から 3D 行列を書き込む mapped ポインタ。
	sphereTransformationMatrixResource->Map(
		0, nullptr, reinterpret_cast<void**>(&sphereTransformationMatrixData));
	sphereTransformationMatrixData->WVP = MakeIdentity4x4();
	sphereTransformationMatrixData->World = MakeIdentity4x4();

	// RootSignature は Shader がどの Resource をどのスロットで読むかを固定する。
	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.GetAddressOf(),
		errorBlob.GetAddressOf());
	if (FAILED(hr)) {
		// errorBlob がある場合は HLSL/RootSignature 側の具体的な失敗理由をログへ出す。
		if (errorBlob != nullptr) {
			Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	ComPtr<ID3D12RootSignature> rootSignature; // rootSignature は PipelineState に設定する GPU 側 RootSignature 実体。
	hr = device->CreateRootSignature(
		0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(rootSignature.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// inputElementDescs は VertexData の position / texcoord / normal を Shader 入力に対応させる。
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

	// blendDesc は RenderTarget へ色を書き込む方法。現状は不透明描画の標準設定。
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// rasterizerDesc は三角形を塗りつぶし、裏表カリングをしない設定。
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.DepthClipEnable = TRUE;

	// depthStencilDesc は手前の物体を優先して描くための深度テスト設定。
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// graphicsPipelineStateDesc は Shader、入力レイアウト、Blend、Depth をまとめた描画設定。
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

	ComPtr<ID3D12PipelineState> graphicsPipelineState; // graphicsPipelineState は Draw 時に CommandList へセットする PSO 実体。
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
	                                         IID_PPV_ARGS(graphicsPipelineState.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// PSO がないと Shader と RenderState が確定せず描画できないため終了する。
	if (FAILED(hr) || graphicsPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ModelData modelData = LoadObjFile("resources", "plane.obj"); // modelData は旧プレビューと既定アセット用に読み込む plane.obj の頂点データ。
	constexpr uint32_t kSubdivision = 64; // kSubdivision は旧球プレビューを作る緯度・経度分割数。
	constexpr float kLonEvery = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kSubdivision);
	// kLonEvery / kLatEvery は球メッシュ 1 セグメントあたりの角度。
	constexpr float kLatEvery = std::numbers::pi_v<float> / static_cast<float>(kSubdivision);

	std::vector<VertexData> vertices; // vertices は旧球プレビュー用に CPU で生成する三角形頂点列。
	vertices.reserve(kSubdivision * kSubdivision * 6);
	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		float lat = -std::numbers::pi_v<float> / 2.0f + kLatEvery * static_cast<float>(latIndex);
		// lat / latNext は現在セルの下側・上側の緯度角。
		float latNext = lat + kLatEvery;

		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			float lon = kLonEvery * static_cast<float>(lonIndex) + std::numbers::pi_v<float>;
			// lon / lonNext は現在セルの左側・右側の経度角。
			float lonNext = lon + kLonEvery;

			float u0 = static_cast<float>(lonIndex) / static_cast<float>(kSubdivision);
			// u0/u1/v0/v1 は球面に uvChecker を貼るための UV 範囲。
			float u1 = static_cast<float>(lonIndex + 1) / static_cast<float>(kSubdivision);
			float v0 = 1.0f - static_cast<float>(latIndex) / static_cast<float>(kSubdivision);
			float v1 = 1.0f - static_cast<float>(latIndex + 1) / static_cast<float>(kSubdivision);

			// a/b/c/d は球面 1 セルの四隅。position と normal は単位球座標から作る。
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

			vertices.push_back(a); // 四角形セルを 2 枚の三角形に分けて VertexBuffer へ追加する。
			vertices.push_back(b);
			vertices.push_back(c);
			vertices.push_back(c);
			vertices.push_back(b);
			vertices.push_back(d);
		}
	}

	// sprite は旧 Sprite プレビューの基準位置とサイズ。
	Sprite sprite{
		.position = {128.0f, 128.0f},
		.size = {256.0f, 256.0f}
	};

	// spriteVertices は中心原点の四角形を表す 4 頂点。
	VertexData spriteVertices[] = {
		{{-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
		{{-0.5f, 0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
		{{0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
		{{0.5f, 0.5f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
	};

	// spriteIndices は四角形を 2 三角形で描くための IndexBuffer。
	uint32_t spriteIndices[] = {
		0, 1, 2,
		2, 1, 3,
	};

	// transform は旧 3D モデルプレビューの初期 Transform。
	Transforms transform{
		.scale = {0.55f, 0.55f, 0.55f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, 0.0f}
	};

	// spriteTransform は旧 Sprite プレビューの Transform。Sprite サイズを scale に入れる。
	Transforms spriteTransform{
		.scale = {sprite.size.x, sprite.size.y, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {sprite.position.x, sprite.position.y, 0.0f}
	};

	// cameraTransform は SceneView 用エディターカメラの初期位置。
	Transforms cameraTransform{
		.scale = {1.0f, 1.0f, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, -5.0f}
	};

	// uvTransform は Material の UV 変換行列を作るための初期 Transform。
	Transforms uvTransform{
		.scale = {1.0f, 1.0f, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, 0.0f}
	};

	ID3D12Resource* vertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * vertices.size());
	// vertexResource は旧球プレビューの頂点を GPU へ渡す Upload Buffer。
	VertexData* mappedVertexData = nullptr; // mappedVertexData は vertexResource に CPU から頂点を書き込むためのポインタ。
	hr = vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedVertexData, vertices.data(), sizeof(VertexData) * vertices.size());

	// vertexBufferView は旧球プレビューの頂点 Buffer を Draw に渡す情報。
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// modelVertexResource は plane.obj の頂点を GPU へ渡す Upload Buffer。
	ID3D12Resource* modelVertexResource = CreateBufferResource(device.Get(),
	                                                           sizeof(VertexData) * modelData.vertices.size());

	VertexData* mappedModelVertexData = nullptr; // mappedModelVertexData は modelVertexResource に CPU から頂点を書き込むためのポインタ。
	hr = modelVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedModelVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedModelVertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());

	// modelVertexBufferView は plane.obj の頂点 Buffer を Draw に渡す情報。
	D3D12_VERTEX_BUFFER_VIEW modelVertexBufferView{};
	modelVertexBufferView.BufferLocation = modelVertexResource->GetGPUVirtualAddress();
	modelVertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelData.vertices.size());
	modelVertexBufferView.StrideInBytes = sizeof(VertexData);

	ModelData primitiveModelData[kEditorModelMeshTypeCount]{}; // primitiveModelData は基本形ごとの CPU 頂点データ。
	ID3D12Resource* primitiveVertexResources[kEditorModelMeshTypeCount] = {};
	// primitiveVertexResources は基本形ごとの GPU 頂点 Buffer。
	D3D12_VERTEX_BUFFER_VIEW primitiveVertexBufferViews[kEditorModelMeshTypeCount]{};
	// primitiveVertexBufferViews は Draw 時に IA へ渡す BufferView。
	uint32_t primitiveVertexCounts[kEditorModelMeshTypeCount] = {}; // primitiveVertexCounts は DrawInstanced の頂点数。
	CreatePrimitiveMeshBuffers(
		device.Get(),
		modelData,
		primitiveModelData,
		primitiveVertexResources,
		primitiveVertexBufferViews,
		primitiveVertexCounts);

	ID3D12Resource* spriteVertexResource = CreateBufferResource(device.Get(), sizeof(spriteVertices));
	// spriteVertexResource は Sprite 四角形の頂点を GPU へ渡す Upload Buffer。
	VertexData* mappedSpriteVertexData = nullptr; // mappedSpriteVertexData は Sprite 頂点を書き込むための CPU mapped ポインタ。
	hr = spriteVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedSpriteVertexData, spriteVertices, sizeof(spriteVertices));

	// spriteVertexBufferView は Sprite 頂点 Buffer を Draw に渡す情報。
	D3D12_VERTEX_BUFFER_VIEW spriteVertexBufferView{};
	spriteVertexBufferView.BufferLocation = spriteVertexResource->GetGPUVirtualAddress();
	spriteVertexBufferView.SizeInBytes = sizeof(spriteVertices);
	spriteVertexBufferView.StrideInBytes = sizeof(VertexData);

	ID3D12Resource* spriteIndexResource = CreateBufferResource(device.Get(), sizeof(spriteIndices));
	// spriteIndexResource は Sprite 四角形の IndexBuffer。
	uint32_t* mappedSpriteIndexData = nullptr; // mappedSpriteIndexData は Sprite Index を CPU から書き込むためのポインタ。
	hr = spriteIndexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteIndexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedSpriteIndexData, spriteIndices, sizeof(spriteIndices));

	// spriteIndexBufferView は Sprite IndexBuffer を DrawIndexed に渡す情報。
	D3D12_INDEX_BUFFER_VIEW spriteIndexBufferView{};
	spriteIndexBufferView.BufferLocation = spriteIndexResource->GetGPUVirtualAddress();
	spriteIndexBufferView.SizeInBytes = sizeof(spriteIndices);
	spriteIndexBufferView.Format = DXGI_FORMAT_R32_UINT;

	constexpr float editorMenuHeight = 20.0f;
	// editorMenuHeight / editorSceneHeaderHeight は初期 SceneView 矩形の Y 座標計算に使う固定高さ。
	constexpr float editorSceneHeaderHeight = 24.0f;

	float editorWindowWidth = static_cast<float>(renderWidth);
	// editorWindowWidth / Height は ImGui と SceneView 用に float で保持する Window サイズ。
	float editorWindowHeight = static_cast<float>(renderHeight);

	float editorLeftWidth = 250.0f; // editorLeft/Right/Bottom は初期 Docking 前の各パネル幅・高さ。
	float editorRightWidth = 320.0f;
	float editorBottomHeight = 190.0f;

	float editorSceneX = editorLeftWidth; // editorSceneX/Y/Width/Height は DirectX viewport を SceneView に合わせるための矩形。
	float editorSceneY = editorMenuHeight + editorSceneHeaderHeight;
	float editorSceneWidth =
		editorWindowWidth - editorLeftWidth - editorRightWidth;
	float editorSceneHeight =
		editorWindowHeight - editorSceneY - editorBottomHeight;
	auto updateEditorLayout = [&]() {
		editorLeftWidth = (std::clamp)(editorLeftWidth, 160.0f, 420.0f); // パネル幅・高さに下限上限を持たせ、SceneView が極端に潰れないようにする。
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

	// viewport は DirectX が描画する画面上の矩形。
	D3D12_VIEWPORT viewport{};
	viewport.TopLeftX = editorSceneX;
	viewport.TopLeftY = editorSceneY;
	viewport.Width = editorWindowWidth;
	viewport.Height = editorWindowHeight;
	viewport.MaxDepth = 1.0f;
	viewport.Width = editorSceneWidth;
	viewport.Height = editorSceneHeight;

	// scissorRect は SceneView の外へ描画しないための切り取り矩形。
	D3D12_RECT scissorRect{};
	scissorRect.left = static_cast<LONG>(editorSceneX);
	scissorRect.top = static_cast<LONG>(editorSceneY);
	scissorRect.right = static_cast<LONG>(editorSceneX + editorSceneWidth);
	scissorRect.bottom = static_cast<LONG>(editorSceneY + editorSceneHeight);

	// textureFilePaths は起動時に GPU へアップロードする標準テクスチャ一覧。
	std::wstring textureFilePaths[] = {
		L"resources/uvChecker.png",
		L"resources/monsterBall.png",
		ConvertString(modelData.material.textureFilePath),
		L"resources/ball.png",
	};

	std::string textureFilePathStrings[_countof(textureFilePaths)];
	// textureFilePathStrings は Project パネルで扱いやすい UTF-8 版パス。
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		textureFilePathStrings[textureIndex] = ConvertString(textureFilePaths[textureIndex]);
	}
	std::vector<std::string> editorTextureFilePaths;
	editorTextureFilePaths.reserve(_countof(textureFilePaths));
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		editorTextureFilePaths.push_back(textureFilePathStrings[textureIndex]);
	}

	DirectX::ScratchImage mipImages[_countof(textureFilePaths)]; // mipImages は DirectXTex が生成した mipmap 付き画像データ。
	DirectX::TexMetadata textureMetadatas[_countof(textureFilePaths)]; // textureMetadatas は各 Texture のサイズ、形式、mip 数。

	// textureResources は GPU 上の Texture 実体。
	ID3D12Resource* textureResources[_countof(textureFilePaths)] = {nullptr};
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		mipImages[textureIndex] = LoadTexture(textureFilePaths[textureIndex]);
		textureMetadatas[textureIndex] = mipImages[textureIndex].GetMetadata();
		textureResources[textureIndex] = CreateTextureResource(device.Get(), textureMetadatas[textureIndex]);
	}

	// cameraMatrix はエディターカメラ Transform から作るワールド行列。
	Matrix4x4 cameraMatrix = MakeAffineMatrix(
		cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);

	Matrix4x4 viewMatrix = Inverse(cameraMatrix); // viewMatrix は cameraMatrix の逆行列。ワールド座標をカメラ空間へ移す。

	// projectionMatrix は SceneView の 3D 表示用透視投影行列。
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(
		0.45f,
		editorSceneWidth / editorSceneHeight,
		0.1f,
		100.0f);

	// spriteProjectionMatrix は 2D Sprite を画面座標で描くための正射影行列。
	Matrix4x4 spriteProjectionMatrix = MakeOrthographicMatrix(
		0.0f,
		0.0f,
		editorWindowWidth,
		editorWindowHeight,
		0.0f,
		100.0f);

	float editorCameraMoveSpeed = 0.12f; // editorCamera*Speed は Inspector から調整できる Scene カメラ操作速度。
	float editorCameraRotateSpeed = 0.006f;
	float editorCameraWheelMoveSpeed = 0.5f;
	float editorCameraPanSpeed = 0.01f;
	float editorCameraFastRate = 4.0f;

	// sceneClearColor は SceneView の背景色 RGBA。
	float sceneClearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};

	bool isSceneGizmoVisible = true; // is*GizmoVisible は SceneView 上の補助表示を切り替えるフラグ。
	bool isLightGizmoVisible = false;
	bool isCameraGizmoVisible = false;

	// directionalLightIconPosition はライトアイコンを SceneView に表示するワールド座標。
	Vector3 directionalLightIconPosition = {-1.8f, 1.4f, 0.0f};

	EditorSceneObjectManager editorSceneObjectManager;
	// editorSceneObjectManager は GameObject に対応する DirectX 描画用 SceneObject を保持する。
	editorSceneObjectManager.Initialize(device.Get());

	std::vector<EditorSceneObject>& editorSceneObjects = editorSceneObjectManager.GetSceneObjects();
	// editorSceneObjects は SceneObjectManager 内部配列への参照。
	int32_t selectedPlacedSceneObjectIndex = -1; // selectedPlacedSceneObjectIndex は SceneObject 配列の選択中 index。-1 は未選択。
	ComPtr<ID3D12Fence> fence; // fence は CPU が GPU 処理完了を待つための同期オブジェクト。
	uint64_t fenceValue = 0; // fenceValue は Signal ごとに進める GPU 完了確認用カウンタ。
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
	assert(SUCCEEDED(hr));

	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	// fenceEvent は GPU 完了通知を CPU が WaitForSingleObject で待つための Win32 Event。
	assert(fenceEvent != nullptr);

	// Event 作成に失敗すると GPU 待ちができないため、描画開始前に終了する。
	if (fenceEvent == nullptr) {
		RequestInitializationFailure();
		return;
	}

	auto waitForGpu = [&]() {
		fenceValue++; // fenceValue を進め、今回待つ GPU 作業番号を作る。
		HRESULT signalResult = commandQueue->Signal(fence.Get(), fenceValue);
		// Signal は commandQueue に現在の fenceValue を完了予定として登録する。
		assert(SUCCEEDED(signalResult));

		// GPU がまだ指定値まで終わっていなければ、Event を登録して CPU を待機させる。
		if (fence->GetCompletedValue() < fenceValue) {
			HRESULT eventResult = fence->SetEventOnCompletion(fenceValue, fenceEvent);
			assert(SUCCEEDED(eventResult));
			WaitForSingleObject(fenceEvent, INFINITE);
		}
	};

	auto resizeRenderTargets = [&](uint32_t width, uint32_t height) {
		// 既に同じサイズなら SwapChain と DepthStencil を作り直さない。
		if (renderWidth == width && renderHeight == height) {
			return;
		}

		waitForGpu(); // ResizeBuffers 前に GPU が古い back buffer を使い終わるまで待つ。

		for (ID3D12Resource*& swapChainResource : swapChainResources) {
			// 古い SwapChain buffer は ResizeBuffers 前に必ず Release する。
			if (swapChainResource != nullptr) {
				swapChainResource->Release();
				swapChainResource = nullptr;
			}
		}

		// 古い DepthStencil も描画サイズが変わるため作り直す。
		if (depthStencilResource != nullptr) {
			depthStencilResource->Release();
			depthStencilResource = nullptr;
		}

		renderWidth = width; // renderWidth / renderHeight は新しい SwapChain サイズ。
		renderHeight = height;

		// ResizeBuffers は SwapChain の back buffer 実体を新しいサイズで再生成する。
		HRESULT resizeResult = swapChain->ResizeBuffers(
			swapChainDesc.BufferCount,
			renderWidth,
			renderHeight,
			swapChainDesc.Format,
			0);
		assert(SUCCEEDED(resizeResult));

		for (uint32_t bufferIndex = 0; bufferIndex < swapChainDesc.BufferCount; ++bufferIndex) {
			// Resize 後の新しい back buffer を取得して RTV を張り直す。
			HRESULT getBufferResult =
				swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapChainResources[bufferIndex]));
			assert(SUCCEEDED(getBufferResult));
			device->CreateRenderTargetView(swapChainResources[bufferIndex], &rtvDesc, rtvHandles[bufferIndex]);
		}

		depthStencilResource = createDepthStencilResource(renderWidth, renderHeight);
		// DepthStencil も新しい renderWidth / renderHeight に合わせて再生成する。
		device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandle);
	};

	hr = commandAllocator->Reset(); // テクスチャアップロード用に CommandAllocator と CommandList を記録可能状態へ戻す。
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr);
	assert(SUCCEEDED(hr));

	// intermediateResources は Texture upload のための一時 Upload Buffer。
	ID3D12Resource* intermediateResources[_countof(textureFilePaths)] = {nullptr};
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		// UploadTextureData は textureResources に mipImages をコピーする命令を CommandList へ積む。
		intermediateResources[textureIndex] = UploadTextureData(
			device.Get(), commandList.Get(), textureResources[textureIndex], mipImages[textureIndex]);
	}

	UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// srvDescriptorSize は SRV Heap 内で次の Descriptor へ進むバイト幅。
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandlesCPU[_countof(textureFilePaths)];
	// textureSrvHandlesCPU は CreateShaderResourceView に渡す CPU 側 SRV ハンドル。
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandlesGPU[_countof(textureFilePaths)];
	// textureSrvHandlesGPU は Draw 時に Shader へ渡す GPU 側 SRV ハンドル。
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		// srvDesc は 2D Texture SRV として mipmap 付き画像を Shader から読む設定。
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = textureMetadatas[textureIndex].format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(textureMetadatas[textureIndex].mipLevels);

		// index + 1 にするのは 0 番を ImGui 用 SRV に空けるため。
		textureSrvHandlesCPU[textureIndex] = GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize,
		                                                            textureIndex + 1);
		textureSrvHandlesGPU[textureIndex] = GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize,
		                                                            textureIndex + 1);
		device->CreateShaderResourceView(textureResources[textureIndex], &srvDesc, textureSrvHandlesCPU[textureIndex]);
	}

	hr = commandList->Close(); // Texture upload 用 CommandList を閉じて GPU に実行させる。
	assert(SUCCEEDED(hr));

	// uploadCommandLists は ExecuteCommandLists に渡す CommandList 配列。
	ID3D12CommandList* uploadCommandLists[] = {commandList.Get()};
	commandQueue->ExecuteCommandLists(1, uploadCommandLists);

	fenceValue++; // 初期アップロードが完了するまで待ち、以降の描画で Texture を安全に参照する。
	hr = commandQueue->Signal(fence.Get(), fenceValue);
	assert(SUCCEEDED(hr));
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		assert(SUCCEEDED(hr));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

#ifdef USE_IMGUI

	IMGUI_CHECKVERSION(); // ImGui のバージョン整合性を確認してから Context を作る。
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO(); // io は Docking 有効化や Font 設定を行う ImGui の入出力設定。
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // DockingEnable でウィンドウのドラッグ移動・ドッキングを許可する。
	io.ConfigDockingWithShift = false; // Shift なしで Docking できるようにして Unity 風の操作感にする。
	ImGui::StyleColorsDark(); // 既存 UI と合わせて Dark Style を基準にする。
	ImGui_ImplWin32_Init(windowHandle); // Win32 backend は HWND からマウス・キーボード入力を受け取る。

	// DX12 backend は SRV Heap 0 番を ImGui Font Texture 用に使う。
	ImGui_ImplDX12_Init(
		device.Get(),
		static_cast<int>(swapChainDesc.BufferCount),
		rtvDesc.Format,
		srvDescriptorHeap,
		GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 0),
		GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 0));

	// meiryo.ttc があれば日本語 UI が文字化けしないように読み込む。
	if (std::filesystem::exists("C:/Windows/Fonts/meiryo.ttc")) {
		io.Fonts->AddFontFromFileTTF(
			"C:/Windows/Fonts/meiryo.ttc", 16.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	}

	io.Fonts->Build(); // Font Atlas をここで構築し、最初のフレームで日本語フォントを使える状態にする。
#endif

	g_instanceHandle = instanceHandle; // ここから下は、Initialize 内のローカル生成物を EditorSharedState の共有状態へ移す。
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
	g_depthStencilResource = depthStencilResource;
	g_renderWidth = renderWidth;
	g_renderHeight = renderHeight;
	g_dxcUtils = dxcUtils;
	g_dxcCompiler = dxcCompiler;
	g_includeHandler = includeHandler;
	g_vertexShaderBlob = vertexShaderBlob;
	g_pixelShaderBlob = pixelShaderBlob;
	g_signatureBlob = signatureBlob;
	g_errorBlob = errorBlob;
	g_rootSignature = rootSignature;
	g_graphicsPipelineState = graphicsPipelineState;
	g_spriteMaterialResource = spriteMaterialResource;
	g_spriteMaterialData = spriteMaterialData;
	g_sphereMaterialResource = sphereMaterialResource;
	g_sphereMaterialData = sphereMaterialData;
	g_directionalLightResource = directionalLightResource;
	g_directionalLightData = directionalLightData;
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
	g_feelKitHaptics.initialize({});
	g_isInitialized = true;
	g_isInitializationFailed = false;
	g_isEndRequested = false;
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

	if (PeekMessage(&g_message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
		TranslateMessage(&g_message);
		DispatchMessage(&g_message);

		if (g_message.message == WM_QUIT) {
			g_exitCode = static_cast<int>(g_message.wParam);
			g_isEndRequested = true;
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
	auto& depthStencilResource = g_depthStencilResource;
	auto& renderWidth = g_renderWidth;
	auto& renderHeight = g_renderHeight;
	auto& dxcUtils = g_dxcUtils;
	auto& dxcCompiler = g_dxcCompiler;
	auto& includeHandler = g_includeHandler;
	auto& vertexShaderBlob = g_vertexShaderBlob;
	auto& pixelShaderBlob = g_pixelShaderBlob;
	auto& signatureBlob = g_signatureBlob;
	auto& errorBlob = g_errorBlob;
	auto& rootSignature = g_rootSignature;
	auto& graphicsPipelineState = g_graphicsPipelineState;
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
