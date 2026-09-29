#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

//========================================
// HRESULT 失敗検査の役割
//========================================

// DirectX API の成否を Debug / Release の両方で必ず記録するための検査。
//
// 従来 assert(SUCCEEDED(hr)) だけで見ていたが、assert は Release 構成(NDEBUG)で
// 丸ごと消える。そのため ResizeBuffers / GetBuffer / Map / CreateCommittedResource の
// 失敗が「無検査で続行」になっていた。続行した先で nullptr の Resource を
// CreateRenderTargetView や書き込みへ渡すため、Debug では再現しない
// Release 限定のクラッシュになる。
//
// ここで用意するのはマクロではなく通常の関数なので、どの構成でも必ず動く。
// 失敗は次の 3 か所へ同時に残す。
//   1. Visual Studio の出力ウィンドウ (OutputDebugStringA)
//   2. 実行ごとの logs/<日時>.Log (EditorSetHrFailureLogStream で登録された Stream)
//   3. プロセス内の一覧 (EditorGetHrFailureLog) — Diagnostics Window 表示用

//========================================
// 型定義
//========================================

// HRESULT は Windows 上で long の typedef である。この Header は
// EditorSharedState.h より前に include されることがあるため、Windows.h へ
// 依存させず long で受ける。SUCCEEDED(hr) は hr >= 0 と同義なので判定も自前で行う。
using EditorHrValue = long;

//========================================
// 検査と記録
//========================================

// 失敗していれば記録して false を返す。成功していれば何もせず true を返す。
// expression / file / line は下のマクロが呼び出し位置から埋める。
bool EditorCheckHr(
	EditorHrValue hr, const char* expression, const char* file, int line) noexcept;

//========================================
// 記録の参照
//========================================

// これまでに記録した失敗の一覧。Diagnostics Window の「DirectX失敗」タブが参照する。
// 記録は上限つきなので、件数と要素数が一致しない場合は打ち切られている。
const std::vector<std::string>& EditorGetHrFailureLog() noexcept;

// 記録済み失敗の総件数。0 以外なら描画かリソース生成のどこかが失敗している。
std::size_t EditorGetHrFailureCount() noexcept;

//========================================
// 記録の設定と解除
//========================================

// 一覧を空にする。Diagnostics Window の Clear 操作用。
void EditorClearHrFailureLog() noexcept;

// 失敗行の書き出し先 Stream を登録する。PlatformManager が実行ログ
// (logs/<日時>.Log) を開いた直後に呼ぶ。登録前の失敗も一覧へは残るので、
// 初期化のごく初期に起きた失敗が消えることはない。
// nullptr を渡すと書き出しを止める。Stream を閉じる前に必ず解除する。
void EditorSetHrFailureLogStream(std::ostream* logStream) noexcept;

//========================================
// 呼び出し側が使うマクロ
//========================================

// 成否を bool で受け取り分岐する形。失敗経路を書くときはこちら。
//   if (!EDITOR_HR_OK(device->CreateFence(...))) { return false; }
#define EDITOR_HR_OK(hrExpression) \
	EditorCheckHr((hrExpression), #hrExpression, __FILE__, __LINE__)

// 記録だけして続行する形。assert(SUCCEEDED(x)) の置き換えに使う。
// 戻り値を参照しない代わりに、失敗しても必ずログへ残る点が assert と違う。
#define EDITOR_HR_VERIFY(hrExpression) \
	(void)EditorCheckHr((hrExpression), #hrExpression, __FILE__, __LINE__)
