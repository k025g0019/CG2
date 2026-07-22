#pragma once

#include "EditorScene.h"
#include "Matrix.h"

#pragma warning(push, 0)
#include <d3d12.h>
#include <Effekseer.h>
#include <EffekseerRendererDX12.h>
#pragma warning(pop)

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// Effekseer Runtime
//================================================================

// Effekseer 1.70e の .efk / .efkefc を CG2 の DirectX12 描画へ接続する。
// 内蔵 GPU Particle とは別系統にし、公式 Runtime の Sprite / Ribbon / Ring / Track / Model を保持する。
class EditorEffekseerManager {
public:
	bool InitializeGraphics(
		ID3D12Device* device,
		ID3D12CommandQueue* commandQueue,
		uint32_t backBufferCount,
		DXGI_FORMAT renderTargetFormat,
		DXGI_FORMAT depthStencilFormat);  // Effekseer の DX12 Device、Renderer、Loader を作る。
	void InitializeScene(EditorScene* editorScene, std::vector<std::string>* consoleMessages);  // 再生対象 Scene と Console を接続する。
	void FinalizeGraphics();  // DX12 Device を破棄する前に Effekseer の参照を解放する。
	void Start();  // Play On Awake の .efk / .efkefc を再生する。
	void Update(float deltaTime);  // Effect 時間と GameObject Transform を更新する。
	void Stop();  // 全 Handle を停止し、Play 中の状態を破棄する。
	void Draw(
		ID3D12GraphicsCommandList* commandList,
		const Matrix4x4& viewMatrix,
		const Matrix4x4& projectionMatrix,
		const Vector3& cameraPosition);  // 現在の HDR RenderTarget へ公式 Renderer で描画する。

	bool PlayEffect(int32_t gameObjectId);  // GameObject に付いた .efk / .efkefc を再生する。
	void StopEffect(int32_t gameObjectId);  // 指定 GameObject の Handle だけ停止する。
	bool IsEffectPlaying(int32_t gameObjectId) const;  // 指定 GameObject の生存 Handle があれば true。
	int32_t GetAliveEffectCount(int32_t gameObjectId) const;  // Inspector 表示用の生存 Handle 数。

private:
	struct EffectInstance {
		uint64_t componentKey = 0u;  // GameObject ID と Component 種類を合わせた識別子。
		int32_t gameObjectId = -1;  // Transform 追従先。
		Effekseer::Handle handle = -1;  // Effekseer Manager が返した再生 Handle。
		std::string assetPath;  // 再生に使った .efk / .efkefc。
		bool isLooping = false;  // 終了後に同じ Effect を再生し直す場合は true。
	};

	EditorScene* editorScene_ = nullptr;  // GameObject Transform の取得元。
	std::vector<std::string>* consoleMessages_ = nullptr;  // 読み込み失敗の表示先。
	Effekseer::Backend::GraphicsDeviceRef graphicsDevice_;  // Effekseer が D3D12 Device を扱うラッパー。
	EffekseerRenderer::RendererRef renderer_;  // Sprite / Model を現在の RenderTarget へ描く Renderer。
	Effekseer::RefPtr<EffekseerRenderer::SingleFrameMemoryPool> memoryPool_;  // 1 フレーム内の定数Bufferを再利用するPool。
	Effekseer::RefPtr<EffekseerRenderer::CommandList> effekseerCommandList_;  // CG2 の CommandList を Effekseer へ渡すアダプタ。
	Effekseer::ManagerRef manager_;  // Effect の読み込み、再生、更新、停止を管理する本体。
	std::unordered_map<std::string, Effekseer::EffectRef> effectCache_;  // 同じ Effect Asset の二重読み込みを防ぐ。
	std::unordered_map<uint64_t, EffectInstance> instances_;  // Component ごとの再生状態。
	float elapsedTime_ = 0.0f;  // Renderer の時間依存 Material へ渡す秒数。
	bool isPlaying_ = false;  // Play 中だけ自動再生と更新を行う。
	bool hasPreparedDrawFrame_ = false;  // Scene / Game の両方を描いても一時GPUメモリはフレーム単位で更新する。

	static bool IsEffekseerAssetPath(const std::string& assetPath);  // .efk / .efkefc なら true。
	static uint64_t MakeComponentKey(int32_t gameObjectId, EditorComponentType componentType);  // 1 GameObject の2種類のEmitterを区別する。
	Effekseer::EffectRef LoadEffect(const std::string& assetPath);  // Cache を利用して公式 Effect を読み込む。
	bool PlayComponent(const EditorGameObject& gameObject, const EditorComponent& component);  // Component 1 個を Handle へ変換する。
	void SynchronizeInstances();  // Handle の位置・回転・拡縮を GameObject へ追従させる。
	void PushConsoleMessage(const std::string& message);  // Console 接続済みの場合だけ追加する。
};

#pragma warning(pop)
