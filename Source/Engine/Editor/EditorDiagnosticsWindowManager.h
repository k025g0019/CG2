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
	void DrawProfiler();
	void DrawVfx();
	void DrawSceneValidation();
	void DrawReplay();
	void DrawRenderTargets();
	void DrawImageComparison();
	void DrawMaterialPreview();
	void DrawScopes();
	void DrawRenderGraph();
	void AddIssue(EditorValidationSeverity severity, int32_t gameObjectId, const std::string& message);
};

#pragma warning(pop)
