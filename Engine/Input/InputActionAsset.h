#pragma once

#include "InputActionMap.h"

#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//============================================================
// JSON から読み込む ActionAsset
//============================================================

class InputActionAsset {
public:
	bool LoadFromJson(const std::string& filePath);  // JSON を読み込み、ActionMap / Action / Binding を再構築する
	InputAction* FindAction(const std::string& actionPath);  // Player/Submit のような ActionPath で Action を返す
	const InputAction* FindAction(const std::string& actionPath) const;  // 読み取り専用版
	InputActionMap* FindActionMap(const std::string& actionMapName);  // ActionMap 名で編集用 ActionMap を返す
	const InputActionMap* FindActionMap(const std::string& actionMapName) const;  // 読み取り専用版
	const std::string& GetLastError() const;  // 最後の読込失敗理由を返す
	void Clear();  // 読込済みデータとエラー文を初期化する

	std::vector<InputActionMap> actionMaps;  // Asset に含まれる ActionMap 一覧

private:
	std::string lastError_;  // JSON 解析失敗時の理由
};

#pragma warning(pop)
