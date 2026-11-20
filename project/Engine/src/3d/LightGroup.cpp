#include "3d/LightGroup.h"
#include "3d/LightGroup.h"


#include <cmath>

#include "base/DirectXCommon.h"
#include "math/MathUtil.h"
#include "resources/shaders/Lihting.hlsli"


namespace {
	// 上限超過の警告は 1 回だけ出す。毎フレーム追加している場合に、
	// ログが警告で埋まって他が読めなくなるのを防ぐ。
	bool g_hasReportedPointOverflow = false;
	bool g_hasReportedSpotOverflow = false;
} // namespace

//========================================
// 初期化処理
//========================================

void LightGroup::Initialize() {
	constantBuffer_.Initialize(Engine::GetDirectXCommon()->GetDevice());

	// 既定のスポットライトは余弦が 0 のまま。使う前に角度を入れてもらう。
	for (SpotLightData& spotLight : data_.spotLights) {
		spotLight.cosOuterAngle = std::cos(ToRadian(30.0f));
		spotLight.cosInnerAngle = std::cos(ToRadian(20.0f));
	}

	Update();
}

//========================================
// 更新処理
//========================================

void LightGroup::Update() {
	ConstBufferDataLightGroup* destination = constantBuffer_.Data();
	if (destination == nullptr) {
		return;
	}

	*destination = data_;

	// 向きは送る直前にここで揃える。Shader 側で正規化すると、
	// 光が当たるピクセルの数だけ平方根を計算することになる。
	destination->directional.direction = Normalize(data_.directional.direction);

	for (int32_t index = 0; index < data_.spotLightCount; ++index) {
		destination->spotLights[index].direction = Normalize(data_.spotLights[index].direction);
	}
}

//========================================
// 平行光源の有効状態設定処理
//========================================

void LightGroup::SetDirectionalLightEnabled(bool isEnabled) {
	data_.directional.isEnabled = isEnabled ? 1 : 0;
}

bool LightGroup::IsDirectionalLightEnabled() const {
	return data_.directional.isEnabled != 0;
}

//========================================
// 点光源の設定処理
//========================================

int32_t LightGroup::AddPointLight(const PointLightData& light) {
	if (data_.pointLightCount >= kMaxPointLights) {
		if (!g_hasReportedPointOverflow) {
			g_hasReportedPointOverflow = true;
		}
		return -1;
	}

	const int32_t index = data_.pointLightCount;
	data_.pointLights[index] = light;
	++data_.pointLightCount;
	return index;
}

PointLightData* LightGroup::GetPointLight(int32_t index) {
	if (index < 0 || index >= data_.pointLightCount) {
		return nullptr;
	}
	return &data_.pointLights[index];
}

const PointLightData* LightGroup::GetPointLight(int32_t index) const {
	if (index < 0 || index >= data_.pointLightCount) {
		return nullptr;
	}
	return &data_.pointLights[index];
}

void LightGroup::SetPointLightEnabled(int32_t index, bool isEnabled) {
	if (PointLightData* light = GetPointLight(index); light != nullptr) {
		light->isEnabled = isEnabled ? 1 : 0;
	}
}

bool LightGroup::IsPointLightEnabled(int32_t index) const {
	const PointLightData* light = GetPointLight(index);
	return light != nullptr && light->isEnabled != 0;
}

// 後ろの光を 1 つずつ前へ詰める。順番が変わらないので、
// 「何番目に追加したか」で見分けている場合でも並びが崩れない。
void LightGroup::RemovePointLight(int32_t index) {
	if (index < 0 || index >= data_.pointLightCount) {
		return;
	}

	for (int32_t moveIndex = index; moveIndex + 1 < data_.pointLightCount; ++moveIndex) {
		data_.pointLights[moveIndex] = data_.pointLights[moveIndex + 1];
	}

	--data_.pointLightCount;
}

//========================================
// スポットライトの設定処理
//========================================

int32_t LightGroup::AddSpotLight(const SpotLightData& light) {
	if (data_.spotLightCount >= kMaxSpotLights) {
		if (!g_hasReportedSpotOverflow) {
			g_hasReportedSpotOverflow = true;
		}
		return -1;
	}

	const int32_t index = data_.spotLightCount;
	data_.spotLights[index] = light;

	// 余弦が入っていない場合は既定の広がりを入れておく。
	// 0 のままだと円錐が真横まで開き、点光源のように見えてしまう。
	if (data_.spotLights[index].cosOuterAngle <= 0.0f && data_.spotLights[index].cosInnerAngle <= 0.0f) {
		data_.spotLights[index].cosOuterAngle = std::cos(ToRadian(30.0f));
		data_.spotLights[index].cosInnerAngle = std::cos(ToRadian(20.0f));
	}

	++data_.spotLightCount;
	return index;
}

SpotLightData* LightGroup::GetSpotLight(int32_t index) {
	if (index < 0 || index >= data_.spotLightCount) {
		return nullptr;
	}
	return &data_.spotLights[index];
}

const SpotLightData* LightGroup::GetSpotLight(int32_t index) const {
	if (index < 0 || index >= data_.spotLightCount) {
		return nullptr;
	}
	return &data_.spotLights[index];
}

void LightGroup::SetSpotLightEnabled(int32_t index, bool isEnabled) {
	if (SpotLightData* light = GetSpotLight(index); light != nullptr) {
		light->isEnabled = isEnabled ? 1 : 0;
	}
}

bool LightGroup::IsSpotLightEnabled(int32_t index) const {
	const SpotLightData* light = GetSpotLight(index);
	return light != nullptr && light->isEnabled != 0;
}

void LightGroup::RemoveSpotLight(int32_t index) {
	if (index < 0 || index >= data_.spotLightCount) {
		return;
	}

	for (int32_t moveIndex = index; moveIndex + 1 < data_.spotLightCount; ++moveIndex) {
		data_.spotLights[moveIndex] = data_.spotLights[moveIndex + 1];
	}

	--data_.spotLightCount;
}

void LightGroup::SetSpotLightAngle(int32_t index, const SpotLightAngle& angle) {
	SpotLightData* spotLight = GetSpotLight(index);
	if (spotLight == nullptr) {
		return;
	}

	// 角度が大きいほど余弦は小さい。内側が外側より広い指定は意味が通らないため、
	// 内側を外側以下へ丸める。こうしないと減衰の分母が負になる。
	const float outerAngle = Clamp(angle.outerAngle, 0.0f, kHalfPi);
	const float innerAngle = Clamp(angle.innerAngle, 0.0f, outerAngle);

	spotLight->cosOuterAngle = std::cos(outerAngle);
	spotLight->cosInnerAngle = std::cos(innerAngle);
}
