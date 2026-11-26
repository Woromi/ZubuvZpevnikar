#pragma once

class QProcess;

// A started program together with everything it starts (lualatex → lilypond → gs, bash →
// lualatex, git → git for submodules), so that Cancel and quitting stop all of it.
// Unix: the program leads its own process group, which gets the signals (also after the leader
// has exited). Windows: the program is created inside a job object (a job list in its startup
// info; on failure it is put in right after it started); terminating the job ends every process
// in it, and closing it (when the QProcess is deleted or started again) does the same, so nothing
// outlives its QProcess. prepare() sets the QProcess's CreateProcess arguments modifier.
namespace processtree {

// Before every start() of `process` (a QProcess may be started again).
void prepare(QProcess* process);

// Asks the tree to stop: SIGTERM to the group on Unix. Windows has no gentle way for console
// programs: the same as kill().
void terminate(QProcess* process);

// Ends the tree now: SIGKILL to the group, or the job terminated; then QProcess::kill(). Does not
// wait.
void kill(QProcess* process);

// Windows: whether the running program was created inside its own job (not put in after it
// started). Unix: false. For the tests.
bool createdInJob(QProcess* process);

} // namespace processtree
