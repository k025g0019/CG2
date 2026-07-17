#pragma once

#include "EditorCommonTypes.h"
#include "EditorScene.h"

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
	struct BaseTransform {
		Vector3 translate;  // Play 開始時の配置
		Vector3 rotate;  // Play 開始時の回転
		Vector3 scale;  // Play 開始時の拡縮
	};

	EditorScene* editorScene_ = nullptr;
	std::unordered_map<int32_t, float> animationTimes_;
	std::unordered_map<int32_t, BaseTransform> baseTransforms_;
	std::unordered_map<int32_t, ModelAnimationClipData> animationClips_;
	bool isStarted_ = false;

	void CacheAnimationClip(const EditorGameObject& gameObject, const EditorComponent& component);
	void UpdateAnimation(EditorGameObject& gameObject, EditorComponent& component, float& playbackTime);
	void UpdateFbxAnimation(EditorGameObject& gameObject, EditorComponent& component, float& playbackTime);
};

#pragma warning(pop)
