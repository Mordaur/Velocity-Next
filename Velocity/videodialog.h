#pragma once

#include <QDialog>

#include <memory>

class QTemporaryDir;
class QAudioOutput;
class QComboBox;
class QIODevice;
class QLabel;
class QMediaPlayer;
class QPushButton;
class QSlider;
class QVideoWidget;
class QWidget;

// Plays one local video file. The file normally lives in a temporary directory
// owned by the dialog; teardown stops and releases the player before the
// directory (and therefore the file) is removed.
class VideoDialog : public QDialog
{
    Q_OBJECT
public:
    VideoDialog(std::unique_ptr<QTemporaryDir> tempDir, const QString &filePath,
            const QString &title, QWidget *parent = nullptr);
    // Streams from a caller-owned device. sourceUrlHint supplies the file name
    // so the backend can pick a demuxer from the extension.
    VideoDialog(std::unique_ptr<QIODevice> device, const QString &sourceUrlHint,
            const QString &title, QWidget *parent = nullptr);
    ~VideoDialog() override;

protected:
    void reject() override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
    void togglePlay();
    void stopPlayback();
    void toggleFullscreen();
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onSliderPressed();
    void onSliderReleased();
    void onVolumeChanged(int value);
    void onPlaybackStateChanged();
    void onMediaStatusChanged();
    void onError(const QString &message);
    void onMuteToggled(bool muted);
    void rebuildAudioTracks();
    void syncAudioTrack();

private:
    void buildUi(const QString &title);
    void seekToSlider();
    void updateSeekControls();
    void updateTimeLabel(qint64 position, qint64 duration);
    static QString formatTime(qint64 milliseconds);

    std::unique_ptr<QTemporaryDir> tempDir;
    std::unique_ptr<QIODevice> sourceDevice;
    QString filePath;
    QMediaPlayer *player = nullptr;
    QAudioOutput *audio = nullptr;
    QVideoWidget *videoWidget = nullptr;
    QWidget *controlBar = nullptr;
    QSlider *positionSlider = nullptr;
    QSlider *volumeSlider = nullptr;
    QComboBox *audioTrackCombo = nullptr;
    QPushButton *muteButton = nullptr;
    QLabel *timeLabel = nullptr;
    QLabel *errorLabel = nullptr;
    QPushButton *playButton = nullptr;
    bool fullscreen = false;
    bool sliderHeld = false;
};
