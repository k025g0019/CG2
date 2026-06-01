#include "EditorScene.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>

#pragma warning(push)
#pragma warning(disable : 5045)

namespace {
	constexpr int32_t kInvalidGameObjectId = -1;

	std::vector<std::string> SplitLine(const std::string& line, char delimiter) {
		std::vector<std::string> elements;
		std::stringstream stream(line);
		std::string element;

		while (std::getline(stream, element, delimiter)) {
			elements.push_back(element);
		}

		return elements;
	}

	int32_t ToInt(const std::string& text) {
		return static_cast<int32_t>(std::stoi(text));
	}

	float ToFloat(const std::string& text) {
		return std::stof(text);
	}
}

//============================================================
// Component
//============================================================

std::string ToString(EditorComponentType type) {
	switch (type) {
	case EditorComponentType::Transform:
		return "Transform";
	case EditorComponentType::ModelRenderer:
		return "ModelRenderer";
	case EditorComponentType::SpriteRenderer:
		return "SpriteRenderer";
	case EditorComponentType::Light:
		return "Light";
	case EditorComponentType::Camera:
		return "Camera";
	case EditorComponentType::AudioSource:
		return "AudioSource";
	default:
		return "Unknown";
	}
}

EditorComponentType ComponentTypeFromIndex(int32_t componentIndex) {
	switch (componentIndex) {
	case 0:
		return EditorComponentType::ModelRenderer;
	case 1:
		return EditorComponentType::SpriteRenderer;
	case 2:
		return EditorComponentType::Light;
	case 3:
		return EditorComponentType::Camera;
	case 4:
		return EditorComponentType::AudioSource;
	default:
		return EditorComponentType::Transform;
	}
}

//============================================================
// Scene
//============================================================

EditorScene::EditorScene() : nextGameObjectId_(1) {
}

void EditorScene::InitializeDefaultScene() {
	gameObjects_.clear();
	undoStack_.clear();
	redoStack_.clear();
	nextGameObjectId_ = 1;

	int32_t modelId = CreateGameObject("モデル");
	AddComponent(modelId, EditorComponentType::ModelRenderer);

	int32_t spriteId = CreateGameObject("スプライト");
	AddComponent(spriteId, EditorComponentType::SpriteRenderer);

	int32_t lightId = CreateGameObject("ライト");
	AddComponent(lightId, EditorComponentType::Light);

	int32_t cameraId = CreateGameObject("デバッグカメラ");
	AddComponent(cameraId, EditorComponentType::Camera);

	undoStack_.clear();
	redoStack_.clear();
}

int32_t EditorScene::CreateGameObject(const std::string& name) {
	EditorGameObject gameObject{};
	gameObject.id = nextGameObjectId_;
	gameObject.parentId = kInvalidGameObjectId;
	gameObject.isActive = true;
	gameObject.name = name;
	gameObject.translate = {0.0f, 0.0f, 0.0f};
	gameObject.rotate = {0.0f, 0.0f, 0.0f};
	gameObject.scale = {1.0f, 1.0f, 1.0f};
	gameObject.components.push_back(CreateComponent(EditorComponentType::Transform));

	nextGameObjectId_++;
	gameObjects_.push_back(gameObject);
	return gameObject.id;
}

int32_t EditorScene::DuplicateGameObject(int32_t gameObjectId) {
	const EditorGameObject* sourceGameObject = FindGameObject(gameObjectId);
	if (sourceGameObject == nullptr) {
		return kInvalidGameObjectId;
	}

	EditorGameObject duplicatedGameObject = *sourceGameObject;
	duplicatedGameObject.id = nextGameObjectId_;
	duplicatedGameObject.parentId = kInvalidGameObjectId;
	duplicatedGameObject.children.clear();
	duplicatedGameObject.name += "_Copy";
	duplicatedGameObject.translate.x += 0.2f;

	nextGameObjectId_++;
	gameObjects_.push_back(duplicatedGameObject);
	RebuildChildren();

	return duplicatedGameObject.id;
}

bool EditorScene::DeleteGameObject(int32_t gameObjectId) {
	if (FindGameObject(gameObjectId) == nullptr) {
		return false;
	}

	DeleteGameObjectRecursive(gameObjectId);
	RebuildChildren();
	return true;
}

bool EditorScene::RenameGameObject(int32_t gameObjectId, const std::string& name) {
	EditorGameObject* gameObject = FindGameObject(gameObjectId);
	if (gameObject == nullptr || name.empty()) {
		return false;
	}

	gameObject->name = name;
	return true;
}

bool EditorScene::SetParent(int32_t childId, int32_t parentId) {
	if (childId == parentId) {
		return false;
	}

	EditorGameObject* child = FindGameObject(childId);
	if (child == nullptr) {
		return false;
	}

	if (parentId != kInvalidGameObjectId && FindGameObject(parentId) == nullptr) {
		return false;
	}

	RemoveFromParent(childId);
	child->parentId = parentId;
	RebuildChildren();

	return true;
}

bool EditorScene::AddComponent(int32_t gameObjectId, EditorComponentType type) {
	EditorGameObject* gameObject = FindGameObject(gameObjectId);
	if (gameObject == nullptr || HasComponent(gameObjectId, type)) {
		return false;
	}

	gameObject->components.push_back(CreateComponent(type));
	return true;
}

bool EditorScene::RemoveComponent(int32_t gameObjectId, EditorComponentType type) {
	if (type == EditorComponentType::Transform) {
		return false;
	}

	EditorGameObject* gameObject = FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	auto removeIterator = std::remove_if(
		gameObject->components.begin(),
		gameObject->components.end(),
		[type](const EditorComponent& component) { return component.type == type; });
	bool isRemoved = removeIterator != gameObject->components.end();
	gameObject->components.erase(removeIterator, gameObject->components.end());

	return isRemoved;
}

bool EditorScene::HasComponent(int32_t gameObjectId, EditorComponentType type) const {
	const EditorGameObject* gameObject = FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	for (const EditorComponent& component : gameObject->components) {
		if (component.type == type) {
			return true;
		}
	}

	return false;
}

//============================================================
// Save / Load
//============================================================

bool EditorScene::SaveScene(const std::string& filePath) const {
	std::ofstream file(filePath);
	if (!file.is_open()) {
		return false;
	}

	for (const EditorGameObject& gameObject : gameObjects_) {
		file << "GameObject|"
		     << gameObject.id << "|"
		     << gameObject.parentId << "|"
		     << gameObject.name << "|"
		     << gameObject.translate.x << "|"
		     << gameObject.translate.y << "|"
		     << gameObject.translate.z << "|"
		     << gameObject.rotate.x << "|"
		     << gameObject.rotate.y << "|"
		     << gameObject.rotate.z << "|"
		     << gameObject.scale.x << "|"
		     << gameObject.scale.y << "|"
		     << gameObject.scale.z << "\n";

		for (const EditorComponent& component : gameObject.components) {
			file << "Component|"
			     << gameObject.id << "|"
			     << static_cast<int32_t>(component.type) << "|"
			     << component.isActive << "|"
			     << component.assetPath << "|"
			     << component.color.x << "|"
			     << component.color.y << "|"
			     << component.color.z << "|"
			     << component.intensity << "\n";
		}
	}

	return true;
}

bool EditorScene::LoadScene(const std::string& filePath) {
	std::ifstream file(filePath);
	if (!file.is_open()) {
		return false;
	}

	std::vector<EditorGameObject> loadedGameObjects;
	std::string line;

	while (std::getline(file, line)) {
		std::vector<std::string> elements = SplitLine(line, '|');
		if (elements.empty()) {
			continue;
		}

		if (elements[0] == "GameObject" && elements.size() >= 13) {
			EditorGameObject gameObject{};
			gameObject.id = ToInt(elements[1]);
			gameObject.parentId = ToInt(elements[2]);
			gameObject.isActive = true;
			gameObject.name = elements[3];
			gameObject.translate = {ToFloat(elements[4]), ToFloat(elements[5]), ToFloat(elements[6])};
			gameObject.rotate = {ToFloat(elements[7]), ToFloat(elements[8]), ToFloat(elements[9])};
			gameObject.scale = {ToFloat(elements[10]), ToFloat(elements[11]), ToFloat(elements[12])};
			loadedGameObjects.push_back(gameObject);
		}
		else if (elements[0] == "Component" && elements.size() >= 9) {
			int32_t ownerId = ToInt(elements[1]);
			for (EditorGameObject& gameObject : loadedGameObjects) {
				if (gameObject.id != ownerId) {
					continue;
				}

				EditorComponent component{};
				component.type = static_cast<EditorComponentType>(ToInt(elements[2]));
				component.isActive = ToInt(elements[3]) != 0;
				component.assetPath = elements[4];
				component.color = {ToFloat(elements[5]), ToFloat(elements[6]), ToFloat(elements[7])};
				component.intensity = ToFloat(elements[8]);
				gameObject.components.push_back(component);
				break;
			}
		}
	}

	if (loadedGameObjects.empty()) {
		return false;
	}

	gameObjects_ = loadedGameObjects;
	RebuildChildren();
	RefreshNextGameObjectId();
	undoStack_.clear();
	redoStack_.clear();

	return true;
}

bool EditorScene::SavePrefab(int32_t gameObjectId, const std::string& filePath) const {
	EditorScene prefabScene;
	const EditorGameObject* gameObject = FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return false;
	}

	prefabScene.gameObjects_.push_back(*gameObject);
	prefabScene.gameObjects_[0].parentId = kInvalidGameObjectId;
	prefabScene.gameObjects_[0].children.clear();
	return prefabScene.SaveScene(filePath);
}

int32_t EditorScene::InstantiatePrefab(const std::string& filePath) {
	EditorScene prefabScene;
	if (!prefabScene.LoadScene(filePath) || prefabScene.gameObjects_.empty()) {
		return kInvalidGameObjectId;
	}

	EditorGameObject gameObject = prefabScene.gameObjects_[0];
	gameObject.id = nextGameObjectId_;
	gameObject.parentId = kInvalidGameObjectId;
	gameObject.children.clear();
	gameObject.name += "_Prefab";
	gameObject.translate.x += 0.3f;

	nextGameObjectId_++;
	gameObjects_.push_back(gameObject);
	return gameObject.id;
}

//============================================================
// Undo / Redo
//============================================================

void EditorScene::PushUndo() {
	undoStack_.push_back(gameObjects_);
	redoStack_.clear();
}

bool EditorScene::Undo() {
	if (undoStack_.empty()) {
		return false;
	}

	redoStack_.push_back(gameObjects_);
	gameObjects_ = undoStack_.back();
	undoStack_.pop_back();
	RebuildChildren();
	RefreshNextGameObjectId();
	return true;
}

bool EditorScene::Redo() {
	if (redoStack_.empty()) {
		return false;
	}

	undoStack_.push_back(gameObjects_);
	gameObjects_ = redoStack_.back();
	redoStack_.pop_back();
	RebuildChildren();
	RefreshNextGameObjectId();
	return true;
}

//============================================================
// Find
//============================================================

EditorGameObject* EditorScene::FindGameObject(int32_t gameObjectId) {
	int32_t gameObjectIndex = FindGameObjectIndex(gameObjectId);
	if (gameObjectIndex < 0) {
		return nullptr;
	}

	return &gameObjects_[static_cast<size_t>(gameObjectIndex)];
}

const EditorGameObject* EditorScene::FindGameObject(int32_t gameObjectId) const {
	int32_t gameObjectIndex = FindGameObjectIndex(gameObjectId);
	if (gameObjectIndex < 0) {
		return nullptr;
	}

	return &gameObjects_[static_cast<size_t>(gameObjectIndex)];
}

std::vector<EditorGameObject>& EditorScene::GetGameObjects() {
	return gameObjects_;
}

const std::vector<EditorGameObject>& EditorScene::GetGameObjects() const {
	return gameObjects_;
}

//============================================================
// Private
//============================================================

EditorComponent EditorScene::CreateComponent(EditorComponentType type) const {
	EditorComponent component{};
	component.type = type;
	component.isActive = true;
	component.assetPath = "";
	component.color = {1.0f, 1.0f, 1.0f};
	component.intensity = 1.0f;

	return component;
}

int32_t EditorScene::FindGameObjectIndex(int32_t gameObjectId) const {
	for (int32_t gameObjectIndex = 0; gameObjectIndex < static_cast<int32_t>(gameObjects_.size()); ++gameObjectIndex) {
		if (gameObjects_[static_cast<size_t>(gameObjectIndex)].id == gameObjectId) {
			return gameObjectIndex;
		}
	}

	return -1;
}

void EditorScene::RemoveFromParent(int32_t childId) {
	for (EditorGameObject& gameObject : gameObjects_) {
		auto removeIterator = std::remove(gameObject.children.begin(), gameObject.children.end(), childId);
		gameObject.children.erase(removeIterator, gameObject.children.end());
	}
}

void EditorScene::RebuildChildren() {
	for (EditorGameObject& gameObject : gameObjects_) {
		gameObject.children.clear();
	}

	for (EditorGameObject& gameObject : gameObjects_) {
		EditorGameObject* parent = FindGameObject(gameObject.parentId);
		if (parent != nullptr) {
			parent->children.push_back(gameObject.id);
		}
	}
}

void EditorScene::DeleteGameObjectRecursive(int32_t gameObjectId) {
	EditorGameObject* gameObject = FindGameObject(gameObjectId);
	if (gameObject == nullptr) {
		return;
	}

	std::vector<int32_t> childIds = gameObject->children;
	for (int32_t childId : childIds) {
		DeleteGameObjectRecursive(childId);
	}

	RemoveFromParent(gameObjectId);
	int32_t gameObjectIndex = FindGameObjectIndex(gameObjectId);
	if (gameObjectIndex >= 0) {
		gameObjects_.erase(gameObjects_.begin() + static_cast<std::ptrdiff_t>(gameObjectIndex));
	}
}

void EditorScene::RefreshNextGameObjectId() {
	nextGameObjectId_ = 1;
	for (const EditorGameObject& gameObject : gameObjects_) {
		nextGameObjectId_ = (std::max)(nextGameObjectId_, gameObject.id + 1);
	}
}

#pragma warning(pop)
