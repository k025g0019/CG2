#pragma once

#include "EditorNativeScript.h"

//================================================================
// ennsui_fbxScript のユーザー編集用データ
//================================================================

class ennsui_fbxScript final : public Script {
public:
	void Start() override;
	void Update(float deltaTime) override;
	void FixedUpdate(float fixedDeltaTime) override;
	void OnCollisionEnter(const EditorScriptPhysicsEvent& physicsEvent) override;
	void Stop() override;

private:
	float moveSpeed_ = 3.0f;
	float rotateSpeed_ = 1.0f;
	bool isStarted_ = false;
};
