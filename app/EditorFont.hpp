#pragma once

#include <QFont>

class QWidget;

// The text editors' font (songs, scores, the songbook's LaTeX, Changes): the system's fixed font,
// made bigger or smaller by View > Bigger Text / Smaller Text (`editor/sizeStep` in the settings).
namespace editorfont {

QFont font();
int sizeStep();
// Clamped to -6..12 steps of one point; every editor under `root` gets the new font.
void setSizeStep(int step, QWidget* root);

} // namespace editorfont
