#include "streambasedevice.h"

#include <algorithm>
#include <exception>
#include <string>

StreamBaseDevice::StreamBaseDevice(std::vector<FileExtent> extentsIn, u64 fileSizeIn,
        ReadAt readAtIn, QObject *parent) :
    QIODevice(parent), extents(std::move(extentsIn)), fileSize(fileSizeIn),
    readAt(std::move(readAtIn))
{
}

qint64 StreamBaseDevice::size() const
{
    return static_cast<qint64>(fileSize);
}

bool StreamBaseDevice::seek(qint64 position)
{
    if (position < 0 || static_cast<quint64>(position) > fileSize)
        return false;

    return QIODevice::seek(position);
}

qint64 StreamBaseDevice::readData(char *data, qint64 maxSize)
{
    if (data == nullptr || maxSize <= 0 || readAt == nullptr)
        return 0;

    const u64 position = static_cast<u64>(pos());
    if (position >= fileSize)
        return 0;

    const u64 toRead = std::min<u64>(static_cast<u64>(maxSize), fileSize - position);

    try
    {
        u64 done = 0;
        while (done < toRead)
        {
            const u64 absolute = position + done;

            const FileExtent *extent = nullptr;
            for (const FileExtent &candidate : extents)
            {
                if (absolute >= candidate.fileOffset &&
                        absolute < candidate.fileOffset + candidate.length)
                {
                    extent = &candidate;
                    break;
                }
            }

            if (extent == nullptr)
                return -1;

            const u64 within = absolute - extent->fileOffset;
            u64 chunk = std::min<u64>(extent->length - within, toRead - done);
            // The helper protocol caps a frame payload at 1 MiB.
            chunk = std::min<u64>(chunk, 0x100000);

            readAt(extent->deviceOffset + within,
                    reinterpret_cast<u8 *>(data + done), static_cast<u32>(chunk));
            done += chunk;
        }

        return static_cast<qint64>(done);
    }
    catch (const std::string &)
    {
        return -1;
    }
    catch (const std::exception &)
    {
        return -1;
    }
    catch (...)
    {
        return -1;
    }
}
