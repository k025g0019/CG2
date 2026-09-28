#pragma once

#include <cstdint>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

enum class EditorValidationSeverity : int32_t {
	Information = 0,
	Warning,
	Error,
};

struct EditorValidationIssue {
	EditorValidationSeverity severity = EditorValidationSeverity::Information;
	int32_t gameObjectId = -1;
	std::string message;
};

class EditorDiagnosticsWindowManager {
public:
	void Initialize();
	void Update();
	void Draw();

private:
	std::vector<EditorValidationIssue> validationIssues_;
	bool shouldAutoValidate_ = true;
	int32_t validationFrameTimer_ = 0;
	int32_t selectedRenderTargetIndex_ = 0;

	void ValidateScene();
	void RunSceneSerializationSmokeTest();  // Scene保存→読込往復でTransform/代表Componentの値が保持されるかを確認する回帰テスト
	void DrawProfiler();
	void DrawPhysics();  // 接触点・ShapeCast・レイヤー除外理由を同じ画面で確認する。
	void DrawVfx();
	void DrawSceneValidation();
	void DrawReplay();
	void DrawRenderTargets();
	void DrawImageComparison();
	void DrawMaterialPreview();
	void DrawScopes();
	void DrawRenderGraph();
	void DrawApiFailures();  // HRESULT失敗とShader compile失敗の一覧。Release構成でも記録される唯一の窓口。
	void AddIssue(EditorValidationSeverity severity, int32_t gameObjectId, const std::string& message);
};

#pragma warning(pop)
