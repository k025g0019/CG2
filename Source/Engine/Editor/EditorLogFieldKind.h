#pragma once

// EditorLogFieldRegistry.generated.h(生成物)とEditorLogMonitorManager.h(手書き)の
// 両方から参照される共通の値種別。生成Scriptを再実行してもこのFileは上書きされないため、
// 生成対象ではない種別(Text等)もここへ安全に追加できる。

#include <cstdint>

enum class LogFieldValueKind : int32_t {
	Float,
	Int,
	Bool,
	Vector3,
	// int32_t のGameObjectId参照。ログ上は数値だが、UIでは参照先の名前解決に使う。
	// 変化判定は参照先ID(intValue)の完全一致で行う。
	GameObjectReference,
	// 数値化できない任意文字列(GameObjectの名前等)。変化判定は文字列の完全一致で行う。
	Text,
};
