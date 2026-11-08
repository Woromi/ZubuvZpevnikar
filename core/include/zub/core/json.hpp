#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// A small JSON reader for the output of chords_converter (suggest --json): objects, arrays,
// strings (with \uXXXX escapes and surrogate pairs, decoded to UTF-8), integers, floating point
// numbers, true, false and null. Not a general-purpose library: nesting is limited to 200 levels,
// duplicate object keys are kept (find() returns the first).
namespace zub::json {

struct Value {
    using Array = std::vector<Value>;
    using Member = std::pair<std::string, Value>;
    using Object = std::vector<Member>;

    std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> data;

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(data); }
    // The accessors return nullopt / nullptr when the value has another type. asInt() also
    // accepts a floating point number with an integral value.
    std::optional<bool> asBool() const;
    std::optional<std::int64_t> asInt() const;
    const std::string* asString() const;
    const Array* asArray() const;
    const Object* asObject() const;
    // The value of `key` if this is an object that has it, else nullptr.
    const Value* find(std::string_view key) const;
};

// The whole text must be one JSON value (surrounding white space is fine, anything else after
// it is an error). The error message names the byte offset.
std::expected<Value, std::string> parse(std::string_view text);

} // namespace zub::json
