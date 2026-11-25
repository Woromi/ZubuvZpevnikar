#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>

// lyluatex's cache of engraved scores (`tmp-ly/` in a build's work dir): every file is named
// after the md5 of its score's code and options, `<md5>.pdf`, `<md5>-1.eps`, `<md5>.bbox`, ...
// The app keeps the scores builds still use and drops the others (buildfiles).
namespace zub::scorecache {

// The scores a LuaLaTeX build used, from its log: lyluatex names each one, compiled or taken
// from the cache ("Compiling score tmp-ly/<md5> ..."). The log's line breaks may fall anywhere.
std::set<std::string> usedScores(std::string_view log);

// The score a file of the cache belongs to: its 32 hex digits, when the name starts with them
// and goes on with '.' or '-' (or ends); nullopt for any other file.
std::optional<std::string> scoreOf(std::string_view fileName);

} // namespace zub::scorecache
