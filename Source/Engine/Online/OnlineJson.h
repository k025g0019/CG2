#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4820)

//================================================================
// Online モジュール専用の最小 JSON
//================================================================
// Worker とやり取りする JSON はキー・文字列・数値・真偽・配列・オブジェクト
// だけで足りるため、外部ライブラリを増やさずここで完結させる。
// Online 以外から使う想定は無い(汎用 JSON が必要になったら別途用意する)。

enum class OnlineJsonType : int32_t {
	Null = 0,
	Bool,
	Number,
	String,
	Array,
	Object,
};

class OnlineJsonValue {
public:
	OnlineJsonType type = OnlineJsonType::Null;
	bool boolValue = false;
	double numberValue = 0.0;
	std::string stringValue;
	std::vector<OnlineJsonValue> arrayValues;
	std::vector<std::pair<std::string, OnlineJsonValue>> objectValues;

	const OnlineJsonValue* Find(const std::string& key) const;  // Object の子を探す。無ければ nullptr。
	std::string GetString(const std::string& key, const std::string& fallbackValue = std::string()) const;
	double GetNumber(const std::string& key, double fallbackValue = 0.0) const;
	int64_t GetInt64(const std::string& key, int64_t fallbackValue = 0) const;
	int32_t GetInt32(const std::string& key, int32_t fallbackValue = 0) const;
	bool GetBool(const std::string& key, bool fallbackValue = false) const;
	const OnlineJsonValue* FindArray(const std::string& key) const;  // Array の子だけを返す。
};

namespace OnlineJson {
	// text を解析する。失敗したら false を返し、outValue は Null のままにする。
	bool Parse(const std::string& text, OnlineJsonValue& outValue);

	// 文字列を JSON の "..." へ変換する(前後の二重引用符付き)。
	std::string EscapeString(const std::string& text);

	// {"key":value,...} を組み立てる補助。value は既に JSON 表現になっているものを渡す。
	class ObjectWriter {
	public:
		ObjectWriter& AddString(const std::string& key, const std::string& value);
		ObjectWriter& AddNumber(const std::string& key, double value);
		ObjectWriter& AddInt64(const std::string& key, int64_t value);
		ObjectWriter& AddBool(const std::string& key, bool value);
		ObjectWriter& AddRaw(const std::string& key, const std::string& jsonText);
		std::string Build() const;

	private:
		std::vector<std::string> members_;
	};
}

#pragma warning(pop)
