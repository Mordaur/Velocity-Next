#include "videoplayback.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QSet>

#ifdef VELOCITY_HAS_VIDEO
#include <QMediaFormat>

#include "videodialog.h"
#endif

namespace
{
    QSet<QString> videoExtensions()
    {
        return {
            "wmv", "asf", "avi", "mp4", "m4v", "mov", "mkv", "webm", "bik"
        };
    }

#ifdef VELOCITY_HAS_VIDEO
    QMediaFormat::FileFormat expectedFormat(const QString &suffix)
    {
        if (suffix == "wmv" || suffix == "asf")
            return QMediaFormat::WMV;
        if (suffix == "avi")
            return QMediaFormat::AVI;
        if (suffix == "mkv")
            return QMediaFormat::Matroska;
        if (suffix == "mp4" || suffix == "m4v")
            return QMediaFormat::MPEG4;
        if (suffix == "mov")
            return QMediaFormat::QuickTime;
        if (suffix == "webm")
            return QMediaFormat::WebM;
        return QMediaFormat::UnspecifiedFormat;
    }
#endif
}

bool VideoPlayback::IsAvailable()
{
#ifdef VELOCITY_HAS_VIDEO
    return true;
#else
    return false;
#endif
}

bool VideoPlayback::IsVideoExtension(const QString &fileName)
{
    return videoExtensions().contains(QFileInfo(fileName).suffix().toLower());
}

bool VideoPlayback::Open(QWidget *parent, std::unique_ptr<QIODevice> device,
        const QString &sourceUrlHint, const QString &title)
{
#ifndef VELOCITY_HAS_VIDEO
    Q_UNUSED(sourceUrlHint);
    Q_UNUSED(title);

    QMessageBox::information(parent, QObject::tr("Video playback"),
            QObject::tr("This build was compiled without video support because Qt "
                        "Multimedia was not available."));
    return false;
#else
    auto *dialog = new VideoDialog(std::move(device), sourceUrlHint, title, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    return true;
#endif
}

bool VideoPlayback::Open(QWidget *parent, const QString &filePath, const QString &title,
        std::unique_ptr<QTemporaryDir> tempDir)
{
#ifndef VELOCITY_HAS_VIDEO
    Q_UNUSED(title);
    Q_UNUSED(tempDir);

    QMessageBox::information(parent, QObject::tr("Video playback"),
            QObject::tr("This build was compiled without video support because Qt "
                        "Multimedia was not available.\n\n%1").arg(filePath));
    return false;
#else
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    const QMediaFormat::FileFormat wanted = expectedFormat(suffix);

    // Ask the backend before handing it the file: a missing decoder shows up as
    // a generic FormatError that is otherwise hard to explain.
    QMediaFormat format;
    const QList<QMediaFormat::FileFormat> formats =
            format.supportedFileFormats(QMediaFormat::Decode);
    if (wanted != QMediaFormat::UnspecifiedFormat && !formats.isEmpty() &&
            !formats.contains(wanted))
    {
        QMessageBox::warning(parent, QObject::tr("Video playback"),
                QObject::tr("The installed Qt Multimedia backend cannot decode %1 files.\n\n"
                            "Install the matching codecs (for example gst-libav or the Qt "
                            "FFmpeg backend) and try again.").arg(suffix.toUpper()));
        return false;
    }

    auto *dialog = new VideoDialog(std::move(tempDir), filePath, title, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    return true;
#endif
}
