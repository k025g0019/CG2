#include "EditorSaveManager.h"

#include "EditorComponentUtility.h"
#include "EditorPhysicsManager.h"
#include "EditorScriptManager.h"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {
	constexpr std::array<unsigned char, 3> kUtf8Bom{{0xEFu, 0xBBu, 0xBFu}};
	constexpr int32_t kSaveVersion = 1;

	void ReplaceAll(std::string& text, const std::string& source, const std::string& replacement) {
		size_t searchPosition = 0U;

		while ((searchPosition = text.find(source, searchPosition)) != std::string::npos) {
			text.replace(searchPosition, source.size(), replacement);
			searchPosition += replacement.size();
		}
	}

	std::string EncodeToken(const std::string& text) {
		std::string encodedText = text;
		ReplaceAll(encodedText, "%", "%25");
		ReplaceAll(encodedText, "|", "%7C");
		ReplaceAll(encodedText, "\r", "%0D");
		ReplaceAll(encodedText, "\n", "%0A");
		return encodedText;
	}

	std::string DecodeToken(const std::string& text) {
		std::string decodedText = text;
		ReplaceAll(decodedText, "%0A", "\n");
		ReplaceAll(decodedText, "%0D", "\r");
		ReplaceAll(decodedText, "%7C", "|");
		ReplaceAll(decodedText, "%25", "%");
		return decodedText;
	}

	std::vector<std::string> SplitLine(const std::string& line) {
		std::vector<std::string> elements;
		std::stringstream stream(line);
		std::string element;

		while (std::getline(stream, element, '|')) {
			elements.push_back(element);
		}

		return elements;
	}

	EditorComponent* FindMutableComponent(EditorGameObject& gameObject, EditorComponentType type) {
		for (EditorComponent& component : gameObject.components) {
			if (component.type == type) {
				return &component;
			}
		}

		return nullptr;
	}
}

void EditorSaveManager::Initialize(
	EditorScene* editorScene,
	EditorPhysicsManager* physicsManager,
	EditorScriptManager* scriptManager,
	std::vector<std::string>* consoleMessages) {
	editorScene_ = editorScene;
	physicsManager_ = physicsManager;
	scriptManager_ = scriptManager;
	consoleMessages_ = consoleMessages;
}

void EditorSaveManager::Start() {
	if (editorScene_ == nullptr) {
		return;
	}

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* checkpointComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Checkpoint);

		if (!gameObject.isActive || checkpointComponent == nullptr || !checkpointComponent->isActive) {
			continue;
		}

		if (!initializedCheckpointIds_.insert(gameObject.id).second) {
			continue;
		}

		if (checkpointComponent->checkpointLoadOnStart &&
			HasSlot(checkpointComponent->checkpointSlotName)) {
			ActivateCheckpoint(gameObject.id, true);
		}
		else if (checkpointComponent->checkpointSaveOnStart) {
			ActivateCheckpoint(gameObject.id, false);
		}
	}
}

void EditorSaveManager::Stop() {
	// Scene置換とAdditive反映でも呼ばれるため、セッション値はここで消さない。
}

void EditorSaveManager::ResetSessionValues() {
	floatValues_.clear();
	stringValues_.clear();
	ResetSceneState();
}

void EditorSaveManager::ResetSceneState() {
	initializedCheckpointIds_.clear();
}

bool EditorSaveManager::SaveSlot(const std::string& slotName) {
	if (editorScene_ == nullptr || slotName.empty()) {
		return false;
	}

	const std::string slotPath = MakeSlotPath(slotName);
	const std::string temporarySlotPath = slotPath + ".tmp";
	std::error_code fileError;
	std::filesystem::create_directories(
		std::filesystem::path(slotPath).parent_path(),
		fileError);

	if (fileError) {
		PushConsoleMessage("Save: 保存フォルダーを作成できません " + slotName);
		return false;
	}

	std::ofstream file(temporarySlotPath, std::ios::binary | std::ios::trunc);

	if (!file.is_open()) {
		PushConsoleMessage("Save: 一時Slotを作成できません " + slotName);
		return false;
	}

	file.write(
		reinterpret_cast<const char*>(kUtf8Bom.data()),
		static_cast<std::streamsize>(kUtf8Bom.size()));
	file << std::setprecision(std::numeric_limits<float>::max_digits10);
	file << "Version|" << kSaveVersion << "\n";

	std::unordered_set<std::string> savedKeys;
	bool hasDuplicateKey = false;

	for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
		const EditorComponent* saveableComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Saveable);

		if (saveableComponent == nullptr || !saveableComponent->isActive) {
			continue;
		}

		const EditorComponent* healthComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Health);
		const EditorComponent* rigidBodyComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::RigidBody);
		const std::string saveableKey = ResolveSaveableKey(gameObject, *saveableComponent);
		if (!savedKeys.insert(saveableKey).second) {
			hasDuplicateKey = true;
			PushConsoleMessage("Save: Saveable Keyが重複しています " + saveableKey);
			continue;
		}

		file << "Object|"
		     << EncodeToken(saveableKey) << "|"
		     << (saveableComponent->saveableTransform ? 1 : 0) << "|"
		     << (saveableComponent->saveableActive ? 1 : 0) << "|"
		     << (saveableComponent->saveableHealth ? 1 : 0) << "|"
		     << (saveableComponent->saveableRigidbody ? 1 : 0) << "|"
		     << gameObject.translate.x << "|" << gameObject.translate.y << "|" << gameObject.translate.z << "|"
		     << gameObject.rotate.x << "|" << gameObject.rotate.y << "|" << gameObject.rotate.z << "|"
		     << gameObject.scale.x << "|" << gameObject.scale.y << "|" << gameObject.scale.z << "|"
		     << (gameObject.isActive ? 1 : 0) << "|"
		     << (healthComponent != nullptr ? healthComponent->healthCurrent : 0.0f) << "|"
		     << (healthComponent != nullptr ? healthComponent->healthMaximum : 0.0f) << "|"
		     << (rigidBodyComponent != nullptr ? rigidBodyComponent->velocity.x : 0.0f) << "|"
		     << (rigidBodyComponent != nullptr ? rigidBodyComponent->velocity.y : 0.0f) << "|"
		     << (rigidBodyComponent != nullptr ? rigidBodyComponent->velocity.z : 0.0f) << "|"
		     << (rigidBodyComponent != nullptr ? rigidBodyComponent->angularVelocity.x : 0.0f) << "|"
		     << (rigidBodyComponent != nullptr ? rigidBodyComponent->angularVelocity.y : 0.0f) << "|"
		     << (rigidBodyComponent != nullptr ? rigidBodyComponent->angularVelocity.z : 0.0f) << "\n";

		if (!saveableComponent->saveableScriptProperties) {
			continue;
		}

		for (const EditorComponent& component : gameObject.components) {
			if (component.type != EditorComponentType::Script &&
				component.type != EditorComponentType::MonoBehaviour) {
				continue;
			}

			for (const EditorScriptProperty& scriptProperty : component.scriptProperties) {
				file << "Script|"
				     << EncodeToken(saveableKey) << "|"
				     << static_cast<int32_t>(component.type) << "|"
				     << EncodeToken(scriptProperty.name) << "|"
				     << scriptProperty.type << "|"
				     << (scriptProperty.boolValue ? 1 : 0) << "|"
				     << scriptProperty.intValue << "|"
				     << scriptProperty.floatValue << "|"
				     << scriptProperty.vector2Value.x << "|" << scriptProperty.vector2Value.y << "|"
				     << scriptProperty.vector3Value.x << "|" << scriptProperty.vector3Value.y << "|" << scriptProperty.vector3Value.z << "|"
				     << EncodeToken(scriptProperty.stringValue) << "\n";
			}
		}
	}

	for (const auto& [key, value] : floatValues_) {
		file << "Float|" << EncodeToken(key) << "|" << value << "\n";
	}

	for (const auto& [key, value] : stringValues_) {
		file << "String|" << EncodeToken(key) << "|" << EncodeToken(value) << "\n";
	}

	const bool wasWritten = file.good() && !hasDuplicateKey;
	file.close();

	if (!wasWritten) {
		std::filesystem::remove(temporarySlotPath, fileError);
		PushConsoleMessage("Save: Slot保存に失敗 " + slotName);
		return false;
	}

	fileError.clear();
	std::filesystem::copy_file(
		temporarySlotPath,
		slotPath,
		std::filesystem::copy_options::overwrite_existing,
		fileError);
	std::error_code removeError;
	std::filesystem::remove(temporarySlotPath, removeError);

	if (fileError) {
		PushConsoleMessage("Save: Slot置換に失敗 " + slotName);
		return false;
	}

	PushConsoleMessage("Save: Slot保存 " + slotName);
	return true;
}

bool EditorSaveManager::LoadSlot(const std::string& slotName) {
	if (editorScene_ == nullptr || slotName.empty()) {
		return false;
	}

	std::ifstream file(MakeSlotPath(slotName), std::ios::binary);
	if (!file.is_open()) {
		return false;
	}

	// 破損SlotでSceneの一部だけを書き換えないよう、一時Sceneへ全件適用してからCommitする。
	EditorScene stagedScene = *editorScene_;
	std::unordered_map<std::string, EditorGameObject*> saveableObjects;
	for (EditorGameObject& gameObject : stagedScene.GetGameObjects()) {
		const EditorComponent* saveableComponent = EditorComponentUtility::FindComponent(
			gameObject,
			EditorComponentType::Saveable);

		if (saveableComponent != nullptr && saveableComponent->isActive) {
			const std::string saveableKey = ResolveSaveableKey(gameObject, *saveableComponent);
			if (!saveableObjects.emplace(saveableKey, &gameObject).second) {
				PushConsoleMessage("Save: Saveable Keyが重複しています " + saveableKey);
				return false;
			}
		}
	}

	std::string line;
	bool isFirstLine = true;
	bool hasReadVersion = false;
	bool hasSupportedVersion = false;
	std::unordered_set<std::string> loadedObjectKeys;
	std::unordered_map<std::string, float> stagedFloatValues;
	std::unordered_map<std::string, std::string> stagedStringValues;

	try {
		while (std::getline(file, line)) {
			if (isFirstLine && line.size() >= kUtf8Bom.size() &&
				static_cast<unsigned char>(line[0]) == kUtf8Bom[0] &&
				static_cast<unsigned char>(line[1]) == kUtf8Bom[1] &&
				static_cast<unsigned char>(line[2]) == kUtf8Bom[2]) {
				line.erase(0, kUtf8Bom.size());
			}

			isFirstLine = false;
			const std::vector<std::string> elements = SplitLine(line);
			if (elements.empty() || elements[0].empty()) {
				continue;
			}

			if (!hasReadVersion) {
				if (elements[0] != "Version" || elements.size() < 2U) {
					throw std::runtime_error("missing save version");
				}

				const int32_t loadedVersion = std::stoi(elements[1]);
				hasSupportedVersion = loadedVersion >= 1 && loadedVersion <= kSaveVersion;
				hasReadVersion = true;
				continue;
			}

			if (!hasSupportedVersion) {
				continue;
			}

			if (elements[0] == "Object") {
				if (elements.size() < 24U) {
					throw std::runtime_error("invalid object row");
				}

				const std::string saveableKey = DecodeToken(elements[1]);
				if (!loadedObjectKeys.insert(saveableKey).second) {
					throw std::runtime_error("duplicate object row");
				}

				const auto objectIterator = saveableObjects.find(saveableKey);
				if (objectIterator == saveableObjects.end()) {
					continue;
				}

				EditorGameObject& gameObject = *objectIterator->second;
				const bool restoresTransform = std::stoi(elements[2]) != 0;
				const bool restoresActive = std::stoi(elements[3]) != 0;
				const bool restoresHealth = std::stoi(elements[4]) != 0;
				const bool restoresRigidbody = std::stoi(elements[5]) != 0;

				if (restoresTransform) {
					gameObject.translate = {std::stof(elements[6]), std::stof(elements[7]), std::stof(elements[8])};
					gameObject.rotate = {std::stof(elements[9]), std::stof(elements[10]), std::stof(elements[11])};
					gameObject.scale = {std::stof(elements[12]), std::stof(elements[13]), std::stof(elements[14])};
				}

				if (restoresActive) {
					gameObject.isActive = std::stoi(elements[15]) != 0;
				}

				if (restoresHealth) {
					if (EditorComponent* healthComponent = FindMutableComponent(gameObject, EditorComponentType::Health)) {
						healthComponent->healthCurrent = std::stof(elements[16]);
						healthComponent->healthMaximum = (std::max)(std::stof(elements[17]), 0.0f);
					}
				}

				if (restoresRigidbody) {
					const Vector3 velocity{std::stof(elements[18]), std::stof(elements[19]), std::stof(elements[20])};
					const Vector3 angularVelocity{std::stof(elements[21]), std::stof(elements[22]), std::stof(elements[23])};
					if (EditorComponent* rigidBodyComponent = FindMutableComponent(gameObject, EditorComponentType::RigidBody)) {
						rigidBodyComponent->velocity = velocity;
						rigidBodyComponent->angularVelocity = angularVelocity;
					}
				}
			}
			else if (elements[0] == "Script") {
				if (elements.size() < 14U) {
					throw std::runtime_error("invalid script row");
				}

				const auto objectIterator = saveableObjects.find(DecodeToken(elements[1]));
				if (objectIterator == saveableObjects.end()) {
					continue;
				}

				EditorComponent* scriptComponent = FindMutableComponent(
					*objectIterator->second,
					ComponentTypeFromIndex(std::stoi(elements[2])));
				if (scriptComponent == nullptr) {
					continue;
				}

				const std::string propertyName = DecodeToken(elements[3]);
				for (EditorScriptProperty& scriptProperty : scriptComponent->scriptProperties) {
					if (scriptProperty.name != propertyName) {
						continue;
					}

					scriptProperty.type = std::stoi(elements[4]);
					scriptProperty.boolValue = std::stoi(elements[5]) != 0;
					scriptProperty.intValue = std::stoi(elements[6]);
					scriptProperty.floatValue = std::stof(elements[7]);
					scriptProperty.vector2Value = {std::stof(elements[8]), std::stof(elements[9])};
					scriptProperty.vector3Value = {std::stof(elements[10]), std::stof(elements[11]), std::stof(elements[12])};
					scriptProperty.stringValue = DecodeToken(elements[13]);
					break;
				}
			}
			else if (elements[0] == "Float") {
				if (elements.size() < 3U) {
					throw std::runtime_error("invalid float row");
				}

				stagedFloatValues[DecodeToken(elements[1])] = std::stof(elements[2]);
			}
			else if (elements[0] == "String") {
				if (elements.size() < 3U) {
					throw std::runtime_error("invalid string row");
				}

				stagedStringValues[DecodeToken(elements[1])] = DecodeToken(elements[2]);
			}
			else {
				throw std::runtime_error("unknown save row");
			}
		}
	}
	catch (const std::exception&) {
		PushConsoleMessage("Save: Slotの内容が破損しています " + slotName);
		return false;
	}

	if (!hasSupportedVersion) {
		PushConsoleMessage("Save: 未対応Version " + slotName);
		return false;
	}

	*editorScene_ = std::move(stagedScene);
	floatValues_ = std::move(stagedFloatValues);
	stringValues_ = std::move(stagedStringValues);

	// Scene Commit後にJolt Bodyへ最終状態を同期する。
	if (physicsManager_ != nullptr) {
		for (const EditorGameObject& gameObject : editorScene_->GetGameObjects()) {
			const EditorComponent* saveableComponent = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::Saveable);
			if (saveableComponent == nullptr || !saveableComponent->isActive) {
				continue;
			}

			physicsManager_->SetGameObjectTransform(gameObject.id, gameObject.translate, gameObject.rotate);
			physicsManager_->SetGameObjectSimulationActive(gameObject.id, gameObject.isActive);
			const EditorComponent* rigidBodyComponent = EditorComponentUtility::FindComponent(
				gameObject,
				EditorComponentType::RigidBody);

			if (rigidBodyComponent != nullptr) {
				physicsManager_->SetVelocity(gameObject.id, rigidBodyComponent->velocity);
				physicsManager_->SetAngularVelocity(gameObject.id, rigidBodyComponent->angularVelocity);
			}
		}
	}

	PushConsoleMessage("Save: Slot復元 " + slotName);
	return true;
}

bool EditorSaveManager::DeleteSlot(const std::string& slotName) {
	std::error_code removeError;
	const bool wasRemoved = std::filesystem::remove(MakeSlotPath(slotName), removeError);
	return wasRemoved && !removeError;
}

bool EditorSaveManager::HasSlot(const std::string& slotName) const {
	return !slotName.empty() && std::filesystem::exists(MakeSlotPath(slotName));
}

bool EditorSaveManager::ActivateCheckpoint(int32_t checkpointGameObjectId, bool shouldLoad) {
	if (editorScene_ == nullptr) {
		return false;
	}

	const EditorGameObject* checkpointGameObject = editorScene_->FindGameObject(checkpointGameObjectId);
	if (checkpointGameObject == nullptr || !checkpointGameObject->isActive) {
		return false;
	}

	const EditorComponent* checkpointComponent = EditorComponentUtility::FindComponent(
		*checkpointGameObject,
		EditorComponentType::Checkpoint);
	if (checkpointComponent == nullptr || !checkpointComponent->isActive) {
		return false;
	}

	const bool wasSucceeded = shouldLoad
		? LoadSlot(checkpointComponent->checkpointSlotName)
		: SaveSlot(checkpointComponent->checkpointSlotName);

	if (wasSucceeded) {
		QueueCheckpointAction(*checkpointGameObject, *checkpointComponent, shouldLoad);
	}

	return wasSucceeded;
}

void EditorSaveManager::SetFloat(const std::string& key, float value) {
	if (!key.empty()) {
		floatValues_[key] = value;
	}
}

bool EditorSaveManager::GetFloat(const std::string& key, float& value) const {
	const auto valueIterator = floatValues_.find(key);
	if (valueIterator == floatValues_.end()) {
		return false;
	}

	value = valueIterator->second;
	return true;
}

void EditorSaveManager::SetString(const std::string& key, const std::string& value) {
	if (!key.empty()) {
		stringValues_[key] = value;
	}
}

bool EditorSaveManager::GetString(const std::string& key, std::string& value) const {
	const auto valueIterator = stringValues_.find(key);
	if (valueIterator == stringValues_.end()) {
		return false;
	}

	value = valueIterator->second;
	return true;
}

std::string EditorSaveManager::MakeSlotPath(const std::string& slotName) const {
	std::ostringstream safeSlotNameStream;
	safeSlotNameStream << std::uppercase << std::hex << std::setfill('0');

	for (const unsigned char letter : slotName) {
		const bool isAlphaNumeric =
			(letter >= 'a' && letter <= 'z') ||
			(letter >= 'A' && letter <= 'Z') ||
			(letter >= '0' && letter <= '9');

		if (isAlphaNumeric || letter == '-' || letter == '_') {
			safeSlotNameStream << static_cast<char>(letter);
		}
		else {
			safeSlotNameStream << '_' << std::setw(2) << static_cast<int32_t>(letter);
		}
	}

	std::string safeSlotName = safeSlotNameStream.str();

	if (safeSlotName.empty()) {
		safeSlotName = "autosave";
	}

	return (std::filesystem::path("SaveData") / (safeSlotName + ".save")).generic_string();
}

std::string EditorSaveManager::ResolveSaveableKey(
	const EditorGameObject& gameObject,
	const EditorComponent& saveable) const {
	return saveable.saveableKey.empty() ? gameObject.name : saveable.saveableKey;
}

void EditorSaveManager::QueueCheckpointAction(
	const EditorGameObject& checkpointGameObject,
	const EditorComponent& checkpointComponent,
	bool wasLoaded) const {
	if (scriptManager_ == nullptr) {
		return;
	}

	const int32_t targetGameObjectId = checkpointComponent.checkpointActionTargetGameObjectId >= 0
		? checkpointComponent.checkpointActionTargetGameObjectId
		: checkpointGameObject.id;
	scriptManager_->QueueActionEvent(
		targetGameObjectId,
		wasLoaded
			? checkpointComponent.checkpointLoadedActionName
			: checkpointComponent.checkpointSavedActionName);
}

void EditorSaveManager::PushConsoleMessage(const std::string& message) const {
	if (consoleMessages_ != nullptr) {
		consoleMessages_->push_back(message);
	}
}
