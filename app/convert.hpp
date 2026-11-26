#pragma once

// The only place where std types and Qt types are converted (see CLAUDE.md).

#include <QByteArrayView>
#include <QDateTime>
#include <QString>
#include <chrono>
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

// With '/' between folders on every system, as Qt writes paths (QDir::toNativeSeparators for
// showing them the Windows way).
inline QString toQString(const std::filesystem::path& path) {
    return QString::fromStdU16String(path.generic_u16string());
}

inline std::string toStdString(const QString& text) {
    return text.toStdString();
}

inline std::filesystem::path toPath(const QString& path) {
    return std::filesystem::path{path.toStdU16String()};
}

// A path as UTF-8 with '/' between folders, and back. Never path::string(), generic_string() or a
// path made from a std::string: on Windows they use the ANSI code page and throw on names it
// cannot hold (Czech letters).
inline std::string toUtf8(const std::filesystem::path& path) {
    const auto u8 = path.generic_u8string();
    return {u8.begin(), u8.end()};
}

inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path{std::u8string(utf8.begin(), utf8.end())};
}

inline QDateTime toDateTime(std::chrono::sys_seconds time) {
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(time.time_since_epoch().count()));
}
