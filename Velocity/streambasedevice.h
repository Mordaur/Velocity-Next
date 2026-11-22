#pragma once

#include <QIODevice>

#include <XboxInternals/Fatx/FatxConstants.h>

#include <functional>
#include <vector>

// Read-only QIODevice over contiguous device extents. The media backend calls
// readData/seek from its own threads; readAt must be safe to call there and
// must not touch the drive's shared IO position.
class StreamBaseDevice : public QIODevice
{
public:
    using ReadAt = std::function<void(u64 offset, u8 *data, u32 length)>;

    StreamBaseDevice(std::vector<FileExtent> extents, u64 fileSize, ReadAt readAt,
            QObject *parent = nullptr);

    bool isSequential() const override { return false; }
    qint64 size() const override;
    bool seek(qint64 position) override;

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    std::vector<FileExtent> extents;
    u64 fileSize;
    ReadAt readAt;
};
