#pragma once

#include "Source/Engine/Effect/EditorEffectManager.h"
#include "Matrix.h"

#pragma warning(push, 0)
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#pragma warning(pop)

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class EditorGpuParticleManager {
public:
	static constexpr uint32_t kMaxParticleCount = 262144u;

	bool Initialize(
		ID3D12Device* device,
		IDxcBlob* clearComputeShader,
		IDxcBlob* updateComputeShader,
		IDxcBlob* spawnComputeShader,
		IDxcBlob* vertexShader,
		IDxcBlob* pixelShader,
		IDxcBlob* modelVertexShader,
		IDxcBlob* modelPixelShader,
		DXGI_FORMAT renderTargetFormat,
		DXGI_FORMAT depthStencilFormat);  // GPU Particle 用のBuffer / RootSignature / PSOを作る。
	void Finalize();  // D3D12リソースを解放する。
	void RequestReset();  // Play停止や再生開始時にGPU側Particleを空にする。
	void Update(
		ID3D12GraphicsCommandList* commandList,
		const std::vector<EditorEffectManager::GpuParticleSpawn>& spawns,
		float deltaTime);  // Spawn転送とCompute Shader更新を行う。
	void Draw(
		ID3D12GraphicsCommandList* commandList,
		const Matrix4x4& viewProjection);  // AliveListを使ってGPUインスタンシング描画する。

private:
	struct GpuFloat4 {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 0.0f;
	};

	struct GpuParticleData {
		GpuFloat4 positionLifetime;  // xyz: World位置 / w: 残り寿命。
		GpuFloat4 velocitySize;  // xyz: 速度 / w: 現在サイズ。
		GpuFloat4 startColor;  // 発生時色。
		GpuFloat4 endColor;  // 消滅時色。
		GpuFloat4 lifeSize;  // x: 経過秒 / y: 総寿命 / z: 開始サイズ / w: 終了サイズ。
		GpuFloat4 physics;  // x: 重力 / y: Drag / z: Noise強さ / w: Noise周波数。
		GpuFloat4 motion0;  // x: 運動方式 / y: 角速度 / z: 半径加速度 / w: 波振幅。
		GpuFloat4 motion1;  // xyz: 運動中心 / w: 波周波数。
		GpuFloat4 motion2;  // x: 吸引力 / y: 現在回転 / z: 回転速度。
		GpuFloat4 rendering;  // x: 描画モデルのグループ番号 / y: 放射強度。0 は板ポリゴン。
	};

	struct ParticleDrawConstants {
		Matrix4x4 viewProjection{};  // Particle のワールド位置を画面へ変換する行列。
		uint32_t renderGroup = 0u;  // 0 は板、1 以上は読み込んだ FBX / OBJ グループ。
		float padding[3]{0.0f, 0.0f, 0.0f};
	};

	struct ParticleModelMesh {
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;  // 読み込み後に再利用する頂点Buffer。
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
		uint32_t vertexCount = 0u;
		uint32_t renderGroup = 0u;
	};

	struct ParticleComputeConstants {
		float deltaTime = 0.0f;
		float globalDamping = 0.0f;
		uint32_t maxParticleCount = kMaxParticleCount;
		uint32_t commandValue = 0u;  // Clear時は全初期化フラグ、Spawn時は発生数。
	};

	bool CreateBuffers(ID3D12Device* device);  // Particle本体とAlive/DeadリストをGPUに作る。
	bool CreateComputePipeline(
		ID3D12Device* device,
		IDxcBlob* clearComputeShader,
		IDxcBlob* updateComputeShader,
		IDxcBlob* spawnComputeShader);
	bool CreateGraphicsPipeline(
		ID3D12Device* device,
		IDxcBlob* vertexShader,
		IDxcBlob* pixelShader,
		IDxcBlob* modelVertexShader,
		IDxcBlob* modelPixelShader,
		DXGI_FORMAT renderTargetFormat,
		DXGI_FORMAT depthStencilFormat);
	void UploadInitialClear(ID3D12GraphicsCommandList* commandList);  // 全ParticleをDeadリストへ登録する。
	void UploadSpawns(
		ID3D12GraphicsCommandList* commandList,
		const std::vector<EditorEffectManager::GpuParticleSpawn>& spawns);  // Deadリストから新規ParticleをGPU上で確保する。
	GpuParticleData ConvertSpawn(const EditorEffectManager::GpuParticleSpawn& spawn);
	uint32_t EnsureModelMesh(const std::string& assetPath);  // FBX / OBJ を初回だけ読み込み、描画グループ番号を返す。
	static void TransitionResource(
		ID3D12GraphicsCommandList* commandList,
		ID3D12Resource* resource,
		D3D12_RESOURCE_STATES beforeState,
		D3D12_RESOURCE_STATES afterState);

	Microsoft::WRL::ComPtr<ID3D12Resource> particleBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> particleUploadBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> aliveListBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> deadListBuffer_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> clearPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> spawnPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> graphicsRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> modelGraphicsPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	std::unordered_map<std::string, ParticleModelMesh> modelMeshes_;  // Asset パスごとの共有 GPU Mesh。
	uint32_t nextRenderGroup_ = 1u;
	D3D12_RESOURCE_STATES particleState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	D3D12_RESOURCE_STATES aliveListState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	bool needsInitialClear_ = true;
	bool hasEverSpawned_ = false;
};
