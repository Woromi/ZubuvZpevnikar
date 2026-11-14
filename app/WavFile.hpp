#pragma once

#include <filesystem>

// Cuts the silent tail off a WAV file in place (zub::midi::trimPlan): the file is mapped, not read
// into memory, and only the two size fields are patched before it is truncated. True when the file
// was trimmed or needed no trim, false on an I/O error (the file may then be damaged: delete it).
bool trimWavFile(const std::filesystem::path& wav);
