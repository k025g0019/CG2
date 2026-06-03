#pragma once

#pragma warning(push, 0)
#include <d3d12.h>
#include "externals/imgui-docking/imgui-docking/imgui.h"
#pragma warning(pop)

#include <cstddef>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorBottomPanel {
public:
	void Initialize();  // 下部パネルの初期化。現時点では保持状態なし
	void Update();  // 下部パネルの更新。現時点では自動更新なし
	// Project と Console タブを描画する
	void Draw(
		float editorWindowHeight,
		float editorBottomHeight,
		float bottomPanelWidth,
		ImGuiWindowFlags dockableWindowFlags,
		char* assetFilter,
		size_t assetFilterSize,
		std::string& selectedAssetPath,
		const std::vector<std::string>& textureFilePaths,
		const D3D12_GPU_DESCRIPTOR_HANDLE* textureSrvHandlesGPU,
		size_t textureCount,
		bool& isConsoleCleared,
		std::vector<std::string>& consoleMessages,
		float editorSceneWidth,
		float editorSceneHeight,
		bool isPlaying);
};

#pragma warning(pop)
