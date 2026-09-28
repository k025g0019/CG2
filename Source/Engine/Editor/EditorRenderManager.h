#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorRenderManager {
public:
	//================================================================
	// SceneObject と ImGui を DirectX12 で描画する Manager
	//================================================================

	void Initialize();  // DirectX リソースは PlatformManager が生成するため、ここでは追加初期化しない。
	void Draw();  // WVP 更新、RenderTarget クリア、SceneObject 描画、ImGui 描画、Present を行う。
};

#pragma warning(pop)
