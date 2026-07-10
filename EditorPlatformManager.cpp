#pragma warning(disable : 4189 4514)

#include "EditorPlatformManager.h"
#include <onnxruntime_cxx_api.h>
#include <xaudio2.h>
#include <xaudio2fx.h>
#include "EditorSharedState.h"
using namespace EditorSharedState;

namespace {
	//================================================================
	// 蛻晄悄蛹門､ｱ謨玲凾縺ｮ邨ゆｺ・ｦ∵ｱ・
	//================================================================

	void RequestInitializationFailure() {
		g_isInitializationFailed = true; // g_isInitializationFailed 縺ｯ GameScene 縺悟ｾ檎ｶ・Manager 蛻晄悄蛹悶ｒ豁｢繧√ｋ縺溘ａ縺ｮ繝輔Λ繧ｰ縲・
		g_isEndRequested = true; // g_isEndRequested 縺ｯ WinMain 縺ｮ繝ｫ繝ｼ繝励∈蜈･繧峨↑縺・ｈ縺・↓縺吶ｋ邨ゆｺ・ｦ∵ｱゅヵ繝ｩ繧ｰ縲・
		g_exitCode = 1; // 1 縺ｯ蛻晄悄蛹門､ｱ謨励ｒ陦ｨ縺咏ｵゆｺ・さ繝ｼ繝峨・
	}

	VertexData MakePrimitiveVertex(float x, float y, float z, float u, float v, const Vector3& normal) {
		// 蝓ｺ譛ｬ蠖｢繝｡繝・す繝･縺ｮ 1 鬆らせ繧剃ｽ懊ｋ縲Ｑosition 縺ｯ繝ｭ繝ｼ繧ｫ繝ｫ蠎ｧ讓吶》excoord 縺ｯ checker 陦ｨ遉ｺ逕ｨ縲・
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
		// DrawInstanced 縺ｯ triangle list 縺ｪ縺ｮ縺ｧ縲∽ｸ芽ｧ貞ｽ｢蜊倅ｽ阪〒鬆らせ繧定ｿｽ蜉縺吶ｋ縲・
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
		// 蝗幄ｧ貞ｽ｢縺ｯ 2 譫壹・荳芽ｧ貞ｽ｢縺ｫ蛻・￠縺ｦ霑ｽ蜉縺吶ｋ縲・
		AddPrimitiveTriangle(vertices, a, b, c);
		AddPrimitiveTriangle(vertices, c, b, d);
	}

	std::vector<VertexData> CreateBoxVertices(const Vector3& halfSize) {
		std::vector<VertexData> vertices; // vertices 縺ｯ Box 6 髱｢蛻・・荳芽ｧ貞ｽ｢鬆らせ縲・
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
		std::vector<VertexData> vertices; // vertices 縺ｯ蛛ｴ髱｢縲∽ｸ企擇縲∽ｸ矩擇繧呈戟縺､蜀・浤繝｡繝・す繝･縲・
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
		std::vector<VertexData> vertices; // vertices 縺ｯ蛛ｴ髱｢縺ｨ蠎暮擇繧呈戟縺､蜀・倹繝｡繝・す繝･縲・
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
		std::vector<VertexData> vertices; // vertices 縺ｯ繝峨・繝翫ヤ蠖｢迥ｶ縺ｮ繝医・繝ｩ繧ｹ繝｡繝・す繝･縲・
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
		std::vector<VertexData> vertices; // vertices 縺ｯ菴弱・繝ｪ繧ｴ繝ｳ蝓ｺ譛ｬ蠖｢縺ｨ縺励※菴ｿ縺・・髱｢菴薙Γ繝・す繝･縲・
		vertices.reserve(24);
		constexpr Vector3 top{0.0f, 0.6f, 0.0f};
		constexpr Vector3 bottom{0.0f, -0.6f, 0.0f};
		constexpr Vector3 front{0.0f, 0.0f, -0.6f};
		constexpr Vector3 right{0.6f, 0.0f, 0.0f};
		constexpr Vector3 back{0.0f, 0.0f, 0.6f};
		constexpr Vector3 left{-0.6f, 0.0f, 0.0f};

		auto addFace = [&](const Vector3& a, const Vector3& b, const Vector3& c) {
			Vector3 normal = Normalize(Cross(Subtract(b, a), Subtract(c, a)));
			// 髱｢豕慕ｷ壹ｒ險育ｮ励＠縺ｦ繝輔Λ繝・ヨ繧ｷ繧ｧ繝ｼ繝・ぅ繝ｳ繧ｰ縺ｫ縺吶ｋ縲・
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
		std::vector<VertexData> vertices; // vertices 縺ｯ邱ｯ蠎ｦ邨悟ｺｦ縺ｧ菴懊ｋ逅・・荳芽ｧ貞ｽ｢鬆らせ縲・
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
		ModelData modelData{}; // modelData 縺ｯ蝓ｺ譛ｬ蠖｢縺斐→縺ｮ鬆らせ驟榊・繧貞・繧後ｋ謌ｻ繧雁､縲・

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
			auto meshType = static_cast<EditorModelMeshType>(meshTypeIndex); // 驟榊・逡ｪ蜿ｷ繧貞渕譛ｬ蠖｢ enum 縺ｨ縺励※謇ｱ縺・・
			primitiveModelData[meshTypeIndex] = CreatePrimitiveModelData(meshType, fallbackPlaneModelData);
			primitiveVertexCounts[meshTypeIndex] =
				static_cast<uint32_t>(primitiveModelData[meshTypeIndex].vertices.size());

			if (primitiveVertexCounts[meshTypeIndex] == 0u) {
				continue;
			}

			size_t bufferSize = sizeof(VertexData) * primitiveModelData[meshTypeIndex].vertices.size();
			// 鬆らせ驟榊・繧剃ｸｸ縺斐→ UploadBuffer 縺ｫ鄂ｮ縺上・
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
	InstallCrashHandler(); // 繧ｯ繝ｩ繝・す繝･譎ゅ↓ dump 繧呈ｮ九○繧九ｈ縺・∵怙蛻昴↓萓句､悶ワ繝ｳ繝峨Λ繧堤匳骭ｲ縺吶ｋ縲・

	//================================================================
	// 繝ｭ繧ｰ繝輔ぃ繧､繝ｫ縺ｮ菴懈・
	//================================================================

	std::filesystem::create_directory("logs"); // logs 繝輔か繝ｫ繝縺ｯ螳溯｡後＃縺ｨ縺ｮ .Log 繝輔ぃ繧､繝ｫ繧剃ｿ晏ｭ倥☆繧句ｴ謇縲・
	std::time_t now = std::time(nullptr); // now / localTime 縺ｯ繝ｭ繧ｰ繝輔ぃ繧､繝ｫ蜷阪↓菴ｿ縺・樟蝨ｨ譎ょ綾縲・
	std::tm localTime{};
	localtime_s(&localTime, &now);

	// dateString 縺ｯ yyyyMMdd_HHmmss 蠖｢蠑上・繝ｭ繧ｰ繝輔ぃ繧､繝ｫ蜷埼Κ蛻・・
	std::string dateString = std::format(
		"{:04}{:02}{:02}_{:02}{:02}{:02}",
		localTime.tm_year + 1900,
		localTime.tm_mon + 1,
		localTime.tm_mday,
		localTime.tm_hour,
		localTime.tm_min,
		localTime.tm_sec);

	auto logFilePath = std::string("logs/" + dateString + ".Log"); // logFilePath 縺ｯ莉雁屓縺ｮ螳溯｡後Ο繧ｰ縺ｮ菫晏ｭ伜・縲・
	std::ofstream logStream(logFilePath); // logStream 縺ｯ蛻晄悄蛹悶Ο繧ｰ縺ｨ繧ｷ繧ｧ繝ｼ繝繝ｼ繧ｳ繝ｳ繝代う繝ｫ繝ｭ繧ｰ繧呈嶌縺崎ｾｼ繧蜃ｺ蜉帛・縲・

	// 繝ｭ繧ｰ縺碁幕縺代↑縺・ｴ蜷医・縲∽ｻ･髯阪・蛻晄悄蛹也憾豕√ｒ險倬鹸縺ｧ縺阪↑縺・◆繧∬ｵｷ蜍輔ｒ豁｢繧√ｋ縲・
	if (!logStream) {
		RequestInitializationFailure();
		return;
	}

	HWND windowHandle = CreateMainWindow(instanceHandle, logStream);
	// windowHandle 縺ｯ DirectX SwapChain 縺ｨ DirectInput 縺ｮ蜊碑ｪｿ繝ｬ繝吶Ν險ｭ螳壹↓菴ｿ縺・HWND縲・

	// Window 菴懈・螟ｱ謨玲凾縺ｯ DirectX / ImGui 縺・HWND 繧剃ｽｿ縺医↑縺・◆繧∫ｵゆｺ・☆繧九・
	if (windowHandle == nullptr) {
		RequestInitializationFailure();
		return;
	}

	HRESULT hr = S_OK; // hr 縺ｯ Win32 / DirectX / XAudio2 API 縺ｮ謌仙凄繧貞女縺大叙繧句・騾・HRESULT縲・
	IDirectInput8* directInput = nullptr; // directInput 縺ｯ繧ｭ繝ｼ繝懊・繝峨ョ繝舌う繧ｹ繧剃ｽ懊ｋ DirectInput 譛ｬ菴薙・
	hr = DirectInput8Create(
		instanceHandle, DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast<void**>(&directInput), nullptr);
	assert(SUCCEEDED(hr));

	// directInput 縺御ｽ懊ｌ縺ｪ縺・ｴ蜷医√お繝・ぅ繧ｿ繝ｼ謫堺ｽ懊・蜈･蜉帙ｒ蜿門ｾ励〒縺阪↑縺・◆繧∬ｵｷ蜍輔ｒ豁｢繧√ｋ縲・
	if (FAILED(hr) || directInput == nullptr) {
		RequestInitializationFailure();
		return;
	}

	IDirectInputDevice8* keyboardDevice = nullptr; // keyboardDevice 縺ｯ DIK_* 縺ｮ謚ｼ荳狗憾諷九ｒ蜿門ｾ励☆繧九◆繧√・繧ｭ繝ｼ繝懊・繝牙・蜉帙ョ繝舌う繧ｹ縲・
	hr = directInput->CreateDevice(GUID_SysKeyboard, &keyboardDevice, nullptr);
	assert(SUCCEEDED(hr));

	// 繧ｭ繝ｼ繝懊・繝峨ョ繝舌う繧ｹ菴懈・螟ｱ謨玲凾縺ｯ directInput 繧定ｧ｣謾ｾ縺励※縺九ｉ邨ゆｺ・☆繧九・
	if (FAILED(hr) || keyboardDevice == nullptr) {
		directInput->Release();
		RequestInitializationFailure();
		return;
	}

	hr = keyboardDevice->SetDataFormat(&c_dfDIKeyboard);
	// c_dfDIKeyboard 縺ｯ DirectInput 縺ｮ 256 繧ｭ繝ｼ驟榊・蠖｢蠑上〒蜈･蜉帙ｒ蜿励￠蜿悶ｋ謖・ｮ壹・
	assert(SUCCEEDED(hr));

	// foreground / nonexclusive 縺ｯ莉悶い繝励Μ縺ｨ蜈･蜉帙ｒ螂ｪ縺・粋繧上↑縺・お繝・ぅ繧ｿ繝ｼ蜷代￠險ｭ螳壹・
	hr = keyboardDevice->SetCooperativeLevel(
		windowHandle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));

	//================================================================
	// DirectInput 繝槭え繧ｹ繝・ヰ繧､繧ｹ菴懈・
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

	// key / preKey 縺ｯ蛻晄悄蛹也峩蠕後・莉翫ヵ繝ｬ繝ｼ繝繝ｻ蜑阪ヵ繝ｬ繝ｼ繝蜈･蜉帷憾諷九・
	BYTE key[256] = {};
	BYTE preKey[256] = {};

#ifdef _DEBUG
	//================================================================
	// DirectX12 繝・ヰ繝・げ繝ｬ繧､繝､繝ｼ
	//================================================================

	ComPtr<ID3D12Debug1> debugController;
	// debugController 縺ｯ DirectX12 縺ｮ繧ｨ繝ｩ繝ｼ繧・ｭｦ蜻翫ｒ Visual Studio 縺ｫ蜃ｺ縺吶◆繧√・蛻ｶ蠕｡繧ｪ繝悶ず繧ｧ繧ｯ繝医・

	// Debug 繝薙Ν繝峨□縺・GPU 讀懆ｨｼ繧呈怏蜉ｹ縺ｫ縺励∝些髯ｺ縺ｪ API 菴ｿ逕ｨ繧呈掠繧√↓讀懷・縺吶ｋ縲・
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	// message 縺ｯ Win32 繝｡繝・そ繝ｼ繧ｸ繝ｫ繝ｼ繝励〒菴ｿ縺・樟蝨ｨ繝｡繝・そ繝ｼ繧ｸ縲・
	MSG message{};
	Log(logStream, "main loop started");

	//================================================================
	// XAudio2 縺ｮ蛻晄悄蛹・
	//================================================================

	IXAudio2* xAudio2 = nullptr; // xAudio2 縺ｯ髻ｳ螢ｰ蜀咲函繧ｨ繝ｳ繧ｸ繝ｳ譛ｬ菴薙・
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(hr));

	// XAudio2 縺御ｽ懊ｌ縺ｪ縺・ｴ蜷医・浹螢ｰ繝ｪ繧ｽ繝ｼ繧ｹ繧剃ｿ晄戟縺ｧ縺阪↑縺・◆繧∫ｵゆｺ・☆繧九・
	if (FAILED(hr) || xAudio2 == nullptr) {
		RequestInitializationFailure();
		return;
	}

	IXAudio2MasteringVoice* masterVoice = nullptr; // masterVoice 縺ｯ譛邨ら噪縺ｫ繧ｹ繝斐・繧ｫ繝ｼ縺ｸ騾√ｋ蜃ｺ蜉・Voice縲・
	hr = xAudio2->CreateMasteringVoice(&masterVoice);
	assert(SUCCEEDED(hr));

	// masterVoice 菴懈・螟ｱ謨玲凾縺ｯ XAudio2 譛ｬ菴薙ｒ隗｣謾ｾ縺励※邨ゆｺ・☆繧九・
	if (FAILED(hr) || masterVoice == nullptr) {
		xAudio2->Release();
		RequestInitializationFailure();
		return;
	}

	SoundData soundData = SoundLoadWave("resources/sound/maou_19_12345.wav");
	// soundData 縺ｯ襍ｷ蜍慕｢ｺ隱咲畑 wav 縺ｮ PCM 繝・・繧ｿ縺ｨ繝輔か繝ｼ繝槭ャ繝域ュ蝣ｱ縲・
	IXAudio2SourceVoice* sourceVoice = nullptr; // sourceVoice 縺ｯ soundData 繧貞・逕溘く繝･繝ｼ縺ｸ遨阪・ Voice縲・
	hr = xAudio2->CreateSourceVoice(&sourceVoice, &soundData.wfex);
	assert(SUCCEEDED(hr));

	// sourceVoice 菴懈・螟ｱ謨玲凾縺ｯ隱ｭ縺ｿ霎ｼ繧薙□ wav 縺ｨ Voice 繧定ｧ｣謾ｾ縺励※邨ゆｺ・☆繧九・
	if (FAILED(hr) || sourceVoice == nullptr) {
		SoundUnload(&soundData);
		masterVoice->DestroyVoice();
		xAudio2->Release();
		RequestInitializationFailure();
		return;
	}

	// soundBuffer 縺ｯ sourceVoice 縺ｫ貂｡縺吝・逕溘ョ繝ｼ繧ｿ縺ｮ遽・峇縺ｨ邨らｫｯ繝輔Λ繧ｰ縲・
	XAUDIO2_BUFFER soundBuffer{};
	soundBuffer.pAudioData = soundData.pBuffer;
	soundBuffer.AudioBytes = soundData.bufferSize;
	soundBuffer.Flags = XAUDIO2_END_OF_STREAM;
	hr = sourceVoice->SubmitSourceBuffer(&soundBuffer);
	assert(SUCCEEDED(hr));

	//================================================================
	// DirectX12 縺ｮ蛻晄悄蛹・
	//================================================================

	ComPtr<IDXGIFactory7> dxgiFactory; // dxgiFactory 縺ｯ GPU Adapter 縺ｨ SwapChain 繧剃ｽ懊ｋ縺溘ａ縺ｮ DXGI 蜈･蜿｣縲・
	hr = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
	assert(SUCCEEDED(hr));

	ComPtr<IDXGIAdapter4> useAdapter; // useAdapter 縺ｯ螳滄圀縺ｫ D3D12Device 繧剃ｽ懊ｋ迚ｩ逅・GPU縲・
	for (UINT adapterIndex = 0;; ++adapterIndex) {
		ComPtr<IDXGIAdapter4> candidateAdapter; // candidateAdapter 縺ｯ諤ｧ閭ｽ蜆ｪ蜈磯・↓蛻玲嫌縺励◆ GPU 蛟呵｣懊・
		if (dxgiFactory->EnumAdapterByGpuPreference(
			adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(candidateAdapter.GetAddressOf())) == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		// adapterDesc 縺ｯ GPU 蜷阪→ Software Adapter 蛻､螳壹↓菴ｿ縺・ュ蝣ｱ縲・
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = candidateAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));

		// Software Adapter 縺ｯ WARP 縺ｪ縺ｮ縺ｧ縲√ご繝ｼ繝謠冗判縺ｫ菴ｿ縺・ｮ・GPU 蛟呵｣懊°繧牙､悶☆縲・
		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
			continue;
		}

		useAdapter = candidateAdapter; // 譛蛻昴↓隕九▽縺九▲縺溷ｮ・GPU 繧剃ｽｿ逕ｨ Adapter 縺ｨ縺励※謗｡逕ｨ縺吶ｋ縲・
		Log(logStream, std::format("Use Adapter:{}", ConvertString(std::wstring{adapterDesc.Description})));
		break;
	}
	assert(useAdapter != nullptr);

	ComPtr<ID3D12Device> device; // device 縺ｯ DirectX12 繝ｪ繧ｽ繝ｼ繧ｹ逕滓・縺ｨ繧ｳ繝槭Φ繝臥匱陦後・荳ｭ蠢・↓縺ｪ繧・GPU 繝・ヰ繧､繧ｹ縲・
	hr = D3D12CreateDevice(useAdapter.Get(), D3D_FEATURE_LEVEL_12_2, IID_PPV_ARGS(device.GetAddressOf()));
	// FeatureLevel 縺ｯ 12.2 縺九ｉ鬆・↓隧ｦ縺励￣C 縺悟ｯｾ蠢懊☆繧倶ｸ逡ｪ鬮倥＞讖溯・繝ｬ繝吶Ν縺ｧ菴懊ｋ縲・
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
	ComPtr<ID3D12InfoQueue> infoQueue; // infoQueue 縺ｯ DirectX12 縺ｮ驥榊､ｧ繧ｨ繝ｩ繝ｼ繧偵ョ繝舌ャ繧ｬ蛛懈ｭ｢縺ｫ螟峨∴繧九◆繧√・險ｺ譁ｭ繧ｭ繝･繝ｼ縲・
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(infoQueue.GetAddressOf())))) {
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// 遐ｴ謳阪・繧ｨ繝ｩ繝ｼ繝ｻ隴ｦ蜻翫ｒ縺昴・蝣ｴ縺ｧ豁｢繧√∝次蝗邂・園繧定ｿｽ縺・ｄ縺吶￥縺吶ｋ縲・
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		// denyIds 縺ｯ譌｢遏･縺ｮ繝弱う繧ｺ隴ｦ蜻翫ｒ Debug 蜃ｺ蜉帙°繧蛾勁螟悶☆繧九◆繧√・ ID 荳隕ｧ縲・
		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE,
		};

		// severities 縺ｯ諠・ｱ繝｡繝・そ繝ｼ繧ｸ繧呈椛蛻ｶ縺吶ｋ縺溘ａ縺ｮ驥榊､ｧ蠎ｦ荳隕ｧ縲・
		D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO};

		// filter 縺ｯ denyIds / severities 繧・InfoQueue 縺ｫ貂｡縺吶◆繧√・險ｭ螳壹・
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		infoQueue->PushStorageFilter(&filter);
	}
#endif

	ComPtr<ID3D12CommandQueue> commandQueue; // commandQueue 縺ｯ GPU 縺ｫ CommandList 繧帝√ｋ繧ｭ繝･繝ｼ縲・

	// commandQueueDesc 縺ｯ讓呎ｺ悶・ Direct CommandQueue 繧剃ｽ懊ｋ縺溘ａ縺ｮ險ｭ螳壹・
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// CommandQueue 縺後↑縺・→謠冗判蜻ｽ莉､繧・GPU 縺ｫ騾√ｌ縺ｪ縺・◆繧∫ｵゆｺ・☆繧九・
	if (FAILED(hr) || commandQueue == nullptr) {
		RequestInitializationFailure();
		return;
	}


	ComPtr<ID3D12CommandAllocator> commandAllocator; // commandAllocator 縺ｯ CommandList 縺瑚ｨ倬鹸縺吶ｋ蜻ｽ莉､繝｡繝｢繝ｪ繧堤ｮ｡逅・☆繧九・
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// CommandAllocator 縺後↑縺・→ CommandList 繧・Reset 縺ｧ縺阪↑縺・◆繧∫ｵゆｺ・☆繧九・
	if (FAILED(hr) || commandAllocator == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12GraphicsCommandList> commandList;
	// commandList 縺ｯ謠冗判繝ｻ繧ｳ繝斐・繝ｻResourceBarrier 縺ｮ蜻ｽ莉､繧定ｨ倬鹸縺吶ｋ繧ｪ繝悶ず繧ｧ繧ｯ繝医・
	hr = device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// CommandList 縺後↑縺・→蛻晄悄繝・け繧ｹ繝√Ε繧｢繝・・繝ｭ繝ｼ繝峨ｂ謠冗判繧ゅ〒縺阪↑縺・◆繧∫ｵゆｺ・☆繧九・
	if (FAILED(hr) || commandList == nullptr) {
		RequestInitializationFailure();
		return;
	}

	hr = commandList->Close(); // 菴懈・逶ｴ蠕後・ CommandList 縺ｯ髢九＞縺ｦ縺・ｋ縺溘ａ縲∽ｸ蠎ｦ Close 縺励※騾壼ｸｸ縺ｮ Reset 謇矩・↓蜷医ｏ縺帙ｋ縲・
	assert(SUCCEEDED(hr));

	// clientRect 縺ｯ Window 蜀・・謠冗判蜿ｯ閭ｽ鬆伜沺縲４wapChain 繧ｵ繧､繧ｺ縺ｮ蛻晄悄蛟､縺ｫ菴ｿ縺・・
	RECT clientRect{};
	GetClientRect(windowHandle, &clientRect);

	uint32_t renderWidth = (std::max)(1u, static_cast<uint32_t>(clientRect.right - clientRect.left));
	// renderWidth / renderHeight 縺ｯ譛蟆丞喧縺ｪ縺ｩ縺ｧ 0 縺ｫ縺ｪ繧峨↑縺・ｈ縺・1 莉･荳翫↓縺吶ｋ縲・
	uint32_t renderHeight = (std::max)(1u, static_cast<uint32_t>(clientRect.bottom - clientRect.top));

	ComPtr<IDXGISwapChain4> swapChain; // swapChain 縺ｯ Window 縺ｫ陦ｨ遉ｺ縺吶ｋ繝舌ャ繧ｯ繝舌ャ繝輔ぃ蛻励・

	// swapChainDesc 縺ｯ繝舌ャ繧ｯ繝舌ャ繝輔ぃ謨ｰ縲∝ｽ｢蠑上∬｡ｨ遉ｺ譁ｹ蠑上ｒ謖・ｮ壹☆繧玖ｨｭ螳壹・
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

	// SwapChain 縺後↑縺・→ Window 縺ｸ Present 縺ｧ縺阪↑縺・◆繧∫ｵゆｺ・☆繧九・
	if (FAILED(hr) || swapChain == nullptr) {
		RequestInitializationFailure();
		return;
	}

	// rtvDescriptorHeap 縺ｯ SwapChain 繝舌ャ繧ｯ繝舌ャ繝輔ぃ繧・RenderTarget 縺ｨ縺励※蜿ら・縺吶ｋ Heap縲・
	ID3D12DescriptorHeap* rtvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	// srvDescriptorHeap 縺ｯ Texture SRV 縺ｨ ImGui 逕ｨ SRV 繧・Shader 縺九ｉ蜿ら・縺吶ｋ Heap縲・
	ID3D12DescriptorHeap* srvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	// dsvDescriptorHeap 縺ｯ DepthStencil 繧貞盾辣ｧ縺吶ｋ Heap縲・
	ID3D12DescriptorHeap* dsvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);

	// swapChainResources 縺ｯ 2 譫壹・繝舌ャ繧ｯ繝舌ャ繝輔ぃ螳滉ｽ薙・
	ID3D12Resource* swapChainResources[2] = {nullptr};
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	// rtvDesc 縺ｯ繝舌ャ繧ｯ繝舌ャ繝輔ぃ繧・2D RenderTarget 縺ｨ縺励※謇ｱ縺・ｨｭ螳壹・
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2]; // rtvHandles 縺ｯ蜷・ヰ繝・け繝舌ャ繝輔ぃ縺ｫ蟇ｾ蠢懊☆繧・CPU 蛛ｴ RTV 繝上Φ繝峨Ν縲・
	rtvHandles[0] = GetCPUDescriptorHandle(
		rtvDescriptorHeap, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), 0);
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);
	rtvHandles[1] = GetCPUDescriptorHandle(
		rtvDescriptorHeap, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV), 1);
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

	// depthClearValue 縺ｯ DepthStencil 繧・Clear 縺吶ｋ譎ゅ・蛻晄悄蛟､縲・
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.DepthStencil.Stencil = 0;

	// dsvDesc 縺ｯ DepthStencilResource 繧・DSV 縺ｨ縺励※菴ｿ縺・◆繧√・險ｭ螳壹・
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	// dsvHandle 縺ｯ DepthStencilView 繧剃ｽ懈・縺吶ｋ CPU 蛛ｴ繝上Φ繝峨Ν縲・

	auto createDepthStencilResource = [&](uint32_t width, uint32_t height) -> ID3D12Resource* {
		// depthStencilResourceDesc 縺ｯ SceneView 縺ｨ蜷後§繧ｵ繧､繧ｺ縺ｮ Depth 繝舌ャ繝輔ぃ險ｭ螳壹・
		D3D12_RESOURCE_DESC depthStencilResourceDesc{};
		depthStencilResourceDesc.Width = width;
		depthStencilResourceDesc.Height = height;
		depthStencilResourceDesc.MipLevels = 1;
		depthStencilResourceDesc.DepthOrArraySize = 1;
		depthStencilResourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilResourceDesc.SampleDesc.Count = 1;
		depthStencilResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthStencilResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		// depthStencilHeapProperties 縺ｯ GPU 蟆ら畑繝｡繝｢繝ｪ荳翫↓ Depth 繝舌ャ繝輔ぃ繧堤ｽｮ縺乗欠螳壹・
		D3D12_HEAP_PROPERTIES depthStencilHeapProperties{};
		depthStencilHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		ID3D12Resource* newDepthStencilResource = nullptr; // newDepthStencilResource 縺ｯ菴懈・縺励※霑斐☆ DepthStencil 螳滉ｽ薙・
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
	// depthStencilResource 縺ｯ迴ｾ蝨ｨ縺ｮ謠冗判繧ｵ繧､繧ｺ縺ｫ蜷医ｏ縺帙◆ Depth 繝舌ャ繝輔ぃ縲・
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandle);

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

	ComPtr<IDxcUtils> dxcUtils; // dxcUtils 縺ｯ HLSL 繝輔ぃ繧､繝ｫ隱ｭ縺ｿ霎ｼ縺ｿ縺ｨ IncludeHandler 菴懈・縺ｫ菴ｿ縺・DXC 陬懷勧縲・
	ComPtr<IDxcCompiler3> dxcCompiler; // dxcCompiler 縺ｯ HLSL 繧・DXIL 縺ｸ繧ｳ繝ｳ繝代う繝ｫ縺吶ｋ DXC 繧ｳ繝ｳ繝代う繝ｩ縲・
	ComPtr<IDxcIncludeHandler> includeHandler; // includeHandler 縺ｯ shader 縺ｮ #include 繧定ｧ｣豎ｺ縺吶ｋ縺溘ａ縺ｮ讓呎ｺ悶ワ繝ｳ繝峨Λ縲・
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxcUtils.GetAddressOf()));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxcCompiler.GetAddressOf()));
	assert(SUCCEEDED(hr));
	hr = dxcUtils->CreateDefaultIncludeHandler(includeHandler.GetAddressOf());
	assert(SUCCEEDED(hr));

	// vertexShaderBlob 縺ｯ VS main 縺ｮ繧ｳ繝ｳ繝代う繝ｫ貂医∩繝舌う繝医さ繝ｼ繝峨・
	ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(
		L"Object3d.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	// pixelShaderBlob 縺ｯ PS main 縺ｮ繧ｳ繝ｳ繝代う繝ｫ貂医∩繝舌う繝医さ繝ｼ繝峨・
	ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(
		L"Object3d.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	ComPtr<IDxcBlob> shadowVertexShaderBlob = CompileShader(
		L"ShadowDepth.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	// descriptorRange 縺ｯ Texture SRV 繧・RootSignature 縺ｮ DescriptorTable 縺ｫ貂｡縺咏ｯ・峇縲・
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

	// descriptionRootSignature 縺ｯ Shader 縺ｸ貂｡縺・CBV / SRV / Sampler 縺ｮ蜈･蜿｣螳夂ｾｩ縲・
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// rootParameters 縺ｯ 0:Material縲・:Transform縲・:DirectionalLight縲・:Texture SRV縲・
	D3D12_ROOT_PARAMETER rootParameters[5] = {};

	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	// rootParameters[0] 縺ｯ PixelShader 縺ｮ b0 Material CBV縲・
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[0].Descriptor.RegisterSpace = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	// rootParameters[1] 縺ｯ VertexShader 縺ｮ b0 Transform CBV縲・
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;
	rootParameters[1].Descriptor.RegisterSpace = 0;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	// rootParameters[2] 縺ｯ PixelShader 縺ｮ b1 DirectionalLight CBV縲・
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].Descriptor.ShaderRegister = 1;
	rootParameters[2].Descriptor.RegisterSpace = 0;

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	// rootParameters[3] 縺ｯ PixelShader 縺ｮ t0 Texture SRV DescriptorTable縲・
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[4].DescriptorTable.pDescriptorRanges = shadowDescriptorRange;
	rootParameters[4].DescriptorTable.NumDescriptorRanges = _countof(shadowDescriptorRange);

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	// staticSampler 縺ｯ Texture 繧堤ｷ壼ｽ｢陬憺俣繝ｻWrap 縺ｧ繧ｵ繝ｳ繝励Μ繝ｳ繧ｰ縺吶ｋ險ｭ螳壹・
	D3D12_STATIC_SAMPLER_DESC staticSamplers[2] = {};
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
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	ComPtr<ID3DBlob> signatureBlob; // signatureBlob 縺ｯ RootSignature 縺ｮ繧ｷ繝ｪ繧｢繝ｩ繧､繧ｺ邨先棡縲・
	ComPtr<ID3DBlob> errorBlob; // errorBlob 縺ｯ RootSignature 繧ｷ繝ｪ繧｢繝ｩ繧､繧ｺ螟ｱ謨玲凾縺ｮ繧ｨ繝ｩ繝ｼ譁・ｭ怜・縲・
	ID3D12Resource* spriteMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));
	// spriteMaterialResource 縺ｯ Sprite 謠冗判逕ｨ Material 螳壽焚繝舌ャ繝輔ぃ縲・
	Material* spriteMaterialData = nullptr;
	// spriteMaterialData 縺ｯ CPU 縺九ｉ逶ｴ謗･譖ｸ縺崎ｾｼ繧√ｋ Sprite Material 縺ｮ mapped 繝昴う繝ｳ繧ｿ縲・
	spriteMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&spriteMaterialData));
	spriteMaterialData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	spriteMaterialData->enableLighting = FALSE;
	spriteMaterialData->useTexture = TRUE;
	spriteMaterialData->metallic = 0.0f;
	spriteMaterialData->roughness = 0.5f;
	spriteMaterialData->reflectance = 0.0f;
	spriteMaterialData->ior = 1.0f;
	spriteMaterialData->emissionStrength = 0.0f;
	spriteMaterialData->padding0 = 0.0f;
	spriteMaterialData->padding1 = 0.0f;
	spriteMaterialData->padding2 = 0.0f;
	spriteMaterialData->uvTransform = MakeIdentity4x4();

	ID3D12Resource* sphereMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));
	// sphereMaterialResource 縺ｯ 3D 繝｢繝・Ν謠冗判逕ｨ Material 螳壽焚繝舌ャ繝輔ぃ縲・
	Material* sphereMaterialData = nullptr;
	// sphereMaterialData 縺ｯ CPU 縺九ｉ逶ｴ謗･譖ｸ縺崎ｾｼ繧√ｋ 3D Material 縺ｮ mapped 繝昴う繝ｳ繧ｿ縲・
	sphereMaterialResource->Map(0, nullptr, reinterpret_cast<void**>(&sphereMaterialData));
	sphereMaterialData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	sphereMaterialData->enableLighting = TRUE;
	sphereMaterialData->useTexture = TRUE;
	sphereMaterialData->metallic = 0.0f;
	sphereMaterialData->roughness = 0.5f;
	sphereMaterialData->reflectance = 0.0f;
	sphereMaterialData->ior = 1.0f;
	sphereMaterialData->emissionStrength = 0.0f;
	sphereMaterialData->padding0 = 0.0f;
	sphereMaterialData->padding1 = 0.0f;
	sphereMaterialData->padding2 = 0.0f;
	sphereMaterialData->uvTransform = MakeIdentity4x4();

	ID3D12Resource* directionalLightResource = CreateBufferResource(device.Get(), sizeof(DirectionalLight));
	// directionalLightResource 縺ｯ PixelShader 縺ｫ貂｡縺吝ｹｳ陦悟・貅仙ｮ壽焚繝舌ャ繝輔ぃ縲・
	DirectionalLight* directionalLightData = nullptr;
	// directionalLightData 縺ｯ Inspector 縺九ｉ濶ｲ繝ｻ蜷代″繝ｻ蠑ｷ縺輔ｒ譖ｸ縺肴鋤縺医ｋ mapped 繝昴う繝ｳ繧ｿ縲・
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = {1.0f, 1.0f, 1.0f, 1.0f};
	directionalLightData->direction = {0.0f, -1.0f, 0.0f};
	directionalLightData->intensity = 1.0f;
	directionalLightData->skyUpperColor = {0.45f, 0.58f, 0.78f};
	directionalLightData->skyIntensity = 1.0f;
	directionalLightData->skyLowerColor = {0.12f, 0.14f, 0.18f};
	directionalLightData->skyEmission = 0.08f;
	directionalLightData->ambientIntensity = 0.35f;
	directionalLightData->horizonSharpness = 1.0f;
	directionalLightData->reflectionIntensity = 0.60f;
	directionalLightData->padding = 0.0f;


	// spriteTransformationMatrixResource 縺ｯ Sprite 縺ｮ WVP / World 陦悟・逕ｨ螳壽焚繝舌ャ繝輔ぃ縲・
	ID3D12Resource* spriteTransformationMatrixResource = CreateBufferResource(
		device.Get(), sizeof(TransformationMatrix));

	TransformationMatrix* spriteTransformationMatrixData = nullptr;
	// spriteTransformationMatrixData 縺ｯ CPU 縺九ｉ Sprite 陦悟・繧呈嶌縺崎ｾｼ繧 mapped 繝昴う繝ｳ繧ｿ縲・
	spriteTransformationMatrixResource->Map(
		0, nullptr, reinterpret_cast<void**>(&spriteTransformationMatrixData));
	spriteTransformationMatrixData->WVP = MakeIdentity4x4();
	spriteTransformationMatrixData->World = MakeIdentity4x4();
	spriteTransformationMatrixData->lightWVP = MakeIdentity4x4();

	// sphereTransformationMatrixResource 縺ｯ譌ｧ 3D 繝励Ξ繝薙Η繝ｼ縺ｮ WVP / World 陦悟・逕ｨ螳壽焚繝舌ャ繝輔ぃ縲・
	ID3D12Resource* sphereTransformationMatrixResource = CreateBufferResource(
		device.Get(), sizeof(TransformationMatrix));

	TransformationMatrix* sphereTransformationMatrixData = nullptr;
	// sphereTransformationMatrixData 縺ｯ CPU 縺九ｉ 3D 陦悟・繧呈嶌縺崎ｾｼ繧 mapped 繝昴う繝ｳ繧ｿ縲・
	sphereTransformationMatrixResource->Map(
		0, nullptr, reinterpret_cast<void**>(&sphereTransformationMatrixData));
	sphereTransformationMatrixData->WVP = MakeIdentity4x4();
	sphereTransformationMatrixData->World = MakeIdentity4x4();
	sphereTransformationMatrixData->lightWVP = MakeIdentity4x4();

	// RootSignature 縺ｯ Shader 縺後←縺ｮ Resource 繧偵←縺ｮ繧ｹ繝ｭ繝・ヨ縺ｧ隱ｭ繧縺九ｒ蝗ｺ螳壹☆繧九・
	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.GetAddressOf(),
		errorBlob.GetAddressOf());
	if (FAILED(hr)) {
		// errorBlob 縺後≠繧句ｴ蜷医・ HLSL/RootSignature 蛛ｴ縺ｮ蜈ｷ菴鍋噪縺ｪ螟ｱ謨礼炊逕ｱ繧偵Ο繧ｰ縺ｸ蜃ｺ縺吶・
		if (errorBlob != nullptr) {
			Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	ComPtr<ID3D12RootSignature> rootSignature; // rootSignature 縺ｯ PipelineState 縺ｫ險ｭ螳壹☆繧・GPU 蛛ｴ RootSignature 螳滉ｽ薙・
	hr = device->CreateRootSignature(
		0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(rootSignature.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// inputElementDescs 縺ｯ VertexData 縺ｮ position / texcoord / normal 繧・Shader 蜈･蜉帙↓蟇ｾ蠢懊＆縺帙ｋ縲・
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

	// blendDesc 縺ｯ RenderTarget 縺ｸ濶ｲ繧呈嶌縺崎ｾｼ繧譁ｹ豕輔ら樟迥ｶ縺ｯ荳埼乗・謠冗判縺ｮ讓呎ｺ冶ｨｭ螳壹・
	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// rasterizerDesc 縺ｯ荳芽ｧ貞ｽ｢繧貞｡励ｊ縺､縺ｶ縺励∬｣剰｡ｨ繧ｫ繝ｪ繝ｳ繧ｰ繧偵＠縺ｪ縺・ｨｭ螳壹・
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.DepthClipEnable = TRUE;

	// depthStencilDesc 縺ｯ謇句燕縺ｮ迚ｩ菴薙ｒ蜆ｪ蜈医＠縺ｦ謠上￥縺溘ａ縺ｮ豺ｱ蠎ｦ繝・せ繝郁ｨｭ螳壹・
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// graphicsPipelineStateDesc 縺ｯ Shader縲∝・蜉帙Ξ繧､繧｢繧ｦ繝医。lend縲．epth 繧偵∪縺ｨ繧√◆謠冗判險ｭ螳壹・
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
	// graphicsPipelineState 縺ｯ Draw 譎ゅ↓ CommandList 縺ｸ繧ｻ繝・ヨ縺吶ｋ PSO 螳滉ｽ薙・
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
	                                         IID_PPV_ARGS(graphicsPipelineState.GetAddressOf()));
	assert(SUCCEEDED(hr));

	// PSO 縺後↑縺・→ Shader 縺ｨ RenderState 縺檎｢ｺ螳壹○縺壽緒逕ｻ縺ｧ縺阪↑縺・◆繧∫ｵゆｺ・☆繧九・
	if (FAILED(hr) || graphicsPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPipelineStateDesc = graphicsPipelineStateDesc;
	shadowPipelineStateDesc.VS = {
		shadowVertexShaderBlob->GetBufferPointer(),
		shadowVertexShaderBlob->GetBufferSize()
	};
	shadowPipelineStateDesc.PS = {};
	shadowPipelineStateDesc.NumRenderTargets = 0;
	shadowPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	shadowPipelineStateDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	shadowPipelineStateDesc.RasterizerState.DepthBias = 1200;
	shadowPipelineStateDesc.RasterizerState.SlopeScaledDepthBias = 1.5f;
	shadowPipelineStateDesc.RasterizerState.DepthBiasClamp = 0.01f;

	ComPtr<ID3D12PipelineState> shadowPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&shadowPipelineStateDesc,
		IID_PPV_ARGS(shadowPipelineState.GetAddressOf()));
	assert(SUCCEEDED(hr));
	if (FAILED(hr) || shadowPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ModelData modelData = LoadObjFile("resources", "plane.obj");
	// modelData 縺ｯ譌ｧ繝励Ξ繝薙Η繝ｼ縺ｨ譌｢螳壹い繧ｻ繝・ヨ逕ｨ縺ｫ隱ｭ縺ｿ霎ｼ繧 plane.obj 縺ｮ鬆らせ繝・・繧ｿ縲・
	constexpr uint32_t kSubdivision = 64; // kSubdivision 縺ｯ譌ｧ逅・・繝ｬ繝薙Η繝ｼ繧剃ｽ懊ｋ邱ｯ蠎ｦ繝ｻ邨悟ｺｦ蛻・牡謨ｰ縲・
	constexpr float kLonEvery = 2.0f * std::numbers::pi_v<float> / static_cast<float>(kSubdivision);
	// kLonEvery / kLatEvery 縺ｯ逅・Γ繝・す繝･ 1 繧ｻ繧ｰ繝｡繝ｳ繝医≠縺溘ｊ縺ｮ隗貞ｺｦ縲・
	constexpr float kLatEvery = std::numbers::pi_v<float> / static_cast<float>(kSubdivision);

	std::vector<VertexData> vertices; // vertices 縺ｯ譌ｧ逅・・繝ｬ繝薙Η繝ｼ逕ｨ縺ｫ CPU 縺ｧ逕滓・縺吶ｋ荳芽ｧ貞ｽ｢鬆らせ蛻励・
	vertices.reserve(kSubdivision * kSubdivision * 6);
	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		float lat = -std::numbers::pi_v<float> / 2.0f + kLatEvery * static_cast<float>(latIndex);
		// lat / latNext 縺ｯ迴ｾ蝨ｨ繧ｻ繝ｫ縺ｮ荳句・繝ｻ荳雁・縺ｮ邱ｯ蠎ｦ隗偵・
		float latNext = lat + kLatEvery;

		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			float lon = kLonEvery * static_cast<float>(lonIndex) + std::numbers::pi_v<float>;
			// lon / lonNext 縺ｯ迴ｾ蝨ｨ繧ｻ繝ｫ縺ｮ蟾ｦ蛛ｴ繝ｻ蜿ｳ蛛ｴ縺ｮ邨悟ｺｦ隗偵・
			float lonNext = lon + kLonEvery;

			float u0 = static_cast<float>(lonIndex) / static_cast<float>(kSubdivision);
			// u0/u1/v0/v1 縺ｯ逅・擇縺ｫ uvChecker 繧定ｲｼ繧九◆繧√・ UV 遽・峇縲・
			float u1 = static_cast<float>(lonIndex + 1) / static_cast<float>(kSubdivision);
			float v0 = 1.0f - static_cast<float>(latIndex) / static_cast<float>(kSubdivision);
			float v1 = 1.0f - static_cast<float>(latIndex + 1) / static_cast<float>(kSubdivision);

			// a/b/c/d 縺ｯ逅・擇 1 繧ｻ繝ｫ縺ｮ蝗幃嚆縲Ｑosition 縺ｨ normal 縺ｯ蜊倅ｽ咲帥蠎ｧ讓吶°繧我ｽ懊ｋ縲・
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

			vertices.push_back(a); // 蝗幄ｧ貞ｽ｢繧ｻ繝ｫ繧・2 譫壹・荳芽ｧ貞ｽ｢縺ｫ蛻・￠縺ｦ VertexBuffer 縺ｸ霑ｽ蜉縺吶ｋ縲・
			vertices.push_back(b);
			vertices.push_back(c);
			vertices.push_back(c);
			vertices.push_back(b);
			vertices.push_back(d);
		}
	}

	// sprite 縺ｯ譌ｧ Sprite 繝励Ξ繝薙Η繝ｼ縺ｮ蝓ｺ貅紋ｽ咲ｽｮ縺ｨ繧ｵ繧､繧ｺ縲・
	Sprite sprite{
		.position = {128.0f, 128.0f},
		.size = {256.0f, 256.0f}
	};

	// spriteVertices 縺ｯ荳ｭ蠢・次轤ｹ縺ｮ蝗幄ｧ貞ｽ｢繧定｡ｨ縺・4 鬆らせ縲・
	VertexData spriteVertices[] = {
		{{-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
		{{-0.5f, 0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
		{{0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
		{{0.5f, 0.5f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
	};

	// spriteIndices 縺ｯ蝗幄ｧ貞ｽ｢繧・2 荳芽ｧ貞ｽ｢縺ｧ謠上￥縺溘ａ縺ｮ IndexBuffer縲・
	uint32_t spriteIndices[] = {
		0, 1, 2,
		2, 1, 3,
	};

	// transform 縺ｯ譌ｧ 3D 繝｢繝・Ν繝励Ξ繝薙Η繝ｼ縺ｮ蛻晄悄 Transform縲・
	Transforms transform{
		.scale = {0.55f, 0.55f, 0.55f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, 0.0f}
	};

	// spriteTransform 縺ｯ譌ｧ Sprite 繝励Ξ繝薙Η繝ｼ縺ｮ Transform縲４prite 繧ｵ繧､繧ｺ繧・scale 縺ｫ蜈･繧後ｋ縲・
	Transforms spriteTransform{
		.scale = {sprite.size.x, sprite.size.y, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {sprite.position.x, sprite.position.y, 0.0f}
	};

	// cameraTransform 縺ｯ SceneView 逕ｨ繧ｨ繝・ぅ繧ｿ繝ｼ繧ｫ繝｡繝ｩ縺ｮ蛻晄悄菴咲ｽｮ縲・
	Transforms cameraTransform{
		.scale = {1.0f, 1.0f, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, -5.0f}
	};

	// uvTransform 縺ｯ Material 縺ｮ UV 螟画鋤陦悟・繧剃ｽ懊ｋ縺溘ａ縺ｮ蛻晄悄 Transform縲・
	Transforms uvTransform{
		.scale = {1.0f, 1.0f, 1.0f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, 0.0f}
	};

	ID3D12Resource* vertexResource = CreateBufferResource(device.Get(), sizeof(VertexData) * vertices.size());
	// vertexResource 縺ｯ譌ｧ逅・・繝ｬ繝薙Η繝ｼ縺ｮ鬆らせ繧・GPU 縺ｸ貂｡縺・Upload Buffer縲・
	VertexData* mappedVertexData = nullptr; // mappedVertexData 縺ｯ vertexResource 縺ｫ CPU 縺九ｉ鬆らせ繧呈嶌縺崎ｾｼ繧縺溘ａ縺ｮ繝昴う繝ｳ繧ｿ縲・
	hr = vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedVertexData, vertices.data(), sizeof(VertexData) * vertices.size());

	// vertexBufferView 縺ｯ譌ｧ逅・・繝ｬ繝薙Η繝ｼ縺ｮ鬆らせ Buffer 繧・Draw 縺ｫ貂｡縺呎ュ蝣ｱ縲・
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// modelVertexResource 縺ｯ plane.obj 縺ｮ鬆らせ繧・GPU 縺ｸ貂｡縺・Upload Buffer縲・
	ID3D12Resource* modelVertexResource = CreateBufferResource(device.Get(),
	                                                           sizeof(VertexData) * modelData.vertices.size());

	VertexData* mappedModelVertexData = nullptr;
	// mappedModelVertexData 縺ｯ modelVertexResource 縺ｫ CPU 縺九ｉ鬆らせ繧呈嶌縺崎ｾｼ繧縺溘ａ縺ｮ繝昴う繝ｳ繧ｿ縲・
	hr = modelVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedModelVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedModelVertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());

	// modelVertexBufferView 縺ｯ plane.obj 縺ｮ鬆らせ Buffer 繧・Draw 縺ｫ貂｡縺呎ュ蝣ｱ縲・
	D3D12_VERTEX_BUFFER_VIEW modelVertexBufferView{};
	modelVertexBufferView.BufferLocation = modelVertexResource->GetGPUVirtualAddress();
	modelVertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelData.vertices.size());
	modelVertexBufferView.StrideInBytes = sizeof(VertexData);

	ModelData primitiveModelData[kEditorModelMeshTypeCount]{}; // primitiveModelData 縺ｯ蝓ｺ譛ｬ蠖｢縺斐→縺ｮ CPU 鬆らせ繝・・繧ｿ縲・
	ID3D12Resource* primitiveVertexResources[kEditorModelMeshTypeCount] = {};
	// primitiveVertexResources 縺ｯ蝓ｺ譛ｬ蠖｢縺斐→縺ｮ GPU 鬆らせ Buffer縲・
	D3D12_VERTEX_BUFFER_VIEW primitiveVertexBufferViews[kEditorModelMeshTypeCount]{};
	// primitiveVertexBufferViews 縺ｯ Draw 譎ゅ↓ IA 縺ｸ貂｡縺・BufferView縲・
	uint32_t primitiveVertexCounts[kEditorModelMeshTypeCount] = {}; // primitiveVertexCounts 縺ｯ DrawInstanced 縺ｮ鬆らせ謨ｰ縲・
	CreatePrimitiveMeshBuffers(
		device.Get(),
		modelData,
		primitiveModelData,
		primitiveVertexResources,
		primitiveVertexBufferViews,
		primitiveVertexCounts);

	ID3D12Resource* spriteVertexResource = CreateBufferResource(device.Get(), sizeof(spriteVertices));
	// spriteVertexResource 縺ｯ Sprite 蝗幄ｧ貞ｽ｢縺ｮ鬆らせ繧・GPU 縺ｸ貂｡縺・Upload Buffer縲・
	VertexData* mappedSpriteVertexData = nullptr;
	// mappedSpriteVertexData 縺ｯ Sprite 鬆らせ繧呈嶌縺崎ｾｼ繧縺溘ａ縺ｮ CPU mapped 繝昴う繝ｳ繧ｿ縲・
	hr = spriteVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteVertexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedSpriteVertexData, spriteVertices, sizeof(spriteVertices));

	// spriteVertexBufferView 縺ｯ Sprite 鬆らせ Buffer 繧・Draw 縺ｫ貂｡縺呎ュ蝣ｱ縲・
	D3D12_VERTEX_BUFFER_VIEW spriteVertexBufferView{};
	spriteVertexBufferView.BufferLocation = spriteVertexResource->GetGPUVirtualAddress();
	spriteVertexBufferView.SizeInBytes = sizeof(spriteVertices);
	spriteVertexBufferView.StrideInBytes = sizeof(VertexData);

	ID3D12Resource* spriteIndexResource = CreateBufferResource(device.Get(), sizeof(spriteIndices));
	// spriteIndexResource 縺ｯ Sprite 蝗幄ｧ貞ｽ｢縺ｮ IndexBuffer縲・
	uint32_t* mappedSpriteIndexData = nullptr; // mappedSpriteIndexData 縺ｯ Sprite Index 繧・CPU 縺九ｉ譖ｸ縺崎ｾｼ繧縺溘ａ縺ｮ繝昴う繝ｳ繧ｿ縲・
	hr = spriteIndexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteIndexData));
	assert(SUCCEEDED(hr));
	std::memcpy(mappedSpriteIndexData, spriteIndices, sizeof(spriteIndices));

	// spriteIndexBufferView 縺ｯ Sprite IndexBuffer 繧・DrawIndexed 縺ｫ貂｡縺呎ュ蝣ｱ縲・
	D3D12_INDEX_BUFFER_VIEW spriteIndexBufferView{};
	spriteIndexBufferView.BufferLocation = spriteIndexResource->GetGPUVirtualAddress();
	spriteIndexBufferView.SizeInBytes = sizeof(spriteIndices);
	spriteIndexBufferView.Format = DXGI_FORMAT_R32_UINT;

	constexpr float editorMenuHeight = 20.0f;
	// editorMenuHeight / editorSceneHeaderHeight 縺ｯ蛻晄悄 SceneView 遏ｩ蠖｢縺ｮ Y 蠎ｧ讓呵ｨ育ｮ励↓菴ｿ縺・崋螳夐ｫ倥＆縲・
	constexpr float editorSceneHeaderHeight = 24.0f;

	float editorWindowWidth = static_cast<float>(renderWidth);
	// editorWindowWidth / Height 縺ｯ ImGui 縺ｨ SceneView 逕ｨ縺ｫ float 縺ｧ菫晄戟縺吶ｋ Window 繧ｵ繧､繧ｺ縲・
	float editorWindowHeight = static_cast<float>(renderHeight);

	float editorLeftWidth = 250.0f; // editorLeft/Right/Bottom 縺ｯ蛻晄悄 Docking 蜑阪・蜷・ヱ繝阪Ν蟷・・鬮倥＆縲・
	float editorRightWidth = 320.0f;
	float editorBottomHeight = 190.0f;

	float editorSceneX = editorLeftWidth;
	// editorSceneX/Y/Width/Height 縺ｯ DirectX viewport 繧・SceneView 縺ｫ蜷医ｏ縺帙ｋ縺溘ａ縺ｮ遏ｩ蠖｢縲・
	float editorSceneY = editorMenuHeight + editorSceneHeaderHeight;
	float editorSceneWidth =
		editorWindowWidth - editorLeftWidth - editorRightWidth;
	float editorSceneHeight =
		editorWindowHeight - editorSceneY - editorBottomHeight;
	auto updateEditorLayout = [&]() {
		editorLeftWidth = (std::clamp)(editorLeftWidth, 160.0f, 420.0f);
		// 繝代ロ繝ｫ蟷・・鬮倥＆縺ｫ荳矩剞荳企剞繧呈戟縺溘○縲ヾceneView 縺梧･ｵ遶ｯ縺ｫ貎ｰ繧後↑縺・ｈ縺・↓縺吶ｋ縲・
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

	// viewport 縺ｯ DirectX 縺梧緒逕ｻ縺吶ｋ逕ｻ髱｢荳翫・遏ｩ蠖｢縲・
	D3D12_VIEWPORT viewport{};
	viewport.TopLeftX = editorSceneX;
	viewport.TopLeftY = editorSceneY;
	viewport.Width = editorWindowWidth;
	viewport.Height = editorWindowHeight;
	viewport.MaxDepth = 1.0f;
	viewport.Width = editorSceneWidth;
	viewport.Height = editorSceneHeight;

	// scissorRect 縺ｯ SceneView 縺ｮ螟悶∈謠冗判縺励↑縺・◆繧√・蛻・ｊ蜿悶ｊ遏ｩ蠖｢縲・
	D3D12_RECT scissorRect{};
	scissorRect.left = static_cast<LONG>(editorSceneX);
	scissorRect.top = static_cast<LONG>(editorSceneY);
	scissorRect.right = static_cast<LONG>(editorSceneX + editorSceneWidth);
	scissorRect.bottom = static_cast<LONG>(editorSceneY + editorSceneHeight);

	// textureFilePaths 縺ｯ襍ｷ蜍墓凾縺ｫ GPU 縺ｸ繧｢繝・・繝ｭ繝ｼ繝峨☆繧区ｨ呎ｺ悶ユ繧ｯ繧ｹ繝√Ε荳隕ｧ縲・
	std::wstring textureFilePaths[] = {
		L"resources/uvChecker.png",
		L"resources/monsterBall.png",
		ConvertString(modelData.material.textureFilePath),
		L"resources/ball.png",
	};

	std::string textureFilePathStrings[_countof(textureFilePaths)];
	// textureFilePathStrings 縺ｯ Project 繝代ロ繝ｫ縺ｧ謇ｱ縺・ｄ縺吶＞ UTF-8 迚医ヱ繧ｹ縲・
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		textureFilePathStrings[textureIndex] = ConvertString(textureFilePaths[textureIndex]);
	}
	std::vector<std::string> editorTextureFilePaths;
	editorTextureFilePaths.reserve(_countof(textureFilePaths));
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		editorTextureFilePaths.push_back(textureFilePathStrings[textureIndex]);
	}

	DirectX::ScratchImage mipImages[_countof(textureFilePaths)];
	// mipImages 縺ｯ DirectXTex 縺檎函謌舌＠縺・mipmap 莉倥″逕ｻ蜒上ョ繝ｼ繧ｿ縲・
	DirectX::TexMetadata textureMetadatas[_countof(textureFilePaths)];
	// textureMetadatas 縺ｯ蜷・Texture 縺ｮ繧ｵ繧､繧ｺ縲∝ｽ｢蠑上［ip 謨ｰ縲・

	// textureResources 縺ｯ GPU 荳翫・ Texture 螳滉ｽ薙・
	ID3D12Resource* textureResources[_countof(textureFilePaths)] = {nullptr};
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		mipImages[textureIndex] = LoadTexture(textureFilePaths[textureIndex]);
		textureMetadatas[textureIndex] = mipImages[textureIndex].GetMetadata();
		textureResources[textureIndex] = CreateTextureResource(device.Get(), textureMetadatas[textureIndex]);
	}

	// cameraMatrix 縺ｯ繧ｨ繝・ぅ繧ｿ繝ｼ繧ｫ繝｡繝ｩ Transform 縺九ｉ菴懊ｋ繝ｯ繝ｼ繝ｫ繝芽｡悟・縲・
	Matrix4x4 cameraMatrix = MakeAffineMatrix(
		cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);

	Matrix4x4 viewMatrix = Inverse(cameraMatrix); // viewMatrix 縺ｯ cameraMatrix 縺ｮ騾・｡悟・縲ゅΡ繝ｼ繝ｫ繝牙ｺｧ讓吶ｒ繧ｫ繝｡繝ｩ遨ｺ髢薙∈遘ｻ縺吶・

	// projectionMatrix 縺ｯ SceneView 縺ｮ 3D 陦ｨ遉ｺ逕ｨ騾剰ｦ匁兜蠖ｱ陦悟・縲・
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(
		0.45f,
		editorSceneWidth / editorSceneHeight,
		0.1f,
		100.0f);

	// spriteProjectionMatrix 縺ｯ 2D Sprite 繧堤判髱｢蠎ｧ讓吶〒謠上￥縺溘ａ縺ｮ豁｣蟆・ｽｱ陦悟・縲・
	Matrix4x4 spriteProjectionMatrix = MakeOrthographicMatrix(
		0.0f,
		0.0f,
		editorWindowWidth,
		editorWindowHeight,
		0.0f,
		100.0f);

	float editorCameraMoveSpeed = 0.12f; // editorCamera*Speed 縺ｯ Inspector 縺九ｉ隱ｿ謨ｴ縺ｧ縺阪ｋ Scene 繧ｫ繝｡繝ｩ謫堺ｽ憺溷ｺｦ縲・
	float editorCameraRotateSpeed = 0.006f;
	float editorCameraWheelMoveSpeed = 0.5f;
	float editorCameraPanSpeed = 0.01f;
	float editorCameraFastRate = 4.0f;

	// sceneClearColor 縺ｯ SceneView 縺ｮ閭梧勹濶ｲ RGBA縲・
	float sceneClearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};

	bool isSceneGizmoVisible = true; // is*GizmoVisible 縺ｯ SceneView 荳翫・陬懷勧陦ｨ遉ｺ繧貞・繧頑崛縺医ｋ繝輔Λ繧ｰ縲・
	bool isLightGizmoVisible = false;
	bool isCameraGizmoVisible = false;

	// directionalLightIconPosition 縺ｯ繝ｩ繧､繝医い繧､繧ｳ繝ｳ繧・SceneView 縺ｫ陦ｨ遉ｺ縺吶ｋ繝ｯ繝ｼ繝ｫ繝牙ｺｧ讓吶・
	Vector3 directionalLightIconPosition = {-1.8f, 1.4f, 0.0f};

	EditorSceneObjectManager editorSceneObjectManager;
	// editorSceneObjectManager 縺ｯ GameObject 縺ｫ蟇ｾ蠢懊☆繧・DirectX 謠冗判逕ｨ SceneObject 繧剃ｿ晄戟縺吶ｋ縲・
	editorSceneObjectManager.Initialize(device.Get());

	std::vector<EditorSceneObject>& editorSceneObjects = editorSceneObjectManager.GetSceneObjects();
	// editorSceneObjects 縺ｯ SceneObjectManager 蜀・Κ驟榊・縺ｸ縺ｮ蜿ら・縲・
	int32_t selectedPlacedSceneObjectIndex = -1;
	// selectedPlacedSceneObjectIndex 縺ｯ SceneObject 驟榊・縺ｮ驕ｸ謚樔ｸｭ index縲・1 縺ｯ譛ｪ驕ｸ謚槭・
	ComPtr<ID3D12Fence> fence; // fence 縺ｯ CPU 縺・GPU 蜃ｦ逅・ｮ御ｺ・ｒ蠕・▽縺溘ａ縺ｮ蜷梧悄繧ｪ繝悶ず繧ｧ繧ｯ繝医・
	uint64_t fenceValue = 0; // fenceValue 縺ｯ Signal 縺斐→縺ｫ騾ｲ繧√ｋ GPU 螳御ｺ・｢ｺ隱咲畑繧ｫ繧ｦ繝ｳ繧ｿ縲・
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
	assert(SUCCEEDED(hr));

	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	// fenceEvent 縺ｯ GPU 螳御ｺ・夂衍繧・CPU 縺・WaitForSingleObject 縺ｧ蠕・▽縺溘ａ縺ｮ Win32 Event縲・
	assert(fenceEvent != nullptr);

	// Event 菴懈・縺ｫ螟ｱ謨励☆繧九→ GPU 蠕・■縺後〒縺阪↑縺・◆繧√∵緒逕ｻ髢句ｧ句燕縺ｫ邨ゆｺ・☆繧九・
	if (fenceEvent == nullptr) {
		RequestInitializationFailure();
		return;
	}

	auto waitForGpu = [&]() {
		fenceValue++; // fenceValue 繧帝ｲ繧√∽ｻ雁屓蠕・▽ GPU 菴懈･ｭ逡ｪ蜿ｷ繧剃ｽ懊ｋ縲・
		HRESULT signalResult = commandQueue->Signal(fence.Get(), fenceValue);
		// Signal 縺ｯ commandQueue 縺ｫ迴ｾ蝨ｨ縺ｮ fenceValue 繧貞ｮ御ｺ・ｺ亥ｮ壹→縺励※逋ｻ骭ｲ縺吶ｋ縲・
		assert(SUCCEEDED(signalResult));

		// GPU 縺後∪縺謖・ｮ壼､縺ｾ縺ｧ邨ゅｏ縺｣縺ｦ縺・↑縺代ｌ縺ｰ縲・vent 繧堤匳骭ｲ縺励※ CPU 繧貞ｾ・ｩ溘＆縺帙ｋ縲・
		if (fence->GetCompletedValue() < fenceValue) {
			HRESULT eventResult = fence->SetEventOnCompletion(fenceValue, fenceEvent);
			assert(SUCCEEDED(eventResult));
			WaitForSingleObject(fenceEvent, INFINITE);
		}
	};

	auto resizeRenderTargets = [&](uint32_t width, uint32_t height) {
		// 譌｢縺ｫ蜷後§繧ｵ繧､繧ｺ縺ｪ繧・SwapChain 縺ｨ DepthStencil 繧剃ｽ懊ｊ逶ｴ縺輔↑縺・・
		if (renderWidth == width && renderHeight == height) {
			return;
		}

		waitForGpu(); // ResizeBuffers 蜑阪↓ GPU 縺悟商縺・back buffer 繧剃ｽｿ縺・ｵゅｏ繧九∪縺ｧ蠕・▽縲・

		for (ID3D12Resource*& swapChainResource : swapChainResources) {
			// 蜿､縺・SwapChain buffer 縺ｯ ResizeBuffers 蜑阪↓蠢・★ Release 縺吶ｋ縲・
			if (swapChainResource != nullptr) {
				swapChainResource->Release();
				swapChainResource = nullptr;
			}
		}

		// 蜿､縺・DepthStencil 繧よ緒逕ｻ繧ｵ繧､繧ｺ縺悟､峨ｏ繧九◆繧∽ｽ懊ｊ逶ｴ縺吶・
		if (depthStencilResource != nullptr) {
			depthStencilResource->Release();
			depthStencilResource = nullptr;
		}

		renderWidth = width; // renderWidth / renderHeight 縺ｯ譁ｰ縺励＞ SwapChain 繧ｵ繧､繧ｺ縲・
		renderHeight = height;

		// ResizeBuffers 縺ｯ SwapChain 縺ｮ back buffer 螳滉ｽ薙ｒ譁ｰ縺励＞繧ｵ繧､繧ｺ縺ｧ蜀咲函謌舌☆繧九・
		HRESULT resizeResult = swapChain->ResizeBuffers(
			swapChainDesc.BufferCount,
			renderWidth,
			renderHeight,
			swapChainDesc.Format,
			0);
		assert(SUCCEEDED(resizeResult));

		for (uint32_t bufferIndex = 0; bufferIndex < swapChainDesc.BufferCount; ++bufferIndex) {
			// Resize 蠕後・譁ｰ縺励＞ back buffer 繧貞叙蠕励＠縺ｦ RTV 繧貞ｼｵ繧顔峩縺吶・
			HRESULT getBufferResult =
				swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapChainResources[bufferIndex]));
			assert(SUCCEEDED(getBufferResult));
			device->CreateRenderTargetView(swapChainResources[bufferIndex], &rtvDesc, rtvHandles[bufferIndex]);
		}

		depthStencilResource = createDepthStencilResource(renderWidth, renderHeight);
		// DepthStencil 繧よ眠縺励＞ renderWidth / renderHeight 縺ｫ蜷医ｏ縺帙※蜀咲函謌舌☆繧九・
		device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandle);
	};

	hr = commandAllocator->Reset(); // 繝・け繧ｹ繝√Ε繧｢繝・・繝ｭ繝ｼ繝臥畑縺ｫ CommandAllocator 縺ｨ CommandList 繧定ｨ倬鹸蜿ｯ閭ｽ迥ｶ諷九∈謌ｻ縺吶・
	assert(SUCCEEDED(hr));
	hr = commandList->Reset(commandAllocator.Get(), nullptr);
	assert(SUCCEEDED(hr));

	// intermediateResources 縺ｯ Texture upload 縺ｮ縺溘ａ縺ｮ荳譎・Upload Buffer縲・
	ID3D12Resource* intermediateResources[_countof(textureFilePaths)] = {nullptr};
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		// UploadTextureData 縺ｯ textureResources 縺ｫ mipImages 繧偵さ繝斐・縺吶ｋ蜻ｽ莉､繧・CommandList 縺ｸ遨阪・縲・
		intermediateResources[textureIndex] = UploadTextureData(
			device.Get(), commandList.Get(), textureResources[textureIndex], mipImages[textureIndex]);
	}

	UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	// srvDescriptorSize 縺ｯ SRV Heap 蜀・〒谺｡縺ｮ Descriptor 縺ｸ騾ｲ繧繝舌う繝亥ｹ・・
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandlesCPU[_countof(textureFilePaths)];
	// textureSrvHandlesCPU 縺ｯ CreateShaderResourceView 縺ｫ貂｡縺・CPU 蛛ｴ SRV 繝上Φ繝峨Ν縲・
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandlesGPU[_countof(textureFilePaths)];
	// textureSrvHandlesGPU 縺ｯ Draw 譎ゅ↓ Shader 縺ｸ貂｡縺・GPU 蛛ｴ SRV 繝上Φ繝峨Ν縲・
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		// srvDesc 縺ｯ 2D Texture SRV 縺ｨ縺励※ mipmap 莉倥″逕ｻ蜒上ｒ Shader 縺九ｉ隱ｭ繧險ｭ螳壹・
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = textureMetadatas[textureIndex].format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(textureMetadatas[textureIndex].mipLevels);

		// index + 1 縺ｫ縺吶ｋ縺ｮ縺ｯ 0 逡ｪ繧・ImGui 逕ｨ SRV 縺ｫ遨ｺ縺代ｋ縺溘ａ縲・
		textureSrvHandlesCPU[textureIndex] = GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize,
		                                                            textureIndex + 1);
		textureSrvHandlesGPU[textureIndex] = GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize,
		                                                            textureIndex + 1);
		device->CreateShaderResourceView(textureResources[textureIndex], &srvDesc, textureSrvHandlesCPU[textureIndex]);
	}

	hr = commandList->Close(); // Texture upload 逕ｨ CommandList 繧帝哩縺倥※ GPU 縺ｫ螳溯｡後＆縺帙ｋ縲・
	assert(SUCCEEDED(hr));

	// uploadCommandLists 縺ｯ ExecuteCommandLists 縺ｫ貂｡縺・CommandList 驟榊・縲・
	ID3D12CommandList* uploadCommandLists[] = {commandList.Get()};
	commandQueue->ExecuteCommandLists(1, uploadCommandLists);

	fenceValue++; // 蛻晄悄繧｢繝・・繝ｭ繝ｼ繝峨′螳御ｺ・☆繧九∪縺ｧ蠕・■縲∽ｻ･髯阪・謠冗判縺ｧ Texture 繧貞ｮ牙・縺ｫ蜿ら・縺吶ｋ縲・
	hr = commandQueue->Signal(fence.Get(), fenceValue);
	assert(SUCCEEDED(hr));
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		assert(SUCCEEDED(hr));
		WaitForSingleObject(fenceEvent, INFINITE);
	}

#ifdef USE_IMGUI

	IMGUI_CHECKVERSION(); // ImGui 縺ｮ繝舌・繧ｸ繝ｧ繝ｳ謨ｴ蜷域ｧ繧堤｢ｺ隱阪＠縺ｦ縺九ｉ Context 繧剃ｽ懊ｋ縲・
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO(); // io 縺ｯ Docking 譛牙柑蛹悶ｄ Font 險ｭ螳壹ｒ陦後≧ ImGui 縺ｮ蜈･蜃ｺ蜉幄ｨｭ螳壹・
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // DockingEnable 縺ｧ繧ｦ繧｣繝ｳ繝峨え縺ｮ繝峨Λ繝・げ遘ｻ蜍輔・繝峨ャ繧ｭ繝ｳ繧ｰ繧定ｨｱ蜿ｯ縺吶ｋ縲・
	io.ConfigDockingWithShift = false; // Shift 縺ｪ縺励〒 Docking 縺ｧ縺阪ｋ繧医≧縺ｫ縺励※ Unity 鬚ｨ縺ｮ謫堺ｽ懈─縺ｫ縺吶ｋ縲・
	ImGui::StyleColorsDark(); // 譌｢蟄・UI 縺ｨ蜷医ｏ縺帙※ Dark Style 繧貞渕貅悶↓縺吶ｋ縲・
	ImGui_ImplWin32_Init(windowHandle); // Win32 backend 縺ｯ HWND 縺九ｉ繝槭え繧ｹ繝ｻ繧ｭ繝ｼ繝懊・繝牙・蜉帙ｒ蜿励￠蜿悶ｋ縲・

	// DX12 backend 縺ｯ SRV Heap 0 逡ｪ繧・ImGui Font Texture 逕ｨ縺ｫ菴ｿ縺・・
	ImGui_ImplDX12_Init(
		device.Get(),
		static_cast<int>(swapChainDesc.BufferCount),
		rtvDesc.Format,
		srvDescriptorHeap,
		GetCPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 0),
		GetGPUDescriptorHandle(srvDescriptorHeap, srvDescriptorSize, 0));

	// meiryo.ttc 縺後≠繧後・譌･譛ｬ隱・UI 縺梧枚蟄怜喧縺代＠縺ｪ縺・ｈ縺・↓隱ｭ縺ｿ霎ｼ繧縲・
	if (std::filesystem::exists("C:/Windows/Fonts/meiryo.ttc")) {
		io.Fonts->AddFontFromFileTTF(
			"C:/Windows/Fonts/meiryo.ttc", 16.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	}

	io.Fonts->Build(); // Font Atlas 繧偵％縺薙〒讒狗ｯ峨＠縲∵怙蛻昴・繝輔Ξ繝ｼ繝縺ｧ譌･譛ｬ隱槭ヵ繧ｩ繝ｳ繝医ｒ菴ｿ縺医ｋ迥ｶ諷九↓縺吶ｋ縲・
#endif

	g_instanceHandle = instanceHandle; // 縺薙％縺九ｉ荳九・縲！nitialize 蜀・・繝ｭ繝ｼ繧ｫ繝ｫ逕滓・迚ｩ繧・EditorSharedState 縺ｮ蜈ｱ譛臥憾諷九∈遘ｻ縺吶・
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
	g_shadowMapResource = shadowMapResource;
	g_shadowMapSrvCpuHandle = shadowMapSrvCpuHandle;
	g_shadowMapSrvGpuHandle = shadowMapSrvGpuHandle;
	g_renderWidth = renderWidth;
	g_renderHeight = renderHeight;
	g_dxcUtils = dxcUtils;
	g_dxcCompiler = dxcCompiler;
	g_includeHandler = includeHandler;
	g_vertexShaderBlob = vertexShaderBlob;
	g_pixelShaderBlob = pixelShaderBlob;
	g_shadowVertexShaderBlob = shadowVertexShaderBlob;
	g_signatureBlob = signatureBlob;
	g_errorBlob = errorBlob;
	g_rootSignature = rootSignature;
	g_graphicsPipelineState = graphicsPipelineState;
	g_shadowPipelineState = shadowPipelineState;
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
	auto& shadowDsvHandle = g_shadowDsvHandle;
	auto& depthStencilResource = g_depthStencilResource;
	auto& shadowMapResource = g_shadowMapResource;
	auto& shadowMapSrvCpuHandle = g_shadowMapSrvCpuHandle;
	auto& shadowMapSrvGpuHandle = g_shadowMapSrvGpuHandle;
	auto& renderWidth = g_renderWidth;
	auto& renderHeight = g_renderHeight;
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
