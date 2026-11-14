#include "SoundTools.hpp"

#include "convert.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <zub/core/midi.hpp>

QString SoundTools::missingHint() const {
    if (fluidsynth.isEmpty())
        return QCoreApplication::translate("SoundTools",
                                           "Sound needs FluidSynth: install the fluidsynth "
                                           "package.");
    if (soundfont.empty())
        return QCoreApplication::translate(
            "SoundTools", "Sound needs a General MIDI soundfont: install a package such as "
                          "fluid-soundfont-gm, or set playback/soundfont in the settings.");
    return {};
}

SoundTools findSoundTools() {
    SoundTools tools;
    tools.fluidsynth = QStandardPaths::findExecutable(QStringLiteral("fluidsynth"));
    const QString chosen = QSettings().value(QStringLiteral("playback/soundfont")).toString();
    if (!chosen.isEmpty() && QFileInfo(chosen).isFile()) {
        tools.soundfont = toPath(chosen);
    } else if (const auto found = zub::midi::findSoundfont(zub::midi::defaultSoundfonts())) {
        tools.soundfont = *found;
    }
    return tools;
}

std::string soundfontIdentity(const std::filesystem::path& soundfont) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(soundfont, ec);
    const auto time = std::filesystem::last_write_time(soundfont, ec);
    return toStdString(toQString(soundfont)) + '\n' + (ec ? std::string() : std::to_string(size)) +
           '\n' + (ec ? std::string() : std::to_string(time.time_since_epoch().count()));
}
