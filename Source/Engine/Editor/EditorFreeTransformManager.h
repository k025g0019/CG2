#pragma once

#include "EditorScene.h"

class EditorFreeTransformManager {
public:
	EditorFreeTransformManager() = default;
	~EditorFreeTransformManager() = default;
	EditorFreeTransformManager(const EditorFreeTransformManager&) = delete;
	EditorFreeTransformManager& operator=(const EditorFreeTransformManager&) = delete;
	EditorFreeTransformManager(EditorFreeTransformManager&&) = delete;
	EditorFreeTransformManager& operator=(EditorFreeTransformManager&&) = delete;

	void Initialize(EditorScene* editorScene);
	void Start();
	void Update(float deltaTime, const uint8_t* keyState);
	void Stop();

private:
	EditorScene* editorScene_ = nullptr;
	bool isStarted_ = false;
};
