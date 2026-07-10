#include "EditorAIManager.h"

#include "EditorAssetUtility.h"
#include "EditorComponentUtility.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#pragma warning(push, 0)
#include "behaviortree_cpp/bt_factory.h"
#include "DetourCommon.h"
#include "OpenSteer/Vec3.h"
#include "ThirdParty/AI/MicroPather-master/micropather.h"
#include "ThirdParty/AI/cppGOAP-develop/src/Planner.h"
#pragma warning(pop)

#pragma warning(disable : 4820)
#pragma warning(disable : 5045)

namespace {
	constexpr int32_t kInvalidGameObjectId = -1;  // 未設定の GameObject ID。
	constexpr float kMinimumDistance = 0.0001f;  // 0 除算を避けるための最小距離。
	constexpr float kPatrolRadius = 3.0f;  // 巡回行動で使う円の半径。
	constexpr float kPi = 3.14159265358979323846f;  // 視野角計算用の円周率。
	constexpr float kPythonUpdateInterval = 0.25f;  // Python AI を呼ぶ最短間隔。毎フレーム起動による重さを避ける。
	constexpr int32_t kGoapHasTarget = 1;  // GOAP の世界状態: 対象がいる。
	constexpr int32_t kGoapHasPath = 2;  // GOAP の世界状態: 経路を作れる。
	constexpr int32_t kGoapInRange = 3;  // GOAP の世界状態: 対象へ届く距離。
	constexpr const char* kDefaultBehaviorTreeXml = R"(
<root BTCPP_format="4">
	<BehaviorTree ID="MainTree">
		<Fallback>
			<Sequence>
				<CanSeeTarget/>
				<MoveToTarget/>
			</Sequence>
			<Patrol/>
		</Fallback>
	</BehaviorTree>
</root>
)";

	bool IsAiAgentType(EditorComponentType type) {
		return
			type == EditorComponentType::AIBehaviorTree ||
			type == EditorComponentType::AIStateMachine ||
			type == EditorComponentType::AIGoapPlanner ||
			type == EditorComponentType::AIHtnPlanner ||
			type == EditorComponentType::AIPathfindingAgent ||
			type == EditorComponentType::AIRecastCrowdAgent ||
			type == EditorComponentType::AISteeringAgent ||
			type == EditorComponentType::AISeekSteering ||
			type == EditorComponentType::AIFleeSteering ||
			type == EditorComponentType::AIArriveSteering ||
			type == EditorComponentType::AIPursuitSteering ||
			type == EditorComponentType::AIWanderSteering ||
			type == EditorComponentType::AIObstacleAvoidanceSteering ||
			type == EditorComponentType::AIFlockSteering;
	}

	bool IsAiSensorType(EditorComponentType type) {
		return
			type == EditorComponentType::AIVisionSensor ||
			type == EditorComponentType::AIOpenCvObjectDetector ||
			type == EditorComponentType::AIOpenCvColorTracker ||
			type == EditorComponentType::AIMotionSensor ||
			type == EditorComponentType::AIWhisperSpeechRecognizer ||
			type == EditorComponentType::AIVoiceCommand;
	}

	float LengthSquaredXZ(const Vector3& vector) {
		return vector.x * vector.x + vector.z * vector.z;
	}

	float LengthXZ(const Vector3& vector) {
		return std::sqrt(LengthSquaredXZ(vector));
	}

	Vector3 NormalizeXZ(const Vector3& vector) {
		const float length = LengthXZ(vector);
		if (length <= kMinimumDistance) {
			return {0.0f, 0.0f, 0.0f};
		}

		return {vector.x / length, 0.0f, vector.z / length};
	}

	Vector3 MakeHorizontalVector(const Vector3& from, const Vector3& to) {
		return {to.x - from.x, 0.0f, to.z - from.z};
	}

	Vector3 MultiplyXZ(const Vector3& vector, float scalar) {
		return {vector.x * scalar, 0.0f, vector.z * scalar};
	}

	OpenSteer::Vec3 ToOpenSteerVector(const Vector3& vector) {
		return OpenSteer::Vec3(vector.x, vector.y, vector.z);
	}

	Vector3 FromOpenSteerVector(const OpenSteer::Vec3& vector) {
		return {vector.x, vector.y, vector.z};
	}

	Vector3 AddVector3(const Vector3& firstVector, const Vector3& secondVector) {
		return {
			firstVector.x + secondVector.x,
			firstVector.y + secondVector.y,
			firstVector.z + secondVector.z};
	}

	Vector3 AddXZ(const Vector3& position, const Vector3& movement) {
		return {position.x + movement.x, position.y, position.z + movement.z};
	}

	Vector3 MoveTowardsVelocity(const Vector3& currentVelocity, const Vector3& targetVelocity, float maxDelta) {
		const Vector3 difference = {
			targetVelocity.x - currentVelocity.x,
			0.0f,
			targetVelocity.z - currentVelocity.z};
		const float distance = LengthXZ(difference);
		if (distance <= maxDelta || distance <= kMinimumDistance) {
			return targetVelocity;
		}

		const Vector3 direction = NormalizeXZ(difference);
		return {
			currentVelocity.x + direction.x * maxDelta,
			0.0f,
			currentVelocity.z + direction.z * maxDelta};
	}

	float DotXZ(const Vector3& firstVector, const Vector3& secondVector) {
		return firstVector.x * secondVector.x + firstVector.z * secondVector.z;
	}

	Vector3 MakeForwardFromYaw(float yaw) {
		return {std::sin(yaw), 0.0f, std::cos(yaw)};
	}

	float ToRadians(float degrees) {
		return degrees * (kPi / 180.0f);
	}

	float DistanceXZ(const Vector3& firstPosition, const Vector3& secondPosition) {
		return LengthXZ(MakeHorizontalVector(firstPosition, secondPosition));
	}

	bool HasPythonAsset(const EditorComponent& component) {
		return !component.assetPath.empty() && EditorAssetUtility::HasExtension(component.assetPath, ".py");
	}

	int64_t MakeSensorKey(int32_t gameObjectId, EditorComponentType sensorComponentType) {
		const uint64_t objectPart = static_cast<uint32_t>(gameObjectId);
		const uint64_t componentPart = static_cast<uint32_t>(sensorComponentType);
		return static_cast<int64_t>((objectPart << 32) | componentPart);
	}

	float Clamp01(float value) {
		return (std::clamp)(value, 0.0f, 1.0f);
	}

	float MakeDistanceConfidence(float distance, float range) {
		if (range <= kMinimumDistance) {
			return 0.0f;
		}

		return Clamp01(1.0f - distance / range);
	}

	std::string TrimText(const std::string& text) {
		size_t firstIndex = 0u;
		while (firstIndex < text.size() && std::isspace(static_cast<unsigned char>(text[firstIndex])) != 0) {
			++firstIndex;
		}

		size_t lastIndex = text.size();
		while (lastIndex > firstIndex && std::isspace(static_cast<unsigned char>(text[lastIndex - 1u])) != 0) {
			--lastIndex;
		}

		return text.substr(firstIndex, lastIndex - firstIndex);
	}

	std::vector<std::string> SplitText(const std::string& text, char delimiter) {
		std::vector<std::string> parts;
		std::stringstream stream(text);
		std::string part;
		while (std::getline(stream, part, delimiter)) {
			parts.push_back(TrimText(part));
		}

		return parts;
	}

	bool TryParseFloatValue(const std::string& text, float& value) {
		try {
			size_t parsedLength = 0u;
			const float parsedValue = std::stof(text, &parsedLength);
			if (parsedLength == 0u) {
				return false;
			}

			value = parsedValue;
			return true;
		}
		catch (const std::exception&) {
			return false;
		}
	}

	bool TryParseIntValue(const std::string& text, int32_t& value) {
		try {
			size_t parsedLength = 0u;
			const int32_t parsedValue = std::stoi(text, &parsedLength);
			if (parsedLength == 0u) {
				return false;
			}

			value = parsedValue;
			return true;
		}
		catch (const std::exception&) {
			return false;
		}
	}

	void ApplySensorKeyValue(EditorAiSensorResult& sensorResult, const std::string& key, const std::string& value) {
		float floatValue = 0.0f;
		int32_t intValue = 0;

		if (key == "label") {
			sensorResult.label = value;
			sensorResult.hasDetails = true;
		}
		else if (key == "text") {
			sensorResult.text = value;
			sensorResult.hasDetails = true;
		}
		else if (key == "command") {
			sensorResult.command = value;
			sensorResult.hasDetails = true;
		}
		else if (key == "commandId" && TryParseIntValue(value, intValue)) {
			sensorResult.commandId = intValue;
			sensorResult.hasDetails = true;
		}
		else if (key == "detectedGameObjectId" && TryParseIntValue(value, intValue)) {
			sensorResult.detectedGameObjectId = intValue;
			sensorResult.hasDetails = true;
		}
		else if (key == "confidence" && TryParseFloatValue(value, floatValue)) {
			sensorResult.confidence = Clamp01(floatValue);
			sensorResult.hasDetails = true;
		}
		else if (key == "distance" && TryParseFloatValue(value, floatValue)) {
			sensorResult.distance = (std::max)(floatValue, 0.0f);
			sensorResult.hasDetails = true;
		}
		else if ((key == "screenX" || key == "x") && TryParseFloatValue(value, floatValue)) {
			sensorResult.screenX = floatValue;
			sensorResult.hasDetails = true;
		}
		else if ((key == "screenY" || key == "y") && TryParseFloatValue(value, floatValue)) {
			sensorResult.screenY = floatValue;
			sensorResult.hasDetails = true;
		}
		else if ((key == "boundsX" || key == "boxX") && TryParseFloatValue(value, floatValue)) {
			sensorResult.boundsX = floatValue;
			sensorResult.hasDetails = true;
		}
		else if ((key == "boundsY" || key == "boxY") && TryParseFloatValue(value, floatValue)) {
			sensorResult.boundsY = floatValue;
			sensorResult.hasDetails = true;
		}
		else if ((key == "boundsWidth" || key == "w") && TryParseFloatValue(value, floatValue)) {
			sensorResult.boundsWidth = floatValue;
			sensorResult.hasDetails = true;
		}
		else if ((key == "boundsHeight" || key == "h") && TryParseFloatValue(value, floatValue)) {
			sensorResult.boundsHeight = floatValue;
			sensorResult.hasDetails = true;
		}
		else if (key == "motionX" && TryParseFloatValue(value, floatValue)) {
			sensorResult.motionX = floatValue;
			sensorResult.hasDetails = true;
		}
		else if (key == "motionY" && TryParseFloatValue(value, floatValue)) {
			sensorResult.motionY = floatValue;
			sensorResult.hasDetails = true;
		}
		else if ((key == "motionMagnitude" || key == "motion") && TryParseFloatValue(value, floatValue)) {
			sensorResult.motionMagnitude = (std::max)(floatValue, 0.0f);
			sensorResult.hasDetails = true;
		}
		else if (key == "directionX" && TryParseFloatValue(value, floatValue)) {
			sensorResult.direction.x = floatValue;
			sensorResult.hasDetails = true;
		}
		else if (key == "directionY" && TryParseFloatValue(value, floatValue)) {
			sensorResult.direction.y = floatValue;
			sensorResult.hasDetails = true;
		}
		else if (key == "directionZ" && TryParseFloatValue(value, floatValue)) {
			sensorResult.direction.z = floatValue;
			sensorResult.hasDetails = true;
		}
	}

	bool TryParseSensorOutput(const std::string& outputLine, EditorAiSensorResult& sensorResult) {
		const std::string trimmedLine = TrimText(outputLine);
		if (trimmedLine.empty()) {
			return false;
		}

		const std::vector<std::string> parts = SplitText(trimmedLine, '|');
		int32_t detectedValue = 0;
		if (!TryParseIntValue(parts[0], detectedValue)) {
			return false;
		}

		sensorResult.isDetected = detectedValue != 0;
		for (size_t partIndex = 1u; partIndex < parts.size(); ++partIndex) {
			const std::string& part = parts[partIndex];
			const size_t equalIndex = part.find('=');
			if (equalIndex == std::string::npos) {
				continue;
			}

			const std::string key = TrimText(part.substr(0u, equalIndex));
			const std::string value = TrimText(part.substr(equalIndex + 1u));
			ApplySensorKeyValue(sensorResult, key, value);
		}

		return true;
	}

	EditorAiSensorResult MakeBaseSensorResult(
		const EditorGameObject& gameObject,
		const EditorComponent& sensorComponent,
		const EditorGameObject* targetGameObject) {
		EditorAiSensorResult sensorResult{};
		sensorResult.connectedGameObjectId = sensorComponent.connectedGameObjectId;
		sensorResult.detectedGameObjectId = targetGameObject != nullptr ? targetGameObject->id : kInvalidGameObjectId;
		sensorResult.range = sensorComponent.colliderRadius;
		sensorResult.angleDegrees = sensorComponent.colliderSize.x;

		if (targetGameObject != nullptr) {
			const Vector3 toTarget = MakeHorizontalVector(gameObject.translate, targetGameObject->translate);
			sensorResult.distance = LengthXZ(toTarget);
			sensorResult.direction = NormalizeXZ(toTarget);
			sensorResult.confidence = MakeDistanceConfidence(sensorResult.distance, sensorResult.range);
			sensorResult.label = targetGameObject->name;
		}

		return sensorResult;
	}

	std::string QuoteCommandArgument(const std::string& text) {
		std::string quotedText = "\"";
		for (const char character : text) {
			if (character == '"') {
				quotedText += "\\\"";
				continue;
			}

			quotedText += character;
		}

		quotedText += "\"";
		return quotedText;
	}

	bool TryParseDirectionText(const std::string& text, Vector3& direction) {
		std::istringstream stream(text);
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		if (!(stream >> x >> y >> z)) {
			return false;
		}

		direction = NormalizeXZ({x, y, z});
		return LengthSquaredXZ(direction) > kMinimumDistance;
	}

	bool TryRunCommandAndReadFirstLine(const std::string& command, std::string& outputLine) {
		FILE* pipe = _popen(command.c_str(), "r");
		if (pipe == nullptr) {
			return false;
		}

		std::array<char, 256> buffer{};
		if (fgets(buffer.data(), static_cast<int32_t>(buffer.size()), pipe) == nullptr) {
			_pclose(pipe);
			return false;
		}

		const int32_t exitCode = _pclose(pipe);
		if (exitCode != 0) {
			return false;
		}

		outputLine = buffer.data();
		return true;
	}

	bool IsBlockingCollider(const EditorComponent& component) {
		return
			component.isActive &&
			!component.isTrigger &&
			(component.type == EditorComponentType::BoxCollider ||
			 component.type == EditorComponentType::SphereCollider ||
			 component.type == EditorComponentType::CapsuleCollider ||
			 component.type == EditorComponentType::MeshCollider ||
			 component.type == EditorComponentType::NavMeshObstacle ||
			 component.type == EditorComponentType::AIDynamicObstacle);
	}

	bool IsInsideObstacleXZ(
		const Vector3& position,
		const EditorGameObject& obstacleObject,
		const EditorComponent& obstacleComponent,
		float agentRadius) {
		const Vector3 obstacleCenter = {
			obstacleObject.translate.x + obstacleComponent.colliderCenter.x * obstacleObject.scale.x,
			obstacleObject.translate.y + obstacleComponent.colliderCenter.y * obstacleObject.scale.y,
			obstacleObject.translate.z + obstacleComponent.colliderCenter.z * obstacleObject.scale.z};

		if (obstacleComponent.type == EditorComponentType::SphereCollider ||
			obstacleComponent.type == EditorComponentType::CapsuleCollider) {
			const float maxScale = (std::max)(std::abs(obstacleObject.scale.x), std::abs(obstacleObject.scale.z));
			const float safeRadius = obstacleComponent.colliderRadius * maxScale + agentRadius;
			return DistanceXZ(position, obstacleCenter) <= safeRadius;
		}

		const float halfX = std::abs(obstacleComponent.colliderSize.x * obstacleObject.scale.x) * 0.5f + agentRadius;
		const float halfZ = std::abs(obstacleComponent.colliderSize.z * obstacleObject.scale.z) * 0.5f + agentRadius;
		const Vector3 toPosition = MakeHorizontalVector(obstacleCenter, position);
		return std::abs(toPosition.x) <= halfX && std::abs(toPosition.z) <= halfZ;
	}

#pragma warning(push)
#pragma warning(disable : 4820)
	class AiGridGraph final : public micropather::Graph {
	public:
		AiGridGraph(
			const EditorScene& editorScene,
			int32_t ownerGameObjectId,
			int32_t targetGameObjectId,
			const Vector3& origin,
			int32_t width,
			int32_t height,
			float cellSize,
			float agentRadius)
			: editorScene_(editorScene),
			  ownerGameObjectId_(ownerGameObjectId),
			  targetGameObjectId_(targetGameObjectId),
			  origin_(origin),
			  width_(width),
			  height_(height),
			  cellSize_(cellSize),
			  agentRadius_(agentRadius),
			  blockedCells_(static_cast<size_t>(width * height), false) {
			BuildBlockedCells();
		}
		~AiGridGraph() override = default;  // MicroPather から Graph として破棄される。
		AiGridGraph(const AiGridGraph&) = delete;  // Scene 参照と Blocked Cell を二重保持しない。
		AiGridGraph& operator=(const AiGridGraph&) = delete;  // Scene 参照と Blocked Cell を二重保持しない。
		AiGridGraph(AiGridGraph&&) = delete;  // MicroPather が参照する Graph の実体を動かさない。
		AiGridGraph& operator=(AiGridGraph&&) = delete;  // MicroPather が参照する Graph の実体を動かさない。

		float LeastCostEstimate(void* stateStart, void* stateEnd) override {
			const int32_t startIndex = StateToIndex(stateStart);
			const int32_t endIndex = StateToIndex(stateEnd);
			const int32_t startX = startIndex % width_;
			const int32_t startZ = startIndex / width_;
			const int32_t endX = endIndex % width_;
			const int32_t endZ = endIndex / width_;
			const float diffX = static_cast<float>(startX - endX);
			const float diffZ = static_cast<float>(startZ - endZ);
			return std::sqrt(diffX * diffX + diffZ * diffZ);
		}

		void AdjacentCost(void* state, MP_VECTOR<micropather::StateCost>* adjacent) override {
			const int32_t stateIndex = StateToIndex(state);
			const int32_t currentX = stateIndex % width_;
			const int32_t currentZ = stateIndex / width_;

			for (int32_t offsetZ = -1; offsetZ <= 1; ++offsetZ) {
				for (int32_t offsetX = -1; offsetX <= 1; ++offsetX) {
					if (offsetX == 0 && offsetZ == 0) {
						continue;
					}

					const int32_t nextX = currentX + offsetX;
					const int32_t nextZ = currentZ + offsetZ;
					if (nextX < 0 || nextZ < 0 || nextX >= width_ || nextZ >= height_) {
						continue;
					}

					const int32_t nextIndex = MakeIndex(nextX, nextZ);
					if (blockedCells_[static_cast<size_t>(nextIndex)]) {
						continue;
					}

					micropather::StateCost cost{};
					cost.state = IndexToState(nextIndex);
					cost.cost = offsetX != 0 && offsetZ != 0 ? 1.41421356f : 1.0f;
					adjacent->push_back(cost);
				}
			}
		}

		void PrintStateInfo(void* state) override {
			static_cast<void>(state);
		}

		void* MakeStateFromWorld(const Vector3& position) const {
			int32_t cellX = static_cast<int32_t>(std::round((position.x - origin_.x) / cellSize_));
			int32_t cellZ = static_cast<int32_t>(std::round((position.z - origin_.z) / cellSize_));
			cellX = (std::clamp)(cellX, 0, width_ - 1);
			cellZ = (std::clamp)(cellZ, 0, height_ - 1);
			return IndexToState(MakeIndex(cellX, cellZ));
		}

		Vector3 MakeWorldFromState(void* state) const {
			const int32_t stateIndex = StateToIndex(state);
			const int32_t cellX = stateIndex % width_;
			const int32_t cellZ = stateIndex / width_;
			return {
				origin_.x + static_cast<float>(cellX) * cellSize_,
				origin_.y,
				origin_.z + static_cast<float>(cellZ) * cellSize_};
		}

		void ClearBlockedForState(void* state) {
			const int32_t stateIndex = StateToIndex(state);
			if (stateIndex >= 0 && stateIndex < static_cast<int32_t>(blockedCells_.size())) {
				blockedCells_[static_cast<size_t>(stateIndex)] = false;
			}
		}

	private:
		const EditorScene& editorScene_;
		int32_t ownerGameObjectId_;
		int32_t targetGameObjectId_;
		Vector3 origin_;
		int32_t width_;
		int32_t height_;
		float cellSize_;
		float agentRadius_;
		std::vector<bool> blockedCells_;

		int32_t MakeIndex(int32_t cellX, int32_t cellZ) const {
			return cellZ * width_ + cellX;
		}

		void* IndexToState(int32_t index) const {
			return reinterpret_cast<void*>(static_cast<intptr_t>(index + 1));
		}

		int32_t StateToIndex(void* state) const {
			return static_cast<int32_t>(reinterpret_cast<intptr_t>(state)) - 1;
		}

		void BuildBlockedCells() {
			for (int32_t cellZ = 0; cellZ < height_; ++cellZ) {
				for (int32_t cellX = 0; cellX < width_; ++cellX) {
					const Vector3 worldPosition = {
						origin_.x + static_cast<float>(cellX) * cellSize_,
						origin_.y,
						origin_.z + static_cast<float>(cellZ) * cellSize_};
					blockedCells_[static_cast<size_t>(MakeIndex(cellX, cellZ))] = IsWorldPositionBlocked(worldPosition);
				}
			}
		}

		bool IsWorldPositionBlocked(const Vector3& position) const {
			for (const EditorGameObject& gameObject : editorScene_.GetGameObjects()) {
				if (!gameObject.isActive ||
					gameObject.id == ownerGameObjectId_ ||
					gameObject.id == targetGameObjectId_) {
					continue;
				}

				for (const EditorComponent& component : gameObject.components) {
					if (!IsBlockingCollider(component)) {
						continue;
					}

					if (IsInsideObstacleXZ(position, gameObject, component, agentRadius_)) {
						return true;
					}
				}
			}

			return false;
		}
	};
#pragma warning(pop)
}

void EditorAIManager::Initialize(EditorScene* editorScene, EditorPhysicsManager* physicsManager, std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;  // Play 中の AI が読む Scene。
	physicsManager_ = physicsManager;  // Rigidbody 付き AI を動かす物理 API。
	consoleMessages_ = consoleMessages;  // AI の開始状態と検知ログの出力先。
}

void EditorAIManager::Start() {
	agentVelocities_.clear();
	patrolOrigins_.clear();
	agentTimers_.clear();
	pythonTimers_.clear();
	pythonWarningLogged_.clear();
	agentStates_.clear();
	visibleTargets_.clear();
	sensorResults_.clear();
	sensorPreviousPositions_.clear();
	isStarted_ = true;
	PushConsoleMessage("AI: ThirdParty/AI の AI Component を開始しました。");
}

void EditorAIManager::Update(float deltaTime) {
	if (!isStarted_ || editorScene_ == nullptr || deltaTime <= 0.0f) {
		return;
	}

	for (EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		if (!gameObject.isActive) {
			continue;
		}

		for (EditorComponent& component : gameObject.components) {
			if (!component.isActive) {
				continue;
			}

			if (IsAiAgentType(component.type)) {
				UpdateAgent(gameObject, component, deltaTime);
			}
			else if (IsAiSensorType(component.type)) {
				UpdateVisionSensor(gameObject, component, deltaTime);
			}
		}
	}
}

void EditorAIManager::Draw() {
}

void EditorAIManager::Stop() {
	agentVelocities_.clear();
	patrolOrigins_.clear();
	agentTimers_.clear();
	pythonTimers_.clear();
	pythonWarningLogged_.clear();
	agentStates_.clear();
	visibleTargets_.clear();
	sensorResults_.clear();
	sensorPreviousPositions_.clear();
	isStarted_ = false;
}

bool EditorAIManager::IsSensorDetected(int32_t gameObjectId) const {
	for (const auto& sensorEntry : sensorResults_) {
		const int32_t entryGameObjectId = static_cast<int32_t>(static_cast<uint64_t>(sensorEntry.first) >> 32);
		if (entryGameObjectId == gameObjectId && sensorEntry.second.isDetected) {
			return true;
		}
	}

	return false;
}

bool EditorAIManager::TryGetSensorResult(
	int32_t gameObjectId,
	EditorComponentType sensorComponentType,
	EditorAiSensorResult& sensorResult) const {
	const int64_t sensorKey = MakeSensorKey(gameObjectId, sensorComponentType);
	const auto sensorIterator = sensorResults_.find(sensorKey);
	if (sensorIterator == sensorResults_.end()) {
		return false;
	}

	sensorResult = sensorIterator->second;
	return true;
}

void EditorAIManager::UpdateAgent(EditorGameObject& gameObject, EditorComponent& aiComponent, float deltaTime) {
	const EditorGameObject* targetGameObject = nullptr;
	if (aiComponent.connectedGameObjectId != kInvalidGameObjectId) {
		targetGameObject = editorScene_->FindGameObject(aiComponent.connectedGameObjectId);
		if (targetGameObject != nullptr && !targetGameObject->isActive) {
			targetGameObject = nullptr;
		}
	}

	Vector3 desiredDirection = MakeDesiredDirection(gameObject, aiComponent, targetGameObject, deltaTime);
	Vector3 pythonDirection{};
	if (TryRunPythonDirection(gameObject, aiComponent, targetGameObject, deltaTime, pythonDirection)) {
		desiredDirection = pythonDirection;
	}

	MoveAgent(gameObject, aiComponent, desiredDirection, deltaTime);
}

void EditorAIManager::UpdateVisionSensor(const EditorGameObject& gameObject, EditorComponent& sensorComponent, float deltaTime) {
	const EditorGameObject* pythonTargetGameObject =
		sensorComponent.connectedGameObjectId != kInvalidGameObjectId ? editorScene_->FindGameObject(sensorComponent.connectedGameObjectId) : nullptr;
	const int64_t sensorKey = MakeSensorKey(gameObject.id, sensorComponent.type);
	EditorAiSensorResult sensorResult = MakeBaseSensorResult(gameObject, sensorComponent, pythonTargetGameObject);

	if (TryRunPythonSensor(gameObject, sensorComponent, pythonTargetGameObject, deltaTime, sensorResult)) {
		const bool wasDetected = sensorResults_[sensorKey].isDetected;
		sensorResults_[sensorKey] = sensorResult;
		visibleTargets_[gameObject.id] = sensorResult.isDetected;
		if (sensorResult.isDetected && !wasDetected) {
			PushConsoleMessage("AI: " + gameObject.name + " の Python センサーが検知しました。");
		}
		return;
	}

	if (sensorComponent.connectedGameObjectId == kInvalidGameObjectId || editorScene_ == nullptr) {
		sensorResults_[sensorKey] = sensorResult;
		visibleTargets_[gameObject.id] = false;
		return;
	}

	const EditorGameObject* targetGameObject = editorScene_->FindGameObject(sensorComponent.connectedGameObjectId);
	if (targetGameObject == nullptr || !targetGameObject->isActive) {
		sensorResults_[sensorKey] = sensorResult;
		visibleTargets_[gameObject.id] = false;
		return;
	}

	const Vector3 toTarget = MakeHorizontalVector(gameObject.translate, targetGameObject->translate);
	const float distance = LengthXZ(toTarget);
	const float viewDistance = (std::max)(sensorComponent.colliderRadius, 0.0f);
	const Vector3 direction = NormalizeXZ(toTarget);
	bool isDetected = distance <= viewDistance;

	if (sensorComponent.type == EditorComponentType::AIVisionSensor &&
		isDetected &&
		sensorComponent.colliderSize.x > 0.0f &&
		sensorComponent.colliderSize.x < 360.0f) {
		const Vector3 forward = NormalizeXZ(MakeForwardFromYaw(gameObject.rotate.y));
		const float cosine = (std::clamp)(DotXZ(forward, direction), -1.0f, 1.0f);
		const float angle = std::acos(cosine);
		isDetected = angle <= ToRadians(sensorComponent.colliderSize.x * 0.5f);
		sensorResult.confidence *= Clamp01(1.0f - angle / ToRadians((std::max)(sensorComponent.colliderSize.x, 1.0f) * 0.5f));
	}

	if (sensorComponent.type == EditorComponentType::AIMotionSensor) {
		const auto previousPositionIterator = sensorPreviousPositions_.find(sensorKey);
		const bool hasPreviousPosition = previousPositionIterator != sensorPreviousPositions_.end();
		const Vector3 previousPosition = hasPreviousPosition ? previousPositionIterator->second : targetGameObject->translate;
		const Vector3 motionVector = {
			targetGameObject->translate.x - previousPosition.x,
			0.0f,
			targetGameObject->translate.z - previousPosition.z};
		const float motionMagnitude = hasPreviousPosition ? LengthXZ(motionVector) / (std::max)(deltaTime, kMinimumDistance) : 0.0f;
		const float motionThreshold = sensorComponent.colliderSize.y > 0.0f ? sensorComponent.colliderSize.y : 0.01f;

		sensorResult.hasDetails = true;
		sensorResult.motionX = hasPreviousPosition ? motionVector.x / (std::max)(deltaTime, kMinimumDistance) : 0.0f;
		sensorResult.motionY = hasPreviousPosition ? motionVector.z / (std::max)(deltaTime, kMinimumDistance) : 0.0f;
		sensorResult.motionMagnitude = motionMagnitude;
		isDetected = distance <= viewDistance && motionMagnitude >= motionThreshold;
		sensorPreviousPositions_[sensorKey] = targetGameObject->translate;
	}
	else if (sensorComponent.type == EditorComponentType::AIOpenCvObjectDetector) {
		sensorResult.hasDetails = true;
		sensorResult.label = targetGameObject->name;
	}
	else if (sensorComponent.type == EditorComponentType::AIOpenCvColorTracker) {
		sensorResult.hasDetails = true;
		sensorResult.label = targetGameObject->name;
		sensorResult.screenX = direction.x;
		sensorResult.screenY = direction.z;
	}
	else if (sensorComponent.type == EditorComponentType::AIWhisperSpeechRecognizer ||
	         sensorComponent.type == EditorComponentType::AIVoiceCommand) {
		isDetected = false;
		sensorResult.confidence = 0.0f;
	}

	sensorResult.isDetected = isDetected;
	sensorResult.detectedGameObjectId = isDetected ? targetGameObject->id : kInvalidGameObjectId;
	sensorResult.distance = distance;
	sensorResult.direction = direction;
	sensorResult.confidence = isDetected ? (std::max)(sensorResult.confidence, MakeDistanceConfidence(distance, viewDistance)) : 0.0f;

	const bool wasDetected = sensorResults_[sensorKey].isDetected;
	sensorResults_[sensorKey] = sensorResult;
	visibleTargets_[gameObject.id] = isDetected;
	if (isDetected && !wasDetected) {
		PushConsoleMessage("AI: " + gameObject.name + " が " + targetGameObject->name + " を検知しました。");
	}
}

Vector3 EditorAIManager::MakeDesiredDirection(
	const EditorGameObject& gameObject,
	const EditorComponent& aiComponent,
	const EditorGameObject* targetGameObject,
	float deltaTime) {
	if (aiComponent.type == EditorComponentType::AIBehaviorTree) {
		return MakeBehaviorTreeDirection(gameObject, aiComponent, targetGameObject, deltaTime);
	}

	if (aiComponent.type == EditorComponentType::AIStateMachine) {
		return MakeStateMachineDirection(gameObject, aiComponent, targetGameObject, deltaTime);
	}

	if (aiComponent.type == EditorComponentType::AIGoapPlanner) {
		return MakeGoapDirection(gameObject, aiComponent, targetGameObject);
	}

	if (aiComponent.type == EditorComponentType::AIHtnPlanner) {
		return MakeHtnDirection(gameObject, aiComponent, targetGameObject);
	}

	if (aiComponent.type == EditorComponentType::AIPathfindingAgent ||
		aiComponent.type == EditorComponentType::AIRecastCrowdAgent) {
		return MakePathfindingDirection(gameObject, aiComponent, targetGameObject);
	}

	if (aiComponent.type == EditorComponentType::AISteeringAgent ||
		aiComponent.type == EditorComponentType::AISeekSteering ||
		aiComponent.type == EditorComponentType::AIFleeSteering ||
		aiComponent.type == EditorComponentType::AIArriveSteering ||
		aiComponent.type == EditorComponentType::AIPursuitSteering ||
		aiComponent.type == EditorComponentType::AIWanderSteering ||
		aiComponent.type == EditorComponentType::AIObstacleAvoidanceSteering ||
		aiComponent.type == EditorComponentType::AIFlockSteering) {
		return MakeSteeringDirection(gameObject, aiComponent, targetGameObject, deltaTime);
	}

	return MakeBehaviorModeDirection(gameObject, aiComponent, targetGameObject, deltaTime);
}

Vector3 EditorAIManager::MakeBehaviorTreeDirection(
	const EditorGameObject& gameObject,
	const EditorComponent& aiComponent,
	const EditorGameObject* targetGameObject,
	float deltaTime) {
	Vector3 treeDirection{};  // BehaviorTree.CPP の Action ノードが最終的に出す移動方向。
	bool treeActionSelected = false;  // Tree が何かしらの行動を選んだかどうか。
	const bool sensorCanSeeTarget = visibleTargets_[gameObject.id];  // 視界センサーの前回結果。

	BT::BehaviorTreeFactory factory;
	factory.registerSimpleCondition(
		"HasTarget",
		[&targetGameObject](BT::TreeNode& treeNode) {
			static_cast<void>(treeNode);
			return targetGameObject != nullptr ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
		});
	factory.registerSimpleCondition(
		"CanSeeTarget",
		[&gameObject, &aiComponent, targetGameObject, sensorCanSeeTarget](BT::TreeNode& treeNode) {
			static_cast<void>(treeNode);
			if (targetGameObject == nullptr) {
				return BT::NodeStatus::FAILURE;
			}

			const float distance = DistanceXZ(gameObject.translate, targetGameObject->translate);
			const float sightRange = (std::max)(aiComponent.colliderRadius, aiComponent.navStoppingDistance);
			const bool canSeeByDistance = sightRange > 0.0f && distance <= sightRange;
			return sensorCanSeeTarget || canSeeByDistance ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
		});
	factory.registerSimpleAction(
		"MoveToTarget",
		[this, &gameObject, &aiComponent, targetGameObject, &treeDirection, &treeActionSelected](BT::TreeNode& treeNode) {
			static_cast<void>(treeNode);
			if (targetGameObject == nullptr) {
				return BT::NodeStatus::FAILURE;
			}

			treeDirection = MakePathfindingDirection(gameObject, aiComponent, targetGameObject);
			treeActionSelected = true;
			return BT::NodeStatus::SUCCESS;
		});
	factory.registerSimpleAction(
		"SeekTarget",
		[&gameObject, targetGameObject, &treeDirection, &treeActionSelected](BT::TreeNode& treeNode) {
			static_cast<void>(treeNode);
			if (targetGameObject == nullptr) {
				return BT::NodeStatus::FAILURE;
			}

			treeDirection = NormalizeXZ(MakeHorizontalVector(gameObject.translate, targetGameObject->translate));
			treeActionSelected = true;
			return BT::NodeStatus::SUCCESS;
		});
	factory.registerSimpleAction(
		"FleeTarget",
		[&gameObject, targetGameObject, &treeDirection, &treeActionSelected](BT::TreeNode& treeNode) {
			static_cast<void>(treeNode);
			if (targetGameObject == nullptr) {
				return BT::NodeStatus::FAILURE;
			}

			treeDirection = MultiplyXZ(NormalizeXZ(MakeHorizontalVector(gameObject.translate, targetGameObject->translate)), -1.0f);
			treeActionSelected = true;
			return BT::NodeStatus::SUCCESS;
		});
	factory.registerSimpleAction(
		"Patrol",
		[this, &gameObject, &aiComponent, targetGameObject, deltaTime, &treeDirection, &treeActionSelected](BT::TreeNode& treeNode) {
			static_cast<void>(treeNode);
			EditorComponent patrolComponent = aiComponent;
			patrolComponent.inputBehavior = 2;
			treeDirection = MakeBehaviorModeDirection(gameObject, patrolComponent, targetGameObject, deltaTime);
			treeActionSelected = true;
			return BT::NodeStatus::SUCCESS;
		});
	factory.registerSimpleAction(
		"Stop",
		[&treeDirection, &treeActionSelected](BT::TreeNode& treeNode) {
			static_cast<void>(treeNode);
			treeDirection = {0.0f, 0.0f, 0.0f};
			treeActionSelected = true;
			return BT::NodeStatus::SUCCESS;
		});

	try {
		BT::Tree tree =
			EditorAssetUtility::HasExtension(aiComponent.assetPath, ".xml") && std::filesystem::exists(aiComponent.assetPath)
				? factory.createTreeFromFile(aiComponent.assetPath)
				: factory.createTreeFromText(kDefaultBehaviorTreeXml);
		tree.tickOnce();
	}
	catch (const std::exception&) {
		treeActionSelected = false;
	}

	if (treeActionSelected) {
		return treeDirection;
	}

	EditorComponent patrolComponent = aiComponent;
	patrolComponent.inputBehavior = 2;
	return MakeBehaviorModeDirection(gameObject, patrolComponent, targetGameObject, deltaTime);
}

Vector3 EditorAIManager::MakeStateMachineDirection(
	const EditorGameObject& gameObject,
	const EditorComponent& aiComponent,
	const EditorGameObject* targetGameObject,
	float deltaTime) {
	int32_t& currentState = agentStates_[gameObject.id];  // 0: 巡回 / 1: 追跡 / 2: 射程内停止。
	if (targetGameObject == nullptr) {
		currentState = 0;
	}
	else {
		const float distance = DistanceXZ(gameObject.translate, targetGameObject->translate);
		if (distance <= (std::max)(aiComponent.navStoppingDistance, 0.0f)) {
			currentState = 2;
		}
		else if (distance <= (std::max)(aiComponent.colliderRadius, aiComponent.navStoppingDistance + 0.01f)) {
			currentState = 1;
		}
		else {
			currentState = 0;
		}
	}

	if (currentState == 2) {
		return {0.0f, 0.0f, 0.0f};
	}

	if (currentState == 1) {
		return MakePathfindingDirection(gameObject, aiComponent, targetGameObject);
	}

	EditorComponent patrolComponent = aiComponent;
	patrolComponent.inputBehavior = 2;
	return MakeBehaviorModeDirection(gameObject, patrolComponent, targetGameObject, deltaTime);
}

Vector3 EditorAIManager::MakeGoapDirection(
	const EditorGameObject& gameObject,
	const EditorComponent& aiComponent,
	const EditorGameObject* targetGameObject) {
	if (targetGameObject == nullptr) {
		return {0.0f, 0.0f, 0.0f};
	}

	const float distance = DistanceXZ(gameObject.translate, targetGameObject->translate);
	goap::WorldState startState("CG2_Start");
	startState.setVariable(kGoapHasTarget, true);
	startState.setVariable(kGoapHasPath, true);
	startState.setVariable(kGoapInRange, distance <= (std::max)(aiComponent.navStoppingDistance, 0.0f));

	goap::WorldState goalState("CG2_Goal");
	goalState.setVariable(kGoapInRange, true);

	std::vector<goap::Action> actions;
	goap::Action findPathAction("FindPath", 1);
	findPathAction.setPrecondition(kGoapHasTarget, true);
	findPathAction.setEffect(kGoapHasPath, true);
	actions.push_back(findPathAction);

	goap::Action moveToTargetAction("MoveToTarget", 2);
	moveToTargetAction.setPrecondition(kGoapHasTarget, true);
	moveToTargetAction.setPrecondition(kGoapHasPath, true);
	moveToTargetAction.setEffect(kGoapInRange, true);
	actions.push_back(moveToTargetAction);

	try {
		goap::Planner planner;
		const std::vector<goap::Action> plan = planner.plan(startState, goalState, actions);
		if (plan.empty()) {
			return {0.0f, 0.0f, 0.0f};
		}

		const goap::Action& nextAction = plan.back();
		if (nextAction.name() == "MoveToTarget" || nextAction.name() == "FindPath") {
			return MakePathfindingDirection(gameObject, aiComponent, targetGameObject);
		}
	}
	catch (const std::runtime_error&) {
		return MakePathfindingDirection(gameObject, aiComponent, targetGameObject);
	}

	return {0.0f, 0.0f, 0.0f};
}

Vector3 EditorAIManager::MakeHtnDirection(
	const EditorGameObject& gameObject,
	const EditorComponent& aiComponent,
	const EditorGameObject* targetGameObject) {
	if (targetGameObject == nullptr) {
		return {0.0f, 0.0f, 0.0f};
	}

	const float distance = DistanceXZ(gameObject.translate, targetGameObject->translate);
	if (distance <= (std::max)(aiComponent.navStoppingDistance, 0.0f)) {
		agentStates_[gameObject.id] = 2;  // HTN の末端タスク: 待機。
		return {0.0f, 0.0f, 0.0f};
	}

	agentStates_[gameObject.id] = 1;  // HTN の末端タスク: 経路移動。
	return MakePathfindingDirection(gameObject, aiComponent, targetGameObject);
}

Vector3 EditorAIManager::MakePathfindingDirection(
	const EditorGameObject& gameObject,
	const EditorComponent& aiComponent,
	const EditorGameObject* targetGameObject) {
	if (targetGameObject == nullptr || editorScene_ == nullptr) {
		return {0.0f, 0.0f, 0.0f};
	}

	const float distance = DistanceXZ(gameObject.translate, targetGameObject->translate);
	if (distance <= (std::max)(aiComponent.navStoppingDistance, 0.0f)) {
		return {0.0f, 0.0f, 0.0f};
	}

	const float agentRadius = (std::max)(aiComponent.navAgentRadius, 0.1f);
	const float cellSize = (std::max)(agentRadius * 2.0f, 0.5f);
	float agentPositionDetour[3] = {gameObject.translate.x, gameObject.translate.y, gameObject.translate.z};  // Detour は float[3] で座標を扱う。
	float targetPositionDetour[3] = {targetGameObject->translate.x, targetGameObject->translate.y, targetGameObject->translate.z};  // 経路探索先。
	const float detourDistance = dtVdist2D(agentPositionDetour, targetPositionDetour);  // RecastNavigation / Detour の 2D 距離。
	const float searchExtent = (std::clamp)(detourDistance + 8.0f, 12.0f, 48.0f);
	const int32_t gridSize = (std::clamp)(static_cast<int32_t>(std::ceil(searchExtent / cellSize)) * 2 + 1, 15, 81);
	const Vector3 center = {
		(gameObject.translate.x + targetGameObject->translate.x) * 0.5f,
		gameObject.translate.y,
		(gameObject.translate.z + targetGameObject->translate.z) * 0.5f};
	const Vector3 origin = {
		center.x - static_cast<float>(gridSize / 2) * cellSize,
		center.y,
		center.z - static_cast<float>(gridSize / 2) * cellSize};

	AiGridGraph graph(
		*editorScene_,
		gameObject.id,
		targetGameObject->id,
		origin,
		gridSize,
		gridSize,
		cellSize,
		agentRadius);
	void* startState = graph.MakeStateFromWorld(gameObject.translate);
	void* goalState = graph.MakeStateFromWorld(targetGameObject->translate);
	graph.ClearBlockedForState(startState);
	graph.ClearBlockedForState(goalState);

	micropather::MicroPather pathfinder(&graph, static_cast<uint32_t>(gridSize * gridSize), 8, false);
	MP_VECTOR<void*> path;
	float totalCost = 0.0f;
	const int32_t solveResult = pathfinder.Solve(startState, goalState, &path, &totalCost);
	static_cast<void>(totalCost);

	if (solveResult != micropather::MicroPather::SOLVED || path.size() <= 1U) {
		return NormalizeXZ(MakeHorizontalVector(gameObject.translate, targetGameObject->translate));
	}

	const Vector3 nextPathPosition = graph.MakeWorldFromState(path[1]);
	return NormalizeXZ(MakeHorizontalVector(gameObject.translate, nextPathPosition));
}

Vector3 EditorAIManager::MakeSteeringDirection(
	const EditorGameObject& gameObject,
	const EditorComponent& aiComponent,
	const EditorGameObject* targetGameObject,
	float deltaTime) {
	if (aiComponent.type == EditorComponentType::AIFlockSteering) {
		return MakeFlockDirection(gameObject, aiComponent);
	}

	if (aiComponent.type == EditorComponentType::AIWanderSteering) {
		EditorComponent wanderComponent = aiComponent;
		wanderComponent.inputBehavior = 2;
		return MakeBehaviorModeDirection(gameObject, wanderComponent, targetGameObject, deltaTime);
	}

	if (targetGameObject == nullptr) {
		return {0.0f, 0.0f, 0.0f};
	}

	const Vector3 toTarget = MakeHorizontalVector(gameObject.translate, targetGameObject->translate);
	if (aiComponent.type == EditorComponentType::AIFleeSteering) {
		const OpenSteer::Vec3 steer = ToOpenSteerVector(toTarget) * -1.0f;  // OpenSteer の Vec3 で逃走方向を作る。
		return NormalizeXZ(FromOpenSteerVector(steer));
	}

	if (aiComponent.type == EditorComponentType::AIArriveSteering) {
		const float distance = LengthXZ(toTarget);
		const float slowDistance = (std::max)(aiComponent.colliderRadius, aiComponent.navStoppingDistance + 0.01f);
		const float speedRate = (std::clamp)(distance / slowDistance, 0.0f, 1.0f);
		const OpenSteer::Vec3 arriveDirection = ToOpenSteerVector(NormalizeXZ(toTarget)) * speedRate;  // OpenSteer のベクトル演算で減速方向を作る。
		return FromOpenSteerVector(arriveDirection);
	}

	if (aiComponent.type == EditorComponentType::AIPursuitSteering) {
		const EditorComponent* targetRigidBody = EditorComponentUtility::FindComponent(*targetGameObject, EditorComponentType::RigidBody);
		const Vector3 targetVelocity = targetRigidBody != nullptr ? targetRigidBody->velocity : Vector3{0.0f, 0.0f, 0.0f};
		const float predictionTime = (std::min)(DistanceXZ(gameObject.translate, targetGameObject->translate) / (std::max)(aiComponent.navMaxSpeed, 0.1f), 1.5f);
		const Vector3 predictedPosition = AddVector3(targetGameObject->translate, MultiplyXZ(targetVelocity, predictionTime));
		const OpenSteer::Vec3 pursuitDirection = ToOpenSteerVector(MakeHorizontalVector(gameObject.translate, predictedPosition)).normalize();
		return FromOpenSteerVector(pursuitDirection);
	}

	return FromOpenSteerVector(ToOpenSteerVector(toTarget).normalize());
}

Vector3 EditorAIManager::MakeFlockDirection(const EditorGameObject& gameObject, const EditorComponent& aiComponent) const {
	if (editorScene_ == nullptr) {
		return {0.0f, 0.0f, 0.0f};
	}

	Vector3 separation{};
	Vector3 cohesionCenter{};
	int32_t neighborCount = 0;
	const float searchRadius = (std::max)(aiComponent.colliderRadius, 1.0f);

	for (const EditorGameObject& otherGameObject : editorScene_->GetGameObjects()) {
		if (!otherGameObject.isActive || otherGameObject.id == gameObject.id) {
			continue;
		}

		const EditorComponent* otherAi = nullptr;
		for (const EditorComponent& component : otherGameObject.components) {
			if (component.isActive && IsAiAgentType(component.type)) {
				otherAi = &component;
				break;
			}
		}

		if (otherAi == nullptr) {
			continue;
		}

		const float distance = DistanceXZ(gameObject.translate, otherGameObject.translate);
		if (distance > searchRadius || distance <= kMinimumDistance) {
			continue;
		}

		const Vector3 away = NormalizeXZ(MakeHorizontalVector(otherGameObject.translate, gameObject.translate));
		separation = AddVector3(separation, MultiplyXZ(away, 1.0f / distance));
		cohesionCenter = AddVector3(cohesionCenter, otherGameObject.translate);
		neighborCount++;
	}

	if (neighborCount <= 0) {
		return {0.0f, 0.0f, 0.0f};
	}

	const float inverseCount = 1.0f / static_cast<float>(neighborCount);
	cohesionCenter = MultiplyXZ(cohesionCenter, inverseCount);
	const Vector3 cohesion = NormalizeXZ(MakeHorizontalVector(gameObject.translate, cohesionCenter));
	return NormalizeXZ(AddVector3(MultiplyXZ(separation, 1.5f), cohesion));
}

Vector3 EditorAIManager::MakeBehaviorModeDirection(
	const EditorGameObject& gameObject,
	const EditorComponent& aiComponent,
	const EditorGameObject* targetGameObject,
	float deltaTime) {
	agentTimers_[gameObject.id] += deltaTime;

	if (aiComponent.inputBehavior == 2) {
		if (patrolOrigins_.find(gameObject.id) == patrolOrigins_.end()) {
			patrolOrigins_[gameObject.id] = gameObject.translate;
		}

		const Vector3 origin = patrolOrigins_[gameObject.id];
		const float time = agentTimers_[gameObject.id];
		const Vector3 patrolTarget = {
			origin.x + std::cos(time) * kPatrolRadius,
			origin.y,
			origin.z + std::sin(time) * kPatrolRadius};
		return NormalizeXZ(MakeHorizontalVector(gameObject.translate, patrolTarget));
	}

	if (aiComponent.inputBehavior == 3 || targetGameObject == nullptr) {
		return {0.0f, 0.0f, 0.0f};
	}

	Vector3 direction = NormalizeXZ(MakeHorizontalVector(gameObject.translate, targetGameObject->translate));
	if (aiComponent.inputBehavior == 1) {
		direction.x *= -1.0f;
		direction.z *= -1.0f;
	}

	return direction;
}

bool EditorAIManager::TryRunPythonDirection(
	const EditorGameObject& gameObject,
	const EditorComponent& aiComponent,
	const EditorGameObject* targetGameObject,
	float deltaTime,
	Vector3& direction) {
	if (!HasPythonAsset(aiComponent)) {
		return false;
	}

	const int32_t pythonTimerKey = gameObject.id * 1000 + static_cast<int32_t>(aiComponent.type);
	pythonTimers_[pythonTimerKey] += deltaTime;
	if (pythonTimers_[pythonTimerKey] < kPythonUpdateInterval) {
		return false;
	}

	pythonTimers_[pythonTimerKey] = 0.0f;

	const Vector3 targetPosition =
		targetGameObject != nullptr ? targetGameObject->translate : gameObject.translate;
	const std::string scriptPath = std::filesystem::path(aiComponent.assetPath).generic_string();
	const std::string arguments =
		QuoteCommandArgument(scriptPath) + " " +
		std::to_string(gameObject.translate.x) + " " +
		std::to_string(gameObject.translate.y) + " " +
		std::to_string(gameObject.translate.z) + " " +
		std::to_string(targetPosition.x) + " " +
		std::to_string(targetPosition.y) + " " +
		std::to_string(targetPosition.z) + " " +
		std::to_string(deltaTime) + " " +
		std::to_string(static_cast<int32_t>(aiComponent.type));

	std::string outputLine;
	if (!TryRunCommandAndReadFirstLine("python " + arguments, outputLine) &&
		!TryRunCommandAndReadFirstLine("py -3 " + arguments, outputLine)) {
		if (!pythonWarningLogged_[gameObject.id]) {
			PushConsoleMessage("AI Python: " + scriptPath + " を実行できませんでした。python または py -3 を確認してください。");
			pythonWarningLogged_[gameObject.id] = true;
		}
		return false;
	}

	if (!TryParseDirectionText(outputLine, direction)) {
		if (!pythonWarningLogged_[gameObject.id]) {
			PushConsoleMessage("AI Python: " + scriptPath + " の出力は \"x y z\" の形式にしてください。");
			pythonWarningLogged_[gameObject.id] = true;
		}
		return false;
	}

	pythonWarningLogged_[gameObject.id] = false;
	return true;
}

bool EditorAIManager::TryRunPythonSensor(
	const EditorGameObject& gameObject,
	const EditorComponent& sensorComponent,
	const EditorGameObject* targetGameObject,
	float deltaTime,
	EditorAiSensorResult& sensorResult) {
	if (!HasPythonAsset(sensorComponent)) {
		return false;
	}

	const int32_t pythonTimerKey = gameObject.id * 1000 + static_cast<int32_t>(sensorComponent.type);
	pythonTimers_[pythonTimerKey] += deltaTime;
	if (pythonTimers_[pythonTimerKey] < kPythonUpdateInterval) {
		const int64_t sensorKey = MakeSensorKey(gameObject.id, sensorComponent.type);
		const auto sensorIterator = sensorResults_.find(sensorKey);
		if (sensorIterator != sensorResults_.end()) {
			sensorResult = sensorIterator->second;
			return true;
		}
	}

	pythonTimers_[pythonTimerKey] = 0.0f;

	const Vector3 targetPosition =
		targetGameObject != nullptr ? targetGameObject->translate : gameObject.translate;
	const std::string scriptPath = std::filesystem::path(sensorComponent.assetPath).generic_string();
	const std::string arguments =
		QuoteCommandArgument(scriptPath) + " " +
		std::to_string(gameObject.translate.x) + " " +
		std::to_string(gameObject.translate.y) + " " +
		std::to_string(gameObject.translate.z) + " " +
		std::to_string(targetPosition.x) + " " +
		std::to_string(targetPosition.y) + " " +
		std::to_string(targetPosition.z) + " " +
		std::to_string(sensorComponent.colliderRadius) + " " +
		std::to_string(static_cast<int32_t>(sensorComponent.type));

	std::string outputLine;
	if (!TryRunCommandAndReadFirstLine("python " + arguments, outputLine) &&
		!TryRunCommandAndReadFirstLine("py -3 " + arguments, outputLine)) {
		if (!pythonWarningLogged_[gameObject.id]) {
			PushConsoleMessage("AI Python: " + scriptPath + " を実行できませんでした。python または py -3 を確認してください。");
			pythonWarningLogged_[gameObject.id] = true;
		}
		return false;
	}

	if (!TryParseSensorOutput(outputLine, sensorResult)) {
		if (!pythonWarningLogged_[gameObject.id]) {
			PushConsoleMessage("AI Python: " + scriptPath + " のセンサー出力は 0/1 または 1|label=...|confidence=... の形式にしてください。");
			pythonWarningLogged_[gameObject.id] = true;
		}
		return false;
	}

	if (targetGameObject != nullptr && sensorResult.detectedGameObjectId == kInvalidGameObjectId && sensorResult.isDetected) {
		sensorResult.detectedGameObjectId = targetGameObject->id;
	}

	if (sensorResult.confidence <= 0.0f && sensorResult.isDetected) {
		sensorResult.confidence = 1.0f;
	}

	pythonWarningLogged_[gameObject.id] = false;
	return true;
}

void EditorAIManager::MoveAgent(EditorGameObject& gameObject, EditorComponent& aiComponent, const Vector3& desiredDirection, float deltaTime) {
	const float maxSpeed = (std::max)(aiComponent.navMaxSpeed, 0.0f);
	const float maxAcceleration = (std::max)(aiComponent.navMaxAcceleration, 0.0f);
	const float directionSpeedRate = (std::clamp)(LengthXZ(desiredDirection), 0.0f, 1.0f);
	const Vector3 targetVelocity = MultiplyXZ(NormalizeXZ(desiredDirection), maxSpeed * directionSpeedRate);
	const Vector3 currentVelocity = agentVelocities_[gameObject.id];
	Vector3 nextVelocity = MoveTowardsVelocity(currentVelocity, targetVelocity, maxAcceleration * deltaTime);
	nextVelocity = AvoidPhysicsObstacle(gameObject.id, gameObject.translate, nextVelocity, aiComponent.navAgentRadius, deltaTime);
	agentVelocities_[gameObject.id] = nextVelocity;

	EditorComponent* rigidBody = EditorComponentUtility::FindComponent(gameObject, EditorComponentType::RigidBody);
	if (rigidBody != nullptr && rigidBody->isActive && !rigidBody->isKinematic && physicsManager_ != nullptr) {
		rigidBody->velocity.x = nextVelocity.x;
		rigidBody->velocity.z = nextVelocity.z;
		physicsManager_->SetVelocity(gameObject.id, rigidBody->velocity);
		return;
	}

	gameObject.translate = AddXZ(gameObject.translate, MultiplyXZ(nextVelocity, deltaTime));
}

Vector3 EditorAIManager::AvoidPhysicsObstacle(
	int32_t gameObjectId,
	const Vector3& currentPosition,
	const Vector3& desiredVelocity,
	float radius,
	float deltaTime) const {
	if (physicsManager_ == nullptr || LengthSquaredXZ(desiredVelocity) <= kMinimumDistance) {
		return desiredVelocity;
	}

	const float safeRadius = (std::max)(radius, 0.05f);
	const float travelDistance = LengthXZ(desiredVelocity) * deltaTime;
	if (travelDistance <= kMinimumDistance) {
		return desiredVelocity;
	}

	const Vector3 direction = NormalizeXZ(desiredVelocity);
	const Vector3 castOrigin = {
		currentPosition.x + direction.x * safeRadius,
		currentPosition.y + safeRadius,
		currentPosition.z + direction.z * safeRadius};
	EditorJoltPhysicsManager::PhysicsHit hit{};
	if (!physicsManager_->SphereCast(castOrigin, safeRadius, direction, travelDistance + safeRadius, hit)) {
		return desiredVelocity;
	}

	if (hit.gameObjectId == gameObjectId || hit.gameObjectId < 0) {
		return desiredVelocity;
	}

	Vector3 sideDirection = NormalizeXZ({-direction.z, 0.0f, direction.x});
	if (LengthSquaredXZ(sideDirection) <= kMinimumDistance) {
		return {0.0f, 0.0f, 0.0f};
	}

	return MultiplyXZ(sideDirection, LengthXZ(desiredVelocity) * 0.75f);
}

void EditorAIManager::PushConsoleMessage(const std::string& message) const {
	if (consoleMessages_ == nullptr) {
		return;
	}

	consoleMessages_->push_back(message);
}
