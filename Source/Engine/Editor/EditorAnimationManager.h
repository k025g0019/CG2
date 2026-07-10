#pragma once

#include "EditorScene.h"

#include <cmath>
#include <unordered_map>

#pragma warning(push)
#pragma warning(disable : 4820)

class EditorAnimationManager {
public:
	EditorAnimationManager() = default;
	~EditorAnimationManager() = default;
	EditorAnimationManager(const EditorAnimationManager&) = delete;
	EditorAnimationManager& operator=(const EditorAnimationManager&) = delete;
	EditorAnimationManager(EditorAnimationManager&&) = delete;
	EditorAnimationManager& operator=(EditorAnimationManager&&) = delete;

	void Initialize(EditorScene* editorScene);
	void Start();
	void Update(float deltaTime);
	void Stop();
	bool IsAnimationPlaying(int32_t gameObjectId) const;  // Play 中にその GameObject の Animation 時間が進んでいるかを返す
	float GetAnimationTime(int32_t gameObjectId) const;  // 現在の Animation 時間を返す

private:
	EditorScene* editorScene_ = nullptr;
	std::unordered_map<int32_t, float> animationTimes_;
	bool isStarted_ = false;

	void UpdateAnimation(EditorGameObject& gameObject, EditorComponent& component, float deltaTime);
};

#pragma warning(pop)
