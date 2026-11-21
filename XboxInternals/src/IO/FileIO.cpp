#include <XboxInternals/IO/FileIO.h>
#include <vector>

FileIO::FileIO(string path, bool truncate) :
    BaseIO(), filePath(path)
{
    std::ios_base::openmode mode = fstream::in | fstream::out | fstream::binary;
    if (truncate)
        mode |= fstream::trunc;

    fstr = std::make_unique<fstream>(path.c_str(), mode);

    // A read-only file (a mounted ISO, a package on read-only media) can still
    // be read. Only writers need the read-write handle.
    if ((!fstr || !fstr->is_open()) && !truncate)
    {
        fstr = std::make_unique<fstream>(path.c_str(), fstream::in | fstream::binary);
        readOnly = true;
    }

    if (!fstr || !fstr->is_open())
    {
        std::string ex("FileIO: Error opening the file. ");
        ex += strerror(errno);
        ex += "\n";
        throw ex;
    }

    endian = BigEndian;

    // Use default std::fstream buffering for best performance
    if (readOnly)
    {
        fstr->seekg(0, std::ios_base::end);
        length = fstr->tellg();
        fstr->seekg(0);
    }
    else
    {
        fstr->seekp(0, std::ios_base::end);
        length = fstr->tellp();
        fstr->seekp(0);
    }
}

void FileIO::SetPosition(u64 pos, ios_base::seekdir dir)
{
    if (readOnly)
        fstr->seekg(pos, static_cast<std::ios_base::seekdir>(dir));
    else
        fstr->seekp(pos, static_cast<std::ios_base::seekdir>(dir));
}

u64 FileIO::GetPosition()
{
    return readOnly ? fstr->tellg() : fstr->tellp();
}

u64 FileIO::Length()
{
    return length;
}

void FileIO::Close()
{
    if (fstr)
        fstr->close();
}

void FileIO::Flush()
{
    if (fstr && !readOnly)
        fstr->flush();
}

void FileIO::ReverseGenericArray(void *arr, int elemSize, int len)
{
    std::vector<char> tempVec;
    tempVec.reserve(elemSize);
    char* temp = tempVec.data();

    for (int i = 0; i < (len / 2); i++)
    {
        memcpy(temp, ((char*)arr) + (i * elemSize), elemSize);
        memcpy(((char*)arr) + (i * elemSize), ((char*)arr) + (((len - 1) * elemSize) - (i * elemSize)),
               elemSize);
        memcpy(((char*)arr) + (((len - 1) * elemSize) - (i * elemSize)), temp, elemSize);
    }
}

string FileIO::GetFilePath()
{
    return filePath;
}

void FileIO::ReadBytes(u8 *outBuffer, u32 len)
{
    fstr->read((fstream::char_type*)outBuffer, len);
    if (fstr->fail())
        throw string("FileIO: Error reading from file.\n");
}

void FileIO::WriteBytes(u8 *buffer, u32 len)
{
    if (readOnly)
        throw string("FileIO: File was opened read-only.\n");

    fstr->write((fstream::char_type*)buffer, len);
    if (fstr->fail())
        throw string("FileIO: Error writing to file.\n");
}

FileIO::~FileIO(void)
{
    if (fstr && fstr->is_open())
        fstr->close();
}


