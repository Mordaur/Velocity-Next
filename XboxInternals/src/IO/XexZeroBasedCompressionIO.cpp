#include <XboxInternals/IO/XexZeroBasedCompressionIO.h>
#include <XboxInternals/Xex/Xex.h>

#include <array>
#include <cstring>

XexZeroBasedCompressionIO::XexZeroBasedCompressionIO(BaseIO *io, Xex *xex) :
    XexBaseIO(io, xex)
{
    xex->io->SetPosition(xex->header.dataAddress);
}

void XexZeroBasedCompressionIO::ReadBytes(BYTE *outBuffer, DWORD readLength)
{
    if (Length() < (position + readLength))
        throw std::string("XexZeroBasedCompressionIO: Cannot read beyond end of data.");

    DWORD bytesRemaining = readLength;
    while (bytesRemaining > 0)
    {
        bool isData = false;
        bool foundBlock = false;
        XexCompressionBlock curBlock{};
        DWORD offset = 0;
        DWORD byteCounter = 0;

        // find the location in the zero compression table
        for (size_t i = 0; i < xex->compressionBlocks.size(); i++)
        {
            curBlock = xex->compressionBlocks.at(i);
            DWORD curBlockTotalSize = curBlock.size + curBlock.nullSize;

            // check to see if the position is in this compression block
            if ((position - byteCounter) < curBlockTotalSize)
            {
                // check if the position is in the data block
                if (position < (byteCounter + curBlock.size))
                {
                    isData = true;
                }
                // otherwise its in the null block
                else
                {
                    // advance past the data block
                    byteCounter += curBlock.size;

                    isData = false;
                }

                offset = position - byteCounter;
                foundBlock = true;
                break;
            }

            // advance past the data block
            byteCounter += curBlockTotalSize;
        }

        if (!foundBlock)
            throw std::string("XexZeroBasedCompressionIO: Position is outside the compression table.");

        // read the data bytes in
        if (isData)
        {
            DWORD dataBytesToRead;
            if (bytesRemaining > curBlock.size - offset)
                dataBytesToRead = curBlock.size - offset;
            else
                dataBytesToRead = bytesRemaining;
            io->ReadBytes(outBuffer + (readLength - bytesRemaining), dataBytesToRead);

            bytesRemaining -= dataBytesToRead;
            position += dataBytesToRead;

            // recalculate the offset
            if (dataBytesToRead + offset == curBlock.size)
                offset = 0;
            else
                offset += dataBytesToRead;
        }

        // check to see if we need to copy over null bytes too
        if (bytesRemaining != 0)
        {
            // calculate the number of null bytes to copy
            DWORD nullBytesToCopy;
            if (bytesRemaining > curBlock.nullSize - offset)
                nullBytesToCopy = curBlock.nullSize - offset;
            else
                nullBytesToCopy = bytesRemaining;

            // allocate a null buffer
            std::array<BYTE, XEX_NULL_BUFFER_SIZE> nullBuffer {};

            // calculate the number of copies to do
            DWORD copyIterations = nullBytesToCopy / XEX_NULL_BUFFER_SIZE;
            if (nullBytesToCopy % XEX_NULL_BUFFER_SIZE != 0)
                copyIterations++;

            // copy over the null bytes in chunks
            for (DWORD i = 0; i < copyIterations; i++)
            {
                // calculate the number of bytes to copy
                DWORD bytesToCopy = XEX_NULL_BUFFER_SIZE;
                if (i + 1 == copyIterations && nullBytesToCopy % XEX_NULL_BUFFER_SIZE != 0)
                    bytesToCopy = nullBytesToCopy % XEX_NULL_BUFFER_SIZE;

                memcpy(outBuffer + (readLength - bytesRemaining), nullBuffer.data(), bytesToCopy);
                bytesRemaining -= bytesToCopy;
            }

            position += nullBytesToCopy;
        }
    }
}

void XexZeroBasedCompressionIO::WriteBytes(BYTE *buffer, DWORD len)
{

}

UINT64 XexZeroBasedCompressionIO::Length()
{
    UINT64 length = 0;
    for (size_t i = 0; i < xex->compressionBlocks.size(); i++)
    {
        length += xex->compressionBlocks.at(i).size;
        length += xex->compressionBlocks.at(i).nullSize;
    }

    return length;
}
