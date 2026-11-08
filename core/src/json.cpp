#include <zub/core/json.hpp>

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>

namespace zub::json {

std::optional<bool> Value::asBool() const {
    if (const auto* b = std::get_if<bool>(&data))
        return *b;
    return std::nullopt;
}

std::optional<std::int64_t> Value::asInt() const {
    if (const auto* i = std::get_if<std::int64_t>(&data))
        return *i;
    if (const auto* d = std::get_if<double>(&data)) {
        if (std::isfinite(*d) && *d == std::floor(*d) && std::abs(*d) < 9e15)
            return static_cast<std::int64_t>(*d);
    }
    return std::nullopt;
}

const std::string* Value::asString() const {
    return std::get_if<std::string>(&data);
}

const Value::Array* Value::asArray() const {
    return std::get_if<Array>(&data);
}

const Value::Object* Value::asObject() const {
    return std::get_if<Object>(&data);
}

const Value* Value::find(std::string_view key) const {
    const auto* object = asObject();
    if (!object)
        return nullptr;
    for (const auto& [name, value] : *object) {
        if (name == key)
            return &value;
    }
    return nullptr;
}

namespace {

constexpr int maxDepth = 200;

void appendUtf8(std::string& out, char32_t cp) {
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

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::expected<Value, std::string> run() {
        skipSpace();
        auto value = parseValue(0);
        if (!value)
            return value;
        skipSpace();
        if (pos_ != text_.size())
            return fail("unexpected text after the value");
        return value;
    }

private:
    std::unexpected<std::string> fail(const std::string& what) const {
        return std::unexpected(what + " at byte " + std::to_string(pos_));
    }

    void skipSpace() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
                text_[pos_] == '\r'))
            ++pos_;
    }

    bool eat(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool eatWord(std::string_view word) {
        if (text_.substr(pos_).starts_with(word)) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    std::expected<Value, std::string> parseValue(int depth) {
        if (depth > maxDepth)
            return fail("nested too deeply");
        if (pos_ >= text_.size())
            return fail("unexpected end of text");
        const char c = text_[pos_];
        if (c == '{')
            return parseObject(depth);
        if (c == '[')
            return parseArray(depth);
        if (c == '"') {
            auto s = parseString();
            if (!s)
                return std::unexpected(s.error());
            return Value{std::move(*s)};
        }
        if (eatWord("true"))
            return Value{true};
        if (eatWord("false"))
            return Value{false};
        if (eatWord("null"))
            return Value{nullptr};
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
            return parseNumber();
        return fail("unexpected character");
    }

    std::expected<Value, std::string> parseObject(int depth) {
        ++pos_; // {
        Value::Object members;
        skipSpace();
        if (eat('}'))
            return Value{std::move(members)};
        for (;;) {
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"')
                return fail("expected a string key");
            auto key = parseString();
            if (!key)
                return std::unexpected(key.error());
            skipSpace();
            if (!eat(':'))
                return fail("expected ':'");
            skipSpace();
            auto value = parseValue(depth + 1);
            if (!value)
                return value;
            members.emplace_back(std::move(*key), std::move(*value));
            skipSpace();
            if (eat('}'))
                return Value{std::move(members)};
            if (!eat(','))
                return fail("expected ',' or '}'");
        }
    }

    std::expected<Value, std::string> parseArray(int depth) {
        ++pos_; // [
        Value::Array items;
        skipSpace();
        if (eat(']'))
            return Value{std::move(items)};
        for (;;) {
            skipSpace();
            auto value = parseValue(depth + 1);
            if (!value)
                return value;
            items.push_back(std::move(*value));
            skipSpace();
            if (eat(']'))
                return Value{std::move(items)};
            if (!eat(','))
                return fail("expected ',' or ']'");
        }
    }

    std::expected<char32_t, std::string> parseHex4() {
        if (text_.size() - pos_ < 4)
            return fail("truncated \\u escape");
        char32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_ + static_cast<std::size_t>(i)];
            int digit = 0;
            if (c >= '0' && c <= '9')
                digit = c - '0';
            else if (c >= 'a' && c <= 'f')
                digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                digit = c - 'A' + 10;
            else
                return fail("bad \\u escape");
            value = value * 16 + static_cast<char32_t>(digit);
        }
        pos_ += 4;
        return value;
    }

    std::expected<std::string, std::string> parseString() {
        ++pos_; // opening quote
        std::string out;
        for (;;) {
            if (pos_ >= text_.size())
                return fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"')
                return out;
            if (static_cast<unsigned char>(c) < 0x20)
                return fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size())
                return fail("unterminated string");
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
                auto unit = parseHex4();
                if (!unit)
                    return std::unexpected(unit.error());
                char32_t cp = *unit;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (!eatWord("\\u"))
                        return fail("lone high surrogate");
                    auto low = parseHex4();
                    if (!low)
                        return std::unexpected(low.error());
                    if (*low < 0xDC00 || *low > 0xDFFF)
                        return fail("bad surrogate pair");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (*low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return fail("lone low surrogate");
                }
                appendUtf8(out, cp);
                break;
            }
            default:
                --pos_;
                return fail("bad escape");
            }
        }
    }

    std::expected<Value, std::string> parseNumber() {
        const std::size_t start = pos_;
        eat('-');
        if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_])))
            return fail("bad number");
        if (text_[pos_] == '0') {
            ++pos_;
        } else {
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])))
                ++pos_;
        }
        bool integral = true;
        if (eat('.')) {
            integral = false;
            if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_])))
                return fail("bad number");
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])))
                ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            integral = false;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
                ++pos_;
            if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_])))
                return fail("bad number");
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])))
                ++pos_;
        }
        const char* first = text_.data() + start;
        const char* last = text_.data() + pos_;
        if (integral) {
            std::int64_t number = 0;
            const auto [ptr, ec] = std::from_chars(first, last, number);
            if (ec == std::errc{} && ptr == last)
                return Value{number};
            // too large for 64 bits: fall through to double
        }
        double number = 0;
        const auto [ptr, ec] = std::from_chars(first, last, number);
        if (ec != std::errc{} || ptr != last)
            return fail("bad number");
        return Value{number};
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace

std::expected<Value, std::string> parse(std::string_view text) {
    return Parser(text).run();
}

} // namespace zub::json
