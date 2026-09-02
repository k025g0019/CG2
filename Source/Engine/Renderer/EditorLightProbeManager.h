#pragma once

#pragma warning(push, 0)
#include <Windows.h>
#include <array>
#include <cstdint>
#include <d3d12.h>
#include <dxcapi.h>
#include <vector>
#include <wrl.h>
#pragma warning(pop)

#include "../Core/EditorCommonTypes.h"

//================================================================
// Light Probe GI (DDGI 準拠) を管理するクラス
//----------------------------------------------------------------
// ・Probeを直方体グリッドへ等間隔に配置する
// ・各Probe位置からシーンを6面キューブへ描き、SH9(球面調和)へ投影する
// ・同時に八面体の距離モーメントを作り、実行時のChebyshev判定で
//   壁越しの光漏れを防ぐ
// ・1フレームに焼くProbeは kBakeBatchSize 個までに分散し、
//   前回値とヒステリシス補間して徐々に収束させる
//================================================================

class EditorLightProbeManager {
public:
	static constexpr uint32_t kShCoefficientCount = 9u;  // SH L2
	static constexpr uint32_t kVisibilityTileSize = 16u;  // 1Probeの八面体マップ1辺
	static constexpr uint32_t kCaptureFaceSize = 32u;  // キャプチャ1面の解像度
	static constexpr uint32_t kBakeBatchSize = 8u;  // 1フレームで焼くProbe数
	static constexpr uint32_t kMaxProbeCount = 4096u;
	static constexpr uint32_t kCubeFaceCount = 6u;

	// Scene の LightProbeGroup から毎フレーム受け取る設定。
	struct GridSettings {
		bool isEnabled = false;
		Vector3 origin{0.0f, 0.0f, 0.0f};  // グリッドの最小コーナー(Probe中心)
		Vector3 spacing{2.0f, 2.0f, 2.0f};  // Probe間隔(m)
		int32_t countX = 0;
		int32_t countY = 0;
		int32_t countZ = 0;
		float intensity = 1.0f;  // GIの強さ倍率
		float normalBias = 0.15f;  // 自己遮蔽回避のため法線方向へ押し出す量(m)
		float captureFarDistance = 60.0f;  // キャプチャの遠クリップ
		float hysteresis = 0.92f;  // 前回値との補間率。1に近いほど滑らかで遅い
		bool captureSky = true;  // 何にも当たらない方向を空として扱うか
	};

	bool Initialize(
		ID3D12Device* device,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		UINT srvDescriptorSize,
		ID3D12RootSignature* objectRootSignature,
		IDxcBlob* captureVertexShaderBlob,
		IDxcBlob* capturePixelShaderBlob,
		IDxcBlob* shProjectionComputeShaderBlob,
		IDxcBlob* visibilityComputeShaderBlob,
		const D3D12_INPUT_ELEMENT_DESC* inputElementDescs,
		UINT inputElementCount);

	void Finalize();

	bool IsReady() const;  // GIとして機能しているか(Bake/実行時参照の可否)
	bool HasResources() const;  // Descriptorが有効か(GI無効でもBindするため)

	// グリッド設定を反映する。形状が変わったらリソースを作り直す。
	void UpdateGrid(const GridSettings& settings);

	// 毎フレーム最初に呼ぶ。作り直した直後のProbeを0クリアし、
	// 読み取り状態へ遷移させる。D3D12はリソースの初期値を保証しないため、
	// これを省くと未Bakeのプローブがゴミ値を返す。
	void PrepareFrame(ID3D12GraphicsCommandList* commandList);

	// このフレームで焼く対象があるなら true。焼く範囲を返す。
	bool PrepareBakeBatch(uint32_t& outBaseProbeIndex, uint32_t& outProbeCount);

	// キャプチャ開始。RTを描画可能状態にする。
	void BeginCapture(ID3D12GraphicsCommandList* commandList);

	// 1面分の描画準備。ViewProjectionは戻り値で受け取り、
	// 呼び出し側が Root 定数 b3 へ書き込んでからジオメトリを描く。
	Matrix4x4 BeginCaptureFace(
		ID3D12GraphicsCommandList* commandList,
		uint32_t batchSlot,
		uint32_t faceIndex,
		uint32_t probeIndex) const;

	// キャプチャ用PSOを選ぶ(両面描画かどうかだけ切り替える)。
	void BindCapturePipelineState(
		ID3D12GraphicsCommandList* commandList,
		bool isDoubleSided) const;

	// キャプチャ終了。RTをSRVへ戻す。
	void EndCapture(ID3D12GraphicsCommandList* commandList);

	// SH投影と可視性モーメントのComputeを実行する。
	void DispatchBake(
		ID3D12GraphicsCommandList* commandList,
		ID3D12DescriptorHeap* srvDescriptorHeap,
		D3D12_GPU_VIRTUAL_ADDRESS directionalLightConstantBufferAddress,
		uint32_t baseProbeIndex,
		uint32_t probeCount);

	Vector3 GetProbeWorldPosition(uint32_t probeIndex) const;
	uint32_t GetProbeCount() const;

	// b2 の EmissiveLightArray へ書き込むグリッド情報を作る。
	void FillGridData(LightProbeGridData& gridDataOut) const;

	// Object3d の Root パラメータへ束縛する t20/t21 の先頭ハンドル。
	D3D12_GPU_DESCRIPTOR_HANDLE GetProbeResourceTableHandle() const;

private:
	bool CreatePipelineStates(
		IDxcBlob* captureVertexShaderBlob,
		IDxcBlob* capturePixelShaderBlob,
		IDxcBlob* shProjectionComputeShaderBlob,
		IDxcBlob* visibilityComputeShaderBlob,
		const D3D12_INPUT_ELEMENT_DESC* inputElementDescs,
		UINT inputElementCount);

	bool CreateComputeRootSignature();
	bool CreateCaptureResources();
	bool CreateProbeResources(uint32_t probeCount);
	void ReleaseProbeResources();
	void TransitionProbeResources(
		ID3D12GraphicsCommandList* commandList,
		D3D12_RESOURCE_STATES afterState);

	D3D12_CPU_DESCRIPTOR_HANDLE GetCpuSrvDescriptorHandle(uint32_t descriptorIndex) const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrvDescriptorHandle(uint32_t descriptorIndex) const;

	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	ID3D12DescriptorHeap* srvDescriptorHeap_ = nullptr;
	UINT srvDescriptorSize_ = 0u;
	UINT rtvDescriptorSize_ = 0u;
	UINT dsvDescriptorSize_ = 0u;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> objectRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> capturePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> captureDoubleSidedPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> shProjectionPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> visibilityPipelineState_;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> captureRtvHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> captureDsvHeap_;
	Microsoft::WRL::ComPtr<ID3D12Resource> captureRadianceResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> captureDistanceResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> captureDepthResource_;

	Microsoft::WRL::ComPtr<ID3D12Resource> probeShResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> probeVisibilityResource_;
	// ClearUnorderedAccessView は Shader 非可視ヒープの CPU ハンドルを要求するため、
	// クリア専用に同じ UAV をもう一組持つ。
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> clearUavDescriptorHeap_;
	D3D12_RESOURCE_STATES probeResourceState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	bool probeResourcesNeedClear_ = false;

	GridSettings settings_{};
	uint32_t probeCount_ = 0u;
	uint32_t visibilityTilesPerRow_ = 0u;
	uint32_t visibilityAtlasWidth_ = 0u;
	uint32_t visibilityAtlasHeight_ = 0u;
	uint32_t nextBakeProbeIndex_ = 0u;
	bool needsFullRebake_ = false;
	bool isInitialized_ = false;
	bool isCapturing_ = false;
};
