#include "videodialog.h"

#include <QAudio>
#include <QAudioOutput>
#include <QComboBox>
#include <QFileInfo>
#include <QMediaMetaData>
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
    buildUi(title);

    player->setSource(QUrl::fromLocalFile(filePath));
    player->play();
}

VideoDialog::VideoDialog(std::unique_ptr<QIODevice> device, const QString &sourceUrlHint,
        const QString &title, QWidget *parent) :
    QDialog(parent), sourceDevice(std::move(device))
{
    buildUi(title);

    const QString suffix = QFileInfo(sourceUrlHint).suffix();
    const QUrl url("velocity:/stream" + (suffix.isEmpty() ? QString() : "." + suffix));
    player->setSourceDevice(sourceDevice.get(), url);
    player->play();
}

void VideoDialog::buildUi(const QString &title)
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

    muteButton = new QPushButton(controlBar);
    muteButton->setCheckable(true);
    muteButton->setToolTip(tr("Mute (M)"));
    muteButton->setFixedWidth(32);
    controls->addWidget(muteButton);

    audioTrackCombo = new QComboBox(controlBar);
    audioTrackCombo->setFocusPolicy(Qt::NoFocus);
    audioTrackCombo->setToolTip(tr("Audio track"));
    audioTrackCombo->hide();
    controls->addWidget(audioTrackCombo);

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

    const bool savedMuted = settings.value("VideoMuted", false).toBool();
    muteButton->setChecked(savedMuted);
    audio->setMuted(savedMuted);
    muteButton->setIcon(style()->standardIcon(savedMuted ? QStyle::SP_MediaVolumeMuted :
                                                           QStyle::SP_MediaVolume));

    // Connect before setSource: errors can be emitted while it loads.
    connect(playButton, &QPushButton::clicked, this, &VideoDialog::togglePlay);
    connect(stopButton, &QPushButton::clicked, this, &VideoDialog::stopPlayback);
    connect(fullscreenButton, &QPushButton::clicked, this, &VideoDialog::toggleFullscreen);
    connect(positionSlider, &QSlider::sliderPressed, this, &VideoDialog::onSliderPressed);
    connect(positionSlider, &QSlider::sliderReleased, this, &VideoDialog::onSliderReleased);
    // A click on the track (or a keyboard step) changes the value without
    // pressing the handle; seek for those too.
    connect(positionSlider, &QSlider::valueChanged, this, [this]() {
        if (!positionSlider->isSliderDown())
            seekToSlider();
    });
    connect(positionSlider, &QSlider::sliderMoved, this, [this](int value) {
        updateTimeLabel(value, player != nullptr ? player->duration() : 0);
    });
    connect(player, &QMediaPlayer::seekableChanged, this,
            [this](bool) { updateSeekControls(); });
    connect(player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia)
            updateSeekControls();
    });
    connect(volumeSlider, &QSlider::valueChanged, this, &VideoDialog::onVolumeChanged);
    connect(player, &QMediaPlayer::positionChanged, this, &VideoDialog::onPositionChanged);
    connect(player, &QMediaPlayer::durationChanged, this, &VideoDialog::onDurationChanged);
    connect(player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState) { onPlaybackStateChanged(); });
    connect(player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus) { onMediaStatusChanged(); });
    connect(player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &message) { onError(message); });
    connect(muteButton, &QPushButton::toggled, this, &VideoDialog::onMuteToggled);
    connect(player, &QMediaPlayer::tracksChanged, this, &VideoDialog::rebuildAudioTracks);
    connect(player, &QMediaPlayer::activeTracksChanged, this, &VideoDialog::syncAudioTrack);
    connect(audioTrackCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (player != nullptr && index >= 0 &&
                player->mediaStatus() == QMediaPlayer::LoadedMedia)
            player->setActiveAudioTrack(index);
    });

    auto *space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(space, &QShortcut::activated, this, &VideoDialog::togglePlay);
    auto *fKey = new QShortcut(QKeySequence(Qt::Key_F), this);
    connect(fKey, &QShortcut::activated, this, &VideoDialog::toggleFullscreen);
    auto *mKey = new QShortcut(QKeySequence(Qt::Key_M), this);
    connect(mKey, &QShortcut::activated, muteButton, &QPushButton::toggle);
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

    // The player does not own the streaming device; release it only after the
    // player is gone.
    sourceDevice.reset();

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
        if (player != nullptr && player->isSeekable())
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
    {
        QSignalBlocker blocker(positionSlider);
        positionSlider->setValue(0);
    }
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
    updateTimeLabel(player != nullptr ? player->position() : 0, duration);
    updateSeekControls();
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

void VideoDialog::onMuteToggled(bool muted)
{
    if (audio != nullptr)
        audio->setMuted(muted);

    muteButton->setIcon(style()->standardIcon(muted ? QStyle::SP_MediaVolumeMuted :
                                                        QStyle::SP_MediaVolume));

    QSettings settings("Exetelek", "Velocity");
    settings.setValue("VideoMuted", muted);
}

void VideoDialog::rebuildAudioTracks()
{
    if (player == nullptr || audioTrackCombo == nullptr)
        return;

    const QList<QMediaMetaData> tracks = player->audioTracks();

    QSignalBlocker blocker(audioTrackCombo);
    audioTrackCombo->clear();

    for (int i = 0; i < tracks.size(); ++i)
    {
        QString label = tr("Track %1").arg(i + 1);
        const QString language = tracks.at(i).stringValue(QMediaMetaData::Language);
        const QString title = tracks.at(i).stringValue(QMediaMetaData::Title);

        if (!language.isEmpty() && !title.isEmpty())
            label += " - " + title + " (" + language + ")";
        else if (!language.isEmpty())
            label += " (" + language + ")";
        else if (!title.isEmpty())
            label += " - " + title;

        audioTrackCombo->addItem(label);
    }

    audioTrackCombo->setVisible(tracks.size() > 1);
    audioTrackCombo->setCurrentIndex(qMax(0, player->activeAudioTrack()));
}

void VideoDialog::syncAudioTrack()
{
    if (player == nullptr || audioTrackCombo == nullptr || !audioTrackCombo->isVisible())
        return;

    const int active = player->activeAudioTrack();
    if (active >= 0 && active < audioTrackCombo->count())
    {
        QSignalBlocker blocker(audioTrackCombo);
        audioTrackCombo->setCurrentIndex(active);
    }
}

void VideoDialog::updateSeekControls()
{
    positionSlider->setEnabled(player != nullptr && player->isSeekable() &&
            player->duration() > 0);
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
