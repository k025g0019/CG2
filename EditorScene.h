#pragma once

#include "Vector.h"

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// エディタ用 Component / GameObject / Scene
//============================================================

enum class EditorComponentType {
	Transform,
	ModelRenderer,
	SpriteRenderer,
	Light,
	Camera,
	AudioSource,
};

struct EditorComponent {
	EditorComponentType type;
	bool isActive;
	std::string assetPath;
	Vector3 color;
	float intensity;
};

struct EditorGameObject {
	int32_t id;
	int32_t parentId;
	bool isActive;
	std::string name;
	Vector3 translate;
	Vector3 rotate;
	Vector3 scale;
	std::vector<int32_t> children;
	std::vector<EditorComponent> components;
};

struct EditorPrefab {
	EditorGameObject gameObject;
	std::string sourcePath;
};

class EditorScene {
public:
	EditorScene();

	void InitializeDefaultScene();

	int32_t CreateGameObject(const std::string& name);
	int32_t DuplicateGameObject(int32_t gameObjectId);
	bool DeleteGameObject(int32_t gameObjectId);
	bool RenameGameObject(int32_t gameObjectId, const std::string& name);
	bool SetParent(int32_t childId, int32_t parentId);

	bool AddComponent(int32_t gameObjectId, EditorComponentType type);
	bool RemoveComponent(int32_t gameObjectId, EditorComponentType type);
	bool HasComponent(int32_t gameObjectId, EditorComponentType type) const;

	bool SaveScene(const std::string& filePath) const;
	bool LoadScene(const std::string& filePath);
	bool SavePrefab(int32_t gameObjectId, const std::string& filePath) const;
	int32_t InstantiatePrefab(const std::string& filePath);

	void PushUndo();
	bool Undo();
	bool Redo();

	EditorGameObject* FindGameObject(int32_t gameObjectId);
	const EditorGameObject* FindGameObject(int32_t gameObjectId) const;
	std::vector<EditorGameObject>& GetGameObjects();
	const std::vector<EditorGameObject>& GetGameObjects() const;

private:
	int32_t nextGameObjectId_;
	std::vector<EditorGameObject> gameObjects_;
	std::vector<std::vector<EditorGameObject>> undoStack_;
	std::vector<std::vector<EditorGameObject>> redoStack_;

	EditorComponent CreateComponent(EditorComponentType type) const;
	int32_t FindGameObjectIndex(int32_t gameObjectId) const;
	void RemoveFromParent(int32_t childId);
	void RebuildChildren();
	void DeleteGameObjectRecursive(int32_t gameObjectId);
	void RefreshNextGameObjectId();
};

std::string ToString(EditorComponentType type);
EditorComponentType ComponentTypeFromIndex(int32_t componentIndex);

#pragma warning(pop)
