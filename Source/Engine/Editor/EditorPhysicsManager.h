#pragma once

#include "EditorJoltPhysicsManager.h"
#include "EditorScene.h"

#include <functional>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorPhysicsManager {
public:
	using WireHandle = uint64_t;
	static constexpr WireHandle kInvalidWireHandle = 0ULL;

	struct RuntimeWireDesc {
		int32_t firstGameObjectId = -1;
		int32_t secondGameObjectId = -1;
		int32_t ownerGameObjectId = -1;
		int32_t rendererSettingsGameObjectId = -1;
		Vector3 firstLocalAnchor = {0.0f, 0.0f, 0.0f};
		Vector3 secondLocalAnchor = {0.0f, 0.0f, 0.0f};
		float maximumLength = 1.0f;
		float minimumLength = 0.1f;
		float stiffness = 1200.0f;
		float damping = 80.0f;
		float maximumTension = 0.0f;
		float breakingTension = 0.0f;
		float shrinkSpeed = 0.0f;
		bool applyReaction = true;
		bool requireConnectable = true;
	};

	struct RuntimeWireState {
		WireHandle handle = kInvalidWireHandle;
		RuntimeWireDesc desc{};
		Vector3 firstWorldAnchor = {0.0f, 0.0f, 0.0f};
		Vector3 secondWorldAnchor = {0.0f, 0.0f, 0.0f};
		float currentLength = 0.0f;
		float currentTension = 0.0f;
		bool isActive = true;
		bool isBroken = false;
	};

	enum class RuntimeWireEventType {
		Connected,
		TensionChanged,
		Broken,
		TargetLost,
		Destroyed
	};

	struct RuntimeWireEvent {
		RuntimeWireEventType type = RuntimeWireEventType::Connected;
		WireHandle handle = kInvalidWireHandle;
		int32_t firstGameObjectId = -1;
		int32_t secondGameObjectId = -1;
		int32_t ownerGameObjectId = -1;
		float tension = 0.0f;
	};

	enum class PhysicsDebugCastType {
		Ray,
		Sphere,
		Capsule
	};

	struct PhysicsDebugCast {
		PhysicsDebugCastType type = PhysicsDebugCastType::Ray;  // SceneView で形状を区別する種類
		Vector3 origin = {0.0f, 0.0f, 0.0f};  // Cast の World 開始位置
		Vector3 direction = {0.0f, 0.0f, 1.0f};  // Cast の World 方向
		float distance = 0.0f;  // Cast の最大距離
		float radius = 0.0f;  // Sphere / Capsule の半径
		float height = 0.0f;  // Capsule の高さ
		bool hasHit = false;  // 命中情報を保持しているか
		EditorJoltPhysicsManager::PhysicsHit hit{};  // 命中点、法線、対象 GameObject
	};

	EditorPhysicsManager() = default;  // RuntimeManager が直接保持する通常コンストラクタ
	~EditorPhysicsManager() = default;  // Jolt の破棄は joltPhysicsManager_ が担当する
	EditorPhysicsManager(const EditorPhysicsManager&) = delete;  // Jolt World を二重所有しないためコピー禁止
	EditorPhysicsManager& operator=(const EditorPhysicsManager&) = delete;  // Jolt World を二重所有しないためコピー代入禁止
	EditorPhysicsManager(EditorPhysicsManager&&) = delete;  // 内部 Jolt World の所有先を動かさない
	EditorPhysicsManager& operator=(EditorPhysicsManager&&) = delete;  // 内部 Jolt World の所有先を動かさない

	void Initialize(EditorScene* editorScene, std::vector<std::string>* consoleMessages);  // 物理更新対象の Scene と Console 出力先を受け取る
	void BeginDebugFrame();  // Script / AI が発行する Cast を描画フレーム単位で記録し直す
	void StartSimulation();  // Play 開始時に Scene の Component から Jolt Body を作る
	int32_t Update(float deltaTime);  // Jolt PhysicsSystem を進め、結果を GameObject へ戻し、実行した固定更新回数を返す
	void Draw();  // 現時点では Jolt のデバッグ描画なし
	void StopSimulation();  // Play 停止時に Jolt Body を破棄する
	void RegisterRuntimeHierarchy(int32_t rootGameObjectId);  // Pool等がPlay中に複製した階層を物理Worldと固定更新Cacheへ追加する
	bool SetGameObjectSimulationActive(int32_t gameObjectId, bool isActive);  // GameObjectの実行状態に合わせてJolt Bodyを物理Worldへ出し入れする
	bool SetGameObjectTransform(int32_t gameObjectId, const Vector3& position, const Vector3& rotation);  // Poolから再利用するBodyのWorld姿勢を同期する
	bool Raycast(const Vector3& origin, const Vector3& direction, float distance, EditorJoltPhysicsManager::PhysicsHit& hit) const;  // Runtime から Physics.Raycast 相当を呼べる入口
	bool RaycastIgnoringGameObject(const Vector3& origin, const Vector3& direction, float distance, int32_t ignoredGameObjectId, EditorJoltPhysicsManager::PhysicsHit& hit) const;  // サスペンションなど所有者自身を除外する Raycast
	bool RaycastIgnoringGameObjects(const Vector3& origin, const Vector3& direction, float distance, const std::vector<int32_t>& ignoredGameObjectIds, EditorJoltPhysicsManager::PhysicsHit& hit) const;  // Attack Filter用の複数除外Raycast
	bool SphereCast(const Vector3& origin, float radius, const Vector3& direction, float distance, EditorJoltPhysicsManager::PhysicsHit& hit) const;  // Runtime から Physics.SphereCast 相当を呼べる入口
	bool SphereCastIgnoringGameObjects(const Vector3& origin, float radius, const Vector3& direction, float distance, const std::vector<int32_t>& ignoredGameObjectIds, EditorJoltPhysicsManager::PhysicsHit& hit) const;  // Attack Filter用の複数除外SphereCast
	bool CapsuleCast(const Vector3& origin, float radius, float height, const Vector3& direction, float distance, EditorJoltPhysicsManager::PhysicsHit& hit) const;  // Runtime から Physics.CapsuleCast 相当を呼べる入口
	bool OverlapSphere(const Vector3& center, float radius, std::vector<int32_t>& hitGameObjectIds) const;  // Runtime から Physics.OverlapSphere 相当を呼べる入口
	bool OverlapBox(const Vector3& center, const Vector3& size, std::vector<int32_t>& hitGameObjectIds) const;  // Runtime から Physics.OverlapBox 相当を呼べる入口
	bool AddForce(int32_t gameObjectId, const Vector3& force);  // Runtime から Rigidbody.AddForce 相当を呼べる入口
	bool GetBodyMass(int32_t gameObjectId, float& bodyMass) const;  // Jolt へ反映済みの実質量を返す（診断用）
	bool GetBodyDiagnostics(int32_t gameObjectId, Vector3& bodyPosition, bool& isAddedToWorld) const;  // Body実座標とWorld登録状態（診断用）
	bool AddForceAtPosition(int32_t gameObjectId, const Vector3& force, const Vector3& worldPosition);  // 船体内部など World 位置へ力を加える入口
	bool AddImpulse(int32_t gameObjectId, const Vector3& impulse);  // Runtime から Rigidbody.AddImpulse 相当を呼べる入口
	bool AddTorque(int32_t gameObjectId, const Vector3& torque);  // Runtime から Rigidbody.AddTorque 相当を呼べる入口
	bool SetVelocity(int32_t gameObjectId, const Vector3& velocity);  // Runtime から Rigidbody.velocity 相当を呼べる入口
	bool SetAngularVelocity(int32_t gameObjectId, const Vector3& angularVelocity);  // Runtime から Rigidbody.angularVelocity 相当を呼べる入口
	int32_t AddExplosionImpulse(const Vector3& center, float radius, float impulseStrength, float upwardModifier);  // 範囲内の Dynamic Rigidbody へ距離減衰付き爆発Impulseを加える
	uint64_t CreateSpringJoint(int32_t ownerGameObjectId, int32_t connectedGameObjectId, const Vector3& ownerAnchor, const Vector3& connectedAnchor, float minDistance, float maxDistance, float frequency, float damping);  // 実行中の2 Body間へSpringJointを生成する
	bool DestroyJoint(uint64_t jointHandle);  // Handleで指定したRuntime Jointを破棄する
	bool SetSpringJointSettings(uint64_t jointHandle, const Vector3& ownerAnchor, const Vector3& connectedAnchor, float minDistance, float maxDistance, float frequency, float damping);  // Runtime SpringJointを再設定する
	bool IsJointValid(uint64_t jointHandle) const;  // Runtime Joint Handleが有効か返す
	uint64_t CreateJoint(EditorJoltPhysicsManager::RuntimeJointType jointType, int32_t ownerGameObjectId, int32_t connectedGameObjectId, const EditorJoltPhysicsManager::RuntimeJointSettings& jointSettings);  // 対応するRuntime Jointを生成する
	bool SetJointSettings(uint64_t jointHandle, const EditorJoltPhysicsManager::RuntimeJointSettings& jointSettings);  // Joint種別を維持して設定を更新する
	bool AttachRope(int32_t ownerGameObjectId, int32_t targetGameObjectId, const Vector3& ownerLocalAnchor, const Vector3& targetAnchor, float maximumLength);  // RopeConstraint を実行中に接続する。target=-1 なら targetAnchor は World 固定点
	bool DetachRope(int32_t ownerGameObjectId);  // RopeConstraint を無効化して張力を止める
	bool SetRopeLength(int32_t ownerGameObjectId, float maximumLength);  // ウインチ用途に実行中の最大長を変更する
	bool RepairRope(int32_t ownerGameObjectId);  // 破断状態を解除して再接続する
	bool GetRopeState(int32_t ownerGameObjectId, bool& isActive, bool& isBroken, int32_t& targetGameObjectId, float& maximumLength, float& currentLength, float& currentTension) const;  // Script / HUD がロープ状態を読む
	WireHandle CreateWire(const RuntimeWireDesc& wireDesc);  // Componentに依存しない複数接続Wireを生成する
	bool DestroyWire(WireHandle wireHandle);  // 指定Wireだけを破棄する
	bool SetWireLength(WireHandle wireHandle, float maximumLength);  // Handle単位で巻取り長を変更する
	bool SetWireShrinkSpeed(WireHandle wireHandle, float shrinkSpeed);  // 自動巻取り速度m/sを変更する
	bool RepairWire(WireHandle wireHandle);  // 破断状態を解除する
	bool GetWireState(WireHandle wireHandle, RuntimeWireState& wireState) const;
	int32_t GetWireCountForGameObject(int32_t gameObjectId) const;
	bool GetWireForGameObject(int32_t gameObjectId, int32_t wireIndex, RuntimeWireState& wireState) const;
	bool CanConnectWire(int32_t gameObjectId) const;  // WireConnectableと最大接続数を検証する
	const std::unordered_map<WireHandle, RuntimeWireState>& GetRuntimeWires() const;  // Game/Scene View描画用
	const std::vector<RuntimeWireEvent>& GetFrameWireEvents() const;  // Script通知用
	void ClearRuntimeWires();  // Play停止、Scene切替時の一括破棄
	const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& GetFrameEvents() const;  // 直近フレームの全固定更新で集めた接触イベント一覧
	const std::vector<EditorJoltPhysicsManager::PhysicsEvent>& GetContactDebugEvents() const;  // 最後に進んだ固定更新の接触を描画フレーム間で保持する
	const std::vector<PhysicsDebugCast>& GetFrameDebugCasts() const;  // 直近フレームで実行した Ray / ShapeCast 一覧
	float GetFixedTimeStep() const;  // Script 側の FixedUpdate とそろえる固定時間
	void SetPreFixedStepCallback(std::function<void(float)> callback);  // Jolt 更新直前に汎用の物理制御を固定時間で実行する
	void SetPostFixedStepCallback(std::function<void(float)> callback);  // Jolt 更新(積分)直後、最終姿勢確定後に固定時間で実行する

private:
	struct PhysicsStepObject {
		EditorGameObject* gameObject = nullptr;  // この固定更新で参照するGameObject
		EditorComponent* rigidBody = nullptr;  // Dynamic判定と速度・質量を共有するRigidbody
		EditorComponent* constantForce = nullptr;  // 常時外力
		EditorComponent* aerodynamics = nullptr;  // 空力面
		EditorComponent* windZone = nullptr;  // 風源
		EditorComponent* gravityField = nullptr;  // 点重力源
		EditorComponent* rotatingFrame = nullptr;  // 回転座標系
		EditorComponent* fluidVolume = nullptr;  // 有限流体領域
		EditorComponent* springForce = nullptr;  // 両方向ばね
		EditorComponent* ropeConstraint = nullptr;  // 片方向ロープ
		EditorComponent* torsionSpring = nullptr;  // 回転ばね
		EditorComponent* thruster = nullptr;  // 推進器
		EditorComponent* pulleyConstraint = nullptr;  // 滑車拘束
		EditorComponent* physicsServo = nullptr;  // 位置・姿勢Servo
		EditorComponent* vortexField = nullptr;  // 渦速度場
		EditorComponent* pressureField = nullptr;  // 放射圧力場
		EditorComponent* suspension = nullptr;  // Ray式サスペンション
		EditorComponent* uprightStabilizer = nullptr;  // 姿勢安定化
		EditorComponent* electromagneticField = nullptr;  // 電磁場源
		EditorComponent* electromagneticBody = nullptr;  // 電荷・磁気モーメント
		EditorComponent* buoyancy = nullptr;  // FFT海面浮力
	};

	struct BuoyancyRuntimeState {
		float previousSubmergedVolume = 0.0f;  // 前回の実 Shape 排水体積。入水時の体積変化率に使う
		Vector3 previousRelativeWaterVelocity = {0.0f, 0.0f, 0.0f};  // 付加質量が使う前回の浮心相対速度
		Vector3 filteredRelativeWaterAcceleration = {0.0f, 0.0f, 0.0f};  // FFT Sample差分の高周波Noiseを除いた相対加速度
		Vector3 previousAngularVelocity = {0.0f, 0.0f, 0.0f};  // 回転付加慣性が使う前回のWorld角速度
		Vector3 filteredAngularAcceleration = {0.0f, 0.0f, 0.0f};  // 回転付加慣性へ使う平滑化済み角加速度
		Vector3 filteredHydrostaticOffset = {0.0f, 0.0f, 0.0f};  // 局所波面から求めた浮力作用点のWorld Offset
		std::vector<EditorJoltPhysicsManager::HydrodynamicSurfaceTriangle> surfaceTriangles;  // Capacityを再利用するWorld水力面Buffer
		bool hasPreviousSample = false;  // Play 開始直後を入水衝撃として扱わないための初期化フラグ
		bool hasPreviousRelativeWaterVelocity = false;  // 初回接水で偽の付加質量Impulseを出さない
		bool hasPreviousAngularVelocity = false;  // 初回接水で偽の回転付加慣性Torqueを出さない
		bool hasPreviousHydrostaticOffset = false;  // 水力面の切替で浮力作用点を瞬間移動させない
	};

	EditorScene* editorScene_ = nullptr;  // 物理 Component を検索する対象 Scene
	std::vector<std::string>* consoleMessages_ = nullptr;  // 物理イベントを Console へ出すための出力先
	EditorJoltPhysicsManager joltPhysicsManager_;  // JoltPhysics-5.5.0 を使う実物理 World
	std::vector<EditorJoltPhysicsManager::PhysicsEvent> frameEvents_;  // 1 描画フレーム中に起きた固定更新イベントをまとめて保持する
	std::vector<EditorJoltPhysicsManager::PhysicsEvent> contactDebugEvents_;  // 高FPS時も接触表示が点滅しないよう最後の固定更新結果を保持する
	mutable std::vector<PhysicsDebugCast> frameDebugCasts_;  // const の Cast API から記録する直近フレームの可視化情報
	std::unordered_map<int32_t, BuoyancyRuntimeState> buoyancyRuntimeStates_;  // Object ごとの入水履歴。Scene 保存対象にはしない
	std::unordered_map<WireHandle, RuntimeWireState> runtimeWires_;  // 複数接続WireをHandle単位で保持する
	std::vector<RuntimeWireEvent> frameWireEvents_;  // この描画フレームに発生したWire通知
	WireHandle nextWireHandle_ = 1ULL;  // 0は無効Handleとして予約する
	std::vector<PhysicsStepObject> physicsStepObjects_;  // 固定更新内でComponent検索結果を再利用する一時索引
	std::vector<PhysicsStepObject*> windZoneObjects_;  // 空力計算が使う有効な風源だけの索引
	std::vector<PhysicsStepObject*> gravityFieldObjects_;  // 点重力計算が使う有効な重力源だけの索引
	std::vector<PhysicsStepObject*> rotatingFrameObjects_;  // 回転座標系計算が使う有効な場だけの索引
	std::vector<PhysicsStepObject*> fluidVolumeObjects_;  // 流体体積計算が使う有効な領域だけの索引
	std::vector<PhysicsStepObject*> vortexFieldObjects_;  // 渦計算が使う有効な場だけの索引
	std::vector<PhysicsStepObject*> pressureFieldObjects_;  // 圧力計算が使う有効な場だけの索引
	std::vector<PhysicsStepObject*> electromagneticFieldObjects_;  // 電磁計算が使う有効な場だけの索引
	std::function<void(float)> preFixedStepCallback_;  // Rail などが描画 FPS に依存せず力を加えるための固定更新入口
	std::function<void(float)> postFixedStepCallback_;  // Jolt積分後、最終姿勢確定後に呼ぶ固定更新入口(Rail絶対角度制限のHard Clamp等)
	float fixedTimeStep_ = 1.0f / 60.0f;  // 物理だけを進める固定時間。Unity の FixedUpdate 相当
	float fixedTimeAccumulator_ = 0.0f;  // 可変 deltaTime を固定時間へ分割するための蓄積時間
	float simulationElapsedTime_ = 0.0f;  // WindZone の連続した乱流位相を固定更新時間で進める
	int32_t maxFixedSubSteps_ = 4;  // フレーム落ち時に 1 フレームで回す物理回数の上限

	void RebuildPhysicsStepCache();  // Sceneを1回だけ走査し、同じ固定更新中のComponent検索を共有する
	void ApplyConstantForces();  // ConstantForce の設定値を Dynamic Rigidbody へ固定更新ごとに加える
	void ApplyAerodynamicForces();  // 相対風速から抗力・揚力・横力・回転力を計算する
	void ApplyGravityFieldForces();  // Scene 内の点重力を逆二乗則または定加速度で加える
	void ApplyRotatingFrameForces();  // 回転座標系の遠心力、Coriolis 力、Euler 力を加える
	void ApplyFluidVolumeForces();  // 有限流体領域の浮力、粘性抵抗、二次抗力を加える
	void ApplySpringForces();  // World点または別Bodyとの間へHookeばね力と減衰を加える
	void ApplyRopeForces();  // 最大長を超えた時だけ片方向の張力を加え、必要なら破断させる
	void ApplyRuntimeWireForces(float fixedDeltaTime);  // Handle Wireを複数本同時に解く
	void PushWireEvent(RuntimeWireEventType eventType, const RuntimeWireState& wireState);
	void ApplyTorsionSpringTorques();  // Worldまたは別Bodyの目標角へ回転ばねTorqueを加える
	void ApplyThrusterForces();  // ローカル作用点へ推進力を加え、重心との差から旋回Torqueを作る
	void ApplyPulleyForces();  // 2本のロープ長と滑車比から両Bodyへ張力を加える
	void ApplyPhysicsServoForces();  // 目標位置と姿勢へPD制御Force / Torqueを加える
	void ApplyVortexFieldForces();  // 回転流体の目標速度場へDynamic Rigidbodyを追従させる
	void ApplyPressureFieldForces();  // 圧力、投影面積、距離減衰から放射Forceを加える
	void ApplySuspensionForces();  // 接地Rayの圧縮量と点速度からばね・減衰Forceを加える
	void ApplyUprightStabilizerTorques();  // 現在の上方向を目標World上方向へ戻すPD Torqueを加える
	void ApplyElectromagneticForces();  // Coulomb力、Lorentz力、磁気双極子Torqueを加える
	void ApplyBuoyancyForces(float fixedDeltaTime);  // FFT局所水面と実Physics Shapeの水没体積・浮心から浮力を加える
	void RecordDebugCast(
		PhysicsDebugCastType type,
		const Vector3& origin,
		const Vector3& direction,
		float distance,
		float radius,
		float height,
		bool hasHit,
		const EditorJoltPhysicsManager::PhysicsHit& hit) const;  // Cast結果をSceneView用に上限付きで保存する
};

#pragma warning(pop)
