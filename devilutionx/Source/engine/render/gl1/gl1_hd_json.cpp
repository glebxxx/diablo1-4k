#include "engine/render/gl1/gl1_hd_json.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace devilution {

namespace {

constexpr int MaxDepth = 64;

class Parser {
public:
	explicit Parser(std::string_view text)
	    : text_(text)
	{
	}

	std::optional<HdJsonValue> ParseDocument()
	{
		SkipWhitespace();
		std::optional<HdJsonValue> value = ParseValue(0);
		if (!value)
			return std::nullopt;
		SkipWhitespace();
		if (pos_ != text_.size())
			return Fail("trailing characters");
		return value;
	}

	std::string error;

private:
	std::nullopt_t Fail(const char *message)
	{
		if (error.empty())
			error = std::string(message) + " at offset " + std::to_string(pos_);
		return std::nullopt;
	}

	void SkipWhitespace()
	{
		while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r'))
			++pos_;
	}

	bool Consume(std::string_view literal)
	{
		if (text_.substr(pos_, literal.size()) != literal)
			return false;
		pos_ += literal.size();
		return true;
	}

	std::optional<HdJsonValue> ParseValue(int depth)
	{
		if (depth > MaxDepth)
			return Fail("nesting too deep");
		if (pos_ >= text_.size())
			return Fail("unexpected end");
		switch (text_[pos_]) {
		case '{':
			return ParseObject(depth);
		case '[':
			return ParseArray(depth);
		case '"': {
			std::optional<std::string> str = ParseString();
			if (!str)
				return std::nullopt;
			return HdJsonValue::MakeString(std::move(*str));
		}
		case 't':
			if (Consume("true"))
				return HdJsonValue::MakeBool(true);
			break;
		case 'f':
			if (Consume("false"))
				return HdJsonValue::MakeBool(false);
			break;
		case 'n':
			if (Consume("null"))
				return HdJsonValue::MakeNull();
			break;
		default:
			return ParseNumber();
		}
		return Fail("invalid literal");
	}

	std::optional<HdJsonValue> ParseObject(int depth)
	{
		++pos_; // '{'
		HdJsonValue::Object members;
		SkipWhitespace();
		if (pos_ < text_.size() && text_[pos_] == '}') {
			++pos_;
			return HdJsonValue::MakeObject(std::move(members));
		}
		while (true) {
			SkipWhitespace();
			if (pos_ >= text_.size() || text_[pos_] != '"')
				return Fail("expected member name");
			std::optional<std::string> name = ParseString();
			if (!name)
				return std::nullopt;
			SkipWhitespace();
			if (!Consume(":"))
				return Fail("expected ':'");
			SkipWhitespace();
			std::optional<HdJsonValue> value = ParseValue(depth + 1);
			if (!value)
				return std::nullopt;
			members.emplace_back(std::move(*name), std::move(*value));
			SkipWhitespace();
			if (Consume("}"))
				return HdJsonValue::MakeObject(std::move(members));
			if (!Consume(","))
				return Fail("expected ',' or '}'");
		}
	}

	std::optional<HdJsonValue> ParseArray(int depth)
	{
		++pos_; // '['
		HdJsonValue::Array elements;
		SkipWhitespace();
		if (Consume("]"))
			return HdJsonValue::MakeArray(std::move(elements));
		while (true) {
			SkipWhitespace();
			std::optional<HdJsonValue> value = ParseValue(depth + 1);
			if (!value)
				return std::nullopt;
			elements.push_back(std::move(*value));
			SkipWhitespace();
			if (Consume("]"))
				return HdJsonValue::MakeArray(std::move(elements));
			if (!Consume(","))
				return Fail("expected ',' or ']'");
		}
	}

	std::optional<uint32_t> ParseHex4()
	{
		if (pos_ + 4 > text_.size())
			return std::nullopt;
		uint32_t value = 0;
		for (int i = 0; i < 4; ++i) {
			const char c = text_[pos_++];
			value <<= 4;
			if (c >= '0' && c <= '9')
				value |= c - '0';
			else if (c >= 'a' && c <= 'f')
				value |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				value |= c - 'A' + 10;
			else
				return std::nullopt;
		}
		return value;
	}

	static void AppendUtf8(std::string &out, uint32_t cp)
	{
		if (cp < 0x80) {
			out += static_cast<char>(cp);
		} else if (cp < 0x800) {
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		} else {
			out += static_cast<char>(0xF0 | (cp >> 18));
			out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
	}

	std::optional<std::string> ParseString()
	{
		++pos_; // '"'
		std::string out;
		while (true) {
			if (pos_ >= text_.size())
				return Fail("unterminated string");
			const char c = text_[pos_++];
			if (c == '"')
				return out;
			if (static_cast<unsigned char>(c) < 0x20)
				return Fail("control character in string");
			if (c != '\\') {
				out += c;
				continue;
			}
			if (pos_ >= text_.size())
				return Fail("unterminated escape");
			const char e = text_[pos_++];
			switch (e) {
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u': {
				std::optional<uint32_t> cp = ParseHex4();
				if (!cp)
					return Fail("invalid \\u escape");
				if (*cp >= 0xD800 && *cp <= 0xDBFF) {
					if (!Consume("\\u"))
						return Fail("unpaired surrogate");
					std::optional<uint32_t> low = ParseHex4();
					if (!low || *low < 0xDC00 || *low > 0xDFFF)
						return Fail("unpaired surrogate");
					*cp = 0x10000 + ((*cp - 0xD800) << 10) + (*low - 0xDC00);
				} else if (*cp >= 0xDC00 && *cp <= 0xDFFF) {
					return Fail("unpaired surrogate");
				}
				AppendUtf8(out, *cp);
				break;
			}
			default:
				return Fail("invalid escape");
			}
		}
	}

	std::optional<HdJsonValue> ParseNumber()
	{
		const size_t start = pos_;
		const auto digits = [&]() {
			const size_t from = pos_;
			while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
				++pos_;
			return pos_ > from;
		};
		if (pos_ < text_.size() && text_[pos_] == '-')
			++pos_;
		if (pos_ < text_.size() && text_[pos_] == '0') {
			++pos_;
		} else if (!digits()) {
			return Fail("invalid value");
		}
		if (pos_ < text_.size() && text_[pos_] == '.') {
			++pos_;
			if (!digits())
				return Fail("invalid number");
		}
		if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
			++pos_;
			if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
				++pos_;
			if (!digits())
				return Fail("invalid number");
		}
		const std::string number(text_.substr(start, pos_ - start));
		return HdJsonValue::MakeNumber(std::strtod(number.c_str(), nullptr));
	}

	std::string_view text_;
	size_t pos_ = 0;
};

} // namespace

const HdJsonValue *HdJsonValue::Find(std::string_view key) const
{
	if (type_ != Type::Object)
		return nullptr;
	for (const auto &[name, value] : object_) {
		if (name == key)
			return &value;
	}
	return nullptr;
}

std::optional<int> HdJsonValue::GetInt(std::string_view key, int min, int max) const
{
	const HdJsonValue *value = Find(key);
	if (value == nullptr || !value->IsNumber())
		return std::nullopt;
	const double number = value->AsNumber();
	if (number != std::floor(number) || number < min || number > max)
		return std::nullopt;
	return static_cast<int>(number);
}

std::optional<std::string_view> HdJsonValue::GetString(std::string_view key) const
{
	const HdJsonValue *value = Find(key);
	if (value == nullptr || !value->IsString())
		return std::nullopt;
	return std::string_view(value->AsString());
}

HdJsonValue HdJsonValue::MakeBool(bool value)
{
	HdJsonValue result(Type::Bool);
	result.bool_ = value;
	return result;
}

HdJsonValue HdJsonValue::MakeNumber(double value)
{
	HdJsonValue result(Type::Number);
	result.number_ = value;
	return result;
}

HdJsonValue HdJsonValue::MakeString(std::string value)
{
	HdJsonValue result(Type::String);
	result.string_ = std::move(value);
	return result;
}

HdJsonValue HdJsonValue::MakeArray(Array value)
{
	HdJsonValue result(Type::Array);
	result.array_ = std::move(value);
	return result;
}

HdJsonValue HdJsonValue::MakeObject(Object value)
{
	HdJsonValue result(Type::Object);
	result.object_ = std::move(value);
	return result;
}

std::optional<HdJsonValue> ParseHdJson(std::string_view text, std::string *error)
{
	Parser parser(text);
	std::optional<HdJsonValue> result = parser.ParseDocument();
	if (!result && error != nullptr)
		*error = parser.error;
	return result;
}

} // namespace devilution
