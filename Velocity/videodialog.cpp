#include "videodialog.h"

#include <QAudio>
#include <QAudioOutput>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaPlayer>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QStyle>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

VideoDialog::VideoDialog(std::unique_ptr<QTemporaryDir> tempDirIn, const QString &filePathIn,
        const QString &title, QWidget *parent) :
    QDialog(parent), tempDir(std::move(tempDirIn)), filePath(filePathIn)
{
    setWindowTitle(title);
    resize(960, 600);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    videoWidget = new QVideoWidget(this);
    videoWidget->setMinimumSize(160, 90);
    layout->addWidget(videoWidget, 1);

    errorLabel = new QLabel(this);
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setWordWrap(true);
    errorLabel->setStyleSheet("QLabel { background: palette(base); padding: 8px; }");
    errorLabel->hide();
    layout->addWidget(errorLabel, 0);

    controlBar = new QWidget(this);
    auto *controls = new QHBoxLayout(controlBar);
    controls->setContentsMargins(8, 4, 8, 4);

    playButton = new QPushButton(controlBar);
    playButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    playButton->setToolTip(tr("Play/Pause (Space)"));
    controls->addWidget(playButton);

    auto *stopButton = new QPushButton(controlBar);
    stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    stopButton->setToolTip(tr("Stop"));
    controls->addWidget(stopButton);

    positionSlider = new QSlider(Qt::Horizontal, controlBar);
    positionSlider->setRange(0, 0);
    controls->addWidget(positionSlider, 1);

    timeLabel = new QLabel("0:00 / 0:00", controlBar);
    controls->addWidget(timeLabel);

    volumeSlider = new QSlider(Qt::Horizontal, controlBar);
    volumeSlider->setRange(0, 100);
    volumeSlider->setFixedWidth(100);
    controls->addWidget(volumeSlider);

    auto *fullscreenButton = new QPushButton(controlBar);
    fullscreenButton->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    fullscreenButton->setToolTip(tr("Fullscreen (F)"));
    controls->addWidget(fullscreenButton);

    layout->addWidget(controlBar, 0);

    player = new QMediaPlayer(this);
    audio = new QAudioOutput(this);
    player->setAudioOutput(audio);
    player->setVideoOutput(videoWidget);

    QSettings settings("Exetelek", "Velocity");
    const int savedVolume = settings.value("VideoVolume", 80).toInt();
    volumeSlider->setValue(savedVolume);
    audio->setVolume(QtAudio::convertVolume(savedVolume / 100.0f,
            QtAudio::LogarithmicVolumeScale, QtAudio::LinearVolumeScale));

    // Connect before setSource: errors can be emitted while it loads.
    connect(playButton, &QPushButton::clicked, this, &VideoDialog::togglePlay);
    connect(stopButton, &QPushButton::clicked, this, &VideoDialog::stopPlayback);
    connect(fullscreenButton, &QPushButton::clicked, this, &VideoDialog::toggleFullscreen);
    connect(positionSlider, &QSlider::sliderPressed, this, &VideoDialog::onSliderPressed);
    connect(positionSlider, &QSlider::sliderReleased, this, &VideoDialog::onSliderReleased);
    connect(volumeSlider, &QSlider::valueChanged, this, &VideoDialog::onVolumeChanged);
    connect(player, &QMediaPlayer::positionChanged, this, &VideoDialog::onPositionChanged);
    connect(player, &QMediaPlayer::durationChanged, this, &VideoDialog::onDurationChanged);
    connect(player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState) { onPlaybackStateChanged(); });
    connect(player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus) { onMediaStatusChanged(); });
    connect(player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &message) { onError(message); });

    auto *space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(space, &QShortcut::activated, this, &VideoDialog::togglePlay);
    auto *fKey = new QShortcut(QKeySequence(Qt::Key_F), this);
    connect(fKey, &QShortcut::activated, this, &VideoDialog::toggleFullscreen);

    player->setSource(QUrl::fromLocalFile(filePath));
    player->play();
}

VideoDialog::~VideoDialog()
{
    if (player != nullptr)
    {
        player->stop();
        player->setSource(QUrl());
        delete player;
        player = nullptr;
    }

    // Removing the temporary directory only succeeds once the player has
    // released the file, which the block above guarantees.
    tempDir.reset();
}

void VideoDialog::reject()
{
    if (fullscreen)
    {
        toggleFullscreen();
        return;
    }

    QDialog::reject();
}

void VideoDialog::keyPressEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_Escape:
        reject();
        return;
    case Qt::Key_Space:
        togglePlay();
        return;
    case Qt::Key_F:
        toggleFullscreen();
        return;
    case Qt::Key_Left:
        if (player != nullptr)
            player->setPosition(qMax<qint64>(0, player->position() - 5000));
        return;
    case Qt::Key_Right:
        if (player != nullptr && player->isSeekable())
            player->setPosition(player->position() + 5000);
        return;
    default:
        break;
    }

    QDialog::keyPressEvent(event);
}

void VideoDialog::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    toggleFullscreen();
}

void VideoDialog::togglePlay()
{
    if (player == nullptr)
        return;

    if (player->playbackState() == QMediaPlayer::PlayingState)
        player->pause();
    else
        player->play();
}

void VideoDialog::stopPlayback()
{
    if (player == nullptr)
        return;

    player->stop();
    positionSlider->setValue(0);
    updateTimeLabel(0, player->duration());
}

void VideoDialog::toggleFullscreen()
{
    if (fullscreen)
    {
        controlBar->show();
        showNormal();
    }
    else
    {
        controlBar->hide();
        showFullScreen();
    }

    fullscreen = !fullscreen;
}

void VideoDialog::onPositionChanged(qint64 position)
{
    if (sliderHeld || player == nullptr)
        return;

    positionSlider->blockSignals(true);
    positionSlider->setValue(static_cast<int>(position));
    positionSlider->blockSignals(false);

    updateTimeLabel(position, player->duration());
}

void VideoDialog::onDurationChanged(qint64 duration)
{
    positionSlider->setRange(0, static_cast<int>(qMax<qint64>(0, duration)));
    positionSlider->setEnabled(duration > 0 && player != nullptr && player->isSeekable());
    updateTimeLabel(player != nullptr ? player->position() : 0, duration);
}

void VideoDialog::onSliderPressed()
{
    sliderHeld = true;
}

void VideoDialog::onSliderReleased()
{
    sliderHeld = false;
    seekToSlider();
}

void VideoDialog::seekToSlider()
{
    if (player == nullptr || !player->isSeekable())
        return;

    player->setPosition(positionSlider->value());
}

void VideoDialog::onVolumeChanged(int value)
{
    if (audio == nullptr)
        return;

    audio->setVolume(QtAudio::convertVolume(value / 100.0f,
            QtAudio::LogarithmicVolumeScale, QtAudio::LinearVolumeScale));

    QSettings settings("Exetelek", "Velocity");
    settings.setValue("VideoVolume", value);
}

void VideoDialog::onPlaybackStateChanged()
{
    if (player == nullptr)
        return;

    const bool playing = player->playbackState() == QMediaPlayer::PlayingState;
    playButton->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause :
                                                      QStyle::SP_MediaPlay));
}

void VideoDialog::onMediaStatusChanged()
{
    if (player == nullptr)
        return;

    if (player->mediaStatus() == QMediaPlayer::EndOfMedia)
    {
        player->pause();
        player->setPosition(0);
    }
}

void VideoDialog::onError(const QString &message)
{
    errorLabel->setText(tr("Could not play this file: %1\n\n"
                           "The codec may not be supported by the installed Qt Multimedia "
                           "backend.").arg(message));
    errorLabel->show();
    playButton->setEnabled(false);
}

void VideoDialog::updateTimeLabel(qint64 position, qint64 duration)
{
    timeLabel->setText(formatTime(position) + " / " + formatTime(duration));
}

QString VideoDialog::formatTime(qint64 milliseconds)
{
    const qint64 totalSeconds = qMax<qint64>(0, milliseconds) / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;

    if (hours > 0)
        return QString("%1:%2:%3").arg(hours)
                .arg(minutes, 2, 10, QChar('0'))
                .arg(seconds, 2, 10, QChar('0'));

    return QString("%1:%2").arg(minutes)
            .arg(seconds, 2, 10, QChar('0'));
}
