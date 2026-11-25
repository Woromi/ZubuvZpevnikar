#pragma once

#include <QString>

// The interpreters the package's scripts need, found the way each system has them.
namespace externaltools {

// bash for the package's scripts/build.sh. Unix: "bash" (from PATH). Windows: the bin\bash.exe of
// a Git for Windows folder (its InstallPath in the registry, the usual install folders, above the
// git on PATH); never System32\bash.exe, which starts WSL. "" when there is none.
QString bash();

// Python 3 for the package's scripts (checkkeys.py, ...). Unix: "python3". Windows: the first of
// python3, python and the py launcher on PATH that runs as Python 3 (the Microsoft Store stubs
// do not: they offer the Store); asked once per run. "" when there is none.
QString python();

} // namespace externaltools
