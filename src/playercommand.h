#pragma once
#include "soundcloud.h"
#include "plugins.h"

// Shared by the application binding and command-route regression tests.
inline auto playerCommandHandler(SoundCloud &soundcloud) {
    return [&soundcloud](const QString &command, const QJsonObject &args) -> QString {
        const auto state = soundcloud.state();
        const auto track = state.value("current").toMap();
        if (command == "player.repeat") { soundcloud.setRepeat(args["enabled"].toBool()); return {}; }
        if (command == "player.shuffle") { soundcloud.setShuffle(args["enabled"].toBool()); return {}; }
        if (command == "player.volume") { soundcloud.volume(args["value"].toDouble()); return {}; }
        if (command == "player.seekRelative") {
            qint64 target = 0;
            if (!Plugins::relativeSeekTarget(soundcloud.position(), soundcloud.duration(),
                                             args["offsetMs"].toInteger(), soundcloud.seekable(), &target))
                return "Перемотка недоступна или позиция трека некорректна.";
            soundcloud.seek(target);
            return {};
        }
        if (track.isEmpty()) return "Нет текущего трека.";
        if (command == "player.pause") soundcloud.pausePlayback();
        else if (command == "player.resume") soundcloud.resumePlayback();
        else if (command == "player.next") {
            if (!soundcloud.canGoNext()) return "Нет следующего трека.";
            soundcloud.next();
        } else if (command == "player.previous") {
            if (!soundcloud.canGoPrevious()) return "Нет предыдущего трека.";
            soundcloud.previous();
        }
        else if (command == "player.seek") {
            if (!soundcloud.seekable() || soundcloud.duration() <= 0) return "Перемотка недоступна.";
            soundcloud.seek(qMin(args["positionMs"].toInteger(), soundcloud.duration()));
        } else return "Неизвестная команда плеера.";
        return {};
    };
}
