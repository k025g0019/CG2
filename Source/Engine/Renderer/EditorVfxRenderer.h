#pragma once

#include "Matrix.h"
#include "Vector.h"

#pragma warning(push, 0)
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#pragma warning(pop)

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

// Stage1 VFX System(Billboard / Flipbook / Ribbon / Ring)専用の最小GPU描画担当。
// EditorGpuParticleManager(Compute Shader Update)とは完全に独立し、
// CPUで計算した三角形をそのままUpload Vertex Bufferへ書き込んで描くだけの単純なRoot Signature/PSOを持つ。
class EditorVfxRenderer {
public:
	// Position(World空間, CPUで計算済み) / UV / RGBA。
	struct VfxVertex {
		Vector3 position{0.0f, 0.0f, 0.0f};
		float u = 0.0f;
		float v = 0.0f;
		float r = 1.0f;
		float g = 1.0f;
		float b = 1.0f;
		float a = 1.0f;
	};

	enum class BlendVariant : int32_t {
		AlphaBlend = 0,
		Additive = 1,
	};

	// 1回のDrawInstancedにまとめる範囲。Texture・BlendModeが同じ間だけまとめる。
	struct VfxBatch {
		std::string texturePath;
		BlendVariant blendVariant = BlendVariant::AlphaBlend;
		uint32_t firstVertex = 0u;
		uint32_t vertexCount = 0u;
		// Stage2 Soft Particle: EffectNodeDefinition::useSoftParticle / softParticleFadeDistanceをそのまま反映する。
		// 異なる値のNodeはFindOrAddBatchでBatchが分かれるため、Batch内では常に同一値。
		bool useSoftParticle = false;
		float softParticleFadeDistance = 1.0f;
	};

	// 既存のOpaque Depth Copy(gWaterSceneDepthと同じ資源、EditorSharedState::g_opaqueDepthCopyResource)を
	// このDraw呼び出しでSoft Particle Fadeに使ってよいかを呼び出し側(EditorRenderManager)が判定して渡す。
	// Planar Reflection パスなど、Depth CopyのCameraとこのDrawのCameraが一致しない場合はfalseにして無効化する。
	struct SoftParticleViewContext {
		bool isSceneDepthValid = false;  // falseの間はBatch側のuseSoftParticleを無視し、常にHard Particleとして描く。
		Matrix4x4 inverseViewProjection{};  // Depth CopyのDevice DepthをWorld座標へ戻すための逆ViewProjection。
		float viewportUvScaleX = 1.0f;   // Scene/Game Viewを同じRender Target内で共有するためのViewport UV変換(Water Passと同じ考え方)。
		float viewportUvScaleY = 1.0f;
		float viewportUvOffsetX = 0.0f;
		float viewportUvOffsetY = 0.0f;
	};

	static constexpr uint32_t kMaxDynamicVertexCount = 131072u;
	static constexpr int32_t kMaxTextureCount = 128;

	bool Initialize(
		ID3D12Device* device,
		IDxcBlob* vertexShader,
		IDxcBlob* pixelShader,
		DXGI_FORMAT renderTargetFormat,
		DXGI_FORMAT depthStencilFormat);
	void Finalize();

	// Effectで使うTextureを事前ロードしてキャッシュする。存在しない場合はFallback(白Texture)を使う。
	bool EnsureTexture(const std::string& texturePath);

	// 今フレーム分の頂点をUpload Bufferへ書き込む。上限を超えた分は切り捨てる。
	void BeginFrame();
	// 頂点を追加し、書き込んだ先頭Indexを返す。容量超過時はUINT32_MAXを返す。
	uint32_t AppendVertices(const VfxVertex* vertices, uint32_t vertexCount);
	void EndFrame();

	// Viewport / Scissor / RenderTargetは呼び出し側(EditorRenderManager)が既に設定済みの状態を前提とする。
	void Draw(
		ID3D12GraphicsCommandList* commandList,
		const Matrix4x4& viewProjection,
		const std::vector<VfxBatch>& batches,
		const SoftParticleViewContext& softParticleViewContext = SoftParticleViewContext{});

	uint32_t GetUploadedVertexCount() const { return uploadedVertexCount_; }

private:
	struct TextureEntry {
		Microsoft::WRL::ComPtr<ID3D12Resource> textureResource;
		Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
		int32_t descriptorIndex = -1;
		D3D12_GPU_DESCRIPTOR_HANDLE srvGpuHandle{};
	};

	bool CreateDescriptorHeap(ID3D12Device* device);
	bool CreateRootSignatureAndPipelines(
		ID3D12Device* device,
		IDxcBlob* vertexShader,
		IDxcBlob* pixelShader,
		DXGI_FORMAT renderTargetFormat,
		DXGI_FORMAT depthStencilFormat);
	bool CreateVertexBuffer(ID3D12Device* device);
	bool LoadFallbackTexture(ID3D12Device* device);
	D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle(const std::string& texturePath) const;

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> alphaBlendPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> additivePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;
	uint32_t srvDescriptorSize_ = 0u;
	int32_t nextTextureDescriptorIndex_ = 0;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexUploadBuffer_;
	VfxVertex* mappedVertices_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	uint32_t uploadedVertexCount_ = 0u;

	std::unordered_map<std::string, TextureEntry> textureCache_;
	D3D12_GPU_DESCRIPTOR_HANDLE fallbackTextureHandle_{};
	D3D12_CPU_DESCRIPTOR_HANDLE fallbackTextureHandleCPU_{};

	// Soft Particle用: srvDescriptorHeap_の末尾(kMaxTextureCount番目)に予約したOpaque Depth Copy SRVの参照先。
	// EditorSharedState::g_opaqueDepthCopyResourceの記述子は別Heapにあるため、Draw毎にこのSlotへCopyDescriptorsSimpleする。
	D3D12_CPU_DESCRIPTOR_HANDLE depthSrvHandleCPU_{};
	D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandleGPU_{};

	// Play中のTexture読み込みに使う一時Command List(EditorSceneObjectManagerと同じ即時Submitパターン)。
	bool UploadTextureImmediate(
		const std::string& texturePath,
		TextureEntry& entry);
};

#pragma warning(pop)
