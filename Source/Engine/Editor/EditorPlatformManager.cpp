#pragma warning(disable : 4189 4514)

#include "EditorPlatformManager.h"
#include <onnxruntime_cxx_api.h>
#include <xaudio2.h>
#include <xaudio2fx.h>
#include "EditorHrCheck.h"
#include "EditorProfilerManager.h"
#include "EditorSharedState.h"
using namespace EditorSharedState;

namespace {
	//================================================================
	// ImGui 動的 Font Texture 用 Descriptor
	//================================================================

	constexpr uint32_t kImGuiSrvDescriptorFirstIndex = 123u;
	constexpr uint32_t kImGuiSrvDescriptorCount = 37u;

	struct ImGuiSrvDescriptorAllocator {
		ID3D12DescriptorHeap* descriptorHeap = nullptr;
		UINT descriptorSize = 0u;
		std::array<bool, kImGuiSrvDescriptorCount> isDescriptorUsed{};
	};

	ImGuiSrvDescriptorAllocator g_imguiSrvDescriptorAllocator{};

	float GetEditorUiScale(HWND windowHandle) {
		const UINT windowDpi = windowHandle != nullptr ? GetDpiForWindow(windowHandle) : 96u;
		const float dpiScale = static_cast<float>((std::max)(windowDpi, 96u)) / 96.0f;
		return (std::clamp)(dpiScale, 1.0f, 2.0f);
	}

	void ApplyEditorVisualTheme(float uiScale) {
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowPadding = ImVec2(10.0f, 9.0f);
		style.FramePadding = ImVec2(8.0f, 5.0f);
		style.CellPadding = ImVec2(7.0f, 5.0f);
		style.ItemSpacing = ImVec2(8.0f, 6.0f);
		style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
		style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
		style.IndentSpacing = 18.0f;
		style.ScrollbarSize = 14.0f;
		style.GrabMinSize = 12.0f;
		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupBorderSize = 1.0f;
		style.FrameBorderSize = 1.0f;
		style.TabBorderSize = 0.0f;
		style.TabBarBorderSize = 1.0f;
		style.WindowRounding = 5.0f;
		style.ChildRounding = 4.0f;
		style.FrameRounding = 3.0f;
		style.PopupRounding = 4.0f;
		style.ScrollbarRounding = 8.0f;
		style.GrabRounding = 3.0f;
		style.TabRounding = 4.0f;
		style.SeparatorTextBorderSize = 1.0f;
		style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
		style.SeparatorTextPadding = ImVec2(12.0f, 4.0f);
		style.DockingSeparatorSize = 2.0f;
		style.DisabledAlpha = 0.48f;

		ImVec4* colors = style.Colors;
		colors[ImGuiCol_Text] = ImVec4(0.90f, 0.92f, 0.94f, 1.0f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.53f, 0.58f, 1.0f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.067f, 0.080f, 1.0f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.065f, 0.078f, 0.092f, 1.0f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.070f, 0.083f, 0.098f, 0.98f);
		colors[ImGuiCol_Border] = ImVec4(0.18f, 0.22f, 0.26f, 0.90f);
		colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.095f, 0.125f, 0.155f, 1.0f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.19f, 0.24f, 1.0f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.25f, 0.31f, 1.0f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.045f, 0.055f, 0.067f, 1.0f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.075f, 0.105f, 0.13f, 1.0f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.045f, 0.055f, 0.067f, 1.0f);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.035f, 0.043f, 0.052f, 0.70f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.28f, 0.33f, 1.0f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.29f, 0.37f, 0.43f, 1.0f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.34f, 0.48f, 0.55f, 1.0f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.78f, 0.78f, 1.0f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.67f, 0.69f, 1.0f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.34f, 0.86f, 0.82f, 1.0f);
		colors[ImGuiCol_Button] = ImVec4(0.11f, 0.27f, 0.31f, 1.0f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.15f, 0.40f, 0.43f, 1.0f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.51f, 0.52f, 1.0f);
		colors[ImGuiCol_Header] = ImVec4(0.10f, 0.25f, 0.29f, 0.90f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.14f, 0.39f, 0.42f, 1.0f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.18f, 0.50f, 0.50f, 1.0f);
		colors[ImGuiCol_Separator] = ImVec4(0.17f, 0.22f, 0.26f, 1.0f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.22f, 0.58f, 0.60f, 1.0f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.28f, 0.74f, 0.73f, 1.0f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.18f, 0.48f, 0.50f, 0.30f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.24f, 0.66f, 0.66f, 0.70f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.28f, 0.78f, 0.75f, 1.0f);
		colors[ImGuiCol_Tab] = ImVec4(0.075f, 0.105f, 0.13f, 1.0f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.15f, 0.39f, 0.42f, 1.0f);
		colors[ImGuiCol_TabSelected] = ImVec4(0.11f, 0.31f, 0.35f, 1.0f);
		colors[ImGuiCol_TabDimmed] = ImVec4(0.055f, 0.067f, 0.080f, 1.0f);
		colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.085f, 0.18f, 0.21f, 1.0f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.20f, 0.72f, 0.72f, 0.55f);
		colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.035f, 0.043f, 0.052f, 1.0f);
		colors[ImGuiCol_TableHeaderBg] = ImVec4(0.085f, 0.12f, 0.145f, 1.0f);
		colors[ImGuiCol_TableBorderStrong] = ImVec4(0.18f, 0.23f, 0.27f, 1.0f);
		colors[ImGuiCol_TableBorderLight] = ImVec4(0.12f, 0.15f, 0.18f, 1.0f);
		colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.10f, 0.13f, 0.15f, 0.38f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.18f, 0.55f, 0.56f, 0.45f);
		colors[ImGuiCol_NavCursor] = ImVec4(0.35f, 0.88f, 0.84f, 1.0f);

		style.ScaleAllSizes(uiScale);
	}

	void AllocateImGuiSrvDescriptor(
		ImGui_ImplDX12_InitInfo* initInfo,
		D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptorHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE* gpuDescriptorHandle) {
		if (initInfo == nullptr || cpuDescriptorHandle == nullptr || gpuDescriptorHandle == nullptr) {
			return;
		}

		auto* allocator = static_cast<ImGuiSrvDescriptorAllocator*>(initInfo->UserData);
		if (allocator == nullptr || allocator->descriptorHeap == nullptr || allocator->descriptorSize == 0u) {
			return;
		}

		for (uint32_t descriptorOffset = 0u;
			descriptorOffset < kImGuiSrvDescriptorCount;
			descriptorOffset++) {
			if (allocator->isDescriptorUsed[descriptorOffset]) {
				continue;
			}

			allocator->isDescriptorUsed[descriptorOffset] = true;
			const uint32_t descriptorIndex = kImGuiSrvDescriptorFirstIndex + descriptorOffset;
			*cpuDescriptorHandle = GetCPUDescriptorHandle(
				allocator->descriptorHeap,
				allocator->descriptorSize,
				descriptorIndex);
			*gpuDescriptorHandle = GetGPUDescriptorHandle(
				allocator->descriptorHeap,
				allocator->descriptorSize,
				descriptorIndex);
			return;
		}

		assert(false && "ImGui SRV descriptor range is exhausted.");
	}

	void ReleaseImGuiSrvDescriptor(
		ImGui_ImplDX12_InitInfo* initInfo,
		D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE) {
		if (initInfo == nullptr) {
			return;
		}

		auto* allocator = static_cast<ImGuiSrvDescriptorAllocator*>(initInfo->UserData);
		if (allocator == nullptr || allocator->descriptorHeap == nullptr || allocator->descriptorSize == 0u) {
			return;
		}

		const D3D12_CPU_DESCRIPTOR_HANDLE firstDescriptorHandle = GetCPUDescriptorHandle(
			allocator->descriptorHeap,
			allocator->descriptorSize,
			kImGuiSrvDescriptorFirstIndex);
		if (cpuDescriptorHandle.ptr < firstDescriptorHandle.ptr) {
			return;
		}

		const SIZE_T descriptorByteOffset = cpuDescriptorHandle.ptr - firstDescriptorHandle.ptr;
		if (descriptorByteOffset % static_cast<SIZE_T>(allocator->descriptorSize) != 0u) {
			return;
		}

		const SIZE_T descriptorOffset =
			descriptorByteOffset / static_cast<SIZE_T>(allocator->descriptorSize);
		if (descriptorOffset >= static_cast<SIZE_T>(kImGuiSrvDescriptorCount)) {
			return;
		}

		allocator->isDescriptorUsed[static_cast<size_t>(descriptorOffset)] = false;
	}

	//================================================================
	//================================================================

	void RequestInitializationFailure() {
		g_isInitializationFailed = true; // GameScene が後続の Manager 初期化を止めるためのフラグ。
		g_isEndRequested = true;
		g_exitCode = 1;
	}

	// 起動後に復帰不能な描画失敗(Device Removed で back buffer が取得できない等)が
	// 起きたときに、null を参照する前にメインループを畳むための終了要求。
	// 初期化失敗ではないので g_isInitializationFailed は立てず、終了コードで区別する。
	void RequestFatalRuntimeFailure() {
		g_isEndRequested = true;
		g_exitCode = 2; // 2 は起動後の復帰不能な描画失敗を表す。
	}

	VertexData MakePrimitiveVertex(float x, float y, float z, float u, float v, const Vector3& normal) {
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
		AddPrimitiveTriangle(vertices, a, b, c);
		AddPrimitiveTriangle(vertices, c, b, d);
	}

	std::vector<VertexData> CreatePlaneVertices() {
		std::vector<VertexData> vertices;
		vertices.reserve(6u);

		AddPrimitiveQuad(
			vertices,
			MakePrimitiveVertex(-0.5f, 0.0f, -0.5f, 0.0f, 1.0f, {0.0f, 1.0f, 0.0f}),
			MakePrimitiveVertex(-0.5f, 0.0f, 0.5f, 0.0f, 0.0f, {0.0f, 1.0f, 0.0f}),
			MakePrimitiveVertex(0.5f, 0.0f, -0.5f, 1.0f, 1.0f, {0.0f, 1.0f, 0.0f}),
			MakePrimitiveVertex(0.5f, 0.0f, 0.5f, 1.0f, 0.0f, {0.0f, 1.0f, 0.0f}));

		return vertices;
	}

	std::vector<VertexData> CreateBoxVertices(const Vector3& halfSize) {
		std::vector<VertexData> vertices;
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
		std::vector<VertexData> vertices;
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
		std::vector<VertexData> vertices;
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
		std::vector<VertexData> vertices;
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
		std::vector<VertexData> vertices;
		vertices.reserve(24);
		constexpr Vector3 top{0.0f, 0.6f, 0.0f};
		constexpr Vector3 bottom{0.0f, -0.6f, 0.0f};
		constexpr Vector3 front{0.0f, 0.0f, -0.6f};
		constexpr Vector3 right{0.6f, 0.0f, 0.0f};
		constexpr Vector3 back{0.0f, 0.0f, 0.6f};
		constexpr Vector3 left{-0.6f, 0.0f, 0.0f};

		auto addFace = [&](const Vector3& a, const Vector3& b, const Vector3& c) {
			Vector3 normal = Normalize(Cross(Subtract(b, a), Subtract(c, a)));
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
		std::vector<VertexData> vertices;
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
		ModelData modelData{};

		switch (meshType) {
		case EditorModelMeshType::Plane:
			if (!fallbackPlaneModelData.vertices.empty()) {
				return fallbackPlaneModelData;
			}

			modelData.vertices = CreatePlaneVertices();
			break;
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
			auto meshType = static_cast<EditorModelMeshType>(meshTypeIndex);
			primitiveModelData[meshTypeIndex] = CreatePrimitiveModelData(meshType, fallbackPlaneModelData);
			primitiveVertexCounts[meshTypeIndex] =
				static_cast<uint32_t>(primitiveModelData[meshTypeIndex].vertices.size());

			if (primitiveVertexCounts[meshTypeIndex] == 0u) {
				continue;
			}

			size_t bufferSize = sizeof(VertexData) * primitiveModelData[meshTypeIndex].vertices.size();
			primitiveVertexResources[meshTypeIndex] = CreateBufferResource(device, bufferSize);

			VertexData* mappedVertexData = nullptr;
			HRESULT mapResult = primitiveVertexResources[meshTypeIndex]->Map(
				0,
				nullptr,
				reinterpret_cast<void**>(&mappedVertexData));
			// Map 失敗時は mappedVertexData が nullptr のままなので memcpy できない。
			// この Primitive だけ諦めて次へ進む。BufferView を書かないので描画側も参照しない。
			if (!EDITOR_HR_OK(mapResult) || mappedVertexData == nullptr) {
				continue;
			}
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
	InstallCrashHandler();

	//================================================================
	//================================================================

	std::filesystem::create_directory("logs"); // logs フォルダには実行ごとの .Log ファイルを保存する。
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
		RequestInitializationFailure();
		return;
	}

	HWND windowHandle = CreateMainWindow(instanceHandle, logStream);
	// Window Handle は DirectX SwapChain と DirectInput の協調レベル設定に使用する。

	if (windowHandle == nullptr) {
		RequestInitializationFailure();
		return;
	}

	HRESULT hr = S_OK;
	IDirectInput8* directInput = nullptr;
	hr = DirectInput8Create(
		instanceHandle, DIRECTINPUT_VERSION, IID_IDirectInput8, reinterpret_cast<void**>(&directInput), nullptr);
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || directInput == nullptr) {
		RequestInitializationFailure();
		return;
	}

	IDirectInputDevice8* keyboardDevice = nullptr;
	hr = directInput->CreateDevice(GUID_SysKeyboard, &keyboardDevice, nullptr);
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || keyboardDevice == nullptr) {
		directInput->Release();
		RequestInitializationFailure();
		return;
	}

	hr = keyboardDevice->SetDataFormat(&c_dfDIKeyboard);
	EDITOR_HR_VERIFY(hr);

	hr = keyboardDevice->SetCooperativeLevel(
		windowHandle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	EDITOR_HR_VERIFY(hr);

	//================================================================
	//================================================================

	IDirectInputDevice8* mouseDevice = nullptr;
	hr = directInput->CreateDevice(GUID_SysMouse, &mouseDevice, nullptr);
	EDITOR_HR_VERIFY(hr);
	if (FAILED(hr) || mouseDevice == nullptr) {
		directInput->Release();
		RequestInitializationFailure();
		return;
	}

	hr = mouseDevice->SetDataFormat(&c_dfDIMouse);
	EDITOR_HR_VERIFY(hr);
	hr = mouseDevice->SetCooperativeLevel(
		windowHandle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	EDITOR_HR_VERIFY(hr);

	BYTE key[256] = {};
	BYTE preKey[256] = {};

#ifdef _DEBUG
	//================================================================
	//================================================================

	ComPtr<ID3D12Debug1> debugController;

	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf())))) {
		debugController->EnableDebugLayer();
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif

	MSG message{};
	Log(logStream, "main loop started");

	//================================================================
	// XAudio2 を初期化する。
	//================================================================

	IXAudio2* xAudio2 = nullptr; // 音声再生エンジン本体。
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || xAudio2 == nullptr) {
		RequestInitializationFailure();
		return;
	}

	IXAudio2MasteringVoice* masterVoice = nullptr;
	hr = xAudio2->CreateMasteringVoice(&masterVoice);
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || masterVoice == nullptr) {
		xAudio2->Release();
		RequestInitializationFailure();
		return;
	}

	// 空の Project でも Editor を開けるよう、起動確認用の WAV は再生しない。
	// Project が必要とする音声は Audio Component / Runtime 側で個別に読み込む。
	// Finalize の既存解放処理と所有関係を揃えるため、未使用の状態だけ保持する。
	SoundData soundData{};
	IXAudio2SourceVoice* sourceVoice = nullptr;

	//================================================================
	// DirectX 12 を初期化する。
	//================================================================

	ComPtr<IDXGIFactory7> dxgiFactory; // GPU Adapter と SwapChain を作成する DXGI Factory。
	hr = CreateDXGIFactory1(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);

	ComPtr<IDXGIAdapter4> useAdapter; // D3D12Device の作成に使用する物理 GPU。
	for (UINT adapterIndex = 0;; ++adapterIndex) {
		ComPtr<IDXGIAdapter4> candidateAdapter;
		if (dxgiFactory->EnumAdapterByGpuPreference(
			adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(candidateAdapter.GetAddressOf())) == DXGI_ERROR_NOT_FOUND) {
			break;
		}

		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = candidateAdapter->GetDesc3(&adapterDesc);
		EDITOR_HR_VERIFY(hr);

		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
			continue;
		}

		useAdapter = candidateAdapter; // 最初に見つかった物理 GPU を使用 Adapter として採用する。
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
		// Keep Debug Layer messages in the Visual Studio output without raising 0x0000087A exceptions.
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, FALSE);

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

	ComPtr<ID3D12CommandQueue> commandQueue; // GPU に CommandList を送るキュー。

	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(commandQueue.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || commandQueue == nullptr) {
		RequestInitializationFailure();
		return;
	}

	D3D12_QUERY_HEAP_DESC renderTimestampQueryHeapDesc{};
	renderTimestampQueryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	renderTimestampQueryHeapDesc.Count = kEditorProfilerTimestampQueryCapacity;
	ComPtr<ID3D12QueryHeap> renderTimestampQueryHeap;
	hr = device->CreateQueryHeap(
		&renderTimestampQueryHeapDesc,
		IID_PPV_ARGS(renderTimestampQueryHeap.GetAddressOf()));

	std::uint64_t renderTimestampFrequency = 0u;

	if (SUCCEEDED(hr)) {
		hr = commandQueue->GetTimestampFrequency(&renderTimestampFrequency);
	}

	D3D12_HEAP_PROPERTIES renderTimestampReadbackHeapProperties{};
	renderTimestampReadbackHeapProperties.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC renderTimestampReadbackDesc{};
	renderTimestampReadbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	renderTimestampReadbackDesc.Width =
		sizeof(std::uint64_t) * static_cast<std::uint64_t>(kEditorProfilerTimestampQueryCapacity);
	renderTimestampReadbackDesc.Height = 1u;
	renderTimestampReadbackDesc.DepthOrArraySize = 1u;
	renderTimestampReadbackDesc.MipLevels = 1u;
	renderTimestampReadbackDesc.SampleDesc.Count = 1u;
	renderTimestampReadbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ComPtr<ID3D12Resource> renderTimestampReadback;

	if (SUCCEEDED(hr)) {
		hr = device->CreateCommittedResource(
			&renderTimestampReadbackHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&renderTimestampReadbackDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(renderTimestampReadback.GetAddressOf()));
	}

	if (FAILED(hr) || renderTimestampQueryHeap == nullptr ||
		renderTimestampReadback == nullptr || renderTimestampFrequency == 0u) {
		Log(logStream, std::format("Render profiler resource creation failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}


	ComPtr<ID3D12CommandAllocator> commandAllocator;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocator.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || commandAllocator == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12GraphicsCommandList> commandList;
	hr = device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || commandList == nullptr) {
		RequestInitializationFailure();
		return;
	}

	hr = commandList->Close();
	EDITOR_HR_VERIFY(hr);

	RECT clientRect{};
	GetClientRect(windowHandle, &clientRect);

	uint32_t renderWidth = (std::max)(1u, static_cast<uint32_t>(clientRect.right - clientRect.left));
	uint32_t renderHeight = (std::max)(1u, static_cast<uint32_t>(clientRect.bottom - clientRect.top));

	ComPtr<IDXGISwapChain4> swapChain; // Window に表示するバックバッファ列。

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
	EDITOR_HR_VERIFY(hr);

	if (FAILED(hr) || swapChain == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ID3D12DescriptorHeap* rtvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRuntimeRtvCount, false);

	ID3D12DescriptorHeap* srvDescriptorHeap =
		CreateDescriptorHeap(
			device.Get(),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			kRuntimeSrvDescriptorHeapCapacity,
			true);

	// DepthStencil を参照する DSV Descriptor Heap。
	ID3D12DescriptorHeap* dsvDescriptorHeap =
		CreateDescriptorHeap(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 2, false);

	ID3D12Resource* swapChainResources[2] = {nullptr};
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	EDITOR_HR_VERIFY(hr);
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	EDITOR_HR_VERIFY(hr);

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

	auto createDepthStencilResource = [&device, &depthClearValue](
			uint32_t width,
			uint32_t height,
			D3D12_RESOURCE_STATES initialState) -> ID3D12Resource* {
		// Scene View と同じサイズの Depth バッファを設定する。
		D3D12_RESOURCE_DESC depthStencilResourceDesc{};
		depthStencilResourceDesc.Width = width;
		depthStencilResourceDesc.Height = height;
		depthStencilResourceDesc.MipLevels = 1;
		depthStencilResourceDesc.DepthOrArraySize = 1;
		depthStencilResourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depthStencilResourceDesc.SampleDesc.Count = 1;
		depthStencilResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		depthStencilResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		// Depth バッファは GPU 専用メモリへ配置する。
		D3D12_HEAP_PROPERTIES depthStencilHeapProperties{};
		depthStencilHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		ID3D12Resource* newDepthStencilResource = nullptr;
		HRESULT createDepthResult = device->CreateCommittedResource(
			&depthStencilHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&depthStencilResourceDesc,
			initialState,
			&depthClearValue,
			IID_PPV_ARGS(&newDepthStencilResource));
		EDITOR_HR_VERIFY(createDepthResult);

		return newDepthStencilResource;
	};

	ID3D12Resource* depthStencilResource = createDepthStencilResource(
		renderWidth,
		renderHeight,
		D3D12_RESOURCE_STATE_DEPTH_WRITE);
	// 現在の描画サイズに合わせた Depth バッファ。
	ID3D12Resource* opaqueDepthCopyResource = createDepthStencilResource(
		renderWidth,
		renderHeight,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	// Depth バッファが作れないまま描画へ進むと、DepthStencilView / SRV / Barrier の
	// すべてが nullptr を参照する。Debug でしか気付けない不正状態なので起動を止める。
	if (depthStencilResource == nullptr || opaqueDepthCopyResource == nullptr) {
		if (depthStencilResource != nullptr) depthStencilResource->Release();
		if (opaqueDepthCopyResource != nullptr) opaqueDepthCopyResource->Release();
		RequestInitializationFailure();
		return;
	}

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
	D3D12_CPU_DESCRIPTOR_HANDLE opaqueDepthCopySrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap,
		device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
		kRuntimeOpaqueDepthCopySrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE opaqueDepthCopySrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap,
		device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
		kRuntimeOpaqueDepthCopySrvDescriptorIndex);
	device->CreateShaderResourceView(
		opaqueDepthCopyResource,
		&depthSrvDesc,
		opaqueDepthCopySrvHandleCPU);

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
	EDITOR_HR_VERIFY(hr);

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
		EDITOR_HR_VERIFY(hr);
		return resource;
	};

	UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	UINT srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// HDR RT (full resolution, R16G16B16A16_FLOAT) ? RTV index 2
	ID3D12Resource* hdrRenderTarget = createRenderTargetResource(renderWidth, renderHeight,
	                                                             DXGI_FORMAT_R16G16B16A16_FLOAT);
	D3D12_CPU_DESCRIPTOR_HANDLE hdrRtvHandle = GetCPUDescriptorHandle(rtvDescriptorHeap, rtvSize, 2);
	D3D12_RENDER_TARGET_VIEW_DESC hdrRtvDesc{};
	hdrRtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	hdrRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device->CreateRenderTargetView(hdrRenderTarget, &hdrRtvDesc, hdrRtvHandle);
	D3D12_CPU_DESCRIPTOR_HANDLE hdrSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap, srvSize, kRuntimeHdrSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE hdrSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap, srvSize, kRuntimeHdrSrvDescriptorIndex);
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

	ID3D12Resource* postProcessRenderTarget = createRenderTargetResource(
		renderWidth, renderHeight, DXGI_FORMAT_R8G8B8A8_UNORM);
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

	//================================================================
	// Weighted Blended OIT Render Targets
	//================================================================

	ID3D12Resource* oitAccumulationRenderTarget = createRenderTargetResource(
		renderWidth,
		renderHeight,
		DXGI_FORMAT_R16G16B16A16_FLOAT);
	ID3D12Resource* oitRevealageRenderTarget = createRenderTargetResource(
		renderWidth,
		renderHeight,
		DXGI_FORMAT_R16_FLOAT);
	D3D12_CPU_DESCRIPTOR_HANDLE oitRtvHandles[2] = {
		GetCPUDescriptorHandle(rtvDescriptorHeap, rtvSize, 12u),
		GetCPUDescriptorHandle(rtvDescriptorHeap, rtvSize, 13u)};
	D3D12_RENDER_TARGET_VIEW_DESC oitAccumulationRtvDesc{};
	oitAccumulationRtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	oitAccumulationRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	D3D12_RENDER_TARGET_VIEW_DESC oitRevealageRtvDesc = oitAccumulationRtvDesc;
	oitRevealageRtvDesc.Format = DXGI_FORMAT_R16_FLOAT;
	device->CreateRenderTargetView(
		oitAccumulationRenderTarget, &oitAccumulationRtvDesc, oitRtvHandles[0]);
	device->CreateRenderTargetView(
		oitRevealageRenderTarget, &oitRevealageRtvDesc, oitRtvHandles[1]);

	D3D12_CPU_DESCRIPTOR_HANDLE oitSrvHandlesCPU[2] = {
		GetCPUDescriptorHandle(srvDescriptorHeap, srvSize, kRuntimeOitAccumulationSrvDescriptorIndex),
		GetCPUDescriptorHandle(srvDescriptorHeap, srvSize, kRuntimeOitRevealageSrvDescriptorIndex)};
	D3D12_GPU_DESCRIPTOR_HANDLE oitSrvHandlesGPU[2] = {
		GetGPUDescriptorHandle(srvDescriptorHeap, srvSize, kRuntimeOitAccumulationSrvDescriptorIndex),
		GetGPUDescriptorHandle(srvDescriptorHeap, srvSize, kRuntimeOitRevealageSrvDescriptorIndex)};
	D3D12_SHADER_RESOURCE_VIEW_DESC oitAccumulationSrvDesc = hdrSrvDesc;
	D3D12_SHADER_RESOURCE_VIEW_DESC oitRevealageSrvDesc = hdrSrvDesc;
	oitRevealageSrvDesc.Format = DXGI_FORMAT_R16_FLOAT;
	device->CreateShaderResourceView(
		oitAccumulationRenderTarget, &oitAccumulationSrvDesc, oitSrvHandlesCPU[0]);
	device->CreateShaderResourceView(
		oitRevealageRenderTarget, &oitRevealageSrvDesc, oitSrvHandlesCPU[1]);
	const D3D12_CPU_DESCRIPTOR_HANDLE oitDuplicateRevealageSrvHandle = GetCPUDescriptorHandle(
		srvDescriptorHeap,
		srvSize,
		kRuntimeOitRevealageDuplicateSrvDescriptorIndex);
	device->CreateShaderResourceView(
		oitRevealageRenderTarget, &oitRevealageSrvDesc, oitDuplicateRevealageSrvHandle);

	ComPtr<IDxcUtils> dxcUtils;
	ComPtr<IDxcCompiler3> dxcCompiler; // HLSL を DXIL へコンパイルする DXC コンパイラ。
	ComPtr<IDxcIncludeHandler> includeHandler; // Shader の #include を解決する標準ハンドラ。
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxcUtils.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxcCompiler.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);
	hr = dxcUtils->CreateDefaultIncludeHandler(includeHandler.GetAddressOf());
	EDITOR_HR_VERIFY(hr);

	// VS main のコンパイル済みバイトコード。
	// 途中の1件で打ち切らず全Shaderを検査し、失敗一覧を最後にまとめて通知する。
	g_shaderCompilationFailures.clear();
	ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(
		L"Assets/Shaders/Object3d.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> batchedVertexShaderBlob = CompileShader(
		L"Assets/Shaders/Instancing/BatchedObject.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(), logStream);

	// PS main のコンパイル済みバイトコード。
	ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(
		L"Assets/Shaders/Object3d.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> oceanSurfacePixelShaderBlob = CompileShader(
		L"Assets/Shaders/Water/OceanSurface.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> oceanTessellationVertexShaderBlob = CompileShader(
		L"Assets/Shaders/Water/OceanTessellation.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> oceanTessellationHullShaderBlob = CompileShader(
		L"Assets/Shaders/Water/OceanTessellation.HS.hlsl", L"hs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> oceanTessellationDomainShaderBlob = CompileShader(
		L"Assets/Shaders/Water/OceanTessellation.DS.hlsl", L"ds_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);

	ComPtr<IDxcBlob> objectReflectionMaskPixelShaderBlob = CompileShader(
		L"Assets/Shaders/Object3dReflectionMask.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> gBufferVertexShaderBlob = CompileShader(
		L"Assets/Shaders/GBuffer/GBuffer.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> batchedGBufferVertexShaderBlob = CompileShader(
		L"Assets/Shaders/Instancing/BatchedGBuffer.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(), logStream);
	ComPtr<IDxcBlob> gBufferPixelShaderBlob = CompileShader(
		L"Assets/Shaders/GBuffer/GBuffer.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);

	Log(std::string("Object3d VS size=") + std::to_string(vertexShaderBlob != nullptr
		                                                      ? vertexShaderBlob->GetBufferSize()
		                                                      : 0ull) +
		" PS size=" + std::to_string(pixelShaderBlob != nullptr ? pixelShaderBlob->GetBufferSize() : 0ull));

	ComPtr<IDxcBlob> shadowVertexShaderBlob = CompileShader(
		L"Assets/Shaders/ShadowDepth.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> batchedShadowVertexShaderBlob = CompileShader(
		L"Assets/Shaders/Instancing/BatchedShadow.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(), logStream);
	ComPtr<IDxcBlob> alphaCutoutShadowPixelShaderBlob = CompileShader(
		L"Assets/Shaders/Shadow/AlphaCutoutShadowDepth.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);

	// Post-process shader compilation
	ComPtr<IDxcBlob> fullscreenVertexShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/FullScreen.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> toneMappingPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/ToneMapping.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> bloomExtractPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/BloomExtract.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> bloomBlurPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/BloomBlur.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> fxaaPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/FXAA.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssaoPixelShaderBlob = CompileShader(
		L"Assets/Shaders/AO/GTAO.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssaoBlurPixelShaderBlob = CompileShader(
		L"Assets/Shaders/Shadow/ContactShadow.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssgiPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/SSGI.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> volumetricLightShaftPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/VolumetricLightShaft.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssgiTemporalPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/SsgiTemporal.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssgiUpsamplePixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/SsgiUpsample.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> probeCaptureVertexShaderBlob = CompileShader(
		L"Assets/Shaders/GI/ProbeCapture.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> probeCapturePixelShaderBlob = CompileShader(
		L"Assets/Shaders/GI/ProbeCapture.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> probeShProjectionComputeShaderBlob = CompileShader(
		L"Assets/Shaders/GI/ProbeShProjection.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> probeVisibilityComputeShaderBlob = CompileShader(
		L"Assets/Shaders/GI/ProbeVisibility.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> skyboxPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/Skybox.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> planarReflectionPixelShaderBlob = CompileShader(
		L"Assets/Shaders/Reflection/PlanarReflection.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> sharpenPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/Sharpen.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> finalCompositePixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/FinalComposite.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> passthroughPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/Passthrough.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> depthOfFieldPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/DepthOfField.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> motionBlurPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/MotionBlur.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> weightedOitPixelShaderBlob = CompileShader(
		L"Assets/Shaders/Transparency/WeightedOIT.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> weightedOitCompositePixelShaderBlob = CompileShader(
		L"Assets/Shaders/Transparency/WeightedOITComposite.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> refractiveSurfacePixelShaderBlob = CompileShader(
		L"Assets/Shaders/Transparency/RefractiveSurface.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> underwaterCausticsPixelShaderBlob = CompileShader(
		L"Assets/Shaders/Water/UnderwaterCaustics.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> skinnedMotionVectorVertexShaderBlob = CompileShader(
		L"Assets/Shaders/Temporal/SkinnedMotionVector.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> bloomPrefilterPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/Bloom/BloomPrefilter.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> bloomDownsamplePixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/Bloom/BloomDownsample.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> bloomUpsamplePixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/Bloom/BloomUpsample.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> smaaEdgePixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/SMAA/SMAAEdgeDetection.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> smaaWeightPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/SMAA/SMAABlendWeight.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> smaaNeighborhoodPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/SMAA/SMAANeighborhoodBlend.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> glarePixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/Glare.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> filterPixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/Filter.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> autoExposurePixelShaderBlob = CompileShader(
		L"Assets/Shaders/PostProcess/AutoExposure.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> histogramExposureComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Compute/HistogramExposure.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> depthPyramidComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Depth/DepthPyramid.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> depthDownsampleComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Depth/DepthDownsample.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> reconstructNormalComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Depth/ReconstructNormal.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> cameraVelocityComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Temporal/CameraVelocity.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> velocityDilateComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Temporal/VelocityDilate.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> disocclusionMaskComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Temporal/DisocclusionMask.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> reactiveMaskComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Temporal/ReactiveMask.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssrTraceComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Reflection/SSRTrace.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssrResolveComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Reflection/SSRResolve.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssrTemporalResolveComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Reflection/SSRTemporalResolve.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssrDenoiseComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Reflection/SSRDenoise.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> ssrCompositeComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Reflection/SSRComposite.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> temporalResolveComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Temporal/TemporalResolve.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> copyDepthComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Temporal/CopyDepth.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> frustumCullingComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Culling/FrustumCulling.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> occlusionCullingComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Culling/OcclusionCulling.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> buildIndirectArgsComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Culling/BuildIndirectArgs.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> particleClearComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Particle/ParticleClear.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> particleUpdateComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Particle/ParticleUpdate.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> particleSpawnComputeShaderBlob = CompileShader(
		L"Assets/Shaders/Particle/ParticleSpawn.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> particleVertexShaderBlob = CompileShader(
		L"Assets/Shaders/Particle/Particle.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> particlePixelShaderBlob = CompileShader(
		L"Assets/Shaders/Particle/Particle.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(), includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> particleModelVertexShaderBlob = CompileShader(
		L"Assets/Shaders/Particle/ParticleModel.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> particleModelPixelShaderBlob = CompileShader(
		L"Assets/Shaders/Particle/ParticleModel.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> vfxPrimitiveVertexShaderBlob = CompileShader(
		L"Assets/Shaders/Effect/EffectPrimitive.VS.hlsl", L"vs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> vfxPrimitivePixelShaderBlob = CompileShader(
		L"Assets/Shaders/Effect/EffectPrimitive.PS.hlsl", L"ps_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> oceanFftUpdateSpectrumShaderBlob = CompileShader(
		L"Assets/Shaders/Water/OceanFFT_UpdateSpectrum.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> oceanFftRowShaderBlob = CompileShader(
		L"Assets/Shaders/Water/OceanFFT_Row.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> oceanFftTransposeShaderBlob = CompileShader(
		L"Assets/Shaders/Water/OceanFFT_Transpose.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);
	ComPtr<IDxcBlob> oceanFftFinalizeShaderBlob = CompileShader(
		L"Assets/Shaders/Water/OceanFFT_Finalize.CS.hlsl", L"cs_6_0", dxcUtils.Get(), dxcCompiler.Get(),
		includeHandler.Get(),
		logStream);

	if (vertexShaderBlob == nullptr ||
		batchedVertexShaderBlob == nullptr ||
		pixelShaderBlob == nullptr ||
		objectReflectionMaskPixelShaderBlob == nullptr ||
		gBufferVertexShaderBlob == nullptr ||
		batchedGBufferVertexShaderBlob == nullptr ||
		gBufferPixelShaderBlob == nullptr ||
		shadowVertexShaderBlob == nullptr ||
		batchedShadowVertexShaderBlob == nullptr ||
		alphaCutoutShadowPixelShaderBlob == nullptr ||
		fullscreenVertexShaderBlob == nullptr ||
		toneMappingPixelShaderBlob == nullptr ||
		bloomExtractPixelShaderBlob == nullptr ||
		bloomBlurPixelShaderBlob == nullptr ||
		fxaaPixelShaderBlob == nullptr ||
		ssaoPixelShaderBlob == nullptr ||
		ssaoBlurPixelShaderBlob == nullptr ||
		ssgiPixelShaderBlob == nullptr ||
		skyboxPixelShaderBlob == nullptr ||
		planarReflectionPixelShaderBlob == nullptr ||
		sharpenPixelShaderBlob == nullptr ||
		finalCompositePixelShaderBlob == nullptr ||
		passthroughPixelShaderBlob == nullptr ||
		depthOfFieldPixelShaderBlob == nullptr ||
		motionBlurPixelShaderBlob == nullptr ||
		weightedOitPixelShaderBlob == nullptr ||
		weightedOitCompositePixelShaderBlob == nullptr ||
		refractiveSurfacePixelShaderBlob == nullptr ||
		underwaterCausticsPixelShaderBlob == nullptr ||
		skinnedMotionVectorVertexShaderBlob == nullptr ||
		bloomPrefilterPixelShaderBlob == nullptr ||
		bloomDownsamplePixelShaderBlob == nullptr ||
		bloomUpsamplePixelShaderBlob == nullptr ||
		smaaEdgePixelShaderBlob == nullptr ||
		smaaWeightPixelShaderBlob == nullptr ||
		smaaNeighborhoodPixelShaderBlob == nullptr ||
		glarePixelShaderBlob == nullptr ||
		filterPixelShaderBlob == nullptr ||
		autoExposurePixelShaderBlob == nullptr ||
		depthPyramidComputeShaderBlob == nullptr ||
		depthDownsampleComputeShaderBlob == nullptr ||
		reconstructNormalComputeShaderBlob == nullptr ||
		cameraVelocityComputeShaderBlob == nullptr ||
		velocityDilateComputeShaderBlob == nullptr ||
		disocclusionMaskComputeShaderBlob == nullptr ||
		reactiveMaskComputeShaderBlob == nullptr ||
		ssrTraceComputeShaderBlob == nullptr ||
		ssrResolveComputeShaderBlob == nullptr ||
		ssrTemporalResolveComputeShaderBlob == nullptr ||
		ssrDenoiseComputeShaderBlob == nullptr ||
		ssrCompositeComputeShaderBlob == nullptr ||
		temporalResolveComputeShaderBlob == nullptr ||
		copyDepthComputeShaderBlob == nullptr ||
		frustumCullingComputeShaderBlob == nullptr ||
		occlusionCullingComputeShaderBlob == nullptr ||
		buildIndirectArgsComputeShaderBlob == nullptr ||
		particleClearComputeShaderBlob == nullptr ||
		particleUpdateComputeShaderBlob == nullptr ||
		particleSpawnComputeShaderBlob == nullptr ||
		particleVertexShaderBlob == nullptr ||
		particlePixelShaderBlob == nullptr ||
		particleModelVertexShaderBlob == nullptr ||
		particleModelPixelShaderBlob == nullptr ||
		vfxPrimitiveVertexShaderBlob == nullptr ||
		vfxPrimitivePixelShaderBlob == nullptr ||
		oceanFftUpdateSpectrumShaderBlob == nullptr ||
		oceanFftRowShaderBlob == nullptr ||
		oceanFftTransposeShaderBlob == nullptr ||
		oceanFftFinalizeShaderBlob == nullptr) {
		std::ostringstream failureText;
		failureText << "Engine同梱Shaderの検査で " << g_shaderCompilationFailures.size()
			<< " 件失敗しました。\n\n";
		constexpr std::size_t kDisplayedFailureCount = 12U;
		for (std::size_t index = 0U;
			index < (std::min)(g_shaderCompilationFailures.size(), kDisplayedFailureCount);
			++index) {
			std::string item = g_shaderCompilationFailures[index];
			if (item.size() > 500U) item.resize(500U);
			failureText << "- " << item << '\n';
		}
		if (g_shaderCompilationFailures.size() > kDisplayedFailureCount) {
			failureText << "\nほか " << (g_shaderCompilationFailures.size() - kDisplayedFailureCount) << " 件";
		}
		failureText << "\n\nこのEngineは配布内容が不完全です。Launcherの修復を実行してください。";
		const std::string failureMessage = failureText.str();
		Log(logStream, failureMessage);
		const std::wstring wideFailureMessage = ConvertString(failureMessage);
		MessageBoxW(windowHandle, wideFailureMessage.c_str(), L"CG2Engine - Engine Resource Error", MB_OK | MB_ICONERROR);
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: shader compile completed");

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

	D3D12_DESCRIPTOR_RANGE waterSceneColorRange[1] = {};
	waterSceneColorRange[0].BaseShaderRegister = 18u;
	waterSceneColorRange[0].NumDescriptors = 1u;
	waterSceneColorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	waterSceneColorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE waterSceneDepthRange[1] = {};
	waterSceneDepthRange[0].BaseShaderRegister = 19u;
	waterSceneDepthRange[0].NumDescriptors = 1u;
	waterSceneDepthRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	waterSceneDepthRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// t7-t13 は Normal / Metallic / Roughness / AO / Emission / Height / Opacity の順で使う。
	D3D12_DESCRIPTOR_RANGE materialMapDescriptorRanges[7][1] = {};
	for (int32_t materialMapIndex = 0; materialMapIndex < 7; materialMapIndex++) {
		materialMapDescriptorRanges[materialMapIndex][0].BaseShaderRegister =
			static_cast<UINT>(7 + materialMapIndex);
		materialMapDescriptorRanges[materialMapIndex][0].NumDescriptors = 1;
		materialMapDescriptorRanges[materialMapIndex][0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		materialMapDescriptorRanges[materialMapIndex][0].OffsetInDescriptorsFromTableStart =
			D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	}

	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// 0-10 は既存描画、11-17 は PBR Map、18-19 は Ocean FFT、20-21 は現在 / 前 Bone 行列。
	// 22-23 は水面専用パスが読む不透明 Scene Color / Depth、24 は Viewport ごとの水面復元定数。
	// 水面SSRでWorldを画面へ戻すため、逆行列20値にView軸と投影倍率12値を加える。
	// t20 = Light Probe の SH 係数、t21 = 八面体の可視性アトラス。
	// Root Signature は 64 DWORD 上限に対して既に 63 使っているため、
	// 2つのSRVを1つのDescriptor Table(1 DWORD)へまとめて丁度 64 に収める。
	D3D12_DESCRIPTOR_RANGE lightProbeDescriptorRange[1] = {};
	lightProbeDescriptorRange[0].BaseShaderRegister = 20u;
	lightProbeDescriptorRange[0].NumDescriptors = 2u;
	lightProbeDescriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	lightProbeDescriptorRange[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParameters[27] = {};

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

	for (int32_t materialMapIndex = 0; materialMapIndex < 7; materialMapIndex++) {
		const int32_t rootParameterIndex = 11 + materialMapIndex;
		rootParameters[rootParameterIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[rootParameterIndex].ShaderVisibility = materialMapIndex == 5
			? D3D12_SHADER_VISIBILITY_ALL
			: D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[rootParameterIndex].DescriptorTable.pDescriptorRanges =
			materialMapDescriptorRanges[materialMapIndex];
		rootParameters[rootParameterIndex].DescriptorTable.NumDescriptorRanges = 1;
	}

	rootParameters[18].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	// FFT変位はVSの輪郭生成とPSの連続面シェーディングで共有する。
	rootParameters[18].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[18].Descriptor.ShaderRegister = 14u;
	rootParameters[18].Descriptor.RegisterSpace = 0u;

	rootParameters[19].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParameters[19].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[19].Descriptor.ShaderRegister = 15u;
	rootParameters[19].Descriptor.RegisterSpace = 0u;

	rootParameters[20].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParameters[20].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[20].Descriptor.ShaderRegister = 16u;
	rootParameters[20].Descriptor.RegisterSpace = 0u;

	rootParameters[21].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParameters[21].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[21].Descriptor.ShaderRegister = 17u;
	rootParameters[21].Descriptor.RegisterSpace = 0u;

	rootParameters[22].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[22].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[22].DescriptorTable.pDescriptorRanges = waterSceneColorRange;
	rootParameters[22].DescriptorTable.NumDescriptorRanges = _countof(waterSceneColorRange);

	rootParameters[23].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[23].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[23].DescriptorTable.pDescriptorRanges = waterSceneDepthRange;
	rootParameters[23].DescriptorTable.NumDescriptorRanges = _countof(waterSceneDepthRange);

	rootParameters[24].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	// 通常描画はPSのWaterView、影描画はVSのShadowViewProjectionとして使う。
	// 同じRoot Constantsをパスごとに記録し、共有Upload Bufferの上書きを避ける。
	rootParameters[24].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[24].Constants.ShaderRegister = 3u;
	rootParameters[24].Constants.RegisterSpace = 0u;
	rootParameters[24].Constants.Num32BitValues = 29u;

	// Hardware TessellationはVS用b0と同じTransformをHS/DSから読む。
	// Pixel用b0とRegisterを重ねないため、同じResourceを独立したb4へ束縛する。
	rootParameters[25].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[25].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[25].Descriptor.ShaderRegister = 4u;
	rootParameters[25].Descriptor.RegisterSpace = 0u;

	rootParameters[26].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[26].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[26].DescriptorTable.pDescriptorRanges = lightProbeDescriptorRange;
	rootParameters[26].DescriptorTable.NumDescriptorRanges = _countof(lightProbeDescriptorRange);

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
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
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

	ComPtr<ID3DBlob> signatureBlob; // RootSignature のシリアライズ結果。
	ComPtr<ID3DBlob> errorBlob;
	Log(logStream, "Init Stage: creating material and transform buffers");
	ID3D12Resource* spriteMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));
	// Sprite 描画用の Material 定数バッファ。
	Material* spriteMaterialData = nullptr;
	// CPU から直接書き込める Sprite Material のマップ済みポインタ。
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
	spriteMaterialData->materialPadding0 = 0.0f;
	spriteMaterialData->materialPadding1 = 0.0f;
	spriteMaterialData->reflectionProbeCenter = {0.0f, 0.0f, 0.0f};
	spriteMaterialData->reflectionProbeBoxProjection = 0.0f;
	spriteMaterialData->reflectionProbeExtent = {1.0f, 1.0f, 1.0f};
	spriteMaterialData->materialPadding2 = 0.0f;
	spriteMaterialData->uvTransform = MakeIdentity4x4();

	ID3D12Resource* sphereMaterialResource = CreateBufferResource(device.Get(), sizeof(Material));
	Material* sphereMaterialData = nullptr;
	// CPU から直接書き込める 3D Material のマップ済みポインタ。
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
	sphereMaterialData->materialPadding0 = 0.0f;
	sphereMaterialData->materialPadding1 = 0.0f;
	sphereMaterialData->reflectionProbeCenter = {0.0f, 0.0f, 0.0f};
	sphereMaterialData->reflectionProbeBoxProjection = 0.0f;
	sphereMaterialData->reflectionProbeExtent = {1.0f, 1.0f, 1.0f};
	sphereMaterialData->materialPadding2 = 0.0f;
	sphereMaterialData->uvTransform = MakeIdentity4x4();

	ID3D12Resource* directionalLightResource = CreateBufferResource(device.Get(),
	                                                                sizeof(DirectionalLight) * kMaxSceneLights);
	DirectionalLight* directionalLightData = nullptr;
	// Inspector から色・向き・強さを書き換えるマップ済みポインタ。
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	for (uint32_t i = 0; i < kMaxSceneLights; i++) {
		directionalLightData[i].color = {0.0f, 0.0f, 0.0f, 1.0f};
		directionalLightData[i].direction = {0.0f, -1.0f, 0.0f};
		directionalLightData[i].intensity = 0.0f;
		directionalLightData[i].position = {0.0f, 0.0f, 0.0f};
		directionalLightData[i].range = 0.0f;
		directionalLightData[i].skyUpperColor = {0.0f, 0.0f, 0.0f};
		directionalLightData[i].skyIntensity = 0.0f;
		directionalLightData[i].skyLowerColor = {0.0f, 0.0f, 0.0f};
		directionalLightData[i].skyEmission = 0.0f;
		directionalLightData[i].ambientIntensity = 0.0f;
		directionalLightData[i].horizonSharpness = 1.0f;
		directionalLightData[i].reflectionIntensity = 0.0f;
		directionalLightData[i].spotCosInner = std::cos(20.0f * 3.1415926f / 180.0f);
		directionalLightData[i].spotCosOuter = std::cos(30.0f * 3.1415926f / 180.0f);
		directionalLightData[i].lightType = 0;
		directionalLightData[i].areaRadius = 0.0f;
		directionalLightData[i].cameraPosition = {0.0f, 0.0f, -5.0f};
		directionalLightData[i].environmentTextureEnabled = 0.0f;
		directionalLightData[i].environmentTextureIntensity = 0.0f;
		directionalLightData[i].environmentTextureRotation = 0.0f;
		directionalLightData[i].environmentTextureMipBias = 0.0f;
		directionalLightData[i].shadowTileIndex = -1.0f;
		directionalLightData[i].shadowTileUvScaleX = 0.0f;
		directionalLightData[i].shadowTileUvScaleY = 0.0f;
		directionalLightData[i].shadowTileUvBiasX = 0.0f;
		directionalLightData[i].shadowTileUvBiasY = 0.0f;
		directionalLightData[i].shadowEnabled = -1.0f;
		directionalLightData[i].shadowCascadeSplits = {};
		directionalLightData[i].shadowCascadeCount = 0.0f;
		directionalLightData[i].shadowCascadePadding0 = 0.0f;
		directionalLightData[i].shadowCascadePadding1 = 0.0f;
		directionalLightData[i].shadowCascadePadding2 = 0.0f;
		directionalLightData[i].shadowCascadeVP.fill({});
		directionalLightData[i].shadowCascadeAtlas.fill({});
	}

	ID3D12Resource* emissiveLightResource = CreateBufferResource(device.Get(), sizeof(EmissiveLightArray));
	EmissiveLightArray* emissiveLightData = nullptr;
	emissiveLightResource->Map(0, nullptr, reinterpret_cast<void**>(&emissiveLightData));
	emissiveLightData->count = 0;


	ID3D12Resource* spriteTransformationMatrixResource = CreateBufferResource(
		device.Get(), sizeof(TransformationMatrix));

	TransformationMatrix* spriteTransformationMatrixData = nullptr;
	spriteTransformationMatrixResource->Map(
		0, nullptr, reinterpret_cast<void**>(&spriteTransformationMatrixData));
	spriteTransformationMatrixData->WVP = MakeIdentity4x4();
	spriteTransformationMatrixData->previousWVP = MakeIdentity4x4();
	spriteTransformationMatrixData->oceanRenderParams = {};
	spriteTransformationMatrixData->temporalParams = {};
	spriteTransformationMatrixData->World = MakeIdentity4x4();
	spriteTransformationMatrixData->lightWVP = MakeIdentity4x4();

	ID3D12Resource* sphereTransformationMatrixResource = CreateBufferResource(
		device.Get(), sizeof(TransformationMatrix));

	TransformationMatrix* sphereTransformationMatrixData = nullptr;
	sphereTransformationMatrixResource->Map(
		0, nullptr, reinterpret_cast<void**>(&sphereTransformationMatrixData));
	sphereTransformationMatrixData->WVP = MakeIdentity4x4();
	sphereTransformationMatrixData->previousWVP = MakeIdentity4x4();
	sphereTransformationMatrixData->oceanRenderParams = {};
	sphereTransformationMatrixData->temporalParams = {};
	sphereTransformationMatrixData->World = MakeIdentity4x4();
	sphereTransformationMatrixData->lightWVP = MakeIdentity4x4();

	ID3D12Resource* identitySkinMatrixResource = CreateBufferResource(
		device.Get(), sizeof(Matrix4x4));
	Matrix4x4* identitySkinMatrixData = nullptr;

	if (identitySkinMatrixResource == nullptr ||
		FAILED(identitySkinMatrixResource->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(&identitySkinMatrixData))) ||
		identitySkinMatrixData == nullptr) {
		Log(logStream, "Identity skin matrix buffer creation failed.");
		RequestInitializationFailure();
		return;
	}

	*identitySkinMatrixData = MakeIdentity4x4();

	ID3D12Resource* batchInstanceResource = CreateBufferResource(
		device.Get(),
		sizeof(EditorBatchInstanceData) * kEditorBatchInstanceCapacity);
	EditorBatchInstanceData* batchInstanceData = nullptr;
	if (batchInstanceResource == nullptr ||
		FAILED(batchInstanceResource->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(&batchInstanceData))) ||
		batchInstanceData == nullptr) {
		Log(logStream, "Batch instance buffer creation failed.");
		RequestInitializationFailure();
		return;
	}
	Log(logStream, "Init Stage: material and transform buffers completed");

	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.GetAddressOf(),
		errorBlob.GetAddressOf());
	if (FAILED(hr)) {
		if (errorBlob != nullptr) {
			Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	Log(logStream, "Init Stage: object root signature serialized");

	ComPtr<ID3D12RootSignature> rootSignature; // PipelineState に設定する GPU 側の RootSignature。
	hr = device->CreateRootSignature(
		0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(rootSignature.GetAddressOf()));

	if (FAILED(hr) || rootSignature == nullptr) {
		Log(logStream, std::format("Object3d RootSignature Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: object root signature created");

	// inputElementDescs は静的 Mesh と Skinned Mesh で同じ VertexData を共有する。
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[5] = {};
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
	inputElementDescs[3].SemanticName = "BLENDINDICES";
	inputElementDescs[3].SemanticIndex = 0;
	inputElementDescs[3].Format = DXGI_FORMAT_R32G32B32A32_UINT;
	inputElementDescs[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[4].SemanticName = "BLENDWEIGHT";
	inputElementDescs[4].SemanticIndex = 0;
	inputElementDescs[4].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[4].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
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
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	ComPtr<ID3D12PipelineState> graphicsPipelineState;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
	                                         IID_PPV_ARGS(graphicsPipelineState.GetAddressOf()));

	if (FAILED(hr) || graphicsPipelineState == nullptr) {
		Log(logStream, std::format("Object3d PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: object pso created");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC planarScenePipelineStateDesc = graphicsPipelineStateDesc;
	// 反射行列を ViewProjection より前へ掛けると winding が反転する。
	// FRONT を落とすことで、鏡から見える本来の表面を反射 RT へ残す。
	planarScenePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;

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

	// 両面材質は通常の深度書き込みを維持したまま Back/Front の両方を描く。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC cullNonePipelineStateDesc = graphicsPipelineStateDesc;
	cullNonePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ComPtr<ID3D12PipelineState> cullNonePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&cullNonePipelineStateDesc,
		IID_PPV_ARGS(cullNonePipelineState.GetAddressOf()));

	if (FAILED(hr) || cullNonePipelineState == nullptr) {
		Log(logStream, std::format("CullNone PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: cull none pso created");

	// 静的Modelの自動Batch用。Root Parameter 20(t16)をBone行列ではなく
	// EditorBatchInstanceDataとして読み、同一Mesh/Materialを1 Drawへまとめる。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC batchedPipelineStateDesc = graphicsPipelineStateDesc;
	batchedPipelineStateDesc.VS = {
		batchedVertexShaderBlob->GetBufferPointer(),
		batchedVertexShaderBlob->GetBufferSize()
	};
	ComPtr<ID3D12PipelineState> batchedGraphicsPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&batchedPipelineStateDesc,
		IID_PPV_ARGS(batchedGraphicsPipelineState.GetAddressOf()));
	if (FAILED(hr) || batchedGraphicsPipelineState == nullptr) {
		Log(logStream, "Batched Object3d PSO Create failed.");
		RequestInitializationFailure();
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC batchedCullFrontPipelineStateDesc = batchedPipelineStateDesc;
	batchedCullFrontPipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
	batchedCullFrontPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ComPtr<ID3D12PipelineState> batchedCullFrontPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&batchedCullFrontPipelineStateDesc,
		IID_PPV_ARGS(batchedCullFrontPipelineState.GetAddressOf()));
	if (FAILED(hr) || batchedCullFrontPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC batchedCullNonePipelineStateDesc = batchedPipelineStateDesc;
	batchedCullNonePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ComPtr<ID3D12PipelineState> batchedCullNonePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&batchedCullNonePipelineStateDesc,
		IID_PPV_ARGS(batchedCullNonePipelineState.GetAddressOf()));
	if (FAILED(hr) || batchedCullNonePipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	// 半透明は Source Alpha で HDR 色を合成し、背後を隠さないよう Depth 書き込みを止める。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPipelineStateDesc = graphicsPipelineStateDesc;
	transparentPipelineStateDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	transparentPipelineStateDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparentPipelineStateDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparentPipelineStateDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	transparentPipelineStateDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	transparentPipelineStateDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	transparentPipelineStateDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparentPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	ComPtr<ID3D12PipelineState> transparentPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&transparentPipelineStateDesc,
		IID_PPV_ARGS(transparentPipelineState.GetAddressOf()));

	if (FAILED(hr) || transparentPipelineState == nullptr) {
		Log(logStream, std::format("Transparent PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentCullNonePipelineStateDesc = transparentPipelineStateDesc;
	transparentCullNonePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ComPtr<ID3D12PipelineState> transparentCullNonePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&transparentCullNonePipelineStateDesc,
		IID_PPV_ARGS(transparentCullNonePipelineState.GetAddressOf()));

	if (FAILED(hr) || transparentCullNonePipelineState == nullptr) {
		Log(logStream, std::format("Transparent CullNone PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: transparent pso created");

	// 水面はコピー済みの不透明DepthをShaderで読み、元Depthへ水面自身の深度を書き込む。
	// 手前の波が奥の波・Foamに上書きされないよう、通常のDepth Testを有効にする。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC waterSurfacePipelineStateDesc = graphicsPipelineStateDesc;
	waterSurfacePipelineStateDesc.PS = {
		oceanSurfacePixelShaderBlob->GetBufferPointer(),
		oceanSurfacePixelShaderBlob->GetBufferSize()};
	waterSurfacePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	waterSurfacePipelineStateDesc.DepthStencilState.DepthEnable = TRUE;
	waterSurfacePipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	// Tessellation Patch の共有辺や両面描画の同一深度Fragmentを再描画しない。
	// LESS_EQUALでは同じ水面を二重に評価し、法線量に応じた発光線として見えていた。
	waterSurfacePipelineStateDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	waterSurfacePipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	ComPtr<ID3D12PipelineState> waterSurfacePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&waterSurfacePipelineStateDesc,
		IID_PPV_ARGS(waterSurfacePipelineState.GetAddressOf()));

	if (FAILED(hr) || waterSurfacePipelineState == nullptr) {
		Log(logStream, std::format("Water surface PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: water surface pso created");

	// Ocean専用のHardware Tessellation。辺の画面Pixel長からHSが分割係数を決め、
	// DSが細分化後の各頂点で既存FFT変位を再評価する。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC waterTessellationPipelineStateDesc =
		waterSurfacePipelineStateDesc;
	waterTessellationPipelineStateDesc.VS = {
		oceanTessellationVertexShaderBlob->GetBufferPointer(),
		oceanTessellationVertexShaderBlob->GetBufferSize()};
	waterTessellationPipelineStateDesc.HS = {
		oceanTessellationHullShaderBlob->GetBufferPointer(),
		oceanTessellationHullShaderBlob->GetBufferSize()};
	waterTessellationPipelineStateDesc.DS = {
		oceanTessellationDomainShaderBlob->GetBufferPointer(),
		oceanTessellationDomainShaderBlob->GetBufferSize()};
	waterTessellationPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

	ComPtr<ID3D12PipelineState> waterTessellationPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&waterTessellationPipelineStateDesc,
		IID_PPV_ARGS(waterTessellationPipelineState.GetAddressOf()));

	if (FAILED(hr) || waterTessellationPipelineState == nullptr) {
		Log(logStream, std::format(
			"Water tessellation PSO Create failed. hr=0x{:08X}",
			static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: water tessellation pso created");

	// Transmission材質はScene Color / Depthを読み、屈折込みの完成色を直接HDRへ書く。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC refractiveSurfacePipelineStateDesc =
		waterSurfacePipelineStateDesc;
	refractiveSurfacePipelineStateDesc.PS = {
		refractiveSurfacePixelShaderBlob->GetBufferPointer(),
		refractiveSurfacePixelShaderBlob->GetBufferSize()};
	refractiveSurfacePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

	ComPtr<ID3D12PipelineState> refractiveSurfacePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&refractiveSurfacePipelineStateDesc,
		IID_PPV_ARGS(refractiveSurfacePipelineState.GetAddressOf()));

	if (FAILED(hr) || refractiveSurfacePipelineState == nullptr) {
		Log(logStream, std::format("Refractive surface PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC refractiveSurfaceCullNonePipelineStateDesc =
		refractiveSurfacePipelineStateDesc;
	refractiveSurfaceCullNonePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ComPtr<ID3D12PipelineState> refractiveSurfaceCullNonePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&refractiveSurfaceCullNonePipelineStateDesc,
		IID_PPV_ARGS(refractiveSurfaceCullNonePipelineState.GetAddressOf()));

	if (FAILED(hr) || refractiveSurfaceCullNonePipelineState == nullptr) {
		Log(logStream, std::format("Refractive CullNone PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: refractive surface pso created");

	// Weighted Blended OIT は描画順に依存せず、色の重み付き総和と透過率を同時出力する。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC weightedOitPipelineStateDesc = graphicsPipelineStateDesc;
	weightedOitPipelineStateDesc.PS = {
		weightedOitPixelShaderBlob->GetBufferPointer(),
		weightedOitPixelShaderBlob->GetBufferSize()};
	weightedOitPipelineStateDesc.NumRenderTargets = 2;
	weightedOitPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	weightedOitPipelineStateDesc.RTVFormats[1] = DXGI_FORMAT_R16_FLOAT;
	weightedOitPipelineStateDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	weightedOitPipelineStateDesc.BlendState.IndependentBlendEnable = TRUE;

	D3D12_RENDER_TARGET_BLEND_DESC& accumulationBlend =
		weightedOitPipelineStateDesc.BlendState.RenderTarget[0];
	accumulationBlend.BlendEnable = TRUE;
	accumulationBlend.SrcBlend = D3D12_BLEND_ONE;
	accumulationBlend.DestBlend = D3D12_BLEND_ONE;
	accumulationBlend.BlendOp = D3D12_BLEND_OP_ADD;
	accumulationBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
	accumulationBlend.DestBlendAlpha = D3D12_BLEND_ONE;
	accumulationBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	accumulationBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_RENDER_TARGET_BLEND_DESC& revealageBlend =
		weightedOitPipelineStateDesc.BlendState.RenderTarget[1];
	revealageBlend.BlendEnable = TRUE;
	revealageBlend.SrcBlend = D3D12_BLEND_ZERO;
	revealageBlend.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
	revealageBlend.BlendOp = D3D12_BLEND_OP_ADD;
	revealageBlend.SrcBlendAlpha = D3D12_BLEND_ZERO;
	revealageBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	revealageBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	revealageBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;

	ComPtr<ID3D12PipelineState> weightedOitPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&weightedOitPipelineStateDesc,
		IID_PPV_ARGS(weightedOitPipelineState.GetAddressOf()));

	if (FAILED(hr) || weightedOitPipelineState == nullptr) {
		Log(logStream, std::format("Weighted OIT PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC weightedOitCullNonePipelineStateDesc = weightedOitPipelineStateDesc;
	weightedOitCullNonePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ComPtr<ID3D12PipelineState> weightedOitCullNonePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&weightedOitCullNonePipelineStateDesc,
		IID_PPV_ARGS(weightedOitCullNonePipelineState.GetAddressOf()));

	if (FAILED(hr) || weightedOitCullNonePipelineState == nullptr) {
		Log(logStream, std::format("Weighted OIT CullNone PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: weighted oit pso created");

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

	// 両面材質は裏面も影へ残す。通常材質と Masked 材質で PixelShader の有無だけを分ける。
	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowCullNonePipelineStateDesc = shadowPipelineStateDesc;
	shadowCullNonePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ComPtr<ID3D12PipelineState> shadowCullNonePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&shadowCullNonePipelineStateDesc,
		IID_PPV_ARGS(shadowCullNonePipelineState.GetAddressOf()));

	if (FAILED(hr) || shadowCullNonePipelineState == nullptr) {
		Log(logStream, std::format("Shadow CullNone PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC batchedShadowPipelineStateDesc = shadowPipelineStateDesc;
	batchedShadowPipelineStateDesc.VS = {
		batchedShadowVertexShaderBlob->GetBufferPointer(),
		batchedShadowVertexShaderBlob->GetBufferSize()
	};
	ComPtr<ID3D12PipelineState> batchedShadowPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&batchedShadowPipelineStateDesc,
		IID_PPV_ARGS(batchedShadowPipelineState.GetAddressOf()));
	if (FAILED(hr) || batchedShadowPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC batchedShadowCullNonePipelineStateDesc = batchedShadowPipelineStateDesc;
	batchedShadowCullNonePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ComPtr<ID3D12PipelineState> batchedShadowCullNonePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&batchedShadowCullNonePipelineStateDesc,
		IID_PPV_ARGS(batchedShadowCullNonePipelineState.GetAddressOf()));
	if (FAILED(hr) || batchedShadowCullNonePipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaCutoutShadowPipelineStateDesc = shadowPipelineStateDesc;
	alphaCutoutShadowPipelineStateDesc.PS = {
		alphaCutoutShadowPixelShaderBlob->GetBufferPointer(),
		alphaCutoutShadowPixelShaderBlob->GetBufferSize()
	};

	ComPtr<ID3D12PipelineState> alphaCutoutShadowPipelineState;
	hr = device->CreateGraphicsPipelineState(
		&alphaCutoutShadowPipelineStateDesc,
		IID_PPV_ARGS(alphaCutoutShadowPipelineState.GetAddressOf()));

	if (FAILED(hr) || alphaCutoutShadowPipelineState == nullptr) {
		Log(logStream, std::format("Alpha cutout shadow PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaCutoutShadowCullNonePipelineStateDesc =
		alphaCutoutShadowPipelineStateDesc;
	alphaCutoutShadowCullNonePipelineStateDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ComPtr<ID3D12PipelineState> alphaCutoutShadowCullNonePipelineState;
	hr = device->CreateGraphicsPipelineState(
		&alphaCutoutShadowCullNonePipelineStateDesc,
		IID_PPV_ARGS(alphaCutoutShadowCullNonePipelineState.GetAddressOf()));

	if (FAILED(hr) || alphaCutoutShadowCullNonePipelineState == nullptr) {
		Log(
			logStream,
			std::format("Alpha cutout shadow CullNone PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: alpha cutout shadow pso created");

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

	D3D12_DESCRIPTOR_RANGE postProcessDescriptorRange5[1] = {};
	postProcessDescriptorRange5[0].BaseShaderRegister = 5u;
	postProcessDescriptorRange5[0].NumDescriptors = 1u;
	postProcessDescriptorRange5[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	postProcessDescriptorRange5[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE postProcessDescriptorRange6[1] = {};
	postProcessDescriptorRange6[0].BaseShaderRegister = 6u;
	postProcessDescriptorRange6[0].NumDescriptors = 1u;
	postProcessDescriptorRange6[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	postProcessDescriptorRange6[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE postProcessDescriptorRange7[1] = {};
	postProcessDescriptorRange7[0].BaseShaderRegister = 7u;
	postProcessDescriptorRange7[0].NumDescriptors = 1u;
	postProcessDescriptorRange7[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	postProcessDescriptorRange7[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER postProcessRootParameters[8] = {};
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
	postProcessRootParameters[2].Constants.Num32BitValues = 48;

	postProcessRootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	postProcessRootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	postProcessRootParameters[3].DescriptorTable.pDescriptorRanges = postProcessDescriptorRange2;
	postProcessRootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(postProcessDescriptorRange2);

	// t4 は Underwater が描画と同じ FFT 変位を参照するための Root SRV。
	postProcessRootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	postProcessRootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	postProcessRootParameters[4].Descriptor.ShaderRegister = 4u;
	postProcessRootParameters[4].Descriptor.RegisterSpace = 0u;

	// t5 は FinalComposite が参照する 1x1 の自動露出履歴。
	postProcessRootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	postProcessRootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	postProcessRootParameters[5].DescriptorTable.pDescriptorRanges = postProcessDescriptorRange5;
	postProcessRootParameters[5].DescriptorTable.NumDescriptorRanges = _countof(postProcessDescriptorRange5);

	// t6 は FinalComposite が参照する 2D strip Color Grading LUT。
	postProcessRootParameters[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	postProcessRootParameters[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	postProcessRootParameters[6].DescriptorTable.pDescriptorRanges = postProcessDescriptorRange6;
	postProcessRootParameters[6].DescriptorTable.NumDescriptorRanges = _countof(postProcessDescriptorRange6);

	// t7 は FinalComposite の遠景Heat Shimmerが近景を除外するためのScene Depth。
	postProcessRootParameters[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	postProcessRootParameters[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	postProcessRootParameters[7].DescriptorTable.pDescriptorRanges = postProcessDescriptorRange7;
	postProcessRootParameters[7].DescriptorTable.NumDescriptorRanges = _countof(postProcessDescriptorRange7);

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
	if (FAILED(hr) || postProcessSignatureBlob == nullptr) {
		Log(logStream, std::format(
			"PostProcess RootSignature serialize failed. hr=0x{:08X}",
			static_cast<uint32_t>(hr)));

		if (postProcessErrorBlob != nullptr) {
			const auto* errorMessage = reinterpret_cast<const char*>(postProcessErrorBlob->GetBufferPointer());
			Log(logStream, std::string(errorMessage, postProcessErrorBlob->GetBufferSize()));
		}

		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: post-process root signature serialized");

	ComPtr<ID3D12RootSignature> postProcessRootSignature;
	hr = device->CreateRootSignature(
		0, postProcessSignatureBlob->GetBufferPointer(), postProcessSignatureBlob->GetBufferSize(),
		IID_PPV_ARGS(postProcessRootSignature.GetAddressOf()));

	if (FAILED(hr) || postProcessRootSignature == nullptr) {
		Log(logStream, std::format("PostProcess RootSignature Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	Log(logStream, "Init Stage: post-process root signature created");

	// Post-process PSOs share common state (no depth, no culling, no vertex buffer)
	auto CreatePostProcessPSO = [&](const char* psoName, IDxcBlob* psBlob,
	                                DXGI_FORMAT rtvFormat,
	                                bool additiveBlend = false) -> ComPtr<ID3D12PipelineState> {
		if (psBlob == nullptr || fullscreenVertexShaderBlob == nullptr) {
			Log(std::string("CreatePostProcessPSO skipped: ") + psoName + " shader blob is null");
			return nullptr;
		}

		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
		desc.pRootSignature = postProcessRootSignature.Get();
		desc.VS = {fullscreenVertexShaderBlob->GetBufferPointer(), fullscreenVertexShaderBlob->GetBufferSize()};
		desc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
		D3D12_BLEND_DESC blendDesc{};
		blendDesc.RenderTarget[0].BlendEnable = additiveBlend ? TRUE : FALSE;
		blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlend = additiveBlend ? D3D12_BLEND_ONE : D3D12_BLEND_ZERO;
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

#ifdef _DEBUG
			if (infoQueue != nullptr) {
				const UINT64 messageCount = infoQueue->GetNumStoredMessages();
				const UINT64 firstMessageIndex = messageCount > 8u ? messageCount - 8u : 0u;

				for (UINT64 messageIndex = firstMessageIndex; messageIndex < messageCount; messageIndex++) {
					SIZE_T messageLength = 0u;
					infoQueue->GetMessage(messageIndex, nullptr, &messageLength);
					std::vector<uint8_t> messageStorage(messageLength);
					auto* message = reinterpret_cast<D3D12_MESSAGE*>(messageStorage.data());

					if (SUCCEEDED(infoQueue->GetMessage(messageIndex, message, &messageLength)) &&
						message->pDescription != nullptr) {
						Log(logStream, std::string("D3D12: ") + message->pDescription);
					}
				}
			}
#endif
		}
		else {
			Log(logStream, std::format("Init Stage: {} pso created", psoName));
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

	ComPtr<ID3D12PipelineState> finalCompositePipelineState = CreatePostProcessPSO(
		"FinalComposite", finalCompositePixelShaderBlob.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);

	if (finalCompositePipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> passthroughPipelineState = CreatePostProcessPSO(
		"Passthrough", passthroughPixelShaderBlob.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
	if (passthroughPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> depthOfFieldPipelineState = CreatePostProcessPSO(
		"DepthOfField", depthOfFieldPixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (depthOfFieldPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> motionBlurPipelineState = CreatePostProcessPSO(
		"MotionBlur", motionBlurPixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (motionBlurPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> weightedOitCompositePipelineState = CreatePostProcessPSO(
		"WeightedOITComposite", weightedOitCompositePixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (weightedOitCompositePipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> underwaterCausticsPipelineState = CreatePostProcessPSO(
		"UnderwaterCaustics", underwaterCausticsPixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (underwaterCausticsPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	const bool isGBufferInitialized = g_gBufferManager.Initialize(
		device.Get(),
		srvDescriptorHeap,
		srvSize,
		rootSignature.Get(),
		gBufferVertexShaderBlob.Get(),
		batchedGBufferVertexShaderBlob.Get(),
		gBufferPixelShaderBlob.Get(),
		inputElementDescs,
		_countof(inputElementDescs),
		renderWidth,
		renderHeight);

	if (!isGBufferInitialized) {
		Log(logStream, "GBuffer initialization failed");
		RequestInitializationFailure();
		return;
	}

	// Light Probe GI は任意機能なので、初期化に失敗しても描画自体は続行する。
	const bool isLightProbeInitialized = g_lightProbeManager.Initialize(
		device.Get(),
		srvDescriptorHeap,
		srvSize,
		rootSignature.Get(),
		probeCaptureVertexShaderBlob.Get(),
		probeCapturePixelShaderBlob.Get(),
		probeShProjectionComputeShaderBlob.Get(),
		probeVisibilityComputeShaderBlob.Get(),
		inputElementDescs,
		_countof(inputElementDescs));

	Log(logStream, isLightProbeInitialized
		? "Init Stage: light probe manager initialized"
		: "Light probe manager initialization failed (GI disabled)");

	const bool isDepthHierarchyInitialized = g_depthHierarchyManager.Initialize(
		device.Get(),
		srvDescriptorHeap,
		srvSize,
		depthPyramidComputeShaderBlob.Get(),
		depthDownsampleComputeShaderBlob.Get(),
		reconstructNormalComputeShaderBlob.Get(),
		renderWidth,
		renderHeight);

	if (!isDepthHierarchyInitialized) {
		Log(logStream, "Depth hierarchy initialization failed");
		RequestInitializationFailure();
		return;
	}

	const std::array<IDxcBlob*, EditorTemporalRenderingManager::kPipelineCount> temporalShaderBlobs = {
		cameraVelocityComputeShaderBlob.Get(),
		velocityDilateComputeShaderBlob.Get(),
		disocclusionMaskComputeShaderBlob.Get(),
		reactiveMaskComputeShaderBlob.Get(),
		ssrTraceComputeShaderBlob.Get(),
		ssrResolveComputeShaderBlob.Get(),
		ssrTemporalResolveComputeShaderBlob.Get(),
		ssrDenoiseComputeShaderBlob.Get(),
		ssrCompositeComputeShaderBlob.Get(),
		temporalResolveComputeShaderBlob.Get(),
		copyDepthComputeShaderBlob.Get(),
	};
	const bool isTemporalRenderingInitialized = g_temporalRenderingManager.Initialize(
		device.Get(),
		srvDescriptorHeap,
		srvSize,
		temporalShaderBlobs,
		renderWidth,
		renderHeight);

	if (!isTemporalRenderingInitialized) {
		Log(logStream, "Temporal rendering initialization failed");
		RequestInitializationFailure();
		return;
	}

	const std::array<IDxcBlob*, EditorPostProcessQualityManager::kPipelineCount>
		postProcessQualityShaderBlobs = {
			bloomPrefilterPixelShaderBlob.Get(),
			bloomDownsamplePixelShaderBlob.Get(),
			bloomUpsamplePixelShaderBlob.Get(),
			smaaEdgePixelShaderBlob.Get(),
			smaaWeightPixelShaderBlob.Get(),
			smaaNeighborhoodPixelShaderBlob.Get(),
			glarePixelShaderBlob.Get(),
			filterPixelShaderBlob.Get(),
			autoExposurePixelShaderBlob.Get(),
		};
	const bool isPostProcessQualityInitialized = g_postProcessQualityManager.Initialize(
		device.Get(),
		srvDescriptorHeap,
		srvSize,
		fullscreenVertexShaderBlob.Get(),
		postProcessQualityShaderBlobs,
		histogramExposureComputeShaderBlob.Get(),
		renderWidth,
		renderHeight);

	if (!isPostProcessQualityInitialized) {
		Log(logStream, "Post-process quality initialization failed");
		RequestInitializationFailure();
		return;
	}

	const bool isGpuCullingInitialized = g_gpuCullingManager.Initialize(
		device.Get(),
		srvDescriptorHeap,
		srvSize,
		frustumCullingComputeShaderBlob.Get(),
		occlusionCullingComputeShaderBlob.Get(),
		buildIndirectArgsComputeShaderBlob.Get());

	if (!isGpuCullingInitialized) {
		Log(logStream, "GPU culling initialization failed");
		RequestInitializationFailure();
		return;
	}

	const bool isGpuParticleInitialized = g_gpuParticleManager.Initialize(
		device.Get(),
		particleClearComputeShaderBlob.Get(),
		particleUpdateComputeShaderBlob.Get(),
		particleSpawnComputeShaderBlob.Get(),
		particleVertexShaderBlob.Get(),
		particlePixelShaderBlob.Get(),
		particleModelVertexShaderBlob.Get(),
		particleModelPixelShaderBlob.Get(),
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_D24_UNORM_S8_UINT);

	if (!isGpuParticleInitialized) {
		Log(logStream, "GPU particle initialization failed");
		RequestInitializationFailure();
		return;
	}

	const bool isVfxRendererInitialized = g_vfxRenderer.Initialize(
		device.Get(),
		vfxPrimitiveVertexShaderBlob.Get(),
		vfxPrimitivePixelShaderBlob.Get(),
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_D24_UNORM_S8_UINT);

	if (!isVfxRendererInitialized) {
		Log(logStream, "VFX renderer initialization failed");
		RequestInitializationFailure();
		return;
	}

	const bool isEffekseerInitialized = g_editorRuntimeManager.GetEffekseerManager().InitializeGraphics(
		device.Get(),
		commandQueue.Get(),
		kRuntimeSwapChainBufferCount,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_D24_UNORM_S8_UINT);

	if (!isEffekseerInitialized) {
		Log(logStream, "Effekseer DX12 initialization failed");
		RequestInitializationFailure();
		return;
	}

	ModelData modelData = LoadObjFile("resources", "plane.obj");
	if (modelData.vertices.empty()) {
		Log(logStream, "resources/plane.obj is missing or invalid; using the procedural plane.");
		modelData.vertices = CreatePlaneVertices();
	}

	if (modelData.material.textureFilePath.empty() ||
		!std::filesystem::exists(ResolveEngineOrProjectFilePath(modelData.material.textureFilePath))) {
		modelData.material.textureFilePath = "resources/editorDefault/uvChecker.png";
	}

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
			// lon と lonNext は現在のセルの左右に対応する経度角。
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

	// 旧 Sprite プレビューの基準位置とサイズ。
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

	// 四角形を 2 枚の三角形で描くための Index Buffer。
	uint32_t spriteIndices[] = {
		0, 1, 2,
		2, 1, 3,
	};

	Transforms transform{
		.scale = {0.55f, 0.55f, 0.55f},
		.rotate = {0.0f, 0.0f, 0.0f},
		.translate = {0.0f, 0.0f, 0.0f}
	};

	// 旧 Sprite プレビューの Transform。Sprite のサイズを scale に設定する。
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
	if (vertexResource == nullptr) {
		Log(logStream, "Sphere vertex buffer creation failed.");
		RequestInitializationFailure();
		return;
	}

	const bool isOceanFftInitialized = g_oceanFftManager.Initialize(
		device.Get(),
		oceanFftUpdateSpectrumShaderBlob.Get(),
		oceanFftRowShaderBlob.Get(),
		oceanFftTransposeShaderBlob.Get(),
		oceanFftFinalizeShaderBlob.Get());

	if (!isOceanFftInitialized) {
		Log(logStream, "Ocean FFT initialization failed");
		RequestInitializationFailure();
		return;
	}

	VertexData* mappedVertexData = nullptr; // vertexResource へ CPU から頂点を書き込むためのポインタ。
	hr = vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertexData));
	if (FAILED(hr) || mappedVertexData == nullptr) {
		Log(logStream, std::format("Sphere vertex buffer Map failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		vertexResource->Release();
		RequestInitializationFailure();
		return;
	}

	std::memcpy(mappedVertexData, vertices.data(), sizeof(VertexData) * vertices.size());

	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// plane.obj の頂点を GPU へ渡す Upload Buffer。
	ID3D12Resource* modelVertexResource = CreateBufferResource(device.Get(),
	                                                           sizeof(VertexData) * modelData.vertices.size());
	if (modelVertexResource == nullptr) {
		Log(logStream, "Plane vertex buffer creation failed.");
		RequestInitializationFailure();
		return;
	}

	VertexData* mappedModelVertexData = nullptr;
	// modelVertexResource へ CPU から頂点を書き込むためのポインタ。
	hr = modelVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedModelVertexData));
	if (FAILED(hr) || mappedModelVertexData == nullptr) {
		Log(logStream, std::format("Plane vertex buffer Map failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		modelVertexResource->Release();
		RequestInitializationFailure();
		return;
	}

	std::memcpy(mappedModelVertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());

	// plane.obj の頂点 Buffer を Draw に渡すための View。
	D3D12_VERTEX_BUFFER_VIEW modelVertexBufferView{};
	modelVertexBufferView.BufferLocation = modelVertexResource->GetGPUVirtualAddress();
	modelVertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(VertexData) * modelData.vertices.size());
	modelVertexBufferView.StrideInBytes = sizeof(VertexData);

	ModelData primitiveModelData[kEditorModelMeshTypeCount]{};
	ID3D12Resource* primitiveVertexResources[kEditorModelMeshTypeCount] = {};
	// 基本形ごとの GPU 頂点 Buffer。
	D3D12_VERTEX_BUFFER_VIEW primitiveVertexBufferViews[kEditorModelMeshTypeCount]{};
	// Draw 時に IA へ渡す基本形ごとの Buffer View。
	uint32_t primitiveVertexCounts[kEditorModelMeshTypeCount] = {}; // DrawInstanced に渡す基本形ごとの頂点数。
	CreatePrimitiveMeshBuffers(
		device.Get(),
		modelData,
		primitiveModelData,
		primitiveVertexResources,
		primitiveVertexBufferViews,
		primitiveVertexCounts);

	ID3D12Resource* spriteVertexResource = CreateBufferResource(device.Get(), sizeof(spriteVertices));
	// Sprite 四角形の頂点を GPU へ渡す Upload Buffer。
	VertexData* mappedSpriteVertexData = nullptr;
	// Sprite 頂点を書き込むための CPU 側マップ済みポインタ。
	hr = spriteVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteVertexData));
	EDITOR_HR_VERIFY(hr);
	std::memcpy(mappedSpriteVertexData, spriteVertices, sizeof(spriteVertices));

	// Sprite 頂点 Buffer を Draw に渡すための View。
	D3D12_VERTEX_BUFFER_VIEW spriteVertexBufferView{};
	spriteVertexBufferView.BufferLocation = spriteVertexResource->GetGPUVirtualAddress();
	spriteVertexBufferView.SizeInBytes = sizeof(spriteVertices);
	spriteVertexBufferView.StrideInBytes = sizeof(VertexData);

	ID3D12Resource* spriteIndexResource = CreateBufferResource(device.Get(), sizeof(spriteIndices));
	// Sprite 四角形の Index Buffer。
	uint32_t* mappedSpriteIndexData = nullptr; // Sprite の Index を CPU から書き込むためのポインタ。
	hr = spriteIndexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedSpriteIndexData));
	EDITOR_HR_VERIFY(hr);
	std::memcpy(mappedSpriteIndexData, spriteIndices, sizeof(spriteIndices));

	// Sprite の Index Buffer を DrawIndexed に渡すための View。
	D3D12_INDEX_BUFFER_VIEW spriteIndexBufferView{};
	spriteIndexBufferView.BufferLocation = spriteIndexResource->GetGPUVirtualAddress();
	spriteIndexBufferView.SizeInBytes = sizeof(spriteIndices);
	spriteIndexBufferView.Format = DXGI_FORMAT_R32_UINT;

	constexpr float editorMenuHeight = 20.0f;
	constexpr float editorSceneHeaderHeight = 24.0f;

	float editorWindowWidth = static_cast<float>(renderWidth);
	// ImGui と Scene View で使用する Window サイズを float で保持する。
	float editorWindowHeight = static_cast<float>(renderHeight);

	float editorLeftWidth = 250.0f;
	float editorRightWidth = 320.0f;
	float editorBottomHeight = 190.0f;

	float editorSceneX = editorLeftWidth;
	// DirectX の Viewport を Scene View に合わせるための矩形。
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
		L"resources/editorDefault/uvChecker.png",
		L"resources/editorDefault/monsterBall.png",
		ConvertString(modelData.material.textureFilePath),
		L"resources/editorDefault/ball.png",
	};
	const std::wstring fallbackTextureFilePath = L"resources/editorDefault/uvChecker.png";
	for (std::wstring& textureFilePath : textureFilePaths) {
		if (textureFilePath.empty() || !std::filesystem::exists(ResolveEngineOrProjectFilePath(textureFilePath))) {
			Log(logStream, std::format(
				"Texture '{}' is missing; using resources/editorDefault/uvChecker.png.",
				ConvertString(textureFilePath)));
			textureFilePath = fallbackTextureFilePath;
		}
	}

	std::string textureFilePathStrings[_countof(textureFilePaths)];
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		textureFilePathStrings[textureIndex] = ConvertString(textureFilePaths[textureIndex]);
	}
	std::vector<std::string> editorTextureFilePaths;
	editorTextureFilePaths.reserve(_countof(textureFilePaths));
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		editorTextureFilePaths.push_back(textureFilePathStrings[textureIndex]);
	}

	DirectX::ScratchImage mipImages[_countof(textureFilePaths)];
	// DirectXTex が生成した mipmap 付き画像データ。
	DirectX::TexMetadata textureMetadatas[_countof(textureFilePaths)];
	// 各 Texture のサイズ、形式、mip 数を保持するメタデータ。

	ID3D12Resource* textureResources[_countof(textureFilePaths)] = {nullptr};
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		mipImages[textureIndex] = LoadTexture(textureFilePaths[textureIndex]);
		if (mipImages[textureIndex].GetImageCount() == 0u) {
			Log(logStream, std::format(
				"Texture load failed: {}",
				ConvertString(textureFilePaths[textureIndex])));
			RequestInitializationFailure();
			return;
		}

		textureMetadatas[textureIndex] = mipImages[textureIndex].GetMetadata();
		textureResources[textureIndex] = CreateTextureResource(device.Get(), textureMetadatas[textureIndex]);
		if (textureResources[textureIndex] == nullptr) {
			Log(logStream, std::format(
				"Texture resource creation failed: {}",
				ConvertString(textureFilePaths[textureIndex])));
			RequestInitializationFailure();
			return;
		}
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

	float editorCameraMoveSpeed = 0.12f; // Inspector から調整できる Scene カメラの移動速度。
	float editorCameraRotateSpeed = 0.006f;
	float editorCameraWheelMoveSpeed = 0.5f;
	float editorCameraPanSpeed = 0.01f;
	float editorCameraFastRate = 4.0f;

	// Scene View の背景色 RGBA。
	float sceneClearColor[4] = {0.1f, 0.25f, 0.5f, 1.0f};

	bool isSceneGizmoVisible = true;
	bool isLightGizmoVisible = false;
	bool isCameraGizmoVisible = false;

	// Directional Light のアイコンを Scene View に表示するワールド座標。
	Vector3 directionalLightIconPosition = {-1.8f, 1.4f, 0.0f};

	EditorSceneObjectManager editorSceneObjectManager;
	// GameObject に対応する DirectX 描画用の SceneObject を保持する。
	editorSceneObjectManager.Initialize(device.Get());

	std::vector<EditorSceneObject>& editorSceneObjects = editorSceneObjectManager.GetSceneObjects();
	int32_t selectedPlacedSceneObjectIndex = -1;
	ComPtr<ID3D12Fence> fence;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
	EDITOR_HR_VERIFY(hr);

	HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent != nullptr);

	if (fenceEvent == nullptr) {
		RequestInitializationFailure();
		return;
	}

	auto waitForGpu = [&]() {
		fenceValue++;
		HRESULT signalResult = commandQueue->Signal(fence.Get(), fenceValue);
		EDITOR_HR_VERIFY(signalResult);

		if (fence->GetCompletedValue() < fenceValue) {
			HRESULT eventResult = fence->SetEventOnCompletion(fenceValue, fenceEvent);
			EDITOR_HR_VERIFY(eventResult);
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

		// 描画サイズの変更に合わせ、古い DepthStencil も作り直す。
		if (depthStencilResource != nullptr) {
			depthStencilResource->Release();
			depthStencilResource = nullptr;
		}

		if (opaqueDepthCopyResource != nullptr) {
			opaqueDepthCopyResource->Release();
			opaqueDepthCopyResource = nullptr;
		}

		renderWidth = width; // 新しい SwapChain の幅を保持する。
		renderHeight = height;

		// SwapChain の Back Buffer を新しいサイズで再生成する。
		HRESULT resizeResult = swapChain->ResizeBuffers(
			swapChainDesc.BufferCount,
			renderWidth,
			renderHeight,
			swapChainDesc.Format,
			0);
		EDITOR_HR_VERIFY(resizeResult);

		// 古い back buffer は上で Release 済みなので、ResizeBuffers が失敗しても
		// GetBuffer は必ず試す。失敗した Buffer に対して RTV を作ると nullptr を
		// 渡すことになるため、取得できた Buffer だけ RTV を張り直す。
		bool swapChainBuffersReady = true;
		for (uint32_t bufferIndex = 0; bufferIndex < swapChainDesc.BufferCount; ++bufferIndex) {
			HRESULT getBufferResult =
				swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&swapChainResources[bufferIndex]));
			if (!EDITOR_HR_OK(getBufferResult) || swapChainResources[bufferIndex] == nullptr) {
				swapChainBuffersReady = false;
				continue;
			}
			device->CreateRenderTargetView(swapChainResources[bufferIndex], &rtvDesc, rtvHandles[bufferIndex]);
		}

		// back buffer が 1 枚でも欠けた状態では Present も RTV 遷移もできない。
		// Device Removed 等の復帰不能な失敗なので、null を触る前に終了要求を出す。
		if (!swapChainBuffersReady) {
			RequestFatalRuntimeFailure();
			return;
		}

		depthStencilResource = createDepthStencilResource(
			renderWidth,
			renderHeight,
			D3D12_RESOURCE_STATE_DEPTH_WRITE);
		opaqueDepthCopyResource = createDepthStencilResource(
			renderWidth,
			renderHeight,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		// DepthStencil も新しい描画サイズに合わせて再生成する。
		device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvHandle);
		device->CreateShaderResourceView(
			opaqueDepthCopyResource,
			&depthSrvDesc,
			opaqueDepthCopySrvHandleCPU);
	};

	hr = commandAllocator->Reset();
	EDITOR_HR_VERIFY(hr);
	hr = commandList->Reset(commandAllocator.Get(), nullptr);
	EDITOR_HR_VERIFY(hr);

	// Texture Upload に使用する一時的な Upload Buffer。
	ID3D12Resource* intermediateResources[_countof(textureFilePaths)] = {nullptr};
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		intermediateResources[textureIndex] = UploadTextureData(
			device.Get(), commandList.Get(), textureResources[textureIndex], mipImages[textureIndex]);
		if (intermediateResources[textureIndex] == nullptr) {
			Log(logStream, std::format(
				"Texture upload failed: {}",
				ConvertString(textureFilePaths[textureIndex])));
			RequestInitializationFailure();
			return;
		}
	}

	UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandlesCPU[_countof(textureFilePaths)];
	// CreateShaderResourceView に渡す CPU 側の SRV Handle。
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandlesGPU[_countof(textureFilePaths)];
	// Draw 時に Shader へ渡す GPU 側の SRV Handle。
	for (uint32_t textureIndex = 0; textureIndex < _countof(textureFilePaths); ++textureIndex) {
		// mipmap 付き画像を 2D Texture SRV として Shader から読む設定。
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = textureMetadatas[textureIndex].format;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = static_cast<UINT>(textureMetadatas[textureIndex].mipLevels);

		// 0 番を ImGui 用 SRV として空けるため、Texture は index + 1 に配置する。
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

	hr = commandList->Close(); // Texture Upload 用 CommandList を閉じて GPU に実行させる。
	EDITOR_HR_VERIFY(hr);

	ID3D12CommandList* uploadCommandLists[] = {commandList.Get()};
	commandQueue->ExecuteCommandLists(1, uploadCommandLists);

	fenceValue++;
	hr = commandQueue->Signal(fence.Get(), fenceValue);
	EDITOR_HR_VERIFY(hr);
	if (fence->GetCompletedValue() < fenceValue) {
		hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
		EDITOR_HR_VERIFY(hr);
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
	D3D12_CPU_DESCRIPTOR_HANDLE colorGradingLutSrvHandleCPU = GetCPUDescriptorHandle(
		srvDescriptorHeap, srvDescriptorSize, kRuntimeColorGradingLutSrvDescriptorIndex);
	D3D12_GPU_DESCRIPTOR_HANDLE colorGradingLutSrvHandleGPU = GetGPUDescriptorHandle(
		srvDescriptorHeap, srvDescriptorSize, kRuntimeColorGradingLutSrvDescriptorIndex);

	ID3D12Resource* iblIrradianceCube = nullptr;
	ID3D12Resource* iblPrefilterCube = nullptr;
	ID3D12Resource* iblEnvironmentCube = nullptr;
	ID3D12Resource* iblBrdfLut = nullptr;
	ID3D12Resource* colorGradingLut = nullptr;
	uint32_t iblPrefilterMipCount = 0;
	std::vector<ID3D12Resource*> iblUploadResources;

	std::wstring iblDir = L"Assets/Textures/IBL/Studio/";
	std::wstring iblFiles[] = {
		iblDir + L"irradiance_cube.dds",
		iblDir + L"prefilter_cube.dds",
		iblDir + L"environment_cube.dds",
		iblDir + L"brdf_lut.dds",
	};

	// DDS が存在すれば読み込む
	auto loadIblCube = [&](const std::wstring& path, ID3D12Resource*& outRes, D3D12_CPU_DESCRIPTOR_HANDLE srvCPU,
	                       uint32_t* mipCount) {
		if (!std::filesystem::exists(ResolveEngineOrProjectFilePath(path))) {
			return;
		}
		DirectX::ScratchImage img = LoadTexture(path);
		const auto& meta = img.GetMetadata();
		outRes = CreateTextureResource(device.Get(), meta);
		UploadTextureData(device.Get(), commandList.Get(), outRes, img);
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = meta.format;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		bool isCube = (meta.arraySize >= 6);
		srv.ViewDimension = isCube ? D3D12_SRV_DIMENSION_TEXTURECUBE : D3D12_SRV_DIMENSION_TEXTURE2D;
		if (isCube) {
			srv.TextureCube.MipLevels = static_cast<UINT>(meta.mipLevels);
		}
		else {
			srv.Texture2D.MipLevels = static_cast<UINT>(meta.mipLevels);
		}
		device->CreateShaderResourceView(outRes, &srv, srvCPU);
		if (mipCount) {
			*mipCount = static_cast<uint32_t>(meta.mipLevels);
		}
	};

	auto makePlaceholder = [&](uint32_t w, uint32_t h, uint32_t arraySize, DXGI_FORMAT fmt, bool isCube,
	                           ID3D12Resource*& outRes, D3D12_CPU_DESCRIPTOR_HANDLE srvCPU) {
		D3D12_RESOURCE_DESC d{};
		d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		d.Width = w;
		d.Height = h;
		d.MipLevels = 1;
		d.DepthOrArraySize = static_cast<UINT16>(arraySize);
		d.Format = fmt;
		d.SampleDesc.Count = 1;
		D3D12_HEAP_PROPERTIES hp{};
		hp.Type = D3D12_HEAP_TYPE_DEFAULT;
		HRESULT hr = device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST,
		                                             nullptr, IID_PPV_ARGS(&outRes));
		if (FAILED(hr) || !outRes) {
			outRes = nullptr;
			return;
		}
		uint32_t bytesPerPixel = (fmt == DXGI_FORMAT_R16G16_FLOAT) ? 4 : 8;
		uint32_t pitch = w * bytesPerPixel;
		uint32_t sliceSize = pitch * h;
		uint32_t totalSize = sliceSize * arraySize;
		std::vector<uint8_t> data(totalSize);
		// Fill with neutral gray: (0.5,0.5,0.5,1.0) for RGBA or (1.0,0.0) for RG
		if (fmt == DXGI_FORMAT_R16G16_FLOAT) {
			constexpr uint16_t half1 = 0x3C00; // 1.0 in half-float
			constexpr uint16_t half0 = 0x0000; // 0.0 in half-float
			uint8_t pix[4];
			memcpy(pix + 0, &half1, 2);
			memcpy(pix + 2, &half0, 2);
			for (size_t j = 0; j < totalSize; j += 4) {
				memcpy(&data[j], pix, 4);
			}
		}
		else {
			constexpr uint16_t half05 = 0x3800; // 0.5 in half-float
			constexpr uint16_t half1 = 0x3C00; // 1.0 in half-float
			uint8_t pix[8];
			memcpy(pix + 0, &half05, 2);
			memcpy(pix + 2, &half05, 2);
			memcpy(pix + 4, &half05, 2);
			memcpy(pix + 6, &half1, 2);
			for (size_t j = 0; j < totalSize; j += 8) {
				memcpy(&data[j], pix, 8);
			}
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
		D3D12_HEAP_PROPERTIES uploadHp{};
		uploadHp.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bufDesc{};
		bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufDesc.Width = uploadBufferSize;
		bufDesc.Height = 1;
		bufDesc.DepthOrArraySize = 1;
		bufDesc.MipLevels = 1;
		bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		bufDesc.SampleDesc.Count = 1;
		if (SUCCEEDED(
			device->CreateCommittedResource(&uploadHp, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ
				, nullptr, IID_PPV_ARGS(&uploadHeap))) && uploadHeap) {
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
		if (isCube) {
			srv.TextureCube.MipLevels = 1;
		}
		else {
			srv.Texture2D.MipLevels = 1;
		}
		device->CreateShaderResourceView(outRes, &srv, srvCPU);
	};

	//================================================================
	// 外部 IBL が無い時も灰色 Texture ではなく、空と太陽を持つ HDR Cube を生成する
	//================================================================

	const auto saturateValue = [](float value) {
		return (std::clamp)(value, 0.0f, 1.0f);
	};
	const auto normalizeDirection = [](const Vector3& value) {
		const float length = std::sqrt(
			value.x * value.x + value.y * value.y + value.z * value.z);
		const float inverseLength = 1.0f / (std::max)(length, 0.000001f);
		return Vector3{
			value.x * inverseLength,
			value.y * inverseLength,
			value.z * inverseLength};
	};
	const auto makeCubeDirection = [&normalizeDirection](
			uint32_t faceIndex,
			float coordinateX,
			float coordinateY) {
		Vector3 direction{};

		switch (faceIndex) {
		case 0u:
			direction = {1.0f, -coordinateY, -coordinateX};
			break;
		case 1u:
			direction = {-1.0f, -coordinateY, coordinateX};
			break;
		case 2u:
			direction = {coordinateX, 1.0f, coordinateY};
			break;
		case 3u:
			direction = {coordinateX, -1.0f, -coordinateY};
			break;
		case 4u:
			direction = {coordinateX, -coordinateY, 1.0f};
			break;
		default:
			direction = {-coordinateX, -coordinateY, -1.0f};
			break;
		}

		return normalizeDirection(direction);
	};
	const Vector3 proceduralSunDirection = normalizeDirection({0.35f, 0.72f, -0.60f});
	const auto evaluateProceduralSky = [
		&proceduralSunDirection,
		&saturateValue](const Vector3& direction, float roughness, bool isIrradiance) {
		const float upperHemisphere = saturateValue(direction.y * 0.5f + 0.5f);
		const float zenithBlend = std::pow(upperHemisphere, 0.42f);
		const float horizonBand = std::exp(-std::abs(direction.y) * 5.0f);
		const Vector3 groundColor{0.035f, 0.045f, 0.055f};
		const Vector3 horizonColor{0.30f, 0.48f, 0.72f};
		const Vector3 zenithColor{0.025f, 0.12f, 0.34f};
		Vector3 skyColor{
			groundColor.x + (zenithColor.x - groundColor.x) * zenithBlend,
			groundColor.y + (zenithColor.y - groundColor.y) * zenithBlend,
			groundColor.z + (zenithColor.z - groundColor.z) * zenithBlend};
		skyColor.x += horizonColor.x * horizonBand;
		skyColor.y += horizonColor.y * horizonBand;
		skyColor.z += horizonColor.z * horizonBand;

		const float sunDot = saturateValue(
			direction.x * proceduralSunDirection.x +
			direction.y * proceduralSunDirection.y +
			direction.z * proceduralSunDirection.z);
		const float sunExponent = 384.0f + roughness * -372.0f;
		const float sunDisk = std::pow(sunDot, (std::max)(sunExponent, 12.0f));
		const float sunEnergy = isIrradiance ? 0.15f : (12.0f * (1.0f - roughness) + 0.35f);
		skyColor.x += sunDisk * sunEnergy;
		skyColor.y += sunDisk * sunEnergy * 0.82f;
		skyColor.z += sunDisk * sunEnergy * 0.58f;

		const Vector3 averageSky{0.16f, 0.25f, 0.38f};
		const float blurWeight = isIrradiance
			? 0.72f
			: std::pow(saturateValue(roughness), 1.35f);
		skyColor.x += (averageSky.x - skyColor.x) * blurWeight;
		skyColor.y += (averageSky.y - skyColor.y) * blurWeight;
		skyColor.z += (averageSky.z - skyColor.z) * blurWeight;

		return skyColor;
	};

	auto createProceduralCube = [
		&](uint32_t baseSize,
			uint32_t mipCount,
			bool isIrradiance,
			ID3D12Resource*& outputResource,
			D3D12_CPU_DESCRIPTOR_HANDLE srvHandle) {
		D3D12_RESOURCE_DESC resourceDescription{};
		resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDescription.Width = baseSize;
		resourceDescription.Height = baseSize;
		resourceDescription.DepthOrArraySize = 6u;
		resourceDescription.MipLevels = static_cast<UINT16>(mipCount);
		resourceDescription.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		resourceDescription.SampleDesc.Count = 1u;

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		const HRESULT createResult = device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDescription,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&outputResource));

		if (FAILED(createResult) || outputResource == nullptr) {
			outputResource = nullptr;
			return false;
		}

		const uint32_t subresourceCount = mipCount * 6u;
		std::vector<std::vector<float>> pixelStorage(subresourceCount);
		std::vector<D3D12_SUBRESOURCE_DATA> subresources(subresourceCount);

		for (uint32_t faceIndex = 0u; faceIndex < 6u; faceIndex++) {
			for (uint32_t mipIndex = 0u; mipIndex < mipCount; mipIndex++) {
				const uint32_t mipSize = (std::max)(baseSize >> mipIndex, 1u);
				const uint32_t subresourceIndex = D3D12CalcSubresource(
					mipIndex,
					faceIndex,
					0u,
					mipCount,
					6u);
				std::vector<float>& pixels = pixelStorage[subresourceIndex];
				pixels.resize(static_cast<size_t>(mipSize) * static_cast<size_t>(mipSize) * 4u);
				const float roughness = mipCount > 1u
					? static_cast<float>(mipIndex) / static_cast<float>(mipCount - 1u)
					: 1.0f;

				for (uint32_t pixelY = 0u; pixelY < mipSize; pixelY++) {
					for (uint32_t pixelX = 0u; pixelX < mipSize; pixelX++) {
						const float coordinateX =
							(static_cast<float>(pixelX) + 0.5f) * 2.0f /
							static_cast<float>(mipSize) - 1.0f;
						const float coordinateY =
							(static_cast<float>(pixelY) + 0.5f) * 2.0f /
							static_cast<float>(mipSize) - 1.0f;
						const Vector3 direction = makeCubeDirection(
							faceIndex,
							coordinateX,
							coordinateY);
						const Vector3 color = evaluateProceduralSky(
							direction,
							roughness,
							isIrradiance);
						const size_t pixelOffset =
							(static_cast<size_t>(pixelY) * static_cast<size_t>(mipSize) + pixelX) * 4u;
						pixels[pixelOffset + 0u] = color.x;
						pixels[pixelOffset + 1u] = color.y;
						pixels[pixelOffset + 2u] = color.z;
						pixels[pixelOffset + 3u] = 1.0f;
					}
				}

				D3D12_SUBRESOURCE_DATA& subresource = subresources[subresourceIndex];
				subresource.pData = pixels.data();
				subresource.RowPitch = static_cast<LONG_PTR>(mipSize) * 4 * sizeof(float);
				subresource.SlicePitch = subresource.RowPitch * static_cast<LONG_PTR>(mipSize);
			}
		}

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(
			outputResource,
			0u,
			subresourceCount);
		ID3D12Resource* uploadResource = nullptr;
		D3D12_HEAP_PROPERTIES uploadHeapProperties{};
		uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC uploadDescription{};
		uploadDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadDescription.Width = uploadBufferSize;
		uploadDescription.Height = 1u;
		uploadDescription.DepthOrArraySize = 1u;
		uploadDescription.MipLevels = 1u;
		uploadDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		uploadDescription.SampleDesc.Count = 1u;
		const HRESULT uploadResult = device->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&uploadDescription,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&uploadResource));

		if (FAILED(uploadResult) || uploadResource == nullptr) {
			outputResource->Release();
			outputResource = nullptr;
			return false;
		}

		UpdateSubresources(
			commandList.Get(),
			outputResource,
			uploadResource,
			0u,
			0u,
			subresourceCount,
			subresources.data());
		iblUploadResources.push_back(uploadResource);

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = outputResource;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1u, &barrier);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
		srvDescription.Format = resourceDescription.Format;
		srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDescription.TextureCube.MipLevels = mipCount;
		device->CreateShaderResourceView(outputResource, &srvDescription, srvHandle);
		return true;
	};

	auto createProceduralBrdfLut = [&](ID3D12Resource*& outputResource, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle) {
		constexpr uint32_t kLutSize = 256u;
		std::vector<float> lutPixels(static_cast<size_t>(kLutSize) * kLutSize * 2u);

		for (uint32_t pixelY = 0u; pixelY < kLutSize; pixelY++) {
			const float roughness = (static_cast<float>(pixelY) + 0.5f) / static_cast<float>(kLutSize);

			for (uint32_t pixelX = 0u; pixelX < kLutSize; pixelX++) {
				const float normalDotView =
					(static_cast<float>(pixelX) + 0.5f) / static_cast<float>(kLutSize);
				const float responseX = roughness * -1.0f + 1.0f;
				const float responseY = roughness * -0.0275f + 0.0425f;
				const float responseZ = roughness * -0.572f + 1.04f;
				const float responseW = roughness * 0.022f - 0.04f;
				const float approximation =
					(std::min)(responseX * responseX, std::exp2(-9.28f * normalDotView)) *
					responseX + responseY;
				const size_t pixelOffset =
					(static_cast<size_t>(pixelY) * kLutSize + pixelX) * 2u;
				lutPixels[pixelOffset + 0u] = -1.04f * approximation + responseZ;
				lutPixels[pixelOffset + 1u] = 1.04f * approximation + responseW;
			}
		}

		D3D12_RESOURCE_DESC resourceDescription{};
		resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDescription.Width = kLutSize;
		resourceDescription.Height = kLutSize;
		resourceDescription.DepthOrArraySize = 1u;
		resourceDescription.MipLevels = 1u;
		resourceDescription.Format = DXGI_FORMAT_R32G32_FLOAT;
		resourceDescription.SampleDesc.Count = 1u;
		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		if (FAILED(device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDescription,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&outputResource))) || outputResource == nullptr) {
			outputResource = nullptr;
			return false;
		}

		D3D12_SUBRESOURCE_DATA subresource{};
		subresource.pData = lutPixels.data();
		subresource.RowPitch = static_cast<LONG_PTR>(kLutSize) * 2 * sizeof(float);
		subresource.SlicePitch = subresource.RowPitch * static_cast<LONG_PTR>(kLutSize);
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(outputResource, 0u, 1u);
		D3D12_HEAP_PROPERTIES uploadHeapProperties{};
		uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC uploadDescription{};
		uploadDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadDescription.Width = uploadBufferSize;
		uploadDescription.Height = 1u;
		uploadDescription.DepthOrArraySize = 1u;
		uploadDescription.MipLevels = 1u;
		uploadDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		uploadDescription.SampleDesc.Count = 1u;
		ID3D12Resource* uploadResource = nullptr;

		if (FAILED(device->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&uploadDescription,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&uploadResource))) || uploadResource == nullptr) {
			outputResource->Release();
			outputResource = nullptr;
			return false;
		}

		UpdateSubresources(commandList.Get(), outputResource, uploadResource, 0u, 0u, 1u, &subresource);
		iblUploadResources.push_back(uploadResource);
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = outputResource;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1u, &barrier);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
		srvDescription.Format = resourceDescription.Format;
		srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDescription.Texture2D.MipLevels = 1u;
		device->CreateShaderResourceView(outputResource, &srvDescription, srvHandle);
		return true;
	};

	auto createIdentityColorGradingLut = [&] (
		ID3D12Resource*& outputResource,
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle) {
		constexpr uint32_t kColorLutSize = 32u;
		constexpr uint32_t kColorLutWidth = kColorLutSize * kColorLutSize;
		constexpr uint32_t kColorLutHeight = kColorLutSize;
		constexpr uint32_t kColorChannelCount = 4u;
		std::vector<float> lutPixels(
			static_cast<size_t>(kColorLutWidth) *
			static_cast<size_t>(kColorLutHeight) *
			static_cast<size_t>(kColorChannelCount));

		for (uint32_t blueIndex = 0u; blueIndex < kColorLutSize; blueIndex++) {
			for (uint32_t greenIndex = 0u; greenIndex < kColorLutSize; greenIndex++) {
				for (uint32_t redIndex = 0u; redIndex < kColorLutSize; redIndex++) {
					const uint32_t textureX = blueIndex * kColorLutSize + redIndex;
					const size_t pixelOffset =
						(static_cast<size_t>(greenIndex) * static_cast<size_t>(kColorLutWidth) +
							static_cast<size_t>(textureX)) *
						static_cast<size_t>(kColorChannelCount);
					const float inverseLastIndex = 1.0f / static_cast<float>(kColorLutSize - 1u);
					lutPixels[pixelOffset + 0u] = static_cast<float>(redIndex) * inverseLastIndex;
					lutPixels[pixelOffset + 1u] = static_cast<float>(greenIndex) * inverseLastIndex;
					lutPixels[pixelOffset + 2u] = static_cast<float>(blueIndex) * inverseLastIndex;
					lutPixels[pixelOffset + 3u] = 1.0f;
				}
			}
		}

		D3D12_RESOURCE_DESC resourceDescription{};
		resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resourceDescription.Width = static_cast<UINT64>(kColorLutWidth);
		resourceDescription.Height = kColorLutHeight;
		resourceDescription.DepthOrArraySize = 1u;
		resourceDescription.MipLevels = 1u;
		resourceDescription.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		resourceDescription.SampleDesc.Count = 1u;

		D3D12_HEAP_PROPERTIES heapProperties{};
		heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

		if (FAILED(device->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&resourceDescription,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&outputResource))) || outputResource == nullptr) {
			outputResource = nullptr;
			return false;
		}

		D3D12_SUBRESOURCE_DATA subresource{};
		subresource.pData = lutPixels.data();
		subresource.RowPitch =
			static_cast<LONG_PTR>(kColorLutWidth) *
			static_cast<LONG_PTR>(kColorChannelCount) *
			static_cast<LONG_PTR>(sizeof(float));
		subresource.SlicePitch =
			subresource.RowPitch * static_cast<LONG_PTR>(kColorLutHeight);

		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(outputResource, 0u, 1u);
		D3D12_HEAP_PROPERTIES uploadHeapProperties{};
		uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC uploadDescription{};
		uploadDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadDescription.Width = uploadBufferSize;
		uploadDescription.Height = 1u;
		uploadDescription.DepthOrArraySize = 1u;
		uploadDescription.MipLevels = 1u;
		uploadDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		uploadDescription.SampleDesc.Count = 1u;
		ID3D12Resource* uploadResource = nullptr;

		if (FAILED(device->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&uploadDescription,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&uploadResource))) || uploadResource == nullptr) {
			outputResource->Release();
			outputResource = nullptr;
			return false;
		}

		UpdateSubresources(commandList.Get(), outputResource, uploadResource, 0u, 0u, 1u, &subresource);
		iblUploadResources.push_back(uploadResource);

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = outputResource;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		commandList->ResourceBarrier(1u, &barrier);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
		srvDescription.Format = resourceDescription.Format;
		srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDescription.Texture2D.MipLevels = 1u;
		device->CreateShaderResourceView(outputResource, &srvDescription, srvHandle);
		return true;
	};

	loadIblCube(iblFiles[0], iblIrradianceCube, iblIrradianceSrvHandleCPU, nullptr);
	if (iblIrradianceCube == nullptr) {
		createProceduralCube(32u, 1u, true, iblIrradianceCube, iblIrradianceSrvHandleCPU);
	}

	loadIblCube(iblFiles[1], iblPrefilterCube, iblPrefilterSrvHandleCPU, &iblPrefilterMipCount);
	if (iblPrefilterCube == nullptr) {
		iblPrefilterMipCount = 8u;
		createProceduralCube(
			128u,
			iblPrefilterMipCount,
			false,
			iblPrefilterCube,
			iblPrefilterSrvHandleCPU);
	}

	loadIblCube(iblFiles[2], iblEnvironmentCube, iblEnvironmentSrvHandleCPU, nullptr);
	g_iblEnvironmentCubeLoaded = iblEnvironmentCube != nullptr;
	if (iblEnvironmentCube == nullptr) {
		createProceduralCube(128u, 1u, false, iblEnvironmentCube, iblEnvironmentSrvHandleCPU);
	}
	g_iblEnvironmentCubeLoaded = iblEnvironmentCube != nullptr;

	loadIblCube(iblFiles[3], iblBrdfLut, iblBrdfLutSrvHandleCPU, nullptr);
	if (iblBrdfLut == nullptr) {
		createProceduralBrdfLut(iblBrdfLut, iblBrdfLutSrvHandleCPU);
	}

	if (!createIdentityColorGradingLut(colorGradingLut, colorGradingLutSrvHandleCPU)) {
		Log(logStream, "Identity Color Grading LUT creation failed.");
		RequestInitializationFailure();
		return;
	}

	// IBL Upload を実行・同期（DDS があってもプレースホルダーでも Upload が発生している）
	{
		hr = commandList->Close();
		if (FAILED(hr)) {
			Log(logStream, std::format("IBL CommandList Close failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
			RequestInitializationFailure();
			return;
		}

		ID3D12CommandList* uploadLists[] = {commandList.Get()};
		commandQueue->ExecuteCommandLists(1, uploadLists);
		fenceValue++;
		hr = commandQueue->Signal(fence.Get(), fenceValue);
		if (FAILED(hr)) {
			Log(logStream, std::format("IBL CommandQueue Signal failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
			RequestInitializationFailure();
			return;
		}

		if (fence->GetCompletedValue() < fenceValue) {
			hr = fence->SetEventOnCompletion(fenceValue, fenceEvent);
			if (FAILED(hr)) {
				Log(logStream, std::format("IBL Fence SetEventOnCompletion failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
				RequestInitializationFailure();
				return;
			}

			WaitForSingleObject(fenceEvent, INFINITE);
		}

		for (ID3D12Resource* uploadResource : iblUploadResources) {
			if (uploadResource != nullptr) {
				uploadResource->Release();
			}
		}
		iblUploadResources.clear();
		// The renderer owns the next Reset. Keep the initialization command list closed here.
	}

#ifdef USE_IMGUI

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO(); // Docking の有効化や Font 設定を行う ImGui の入出力設定。
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // タブをメイン Window 外へ出した時、個別の OS Window として表示する。
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	// Game ViewのUI(Button/Toggle/Slider)をGamepadの十字キー・スティックで選択できるようにする。
	// Editor側のWindow操作も同じ経路で動くが、入力はImGuiのNav処理内で完結する。
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.ConfigDockingWithShift = false; // Shift なしで Docking できる Unity 風の操作にする。
	io.ConfigViewportsNoTaskBarIcon = true; // 分離した Editor タブをタスクバーへ個別に並べない。
	io.ConfigWindowsMoveFromTitleBarOnly = true;
	const float editorUiScale = GetEditorUiScale(windowHandle);
	ApplyEditorVisualTheme(editorUiScale);
	ImGui_ImplWin32_Init(windowHandle);

	// 動的 Font Atlas が文字サイズごとの Glyph Texture を更新できるよう、複数 SRV 対応 API を使う。
	g_imguiSrvDescriptorAllocator.descriptorHeap = srvDescriptorHeap;
	g_imguiSrvDescriptorAllocator.descriptorSize = srvDescriptorSize;
	g_imguiSrvDescriptorAllocator.isDescriptorUsed.fill(false);

	ImGui_ImplDX12_InitInfo imguiDx12InitInfo{};
	imguiDx12InitInfo.Device = device.Get();
	imguiDx12InitInfo.CommandQueue = commandQueue.Get();
	imguiDx12InitInfo.NumFramesInFlight = static_cast<int>(swapChainDesc.BufferCount);
	imguiDx12InitInfo.RTVFormat = rtvDesc.Format;
	imguiDx12InitInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	imguiDx12InitInfo.UserData = &g_imguiSrvDescriptorAllocator;
	imguiDx12InitInfo.SrvDescriptorHeap = srvDescriptorHeap;
	imguiDx12InitInfo.SrvDescriptorAllocFn = AllocateImGuiSrvDescriptor;
	imguiDx12InitInfo.SrvDescriptorFreeFn = ReleaseImGuiSrvDescriptor;

	if (!ImGui_ImplDX12_Init(&imguiDx12InitInfo)) {
		RequestInitializationFailure();
		return;
	}

	// SSGI は専用の半解像度RTへ書くので上書き。加算は最後のUpsampleで行う。
	ComPtr<ID3D12PipelineState> ssgiPipelineState = CreatePostProcessPSO(
		"SSGI", ssgiPixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, false);
	if (ssgiPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> ssgiTemporalPipelineState = CreatePostProcessPSO(
		"SsgiTemporal", ssgiTemporalPixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, false);
	if (ssgiTemporalPipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> ssgiUpsamplePipelineState = CreatePostProcessPSO(
		"SsgiUpsample", ssgiUpsamplePixelShaderBlob.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, true);
	if (ssgiUpsamplePipelineState == nullptr) {
		RequestInitializationFailure();
		return;
	}

	//================================================================
	// Volumetric Light Shaft (Sun Beams): 専用の小さなRootSignature。
	// 既存のpostProcessRootSignatureは32bit定数が48値しかなく、Cascaded
	// Shadowを見るのに必要なデータ量に足りないため、共有シグネチャを
	// 太らせて他パスへ影響させるより、独立させたほうが安全。
	//================================================================
	D3D12_DESCRIPTOR_RANGE volumetricDepthRange[1] = {};
	volumetricDepthRange[0].BaseShaderRegister = 0u;
	volumetricDepthRange[0].NumDescriptors = 1u;
	volumetricDepthRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	volumetricDepthRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_DESCRIPTOR_RANGE volumetricShadowRange[1] = {};
	volumetricShadowRange[0].BaseShaderRegister = 1u;
	volumetricShadowRange[0].NumDescriptors = 1u;
	volumetricShadowRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	volumetricShadowRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER volumetricRootParameters[4] = {};
	volumetricRootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	volumetricRootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	volumetricRootParameters[0].DescriptorTable.pDescriptorRanges = volumetricDepthRange;
	volumetricRootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(volumetricDepthRange);

	volumetricRootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	volumetricRootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	volumetricRootParameters[1].DescriptorTable.pDescriptorRanges = volumetricShadowRange;
	volumetricRootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(volumetricShadowRange);

	// b0: Sunの情報一式(色・強さ・Cascaded ShadowVP・Atlas UV・volumetric設定)。
	// 既存のdirectionalLightResourceをそのまま束縛するだけで、追加の
	// アップロード処理を持たない。
	volumetricRootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	volumetricRootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	volumetricRootParameters[2].Descriptor.ShaderRegister = 0u;
	volumetricRootParameters[2].Descriptor.RegisterSpace = 0u;

	// b1: 画面 <-> ワールド復元用の行列とビューポート情報。
	volumetricRootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	volumetricRootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	volumetricRootParameters[3].Constants.ShaderRegister = 1u;
	volumetricRootParameters[3].Constants.Num32BitValues = 24u;

	D3D12_STATIC_SAMPLER_DESC volumetricSamplers[2] = {};
	volumetricSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	volumetricSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	volumetricSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	volumetricSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	volumetricSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	volumetricSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	volumetricSamplers[0].ShaderRegister = 0u;
	volumetricSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	volumetricSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	volumetricSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	volumetricSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	volumetricSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	volumetricSamplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	volumetricSamplers[1].MaxLOD = D3D12_FLOAT32_MAX;
	volumetricSamplers[1].ShaderRegister = 1u;
	volumetricSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC volumetricRootSignatureDesc{};
	volumetricRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	volumetricRootSignatureDesc.pParameters = volumetricRootParameters;
	volumetricRootSignatureDesc.NumParameters = _countof(volumetricRootParameters);
	volumetricRootSignatureDesc.pStaticSamplers = volumetricSamplers;
	volumetricRootSignatureDesc.NumStaticSamplers = _countof(volumetricSamplers);

	ComPtr<ID3DBlob> volumetricSignatureBlob;
	ComPtr<ID3DBlob> volumetricErrorBlob;
	hr = D3D12SerializeRootSignature(
		&volumetricRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		volumetricSignatureBlob.GetAddressOf(), volumetricErrorBlob.GetAddressOf());
	if (FAILED(hr) || volumetricSignatureBlob == nullptr) {
		Log(logStream, std::format(
			"VolumetricLightShaft RootSignature serialize failed. hr=0x{:08X}",
			static_cast<uint32_t>(hr)));

		if (volumetricErrorBlob != nullptr) {
			const auto* errorMessage = reinterpret_cast<const char*>(volumetricErrorBlob->GetBufferPointer());
			Log(logStream, std::string(errorMessage, volumetricErrorBlob->GetBufferSize()));
		}

		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12RootSignature> volumetricLightShaftRootSignature;
	hr = device->CreateRootSignature(
		0, volumetricSignatureBlob->GetBufferPointer(), volumetricSignatureBlob->GetBufferSize(),
		IID_PPV_ARGS(volumetricLightShaftRootSignature.GetAddressOf()));

	if (FAILED(hr) || volumetricLightShaftRootSignature == nullptr) {
		Log(logStream, std::format(
			"VolumetricLightShaft RootSignature Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
		RequestInitializationFailure();
		return;
	}

	ComPtr<ID3D12PipelineState> volumetricLightShaftPipelineState;

	if (volumetricLightShaftPixelShaderBlob != nullptr && fullscreenVertexShaderBlob != nullptr) {
		D3D12_GRAPHICS_PIPELINE_STATE_DESC volumetricDesc{};
		volumetricDesc.pRootSignature = volumetricLightShaftRootSignature.Get();
		volumetricDesc.VS = {
			fullscreenVertexShaderBlob->GetBufferPointer(), fullscreenVertexShaderBlob->GetBufferSize()};
		volumetricDesc.PS = {
			volumetricLightShaftPixelShaderBlob->GetBufferPointer(),
			volumetricLightShaftPixelShaderBlob->GetBufferSize()};
		D3D12_BLEND_DESC volumetricBlendDesc{};
		// God Ray はHDRシーンへ加算合成する光なので、常に加算ブレンド。
		volumetricBlendDesc.RenderTarget[0].BlendEnable = TRUE;
		volumetricBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
		volumetricBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		volumetricBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		volumetricBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
		volumetricBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
		volumetricBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		volumetricBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		volumetricDesc.BlendState = volumetricBlendDesc;
		D3D12_RASTERIZER_DESC volumetricRasterDesc{};
		volumetricRasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
		volumetricRasterDesc.CullMode = D3D12_CULL_MODE_NONE;
		volumetricRasterDesc.DepthClipEnable = TRUE;
		volumetricDesc.RasterizerState = volumetricRasterDesc;
		D3D12_DEPTH_STENCIL_DESC volumetricDsDesc{};
		volumetricDsDesc.DepthEnable = FALSE;
		volumetricDsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		volumetricDsDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		volumetricDsDesc.StencilEnable = FALSE;
		volumetricDesc.DepthStencilState = volumetricDsDesc;
		volumetricDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
		volumetricDesc.NumRenderTargets = 1;
		volumetricDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

		for (int i = 1; i < 8; ++i) {
			volumetricDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
		}

		volumetricDesc.SampleDesc.Count = 1;
		volumetricDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		volumetricDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		volumetricDesc.InputLayout.pInputElementDescs = nullptr;
		volumetricDesc.InputLayout.NumElements = 0;
		hr = device->CreateGraphicsPipelineState(
			&volumetricDesc, IID_PPV_ARGS(volumetricLightShaftPipelineState.GetAddressOf()));

		if (FAILED(hr) || volumetricLightShaftPipelineState == nullptr) {
			Log(logStream, std::format(
				"VolumetricLightShaft PSO Create failed. hr=0x{:08X}", static_cast<uint32_t>(hr)));
			volumetricLightShaftPipelineState = nullptr;
		}
	}
	else {
		Log(std::string("VolumetricLightShaft PSO skipped: shader blob is null"));
	}

	// 日本語UIはDPIに合わせたGlyphを構築し、拡大表示時の文字の粗さを防ぐ。
	const float editorFontSize = 16.0f * editorUiScale;
	ImFontConfig editorFontConfig{};
	editorFontConfig.OversampleH = 0;
	editorFontConfig.OversampleV = 0;
	editorFontConfig.PixelSnapH = false;
	ImFont* editorFont = nullptr;
	const std::array<const char*, 3u> editorFontCandidates = {
		"C:/Windows/Fonts/YuGothM.ttc",
		"C:/Windows/Fonts/meiryo.ttc",
		"C:/Windows/Fonts/msgothic.ttc"};

	for (const char* editorFontPath : editorFontCandidates) {
		if (!std::filesystem::exists(editorFontPath)) {
			continue;
		}

		editorFont = io.Fonts->AddFontFromFileTTF(
			editorFontPath,
			editorFontSize,
			&editorFontConfig,
			io.Fonts->GetGlyphRangesJapanese());
		break;
	}

	if (editorFont == nullptr) {
		editorFont = io.Fonts->AddFontDefault();
	}

	io.FontDefault = editorFont;

	// Text/TextMeshProUGUIのtextFontIndexが選ぶFont候補。0番はeditorFontと同じ既定Font。
	// 見つからない候補はnullptrのままにし、描画側でeditorFontへFallbackする。
	EditorSharedState::g_uiFontVariants[0] = editorFont;
	const std::array<std::pair<int32_t, const char*>, 4u> uiFontVariantCandidates = {{
		{1, "C:/Windows/Fonts/meiryo.ttc"},
		{2, "C:/Windows/Fonts/msgothic.ttc"},
		{3, "C:/Windows/Fonts/msmincho.ttc"},
		{4, "C:/Windows/Fonts/YuGothB.ttc"}}};

	for (const auto& [variantIndex, variantPath] : uiFontVariantCandidates) {
		if (!std::filesystem::exists(variantPath)) {
			continue;
		}

		ImFontConfig variantFontConfig{};
		variantFontConfig.OversampleH = 0;
		variantFontConfig.OversampleV = 0;
		variantFontConfig.PixelSnapH = false;
		EditorSharedState::g_uiFontVariants[static_cast<size_t>(variantIndex)] = io.Fonts->AddFontFromFileTTF(
			variantPath,
			editorFontSize,
			&variantFontConfig,
			io.Fonts->GetGlyphRangesJapanese());
	}

	// 16px は Editor UI の基準値。Game View の Text は要求サイズで動的に再ラスタライズされる。
	io.Fonts->Build();
#endif

	g_instanceHandle = instanceHandle;
	g_logStream = std::move(logStream);
	// HRESULT 失敗行を実行ログ(logs/<日時>.Log)へも残す。g_logStream は
	// Finalize まで生きるグローバルなので、寿命の逆転は起きない。
	EditorSetHrFailureLogStream(&g_logStream);
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
	g_renderTimestampQueryHeap = renderTimestampQueryHeap;
	g_renderTimestampReadback = renderTimestampReadback;
	g_renderTimestampFrequency = renderTimestampFrequency;
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
	g_opaqueDepthCopyResource = opaqueDepthCopyResource;
	g_opaqueDepthCopySrvHandleCPU = opaqueDepthCopySrvHandleCPU;
	g_opaqueDepthCopySrvHandleGPU = opaqueDepthCopySrvHandleGPU;
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
	g_oitAccumulationRenderTarget = oitAccumulationRenderTarget;
	g_oitRevealageRenderTarget = oitRevealageRenderTarget;

	for (uint32_t oitTargetIndex = 0u; oitTargetIndex < 2u; ++oitTargetIndex) {
		g_oitRtvHandles[oitTargetIndex] = oitRtvHandles[oitTargetIndex];
		g_oitSrvHandlesCPU[oitTargetIndex] = oitSrvHandlesCPU[oitTargetIndex];
		g_oitSrvHandlesGPU[oitTargetIndex] = oitSrvHandlesGPU[oitTargetIndex];
	}

	g_dxcUtils = dxcUtils;
	g_dxcCompiler = dxcCompiler;
	g_includeHandler = includeHandler;
	g_vertexShaderBlob = vertexShaderBlob;
	g_pixelShaderBlob = pixelShaderBlob;
	g_objectReflectionMaskPixelShaderBlob = objectReflectionMaskPixelShaderBlob;
	g_shadowVertexShaderBlob = shadowVertexShaderBlob;
	g_alphaCutoutShadowPixelShaderBlob = alphaCutoutShadowPixelShaderBlob;
	g_fullscreenVertexShaderBlob = fullscreenVertexShaderBlob;
	g_toneMappingPixelShaderBlob = toneMappingPixelShaderBlob;
	g_bloomExtractPixelShaderBlob = bloomExtractPixelShaderBlob;
	g_bloomBlurPixelShaderBlob = bloomBlurPixelShaderBlob;
	g_fxaaPixelShaderBlob = fxaaPixelShaderBlob;
	g_ssaoPixelShaderBlob = ssaoPixelShaderBlob;
	g_ssaoBlurPixelShaderBlob = ssaoBlurPixelShaderBlob;
	g_ssgiPixelShaderBlob = ssgiPixelShaderBlob;
	g_ssgiTemporalPixelShaderBlob = ssgiTemporalPixelShaderBlob;
	g_ssgiUpsamplePixelShaderBlob = ssgiUpsamplePixelShaderBlob;
	g_volumetricLightShaftPixelShaderBlob = volumetricLightShaftPixelShaderBlob;
	g_probeCaptureVertexShaderBlob = probeCaptureVertexShaderBlob;
	g_probeCapturePixelShaderBlob = probeCapturePixelShaderBlob;
	g_probeShProjectionComputeShaderBlob = probeShProjectionComputeShaderBlob;
	g_probeVisibilityComputeShaderBlob = probeVisibilityComputeShaderBlob;
	g_skyboxPixelShaderBlob = skyboxPixelShaderBlob;
	g_planarReflectionPixelShaderBlob = planarReflectionPixelShaderBlob;
	g_sharpenPixelShaderBlob = sharpenPixelShaderBlob;
	g_finalCompositePixelShaderBlob = finalCompositePixelShaderBlob;
	g_passthroughPixelShaderBlob = passthroughPixelShaderBlob;
	g_depthOfFieldPixelShaderBlob = depthOfFieldPixelShaderBlob;
	g_motionBlurPixelShaderBlob = motionBlurPixelShaderBlob;
	g_weightedOitPixelShaderBlob = weightedOitPixelShaderBlob;
	g_weightedOitCompositePixelShaderBlob = weightedOitCompositePixelShaderBlob;
	g_refractiveSurfacePixelShaderBlob = refractiveSurfacePixelShaderBlob;
	g_underwaterCausticsPixelShaderBlob = underwaterCausticsPixelShaderBlob;
	g_skinnedMotionVectorVertexShaderBlob = skinnedMotionVectorVertexShaderBlob;
	g_signatureBlob = signatureBlob;
	g_errorBlob = errorBlob;
	g_rootSignature = rootSignature;
	g_graphicsPipelineState = graphicsPipelineState;
	g_planarScenePipelineState = planarScenePipelineState;
	g_planarSurfacePipelineState = planarSurfacePipelineState;
	g_objectReflectionMaskPipelineState = objectReflectionMaskPipelineState;
	g_cullFrontPipelineState = cullFrontPipelineState;
	g_cullNonePipelineState = cullNonePipelineState;
	g_transparentPipelineState = transparentPipelineState;
	g_transparentCullNonePipelineState = transparentCullNonePipelineState;
	g_weightedOitPipelineState = weightedOitPipelineState;
	g_weightedOitCullNonePipelineState = weightedOitCullNonePipelineState;
	g_waterSurfacePipelineState = waterSurfacePipelineState;
	g_waterTessellationPipelineState = waterTessellationPipelineState;
	g_refractiveSurfacePipelineState = refractiveSurfacePipelineState;
	g_refractiveSurfaceCullNonePipelineState = refractiveSurfaceCullNonePipelineState;
	g_shadowPipelineState = shadowPipelineState;
	g_shadowCullNonePipelineState = shadowCullNonePipelineState;
	g_alphaCutoutShadowPipelineState = alphaCutoutShadowPipelineState;
	g_alphaCutoutShadowCullNonePipelineState = alphaCutoutShadowCullNonePipelineState;
	g_batchedGraphicsPipelineState = batchedGraphicsPipelineState;
	g_batchedCullFrontPipelineState = batchedCullFrontPipelineState;
	g_batchedCullNonePipelineState = batchedCullNonePipelineState;
	g_batchedShadowPipelineState = batchedShadowPipelineState;
	g_batchedShadowCullNonePipelineState = batchedShadowCullNonePipelineState;
	g_postProcessRootSignature = postProcessRootSignature;
	g_toneMappingPipelineState = toneMappingPipelineState;
	g_bloomExtractPipelineState = bloomExtractPipelineState;
	g_bloomBlurPipelineState = bloomBlurPipelineState;
	g_fxaaPipelineState = fxaaPipelineState;
	g_ssaoPipelineState = ssaoPipelineState;
	g_ssaoBlurPipelineState = ssaoBlurPipelineState;
	g_ssgiPipelineState = ssgiPipelineState;
	g_ssgiTemporalPipelineState = ssgiTemporalPipelineState;
	g_ssgiUpsamplePipelineState = ssgiUpsamplePipelineState;
	g_volumetricLightShaftRootSignature = volumetricLightShaftRootSignature;
	g_volumetricLightShaftPipelineState = volumetricLightShaftPipelineState;
	g_skyboxPipelineState = skyboxPipelineState;
	g_planarReflectionPipelineState = planarReflectionPipelineState;
	g_sharpenPipelineState = sharpenPipelineState;
	g_finalCompositePipelineState = finalCompositePipelineState;
	g_passthroughPipelineState = passthroughPipelineState;
	g_depthOfFieldPipelineState = depthOfFieldPipelineState;
	g_motionBlurPipelineState = motionBlurPipelineState;
	g_weightedOitCompositePipelineState = weightedOitCompositePipelineState;
	g_underwaterCausticsPipelineState = underwaterCausticsPipelineState;
	g_iblIrradianceCube = iblIrradianceCube;
	g_iblPrefilterCube = iblPrefilterCube;
	g_iblEnvironmentCube = iblEnvironmentCube;
	g_iblBRDFLUT = iblBrdfLut;
	g_colorGradingLut = colorGradingLut;
	g_iblIrradianceSrvHandleCPU = iblIrradianceSrvHandleCPU;
	g_iblIrradianceSrvHandleGPU = iblIrradianceSrvHandleGPU;
	g_iblPrefilterSrvHandleCPU = iblPrefilterSrvHandleCPU;
	g_iblPrefilterSrvHandleGPU = iblPrefilterSrvHandleGPU;
	g_iblEnvironmentSrvHandleCPU = iblEnvironmentSrvHandleCPU;
	g_iblEnvironmentSrvHandleGPU = iblEnvironmentSrvHandleGPU;
	g_iblBrdfLutSrvHandleCPU = iblBrdfLutSrvHandleCPU;
	g_iblBrdfLutSrvHandleGPU = iblBrdfLutSrvHandleGPU;
	g_colorGradingLutSrvHandleCPU = colorGradingLutSrvHandleCPU;
	g_colorGradingLutSrvHandleGPU = colorGradingLutSrvHandleGPU;
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
	g_identitySkinMatrixResource = identitySkinMatrixResource;
	g_identitySkinMatrixData = identitySkinMatrixData;
	g_batchInstanceResource = batchInstanceResource;
	g_batchInstanceData = batchInstanceData;
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
	auto& opaqueDepthCopyResource = g_opaqueDepthCopyResource;
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
	auto& identitySkinMatrixResource = g_identitySkinMatrixResource;
	auto& identitySkinMatrixData = g_identitySkinMatrixData;
	auto& batchInstanceResource = g_batchInstanceResource;
	auto& batchInstanceData = g_batchInstanceData;
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
	if (identitySkinMatrixResource != nullptr) {
		identitySkinMatrixResource->Release();
		identitySkinMatrixResource = nullptr;
		identitySkinMatrixData = nullptr;
	}
	if (batchInstanceResource != nullptr) {
		batchInstanceResource->Release();
		batchInstanceResource = nullptr;
		batchInstanceData = nullptr;
	}
	spriteIndexResource->Release();
	spriteVertexResource->Release();
	for (ID3D12Resource* primitiveVertexResource : editorPrimitiveVertexResources) {
		if (primitiveVertexResource != nullptr) {
			primitiveVertexResource->Release();
		}
	}
	modelVertexResource->Release();
	vertexResource->Release();
	g_editorRuntimeManager.GetEffekseerManager().FinalizeGraphics();
	g_gpuCullingManager.Finalize();
	g_oceanFftManager.Finalize();
	g_gpuParticleManager.Finalize();
	g_vfxRenderer.Finalize();
	g_postProcessQualityManager.Finalize();
	g_temporalRenderingManager.Finalize();
	g_depthHierarchyManager.Finalize();
	g_gBufferManager.Finalize();
	g_lightProbeManager.Finalize();
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

	if (g_oitAccumulationRenderTarget != nullptr) {
		g_oitAccumulationRenderTarget->Release();
		g_oitAccumulationRenderTarget = nullptr;
	}

	if (g_oitRevealageRenderTarget != nullptr) {
		g_oitRevealageRenderTarget->Release();
		g_oitRevealageRenderTarget = nullptr;
	}
	if (g_environmentTextureUploadResource != nullptr) {
		g_environmentTextureUploadResource->Release();
		g_environmentTextureUploadResource = nullptr;
	}
	if (g_environmentTextureResource != nullptr) {
		g_environmentTextureResource->Release();
		g_environmentTextureResource = nullptr;
	}
	if (g_iblIrradianceCube != nullptr) {
		g_iblIrradianceCube->Release();
		g_iblIrradianceCube = nullptr;
	}
	if (g_iblPrefilterCube != nullptr) {
		g_iblPrefilterCube->Release();
		g_iblPrefilterCube = nullptr;
	}
	if (g_iblEnvironmentCube != nullptr) {
		g_iblEnvironmentCube->Release();
		g_iblEnvironmentCube = nullptr;
	}
	if (g_iblBRDFLUT != nullptr) {
		g_iblBRDFLUT->Release();
		g_iblBRDFLUT = nullptr;
	}
	if (g_colorGradingLut != nullptr) {
		g_colorGradingLut->Release();
		g_colorGradingLut = nullptr;
	}
	if (g_customColorGradingLutUploadResource != nullptr) {
		g_customColorGradingLutUploadResource->Release();
		g_customColorGradingLutUploadResource = nullptr;
	}
	if (g_customColorGradingLutResource != nullptr) {
		g_customColorGradingLutResource->Release();
		g_customColorGradingLutResource = nullptr;
	}
	g_loadedColorGradingLutAssetPath.clear();
	if (hdrRenderTarget != nullptr) {
		hdrRenderTarget->Release();
		hdrRenderTarget = nullptr;
	}
	if (opaqueDepthCopyResource != nullptr) {
		opaqueDepthCopyResource->Release();
		opaqueDepthCopyResource = nullptr;
	}
	// 初期化が途中で失敗した場合や、Resize 中に back buffer を取得できなかった
	// 場合はこれらが nullptr のまま Finalize に来る。無条件 Release は終了時の
	// null 参照になるため、他のリソースと同じように存在確認してから解放する。
	if (depthStencilResource != nullptr) {
		depthStencilResource->Release();
		depthStencilResource = nullptr;
	}
	for (ID3D12Resource*& swapChainResource : swapChainResources) {
		if (swapChainResource != nullptr) {
			swapChainResource->Release();
			swapChainResource = nullptr;
		}
	}

	// g_logStream をこの先で閉じるため、書き出し先の登録を先に外す。
	EditorSetHrFailureLogStream(nullptr);

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
