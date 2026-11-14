#include "WavFile.hpp"

#include "convert.hpp"

#include <QFile>
#include <string_view>
#include <zub/core/midi.hpp>

bool trimWavFile(const std::filesystem::path& wav) {
    QFile file(toQString(wav));
    if (!file.open(QIODevice::ReadWrite))
        return false;
    const qint64 size = file.size();
    std::optional<zub::midi::WavTrim> plan;
    {
        uchar* mapped = size > 0 ? file.map(0, size) : nullptr;
        if (!mapped)
            return size == 0; // an empty file needs no trim; one that cannot be mapped is an error
        plan = zub::midi::trimPlan(
            std::string_view(reinterpret_cast<const char*>(mapped), static_cast<std::size_t>(size)));
        file.unmap(mapped);
    }
    if (!plan)
        return true;
    const auto putLe32 = [&file](qint64 at, unsigned value) {
        char bytes[4];
        for (int i = 0; i < 4; ++i)
            bytes[i] = static_cast<char>((value >> (8 * i)) & 0xff);
        return file.seek(at) && file.write(bytes, 4) == 4;
    };
    return putLe32(static_cast<qint64>(plan->dataSizeAt), plan->dataSize) &&
           putLe32(4, static_cast<unsigned>(plan->size - 8)) &&
           file.resize(static_cast<qint64>(plan->size)) && file.flush();
}
