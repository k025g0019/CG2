#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Component用ログフィールドレジストリの生成スクリプト。

Source/Engine/Editor/EditorInspectorPanel.cpp は、Inspectorに表示する
全Componentの全フィールドを DrawFloatRow / DrawIntRow / DrawCheckboxRow /
DrawVector3Row / DrawGameObjectReferenceRow という一貫した呼び出し形式で
既に「ラベル文字列 + component.フィールド」として宣言している。

このスクリプトはそれを解析し、汎用ログ・監視システムが使う
Source/Engine/Editor/EditorLogFieldRegistry.generated.h / .cpp を再生成する。

Inspectorにフィールドを追加/変更したら、このスクリプトを再実行して
生成ファイルを更新すること(ビルド時にPythonは不要。生成物はリポジトリへコミットする)。

実行方法:
    python Tools/generate_log_field_registry.py
"""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INSPECTOR_CPP = ROOT / "Source/Engine/Editor/EditorInspectorPanel.cpp"
SCENE_H = ROOT / "Source/Engine/Editor/EditorScene.h"
OUT_H = ROOT / "Source/Engine/Editor/EditorLogFieldRegistry.generated.h"
OUT_CPP = ROOT / "Source/Engine/Editor/EditorLogFieldRegistry.generated.cpp"

KIND_FLOAT = "Float"
KIND_INT = "Int"
KIND_BOOL = "Bool"
KIND_VECTOR3 = "Vector3"
KIND_GAMEOBJECT_REFERENCE = "GameObjectReference"

# EditorComponent構造体のフィールド型 (float/int32_t/bool/Vector3のみ対象。
# std::string/std::vector等は汎用ログ対象外)
FIELD_TYPE_PATTERN = re.compile(
    r"^\t(float|int32_t|bool|Vector3)\s+(\w+)(?:\[[^\]]*\])?\s*;", re.MULTILINE
)


def find_matching_brace(text: str, open_brace_index: int) -> int:
    """text[open_brace_index] が '{' である前提で、対応する '}' のindexを返す。"""
    depth = 0
    index = open_brace_index
    while index < len(text):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    raise ValueError("対応する閉じ括弧が見つかりません")


def extract_component_struct_fields(scene_h_text: str) -> dict:
    """EditorComponent構造体の {フィールド名: 型} を返す。"""
    struct_start = scene_h_text.index("struct EditorComponent {")
    brace_index = scene_h_text.index("{", struct_start)
    brace_end = find_matching_brace(scene_h_text, brace_index)
    struct_body = scene_h_text[brace_index:brace_end]

    fields = {}
    for match in FIELD_TYPE_PATTERN.finditer(struct_body):
        field_type, field_name = match.group(1), match.group(2)
        fields[field_name] = field_type
    return fields


def extract_switch_body(inspector_cpp_text: str) -> str:
    """DrawComponentBody内、switch (component.type) { ... } の中身を返す。"""
    switch_start = inspector_cpp_text.index("switch (component.type) {")
    brace_index = inspector_cpp_text.index("{", switch_start)
    brace_end = find_matching_brace(inspector_cpp_text, brace_index)
    return inspector_cpp_text[brace_index + 1 : brace_end]


CASE_PATTERN = re.compile(r"case EditorComponentType::(\w+):")
CALL_PATTERN = re.compile(r"\b(Draw\w+Component)\s*\(")
BREAK_PATTERN = re.compile(r"\bbreak;")


def extract_type_to_function(switch_body: str) -> dict:
    """{EditorComponentType名: DrawXxxComponent関数名} を返す(fallthrough caseに対応)。"""
    type_to_function = {}
    pending_types = []
    pos = 0
    length = len(switch_body)

    while pos < length:
        case_match = CASE_PATTERN.match(switch_body, pos)
        if case_match:
            pending_types.append(case_match.group(1))
            pos = case_match.end()
            # 改行・空白を読み飛ばす
            while pos < length and switch_body[pos] in " \t\r\n":
                pos += 1
            continue

        call_match = CALL_PATTERN.match(switch_body, pos)
        if call_match and pending_types:
            function_name = call_match.group(1)
            for type_name in pending_types:
                type_to_function[type_name] = function_name

        break_match = BREAK_PATTERN.match(switch_body, pos)
        if break_match:
            pending_types = []
            pos = break_match.end()
            continue

        pos += 1

    return type_to_function


def find_function_body(inspector_cpp_text: str, function_name: str) -> str:
    """void <function_name>(...) { ... } の本体テキストを返す。見つからなければ空文字列。"""
    definition_pattern = re.compile(
        r"void\s+" + re.escape(function_name) + r"\s*\([^;{]*\)\s*\{"
    )
    match = definition_pattern.search(inspector_cpp_text)
    if match is None:
        return ""
    brace_index = match.end() - 1
    brace_end = find_matching_brace(inspector_cpp_text, brace_index)
    return inspector_cpp_text[brace_index + 1 : brace_end]


ROW_PATTERNS = [
    (KIND_FLOAT, re.compile(r'DrawFloatRow\(\s*"([^"]+)"\s*,\s*component\.(\w+)', re.DOTALL)),
    (KIND_INT, re.compile(r'DrawIntRow\(\s*"([^"]+)"\s*,\s*component\.(\w+)', re.DOTALL)),
    (KIND_INT, re.compile(r'DrawComboRow\(\s*"([^"]+)"\s*,\s*component\.(\w+)', re.DOTALL)),
    (KIND_BOOL, re.compile(r'DrawCheckboxRow\(\s*"([^"]+)"\s*,\s*component\.(\w+)', re.DOTALL)),
    (KIND_VECTOR3, re.compile(r'DrawVector3Row\(\s*"([^"]+)"\s*,\s*component\.(\w+)', re.DOTALL)),
    (KIND_VECTOR3, re.compile(r'DrawColor3Row\(\s*"([^"]+)"\s*,\s*component\.(\w+)', re.DOTALL)),
    # 物理側が書き込むRuntime診断値。編集不可だが監視・記録の対象にしたいので登録する。
    # 式ではなく単一フィールドだけを拾うよう、直後が ')' であることを要求する。
    (KIND_FLOAT, re.compile(r'DrawReadOnlyFloatRow\(\s*"([^"]+)"\s*,\s*component\.(\w+)\s*\)', re.DOTALL)),
    (KIND_VECTOR3, re.compile(r'DrawReadOnlyVector3Row\(\s*"([^"]+)"\s*,\s*component\.(\w+)\s*\)', re.DOTALL)),
    (
        KIND_GAMEOBJECT_REFERENCE,
        re.compile(
            r'DrawGameObjectReferenceRow\(\s*context\s*,\s*[^,]+,\s*"([^"]+)"\s*,\s*component\.(\w+)',
            re.DOTALL,
        ),
    ),
]

EXPECTED_CPP_TYPE = {
    KIND_FLOAT: "float",
    KIND_INT: "int32_t",
    KIND_BOOL: "bool",
    KIND_VECTOR3: "Vector3",
    KIND_GAMEOBJECT_REFERENCE: "int32_t",
}


def extract_fields_from_function_body(
    function_body: str, struct_fields: dict
) -> list:
    """[(label, field_name, kind), ...] を返す。struct_fieldsの型と一致しないものは除外する。"""
    entries = []
    seen = set()
    for kind, pattern in ROW_PATTERNS:
        for match in pattern.finditer(function_body):
            label, field_name = match.group(1), match.group(2)
            actual_type = struct_fields.get(field_name)
            if actual_type != EXPECTED_CPP_TYPE[kind]:
                # 配列要素・別struct経由等、単純なpointer-to-memberで表現できないものは除外する
                continue
            key = (field_name, kind)
            if key in seen:
                continue
            seen.add(key)
            entries.append((label, field_name, kind))
    return entries


def escape_cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def generate():
    inspector_text = INSPECTOR_CPP.read_text(encoding="utf-8-sig")
    scene_text = SCENE_H.read_text(encoding="utf-8-sig")

    struct_fields = extract_component_struct_fields(scene_text)
    switch_body = extract_switch_body(inspector_text)
    type_to_function = extract_type_to_function(switch_body)

    function_body_cache = {}
    registry_rows = []
    skipped_types = []

    for type_name, function_name in type_to_function.items():
        if function_name not in function_body_cache:
            function_body_cache[function_name] = find_function_body(
                inspector_text, function_name
            )
        function_body = function_body_cache[function_name]
        if not function_body:
            skipped_types.append((type_name, function_name, "関数定義が見つからない"))
            continue

        fields = extract_fields_from_function_body(function_body, struct_fields)
        if not fields:
            continue

        for label, field_name, kind in fields:
            registry_rows.append((type_name, label, field_name, kind))

    write_header()
    write_source(registry_rows)

    print(f"登録Component型数: {len(type_to_function)}")
    print(f"生成Fieldエントリ数: {len(registry_rows)}")
    if skipped_types:
        print(f"関数定義が見つからず読み飛ばしたCase数: {len(skipped_types)}")
        for type_name, function_name, reason in skipped_types[:20]:
            print(f"  - {type_name} -> {function_name} ({reason})")


def write_header():
    header = """#pragma once

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
\tEditorComponentType componentType;
\tconst char* displayName;
\t// Config保存用の安定Key。実体はC++のMember名で、配列内indexが変わっても
\t// (componentType, fieldKey)の組で常に同じFieldを再解決できる。
\tconst char* fieldKey;
\tLogFieldValueKind kind;
\tfloat EditorComponent::* floatMember;
\tint32_t EditorComponent::* intMember;
\tbool EditorComponent::* boolMember;
\tVector3 EditorComponent::* vector3Member;
};

// EditorInspectorPanel.cppのDraw*Component関数群から自動抽出したField一覧。
// Component用のログ対象選択・値取得は全てこのTableを介して行う。
const std::vector<LogComponentFieldDescriptor>& GetLogComponentFieldRegistry();
"""
    OUT_H.write_text(header, encoding="utf-8-sig")


def write_source(registry_rows):
    lines = []
    lines.append('#include "EditorLogFieldRegistry.generated.h"')
    lines.append("")
    lines.append("// このファイルは Tools/generate_log_field_registry.py が自動生成する。")
    lines.append("// 手動で編集しないこと。")
    lines.append("")
    lines.append("namespace {")
    lines.append("\tconst std::vector<LogComponentFieldDescriptor> kLogComponentFieldRegistry = {")

    kind_enum = {
        KIND_FLOAT: "LogFieldValueKind::Float",
        KIND_INT: "LogFieldValueKind::Int",
        KIND_BOOL: "LogFieldValueKind::Bool",
        KIND_VECTOR3: "LogFieldValueKind::Vector3",
        KIND_GAMEOBJECT_REFERENCE: "LogFieldValueKind::GameObjectReference",
    }
    member_slot = {
        KIND_FLOAT: 0,
        KIND_INT: 1,
        KIND_BOOL: 2,
        KIND_VECTOR3: 3,
        KIND_GAMEOBJECT_REFERENCE: 1,
    }

    for type_name, label, field_name, kind in registry_rows:
        slots = ["nullptr", "nullptr", "nullptr", "nullptr"]
        slots[member_slot[kind]] = f"&EditorComponent::{field_name}"
        lines.append(
            "\t\t{{EditorComponentType::{type_name}, {label}, {field_key}, {kind_enum}, "
            "{floatm}, {intm}, {boolm}, {vec3m}}},".format(
                type_name=type_name,
                label=escape_cpp_string(label),
                field_key=escape_cpp_string(field_name),
                kind_enum=kind_enum[kind],
                floatm=slots[0],
                intm=slots[1],
                boolm=slots[2],
                vec3m=slots[3],
            )
        )

    lines.append("\t};")
    lines.append("}")
    lines.append("")
    lines.append("const std::vector<LogComponentFieldDescriptor>& GetLogComponentFieldRegistry() {")
    lines.append("\treturn kLogComponentFieldRegistry;")
    lines.append("}")
    lines.append("")

    OUT_CPP.write_text("\n".join(lines), encoding="utf-8-sig")


if __name__ == "__main__":
    generate()
