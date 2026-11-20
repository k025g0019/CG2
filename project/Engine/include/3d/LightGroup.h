#pragma once
#pragma once


#include <d3d12.h>

#include <cstdint>

#include "3d/LightTypes.h"


//========================================
// ライトのまとまり
//========================================

//------------------------------
// Shader へ渡す形
//------------------------------

// 定数バッファ 1 つにシーンの光をすべて載せる。
// 光の数だけ定数バッファを分けると、Draw のたびに束縛し直すことになるため、
// 1 つにまとめて 1 回で渡す。
struct ConstBufferDataLightGroup {
	DirectionalLightData directional;

	// 使っている数だけ Shader が回る。配列の残りは読まれない。
	PointLightData pointLights[16];
	SpotLightData spotLights[8];

	int32_t pointLightCount = 0;
	int32_t spotLightCount = 0;
	float padding[2]{};

	AmbientLightData ambient;
};

// HLSL 側の LightGroupData と 1 byte でもずれると、別の光の値を読んで
// 静かに壊れる。足し引きしたらここの数値も直すこと。
static_assert(sizeof(ConstBufferDataLightGroup) == 1360);

//------------------------------
// ライト群
//------------------------------

// シーンの光をまとめて持ち、Model へ渡す。
//
//   LightGroup lights;
//   lights.Initialize();
//
//   lights.GetDirectionalLight().direction = {0.4f, -1.0f, 0.5f};
//
//   const int32_t lampIndex = lights.AddPointLight({.position = {0.0f, 3.0f, 0.0f}, .intensity = 8.0f});
//   lights.GetPointLight(lampIndex)->color = Color::kYellow;
//
//   lights.Update();                 // 値を変えたフレームで呼ぶ
//   model->SetLightGroup(&lights);
//
// SetLightGroup を呼ばなかった Model は、ModelCommon が持つ既定の光で描かれる。
// 既定は「斜め上からの白い平行光源 + 弱い環境光」で、点光源とスポットは無い。
//
// 光の数には上限がある。超えた分は無視され、警告を 1 回だけ出す。
// 上限を増やすと定数バッファが大きくなり、光が届かない面でも計算量が増える。
class LightGroup {
public:
	static constexpr int32_t kMaxPointLights = 16;
	static constexpr int32_t kMaxSpotLights = 8;

	LightGroup() = default;
	~LightGroup() = default;

	LightGroup(const LightGroup&) = delete;
	LightGroup& operator=(const LightGroup&) = delete;

	//------------------------------
	// 初期化処理
	//------------------------------

	void Initialize();

	//------------------------------
	// 更新処理
	//------------------------------

	// 今の内容を GPU へ送る。値を変えたフレームで呼ぶ。
	// 呼ばないと前フレームの光のまま描かれる。
	void Update();

	//------------------------------
	// 平行光源の設定処理
	//------------------------------

	DirectionalLightData& GetDirectionalLight() { return data_.directional; }
	const DirectionalLightData& GetDirectionalLight() const { return data_.directional; }
	void SetDirectionalLightEnabled(bool isEnabled);
	bool IsDirectionalLightEnabled() const;

	//------------------------------
	// 環境光の設定処理
	//------------------------------

	AmbientLightData& GetAmbientLight() { return data_.ambient; }
	const AmbientLightData& GetAmbientLight() const { return data_.ambient; }

	//------------------------------
	// 点光源の設定処理
	//------------------------------

	// 追加して添字を返す。上限を超えた場合は -1。
	int32_t AddPointLight(const PointLightData& light);

	// 範囲外なら nullptr。呼び出し側で添字を確かめなくて済む。
	PointLightData* GetPointLight(int32_t index);
	const PointLightData* GetPointLight(int32_t index) const;
	void SetPointLightEnabled(int32_t index, bool isEnabled);
	bool IsPointLightEnabled(int32_t index) const;

	// 添字で消す。後ろの光が前へ詰まるため、保持していた添字はずれる。
	void RemovePointLight(int32_t index);

	void ClearPointLights() { data_.pointLightCount = 0; }

	int32_t GetPointLightCount() const { return data_.pointLightCount; }

	//------------------------------
	// スポットライトの設定処理
	//------------------------------

	int32_t AddSpotLight(const SpotLightData& light);

	SpotLightData* GetSpotLight(int32_t index);
	const SpotLightData* GetSpotLight(int32_t index) const;
	void SetSpotLightEnabled(int32_t index, bool isEnabled);
	bool IsSpotLightEnabled(int32_t index) const;

	void RemoveSpotLight(int32_t index);

	void ClearSpotLights() { data_.spotLightCount = 0; }

	int32_t GetSpotLightCount() const { return data_.spotLightCount; }

	// 角度で円錐の広がりを決める。内側までは減衰せず、外側で 0 になる。
	// Shader は余弦で比べるため、ここで変換して持たせる。
	void SetSpotLightAngle(int32_t index, const SpotLightAngle& angle);

	//------------------------------
	// 取得処理
	//------------------------------

	D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress() const { return constantBuffer_.GetGpuAddress(); }

private:
	ConstBufferDataLightGroup data_{};
};
