#pragma once

class QProcess;

// A started program together with everything it starts (lualatex → lilypond → gs, bash →
// lualatex, git → git for submodules), so that Cancel and quitting stop all of it.
// Unix: the program leads its own process group, which gets the signals (also after the leader
// has exited). Windows: the program is put into a job object when it has started; terminating
// the job ends every process in it, and closing it (when the QProcess is deleted or started
// again) does the same, so nothing outlives its QProcess.
namespace processtree {

// Before every start() of `process` (a QProcess may be started again).
void prepare(QProcess* process);

// Asks the tree to stop: SIGTERM to the group on Unix. Windows has no gentle way for console
// programs: the same as kill().
void terminate(QProcess* process);

// Ends the tree now: SIGKILL to the group, or the job terminated; then QProcess::kill(). Does not
// wait.
void kill(QProcess* process);

} // namespace processtree
