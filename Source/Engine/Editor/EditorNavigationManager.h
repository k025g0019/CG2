#pragma once

#include "EditorPhysicsManager.h"
#include "EditorScene.h"
#include "Vector.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorNavigationManager {
public:
	EditorNavigationManager() = default;  // RuntimeManager が 1 つだけ保持する通常コンストラクタ。
	~EditorNavigationManager() = default;  // Scene / Physics は外部所有なのでここでは破棄しない。
	EditorNavigationManager(const EditorNavigationManager&) = delete;  // Agent の実行状態を二重管理しないためコピー禁止。
	EditorNavigationManager& operator=(const EditorNavigationManager&) = delete;  // Agent の実行状態を二重管理しないためコピー代入禁止。
	EditorNavigationManager(EditorNavigationManager&&) = delete;  // Runtime 中の参照先を動かさない。
	EditorNavigationManager& operator=(EditorNavigationManager&&) = delete;  // Runtime 中の参照先を動かさない。

	void Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager, std::vector<std::string>* consoleMessages);  // Scene と物理 API を受け取る。
	void Start();  // Play 開始時に Agent の速度キャッシュと NavMesh 情報を初期化する。
	void Update(float deltaTime);  // Play 中の Navigation 6 Component を実行する。
	void Draw();  // 将来の NavMesh Debug 表示用。現時点では描画処理を持たない。
	void Stop();  // Play 停止時に Navigation の実行状態を捨てる。

public:
	struct NavigationSurface {
		int32_t gameObjectId = -1;  // Surface を持つ GameObject ID。
		float minX = 0.0f;  // XZ 平面上の移動可能範囲の左端。
		float maxX = 0.0f;  // XZ 平面上の移動可能範囲の右端。
		float minZ = 0.0f;  // XZ 平面上の移動可能範囲の手前端。
		float maxZ = 0.0f;  // XZ 平面上の移動可能範囲の奥端。
		int32_t area = 0;  // NavMeshModifier の Area 上書き結果。
	};

	enum class NavigationObstacleShape {
		Circle,  // Sphere / Capsule など、XZ 平面で半径として扱う障害物。
		Box,  // BoxCollider / MeshCollider など、XZ 平面で幅と奥行きを持つ障害物。
	};

	struct NavigationObstacle {
		int32_t gameObjectId = -1;  // Obstacle を持つ GameObject ID。
		Vector3 center = {0.0f, 0.0f, 0.0f};  // ワールド空間の障害物中心。
		Vector3 halfSize = {0.5f, 0.5f, 0.5f};  // Box 障害物の半分サイズ。Mesh は頂点範囲から作る。
		float radius = 0.5f;  // XZ 平面上で Agent が避ける半径。
		float rotationY = 0.0f;  // Box 障害物を GameObject の Y 回転に合わせる。
		bool canCarve = true;  // false なら経路回避対象にしない。
		NavigationObstacleShape shape = NavigationObstacleShape::Circle;  // 回避判定で使う障害物形状。
	};

	struct NavigationModifierVolume {
		int32_t gameObjectId = -1;  // Volume を持つ GameObject ID。
		float minX = 0.0f;  // XZ 平面上の範囲の左端。
		float maxX = 0.0f;  // XZ 平面上の範囲の右端。
		float minZ = 0.0f;  // XZ 平面上の範囲の手前端。
		float maxZ = 0.0f;  // XZ 平面上の範囲の奥端。
		int32_t area = 0;  // 0 は通常、1 以上は移動コスト、負数は通行不可として扱う。
	};

	struct NavigationLink {
		int32_t startGameObjectId = -1;  // Link Component を持つ始点 GameObject ID。
		int32_t endGameObjectId = -1;  // Inspector の接続先 ID。
		Vector3 start = {0.0f, 0.0f, 0.0f};  // 始点のワールド位置。
		Vector3 end = {0.0f, 0.0f, 0.0f};  // 終点のワールド位置。
		float radius = 0.5f;  // Link に乗れる距離。
		float costModifier = 1.0f;  // 小さいほど使いやすい接続として扱う。
		bool isBidirectional = true;  // true なら終点側からも戻れる。
	};

private:
	EditorScene* editorScene_ = nullptr;  // Navigation Component を検索する Scene。
	EditorPhysicsManager* physicsManager_ = nullptr;  // Rigidbody 付き Agent へ速度を渡す物理 API。
	std::vector<std::string>* consoleMessages_ = nullptr;  // Play 開始時の Navigation 状態を Console へ出す。
	std::vector<NavigationSurface> surfaces_;  // 現在の NavMeshSurface 一覧。
	std::vector<NavigationObstacle> obstacles_;  // 現在の NavMeshObstacle 一覧。
	std::vector<NavigationModifierVolume> modifierVolumes_;  // 現在の NavMeshModifierVolume 一覧。
	std::vector<NavigationLink> links_;  // 現在の NavMeshLink 一覧。
	std::unordered_map<int32_t, Vector3> agentVelocities_;  // Agent ごとの水平移動速度。
	std::unordered_map<int32_t, Vector3> agentDestinations_;  // 自動再経路探索 OFF の Agent が使う固定目的地。
	bool isStarted_ = false;  // Play 中の Navigation が開始済みなら true。

	void BuildNavigationData(bool shouldLog);  // Surface / Obstacle / Volume / Link を Scene から収集する。
	void UpdateAgent(EditorGameObject& gameObject, EditorComponent& agent, float deltaTime);  // Agent 1 体を目的地へ進める。
	void PushConsoleMessage(const std::string& message) const;  // Console 出力先がある時だけログを追加する。
	bool TryClampToSurface(Vector3& position, float radius) const;  // 位置を最寄りの Surface 範囲へ収める。
	bool TryApplyLink(EditorGameObject& gameObject, const EditorComponent& agent, const Vector3& targetPosition);  // 近い NavMeshLink に乗れるなら移動する。
	float GetAreaSpeedScale(const Vector3& position) const;  // ModifierVolume の Area から速度倍率を作る。
	Vector3 AvoidObstacles(const EditorGameObject& gameObject, const EditorComponent& agent, const Vector3& currentPosition, const Vector3& desiredPosition) const;  // Obstacle と重ならない候補位置を返す。
};

#pragma warning(pop)
