#include "OnlineJson.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
	// 解析位置を進めながら 1 つの値を読む再帰下降パーサ。
	class Parser {
	public:
		explicit Parser(const std::string& text)
			: text_(text) {
		}

		bool ParseValue(OnlineJsonValue& outValue) {
			SkipWhitespace();

			if (position_ >= text_.size()) {
				return false;
			}

			const char character = text_[position_];

			switch (character) {
			case '{':
				return ParseObject(outValue);
			case '[':
				return ParseArray(outValue);
			case '"': {
				std::string parsedText;

				if (!ParseString(parsedText)) {
					return false;
				}

				outValue.type = OnlineJsonType::String;
				outValue.stringValue = parsedText;
				return true;
			}
			case 't':
				return ParseLiteral("true", OnlineJsonType::Bool, true, outValue);
			case 'f':
				return ParseLiteral("false", OnlineJsonType::Bool, false, outValue);
			case 'n':
				return ParseLiteral("null", OnlineJsonType::Null, false, outValue);
			default:
				return ParseNumber(outValue);
			}
		}

		void SkipWhitespace() {
			while (position_ < text_.size()) {
				const char character = text_[position_];

				if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
					++position_;
					continue;
				}

				break;
			}
		}

		bool IsAtEndAfterWhitespace() {
			SkipWhitespace();
			return position_ >= text_.size();
		}

	private:
		bool ParseLiteral(
			const char* literal,
			OnlineJsonType type,
			bool boolValue,
			OnlineJsonValue& outValue) {
			const std::string literalText(literal);

			if (text_.compare(position_, literalText.size(), literalText) != 0) {
				return false;
			}

			position_ += literalText.size();
			outValue.type = type;
			outValue.boolValue = boolValue;
			return true;
		}

		bool ParseNumber(OnlineJsonValue& outValue) {
			const size_t startPosition = position_;

			if (position_ < text_.size() && (text_[position_] == '-' || text_[position_] == '+')) {
				++position_;
			}

			bool hasDigit = false;

			while (position_ < text_.size()) {
				const char character = text_[position_];

				if (character >= '0' && character <= '9') {
					hasDigit = true;
					++position_;
					continue;
				}

				if (character == '.' || character == 'e' || character == 'E' ||
					character == '-' || character == '+') {
					++position_;
					continue;
				}

				break;
			}

			if (!hasDigit) {
				return false;
			}

			const std::string numberText = text_.substr(startPosition, position_ - startPosition);
			outValue.type = OnlineJsonType::Number;
			outValue.numberValue = std::strtod(numberText.c_str(), nullptr);
			return true;
		}

		bool ParseString(std::string& outText) {
			if (position_ >= text_.size() || text_[position_] != '"') {
				return false;
			}

			++position_;
			outText.clear();

			while (position_ < text_.size()) {
				const char character = text_[position_];

				if (character == '"') {
					++position_;
					return true;
				}

				if (character != '\\') {
					outText.push_back(character);
					++position_;
					continue;
				}

				++position_;

				if (position_ >= text_.size()) {
					return false;
				}

				const char escaped = text_[position_];
				++position_;

				switch (escaped) {
				case '"':
					outText.push_back('"');
					break;
				case '\\':
					outText.push_back('\\');
					break;
				case '/':
					outText.push_back('/');
					break;
				case 'b':
					outText.push_back('\b');
					break;
				case 'f':
					outText.push_back('\f');
					break;
				case 'n':
					outText.push_back('\n');
					break;
				case 'r':
					outText.push_back('\r');
					break;
				case 't':
					outText.push_back('\t');
					break;
				case 'u': {
					// \uXXXX は UTF-8 へ変換する。サロゲートペアは 2 組続けて来る前提で扱う。
					if (position_ + 4u > text_.size()) {
						return false;
					}

					const std::string hexText = text_.substr(position_, 4u);
					position_ += 4u;
					uint32_t codePoint = static_cast<uint32_t>(std::strtoul(hexText.c_str(), nullptr, 16));

					if (codePoint >= 0xD800u && codePoint <= 0xDBFFu &&
						position_ + 6u <= text_.size() &&
						text_[position_] == '\\' && text_[position_ + 1u] == 'u') {
						const std::string lowText = text_.substr(position_ + 2u, 4u);
						const uint32_t lowSurrogate =
							static_cast<uint32_t>(std::strtoul(lowText.c_str(), nullptr, 16));

						if (lowSurrogate >= 0xDC00u && lowSurrogate <= 0xDFFFu) {
							position_ += 6u;
							codePoint = 0x10000u +
								((codePoint - 0xD800u) << 10) + (lowSurrogate - 0xDC00u);
						}
					}

					AppendUtf8(codePoint, outText);
					break;
				}
				default:
					outText.push_back(escaped);
					break;
				}
			}

			return false;
		}

		static void AppendUtf8(uint32_t codePoint, std::string& outText) {
			if (codePoint < 0x80u) {
				outText.push_back(static_cast<char>(codePoint));
			}
			else if (codePoint < 0x800u) {
				outText.push_back(static_cast<char>(0xC0u | (codePoint >> 6)));
				outText.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
			}
			else if (codePoint < 0x10000u) {
				outText.push_back(static_cast<char>(0xE0u | (codePoint >> 12)));
				outText.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
				outText.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
			}
			else {
				outText.push_back(static_cast<char>(0xF0u | (codePoint >> 18)));
				outText.push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu)));
				outText.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
				outText.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
			}
		}

		bool ParseArray(OnlineJsonValue& outValue) {
			if (position_ >= text_.size() || text_[position_] != '[') {
				return false;
			}

			++position_;
			outValue.type = OnlineJsonType::Array;
			SkipWhitespace();

			if (position_ < text_.size() && text_[position_] == ']') {
				++position_;
				return true;
			}

			while (position_ < text_.size()) {
				OnlineJsonValue elementValue{};

				if (!ParseValue(elementValue)) {
					return false;
				}

				outValue.arrayValues.push_back(std::move(elementValue));
				SkipWhitespace();

				if (position_ >= text_.size()) {
					return false;
				}

				if (text_[position_] == ',') {
					++position_;
					continue;
				}

				if (text_[position_] == ']') {
					++position_;
					return true;
				}

				return false;
			}

			return false;
		}

		bool ParseObject(OnlineJsonValue& outValue) {
			if (position_ >= text_.size() || text_[position_] != '{') {
				return false;
			}

			++position_;
			outValue.type = OnlineJsonType::Object;
			SkipWhitespace();

			if (position_ < text_.size() && text_[position_] == '}') {
				++position_;
				return true;
			}

			while (position_ < text_.size()) {
				SkipWhitespace();
				std::string key;

				if (!ParseString(key)) {
					return false;
				}

				SkipWhitespace();

				if (position_ >= text_.size() || text_[position_] != ':') {
					return false;
				}

				++position_;
				OnlineJsonValue memberValue{};

				if (!ParseValue(memberValue)) {
					return false;
				}

				outValue.objectValues.emplace_back(key, std::move(memberValue));
				SkipWhitespace();

				if (position_ >= text_.size()) {
					return false;
				}

				if (text_[position_] == ',') {
					++position_;
					continue;
				}

				if (text_[position_] == '}') {
					++position_;
					return true;
				}

				return false;
			}

			return false;
		}

		const std::string& text_;
		size_t position_ = 0u;
	};
}

const OnlineJsonValue* OnlineJsonValue::Find(const std::string& key) const {
	if (type != OnlineJsonType::Object) {
		return nullptr;
	}

	for (const std::pair<std::string, OnlineJsonValue>& member : objectValues) {
		if (member.first == key) {
			return &member.second;
		}
	}

	return nullptr;
}

std::string OnlineJsonValue::GetString(const std::string& key, const std::string& fallbackValue) const {
	const OnlineJsonValue* member = Find(key);

	if (member == nullptr) {
		return fallbackValue;
	}

	if (member->type == OnlineJsonType::String) {
		return member->stringValue;
	}

	if (member->type == OnlineJsonType::Number) {
		// 数値で来た ID も文字列として受け取れるようにする。
		char buffer[64] = {};
		const double rounded = std::floor(member->numberValue);

		if (std::fabs(member->numberValue - rounded) < 0.000001) {
			std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(rounded));
		}
		else {
			std::snprintf(buffer, sizeof(buffer), "%g", member->numberValue);
		}

		return std::string(buffer);
	}

	return fallbackValue;
}

double OnlineJsonValue::GetNumber(const std::string& key, double fallbackValue) const {
	const OnlineJsonValue* member = Find(key);

	if (member == nullptr) {
		return fallbackValue;
	}

	if (member->type == OnlineJsonType::Number) {
		return member->numberValue;
	}

	if (member->type == OnlineJsonType::String) {
		return std::strtod(member->stringValue.c_str(), nullptr);
	}

	return fallbackValue;
}

int64_t OnlineJsonValue::GetInt64(const std::string& key, int64_t fallbackValue) const {
	const double numberValue = GetNumber(key, static_cast<double>(fallbackValue));
	return static_cast<int64_t>(numberValue);
}

int32_t OnlineJsonValue::GetInt32(const std::string& key, int32_t fallbackValue) const {
	const double numberValue = GetNumber(key, static_cast<double>(fallbackValue));
	return static_cast<int32_t>(numberValue);
}

bool OnlineJsonValue::GetBool(const std::string& key, bool fallbackValue) const {
	const OnlineJsonValue* member = Find(key);

	if (member == nullptr) {
		return fallbackValue;
	}

	if (member->type == OnlineJsonType::Bool) {
		return member->boolValue;
	}

	if (member->type == OnlineJsonType::Number) {
		return member->numberValue != 0.0;
	}

	return fallbackValue;
}

const OnlineJsonValue* OnlineJsonValue::FindArray(const std::string& key) const {
	const OnlineJsonValue* member = Find(key);
	return member != nullptr && member->type == OnlineJsonType::Array ? member : nullptr;
}

namespace OnlineJson {
	bool Parse(const std::string& text, OnlineJsonValue& outValue) {
		outValue = OnlineJsonValue{};

		if (text.empty()) {
			return false;
		}

		Parser parser(text);

		if (!parser.ParseValue(outValue)) {
			outValue = OnlineJsonValue{};
			return false;
		}

		return true;
	}

	std::string EscapeString(const std::string& text) {
		std::string escapedText;
		escapedText.reserve(text.size() + 2u);
		escapedText.push_back('"');

		for (const char character : text) {
			switch (character) {
			case '"':
				escapedText += "\\\"";
				break;
			case '\\':
				escapedText += "\\\\";
				break;
			case '\n':
				escapedText += "\\n";
				break;
			case '\r':
				escapedText += "\\r";
				break;
			case '\t':
				escapedText += "\\t";
				break;
			default:
				if (static_cast<unsigned char>(character) < 0x20u) {
					char buffer[8] = {};
					std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(character));
					escapedText += buffer;
				}
				else {
					escapedText.push_back(character);
				}
				break;
			}
		}

		escapedText.push_back('"');
		return escapedText;
	}

	ObjectWriter& ObjectWriter::AddString(const std::string& key, const std::string& value) {
		members_.push_back(EscapeString(key) + ":" + EscapeString(value));
		return *this;
	}

	ObjectWriter& ObjectWriter::AddNumber(const std::string& key, double value) {
		char buffer[64] = {};
		std::snprintf(buffer, sizeof(buffer), "%.10g", value);
		members_.push_back(EscapeString(key) + ":" + buffer);
		return *this;
	}

	ObjectWriter& ObjectWriter::AddInt64(const std::string& key, int64_t value) {
		members_.push_back(EscapeString(key) + ":" + std::to_string(value));
		return *this;
	}

	ObjectWriter& ObjectWriter::AddBool(const std::string& key, bool value) {
		members_.push_back(EscapeString(key) + ":" + (value ? "true" : "false"));
		return *this;
	}

	ObjectWriter& ObjectWriter::AddRaw(const std::string& key, const std::string& jsonText) {
		members_.push_back(EscapeString(key) + ":" + jsonText);
		return *this;
	}

	std::string ObjectWriter::Build() const {
		std::string objectText = "{";

		for (size_t memberIndex = 0u; memberIndex < members_.size(); ++memberIndex) {
			if (memberIndex > 0u) {
				objectText.push_back(',');
			}

			objectText += members_[memberIndex];
		}

		objectText.push_back('}');
		return objectText;
	}
}
