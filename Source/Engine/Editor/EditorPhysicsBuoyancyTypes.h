#pragma once

#include "EditorOceanSystem.h"
#include "EditorScene.h"

#include <array>
#include <cstdint>

//========================================
// 浮力計算の入力型
//========================================

// EditorPhysicsManager の浮力処理が使う入力型をここへ置く。
// Manager の private メンバ関数の引数へ現れるため、cpp の無名 namespace には置けない。
// 状態は持たず、固定更新のあいだだけ生きる値の入れ物として扱う。

// 実行時の浮力設定。
// Buoyancy Component があればその値を、無ければ Dynamic Rigidbody の 3D Collider から
// 安全な既定値を作って埋める。以降の計算はこの解決済みの値だけを読む。
struct RuntimeBuoyancySettings {
	int32_t oceanGameObjectId = -1;                 // 水面を供給する Ocean。-1 なら Scene 内から解決する
	Vector3 centerOffset = {0.0f, 0.0f, 0.0f};      // 船体基準点の Local オフセット
	Vector3 hullSize = {1.0f, 1.0f, 1.0f};          // 船体の Local 寸法
	float strength = 24.0f;                         // 手動指定時の浮力係数
	float damping = 7.0f;                           // 上下動の減衰
	float waterDrag = 1.4f;                         // 前後方向の抗力
	float angularDrag = 1.8f;                       // 回転の抗力
	float normalInfluence = 0.2f;                   // 水面法線を力の向きへ混ぜる割合
	float lateralDrag = 4.0f;                        // 左右方向の抗力
	float verticalDrag = 2.5f;                       // 上下方向の抗力
	float slammingStrength = 2.0f;                   // 入水衝撃の強さ
	bool automaticPhysicalProperties = false;        // true なら水密度と排水体積から物理量を自動算出する
	float waterDensity = 1025.0f;                    // 自動算出で使う水の密度（kg/m^3）
	bool limitDraftHeight = false;                   // Collider から自動で作る船体は喫水高さに縦範囲を制限する
};

// 1 つの浮力物体について、実 Shape 方式と旧グリッド方式の両方が共通で使う入力。
// 有効性判定と World 姿勢の解決は呼び出し側で済ませてから渡す。
// ここに入る値はどちらの方式でも書き換えない。結果は Jolt への Force / Torque と
// Runtime 診断値として外へ出る。
struct BuoyancyObjectInput {
	EditorGameObject* gameObject = nullptr;          // 力を掛ける対象。id から Jolt Body を引く
	EditorComponent* rigidBody = nullptr;            // 速度・角速度・質量の読み取り元
	EditorComponent* buoyancyDiagnostics = nullptr;  // Runtime 診断値の書き出し先。Component 無しなら nullptr
	RuntimeBuoyancySettings settings{};              // Component と Collider 自動算出を解決した後の設定
	Vector3 hullSize = {1.0f, 1.0f, 1.0f};           // 0 割りを避けるため下限を掛けた船体寸法（Local）
	Vector3 worldScale = {1.0f, 1.0f, 1.0f};         // 親まで合成した World Scale
	Vector3 worldRotation = {0.0f, 0.0f, 0.0f};      // 同じく World 回転（Euler / X→Y→Z 順）
	Vector3 worldPosition = {0.0f, 0.0f, 0.0f};      // 同じく World 位置
	float fixedDeltaTime = 0.0f;                     // 固定更新間隔（秒）。0 以下は呼び出し前に弾く
	float oceanElapsedTime = 0.0f;                   // FFT 波形を評価するための経過時間
	float gravityMagnitude = 0.0f;                   // 重力の大きさ。0 なら静水圧を作れない
	Vector3 buoyancyUp = {0.0f, 1.0f, 0.0f};         // 重力の逆向き単位ベクトル。浮力はこの向きへ働く
};

//========================================
// 局所水面モデル
//========================================

// 局所 FFT 水面を補間するための固定 Probe 数。
// 面の枚数に比例して FFT を評価しないよう、1 物体あたりこの数だけに抑える。
constexpr int32_t kBuoyancySurfaceProbeAxisCount = 5;
constexpr size_t kBuoyancySurfaceProbeCount =
	static_cast<size_t>(kBuoyancySurfaceProbeAxisCount * kBuoyancySurfaceProbeAxisCount);

// 船体下面を覆う 5x5 Probe から作る局所水面。
//
// 25 点の FFT Sample と、そこへ当てた最小二乗 Plane の両方を持つ。
// 水没面の水深は Sample() で問い合わせ、4 近傍がそろえば双線形補間、
// 欠ければ Plane の式へ落ちる。
//
// EditorPhysicsManager::BuildLocalWaterSurface() が埋め、以降は読み取りだけ。
struct LocalWaterSurfaceModel {
	//------------------------------
	// Probe の配置
	//------------------------------

	Vector3 hullCenter = {0.0f, 0.0f, 0.0f};          // Probe 格子の中心（船体基準点の World 位置）
	Vector3 horizontalRight = {1.0f, 0.0f, 0.0f};     // 格子の右方向。船体右を水平化したもの
	Vector3 horizontalForward = {0.0f, 0.0f, 1.0f};   // 格子の前方向。船体前を水平化したもの
	float halfWidth = 0.25f;                          // 格子の右方向の半幅（m）
	float halfLength = 0.25f;                         // 格子の前方向の半長（m）
	std::array<float, kBuoyancySurfaceProbeCount> rightOffsets{};    // 中心からの右方向オフセット
	std::array<float, kBuoyancySurfaceProbeCount> forwardOffsets{};  // 中心からの前方向オフセット

	//------------------------------
	// 25 点の Sample と当てはめた Plane
	//------------------------------

	std::array<EditorOceanSurfaceSample, kBuoyancySurfaceProbeCount> samples{};
	std::array<bool, kBuoyancySurfaceProbeCount> hasSample{};  // Ocean が無い位置は false
	int32_t oceanGameObjectId = -1;                   // 実際に水面を返した Ocean
	int32_t validSampleCount = 0;                     // 取れた Probe 数。25 でだけ Plane を当てる
	Vector3 averageVelocity = {0.0f, 0.0f, 0.0f};     // 取れた Probe の表面速度の平均
	Vector3 fittedNormal = {0.0f, 1.0f, 0.0f};        // 最小二乗 Plane の法線（重力の逆向き寄り）
	float fittedHeight = 0.0f;                        // Plane の中心高さ（World Y）

	//------------------------------
	// 問い合わせ
	//------------------------------

	void Sample(const Vector3& queryPosition, EditorOceanSurfaceSample& interpolatedSample) const;
};
