#include <vector>
#include <zub/core/desktopentry.hpp>

namespace zub::desktopentry {

namespace {

// The value of a string key: a backslash is written as two.
std::string stringValue(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '\\')
            out += '\\';
        out += c;
    }
    return out;
}

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const auto end = text.find('\n');
        out.push_back(text.substr(0, end));
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return out;
}

std::string_view trimmedCr(std::string_view line) {
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    return line;
}

bool isKey(std::string_view line, std::string_view key) {
    line = trimmedCr(line);
    if (!line.starts_with(key))
        return false;
    line.remove_prefix(key.size());
    while (!line.empty() && line.front() == ' ')
        line.remove_prefix(1);
    return line.starts_with('=');
}

} // namespace

std::string execArgument(std::string_view path) {
    std::string quoted = "\"";
    for (const char c : path) {
        if (c == '"' || c == '`' || c == '$' || c == '\\')
            quoted += '\\';
        quoted += c;
    }
    quoted += '"';
    std::string out;
    for (const char c : stringValue(quoted)) {
        if (c == '%')
            out += '%';
        out += c;
    }
    return out;
}

std::string forAppImage(std::string_view entry, std::string_view appImage) {
    std::string out;
    bool inMain = false;
    bool written = false;
    const auto addOurs = [&] {
        out += "Exec=" + execArgument(appImage) + "\n";
        out += "TryExec=" + stringValue(appImage) + "\n";
        out += std::string(kMarker) + "\n";
        written = true;
    };
    for (const std::string_view line : lines(entry)) {
        const std::string_view plain = trimmedCr(line);
        if (plain.starts_with('[')) {
            if (inMain && !written)
                addOurs();
            inMain = plain == "[Desktop Entry]";
            out += std::string(plain) + "\n";
            continue;
        }
        if (inMain) {
            if (isKey(plain, "TryExec") || plain == kMarker)
                continue;
            if (isKey(plain, "Exec")) {
                if (!written)
                    addOurs();
                continue;
            }
        }
        out += std::string(plain) + "\n";
    }
    if (!written) {
        if (!inMain)
            out += "[Desktop Entry]\n";
        addOurs();
    }
    return out;
}

bool writtenForAppImage(std::string_view entry) {
    for (const std::string_view line : lines(entry))
        if (trimmedCr(line) == kMarker)
            return true;
    return false;
}

} // namespace zub::desktopentry
