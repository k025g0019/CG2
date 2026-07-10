#pragma once

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// InputAction の入力段階
//============================================================

enum class InputPhase {
	Started,  // 前フレームは未押下、今フレーム押された瞬間
	Performed,  // Button 入力では Started と同タイミングで 1 回だけ呼ぶ
	Canceled,  // 前フレームは押下中、今フレーム離された瞬間
};

#pragma warning(pop)
