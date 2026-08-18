#pragma once

// このファイルは Tools/generate_log_field_registry.py が自動生成する。
// 手動で編集しないこと。Inspectorにフィールドを追加/変更したら、
// スクリプトを再実行して再生成すること。
//
// LogFieldValueKind は EditorLogFieldKind.h (手書き、再生成で上書きされない) で定義する。

#include "EditorLogFieldKind.h"
#include "EditorScene.h"

#include <cstdint>
#include <vector>

struct LogComponentFieldDescriptor {
	EditorComponentType componentType;
	const char* displayName;
	// Config保存用の安定Key。実体はC++のMember名で、配列内indexが変わっても
	// (componentType, fieldKey)の組で常に同じFieldを再解決できる。
	const char* fieldKey;
	LogFieldValueKind kind;
	float EditorComponent::* floatMember;
	int32_t EditorComponent::* intMember;
	bool EditorComponent::* boolMember;
	Vector3 EditorComponent::* vector3Member;
};

// EditorInspectorPanel.cppのDraw*Component関数群から自動抽出したField一覧。
// Component用のログ対象選択・値取得は全てこのTableを介して行う。
const std::vector<LogComponentFieldDescriptor>& GetLogComponentFieldRegistry();
