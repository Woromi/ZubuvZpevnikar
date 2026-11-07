#include "zub/core/textfile.hpp"

#include <cerrno>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace zub {

namespace {

// UTF-8 text of a path (see utf8() in library.cpp).
std::string utf8(const fs::path& p) {
    const auto u8 = p.generic_u8string();
    return {u8.begin(), u8.end()};
}

std::string failure(const fs::path& path, const std::string& reason) {
    return utf8(path) + ": " + reason;
}

// Reason for a failed stream operation: errno if the C library set it, else a generic text.
std::string streamReason(int savedErrno, const char* fallback) {
    return savedErrno != 0 ? std::generic_category().message(savedErrno) : fallback;
}

fs::path randomTempName(const fs::path& path) {
    static std::random_device device;
    std::uniform_int_distribution<unsigned long long> dist;
    std::ostringstream name;
    name << '.' << utf8(path.filename()) << ".tmp-" << std::hex << dist(device);
    return path.parent_path() / fs::path(name.str());
}

} // namespace

std::expected<std::string, std::string> readTextFile(const fs::path& path) {
    std::error_code ec;
    const auto status = fs::status(path, ec);
    if (ec)
        return std::unexpected(failure(path, ec.message()));
    if (status.type() == fs::file_type::not_found)
        return std::unexpected(
            failure(path, std::make_error_code(std::errc::no_such_file_or_directory).message()));
    if (status.type() == fs::file_type::directory)
        return std::unexpected(
            failure(path, std::make_error_code(std::errc::is_a_directory).message()));

    errno = 0;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::unexpected(failure(path, streamReason(errno, "cannot open the file")));
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad())
        return std::unexpected(failure(path, "read error"));
    return std::move(buffer).str();
}

std::expected<void, std::string> writeTextFileAtomic(const fs::path& requested,
                                                     std::string_view content) {
    std::error_code ec;

    // Write through symlinks: the temp file goes next to the real target and replaces it, so
    // a symlinked song stays a symlink.
    fs::path path = requested;
    if (fs::exists(requested, ec)) {
        std::error_code canonicalEc;
        auto resolved = fs::canonical(requested, canonicalEc);
        if (!canonicalEc)
            path = std::move(resolved);
    }

    // Pick a temp name that does not exist yet.
    fs::path temp;
    bool found = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        temp = randomTempName(path);
        // status() reports "not found" through the type; ec may be set for it, so ignore ec.
        if (fs::symlink_status(temp, ec).type() == fs::file_type::not_found) {
            found = true;
            break;
        }
    }
    if (!found)
        return std::unexpected(failure(path, "cannot choose a temporary file name"));

    const auto fail = [&](const std::string& reason) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        return std::unexpected(failure(path, reason));
    };

    {
        errno = 0;
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out)
            return std::unexpected(
                failure(path, streamReason(errno, "cannot create a temporary file")));
        // From here on the temp file exists.
        errno = 0;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        const int writeErrno = errno;
        out.close();
        if (!out)
            return fail(streamReason(writeErrno, "write error"));
    }

    std::error_code statusEc;
    const auto existing = fs::status(path, statusEc);
    if (!statusEc && fs::exists(existing)) {
        fs::permissions(temp, existing.permissions(), fs::perm_options::replace, ec);
        if (ec)
            return fail(ec.message());
    }

    fs::rename(temp, path, ec);
    if (ec)
        return fail(ec.message());
    return {};
}

} // namespace zub
