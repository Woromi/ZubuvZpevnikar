#pragma once

// The only place where std types and Qt types are converted (see CLAUDE.md).

#include <QByteArrayView>
#include <QString>
#include <filesystem>
#include <string>
#include <string_view>

inline QString toQString(std::string_view utf8) {
    return QString::fromUtf8(QByteArrayView{utf8});
}

// Exact match, so std::string is not ambiguous between string_view and path.
inline QString toQString(const std::string& utf8) {
    return toQString(std::string_view{utf8});
}

inline QString toQString(const std::filesystem::path& path) {
    return QString::fromStdU16String(path.u16string());
}

inline std::string toStdString(const QString& text) {
    return text.toStdString();
}

inline std::filesystem::path toPath(const QString& path) {
    return std::filesystem::path{path.toStdU16String()};
}
