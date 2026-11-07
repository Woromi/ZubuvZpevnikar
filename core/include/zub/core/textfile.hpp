#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace zub {

// Reads the whole file as raw bytes: no newline or encoding conversion. On failure returns
// a short message that includes the path and the reason.
std::expected<std::string, std::string> readTextFile(const std::filesystem::path& path);

// Replaces the file with `content` atomically: writes a temporary file in the same directory,
// flushes it, then renames it over `path`. The rename protects against the app crashing
// mid-save (the file is either the old or the new content, never half-written); without an
// fsync it does not guarantee durability on power loss. If `path` is a symlink the target is
// replaced and the link kept. If `path` already exists its permissions are kept. The
// temporary file is removed on failure. On failure returns a short message that includes the path
// and the reason.
std::expected<void, std::string> writeTextFileAtomic(const std::filesystem::path& path,
                                                     std::string_view content);

} // namespace zub
