#pragma once

#include <QString>

#include <memory>

class QWidget;
class QTemporaryDir;

// Thin wrapper so the viewers never need #ifdefs: videoplayback.cpp is always
// compiled, and its stub reports that the build has no video support.
namespace VideoPlayback
{
    bool IsAvailable();
    bool IsVideoExtension(const QString &fileName);

    // Opens filePath in a player window. Takes ownership of tempDir, which is
    // removed when the player closes. Returns false when playback is not
    // available or the media backend rejects the format.
    bool Open(QWidget *parent, const QString &filePath, const QString &title,
            std::unique_ptr<QTemporaryDir> tempDir);
}
