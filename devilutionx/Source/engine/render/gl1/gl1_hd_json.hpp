/**
 * @file gl1_hd_json.hpp
 *
 * A small JSON reader for HD pack manifests (RFC 8259, UTF-8, no extensions).
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace devilution {

class HdJsonValue {
public:
	enum class Type : uint8_t {
		Null,
		Bool,
		Number,
		String,
		Array,
		Object,
	};

	using Array = std::vector<HdJsonValue>;
	using Object = std::vector<std::pair<std::string, HdJsonValue>>;

	[[nodiscard]] Type type() const { return type_; }
	[[nodiscard]] bool IsNumber() const { return type_ == Type::Number; }
	[[nodiscard]] bool IsString() const { return type_ == Type::String; }
	[[nodiscard]] bool IsArray() const { return type_ == Type::Array; }
	[[nodiscard]] bool IsObject() const { return type_ == Type::Object; }

	[[nodiscard]] bool AsBool() const { return bool_; }
	[[nodiscard]] double AsNumber() const { return number_; }
	[[nodiscard]] const std::string &AsString() const { return string_; }
	[[nodiscard]] const Array &AsArray() const { return array_; }
	[[nodiscard]] const Object &AsObject() const { return object_; }

	/** @brief Member of an object, or null if absent or not an object. */
	[[nodiscard]] const HdJsonValue *Find(std::string_view key) const;

	/** @brief Integer member, if present and a whole number in [min, max]. */
	[[nodiscard]] std::optional<int> GetInt(std::string_view key, int min, int max) const;
	[[nodiscard]] std::optional<std::string_view> GetString(std::string_view key) const;

	static HdJsonValue MakeNull() { return HdJsonValue(Type::Null); }
	static HdJsonValue MakeBool(bool value);
	static HdJsonValue MakeNumber(double value);
	static HdJsonValue MakeString(std::string value);
	static HdJsonValue MakeArray(Array value);
	static HdJsonValue MakeObject(Object value);

private:
	explicit HdJsonValue(Type type)
	    : type_(type)
	{
	}

	Type type_;
	bool bool_ = false;
	double number_ = 0;
	std::string string_;
	Array array_;
	Object object_;
};

/**
 * @brief Parses a complete JSON document.
 * @param error Set to a short description on failure.
 */
std::optional<HdJsonValue> ParseHdJson(std::string_view text, std::string *error = nullptr);

} // namespace devilution
