#include "filesystem_stl.h"
#include <fstream>
#include <filesystem>
#include <utlbuffer.h>

namespace fs = std::filesystem;
namespace chrono = std::chrono;

int CFileSystem_STL::Read(void* pOutput, int size, FileHandle_t file)
{
    return ReadEx(pOutput, size, size, file);
}

int CFileSystem_STL::Write(void const* pInput, int size, FileHandle_t file)
{
    if (file == nullptr) return 0;

    std::fstream* f = static_cast<std::fstream*>(file);
    if (!f->good())
        return 0;

    f->write(static_cast<const char*>(pInput), size);

    return f->good() ? size : 0;
}

FileHandle_t CFileSystem_STL::Open(const char* pFileName, const char* pOptions, const char* pathID)
{
    std::ios::openmode mode{};

    switch (*pOptions++)
    {
    case 'r': /* open for reading */
        mode |= std::ios::in;
        break;
    case 'w': /* open for writing */
        mode |= std::ios::out | std::ios::trunc;
        break;
    case 'a': /* open for appending */
        mode |= std::ios::out | std::ios::app;
        break;
    default: /* illegal mode */
        return nullptr;
    }

    if (*pOptions == 'b')
        mode |= std::ios::binary;

    if (*pOptions == '+' || (*pOptions == 'b' && pOptions[1] == '+')) {
        mode |= std::ios::in | std::ios::out;
    }

    std::fstream* f = new std::fstream(pFileName, mode);
    if (!f->is_open())
    {
        delete f;
        return nullptr;
    }

    return f;
}

void CFileSystem_STL::Close(FileHandle_t file)
{
    if (file == nullptr) return;

    std::fstream *f = static_cast<std::fstream*>(file);
    f->close();
    delete f;
    f = nullptr;
}

void CFileSystem_STL::Seek(FileHandle_t file, long long pos, FileSystemSeek_t seekType)
{
    if (file == nullptr) return;

    std::fstream* f = static_cast<std::fstream*>(file);

    std::ios_base::seekdir seek;
    switch (seekType)
    {
    case FILESYSTEM_SEEK_HEAD:
        seek = std::ios::beg;
        break;
    case FILESYSTEM_SEEK_CURRENT:
        seek = std::ios::cur;
        break;
    case FILESYSTEM_SEEK_TAIL:
        seek = std::ios::end;
        break;
    default:
        seek = std::ios::beg;
        break;
    }

    if (f->good())
        f->seekg(pos, seek);
}

unsigned long long CFileSystem_STL::Tell(FileHandle_t file)
{
    if (file == nullptr) return 0;

    std::fstream* f = static_cast<std::fstream*>(file);
    if (!f->good())
        return 0;

    return f->good() ? static_cast<unsigned long long>(f->tellg()) : 0;
}

unsigned long long CFileSystem_STL::Size(FileHandle_t file)
{
    if (file == nullptr) return 0;

    std::fstream* f = static_cast<std::fstream*>(file);
    if (!f->good())
        return 0;

    std::streampos old_seek = f->tellg();
    f->seekg(0, std::ios::end);
    std::streampos size = f->tellg();
    f->seekg(old_seek, std::ios::beg);
    
    return static_cast<unsigned long long>(size);
}

unsigned long long CFileSystem_STL::Size(const char* pFileName, const char* pPathID)
{
    if (!fs::exists(pFileName) ||
        !fs::is_regular_file(pFileName))
        return 0;

    return static_cast<unsigned long long>(fs::file_size(pFileName));
}

void CFileSystem_STL::Flush(FileHandle_t file)
{
    if (file == nullptr) return;

    std::fstream* f = static_cast<std::fstream*>(file);
    if (!f->good())
        return;

    f->flush();
}

bool CFileSystem_STL::Precache(const char* pFileName, const char* pPathID)
{
    return false;
}

bool CFileSystem_STL::FileExists(const char* pFileName, const char* pPathID)
{
    return fs::exists(pFileName);
}

bool CFileSystem_STL::IsFileWritable(char const* pFileName, const char* pPathID)
{
    if (!fs::exists(pFileName))
        return false;

    fs::perms stat = fs::status(pFileName).permissions();
    return (stat & fs::perms::owner_write) != fs::perms::none;
}

bool CFileSystem_STL::SetFileWritable(char const* pFileName, bool writable, const char* pPathID)
{
    return false;
}

long CFileSystem_STL::GetFileTime(const char* pFileName, const char* pPathID)
{
    if (!fs::exists(pFileName) ||
        !fs::is_regular_file(pFileName))
        return 0;

    auto time = fs::last_write_time(pFileName);
    return chrono::duration_cast<chrono::seconds>(time.time_since_epoch()).count();
}

bool CFileSystem_STL::ReadFile(const char* pFileName, const char* pPath, CUtlBuffer& buf, int nMaxBytes, int nStartingByte, FSAllocFunc_t pfnAlloc)
{
    bool bBinary = !(buf.IsText() && !buf.ContainsCRLF());

    FileHandle_t fp = Open(pFileName, (bBinary) ? "rb" : "rt", pPath);
    if (fp == nullptr)
        return false;

    int nBytesToRead = Size(fp);
    if (nMaxBytes > 0)
    {
        nBytesToRead = std::min(nMaxBytes, nBytesToRead);
    }
    buf.EnsureCapacity(nBytesToRead + buf.TellPut());

    if (nStartingByte != 0)
    {
        Seek(fp, nStartingByte, FILESYSTEM_SEEK_HEAD);
    }

    int nBytesRead = Read(buf.PeekPut(), nBytesToRead, fp);
    buf.SeekPut(CUtlBuffer::SEEK_CURRENT, nBytesRead);

    Close(fp);

    return (nBytesRead != 0);
}

bool CFileSystem_STL::WriteFile(const char* pFileName, const char* pPath, CUtlBuffer& buf)
{
    bool isBinary = !(buf.IsText() && !buf.ContainsCRLF());
    FileHandle_t fp = Open(pFileName, (isBinary) ? "rb" : "rt", pPath);
    if (fp == nullptr)
        return false;

    int nBytesWritten = Write(buf.Base(), buf.TellPut(), fp);

    Close(fp);

    return (nBytesWritten != 0);
}

bool CFileSystem_STL::UnzipFile(const char* pFileName, const char* pPath, const char* pDestination)
{
    return false;
}

int CFileSystem_STL::ReadEx(void* pOutput, int sizeDest, int size, FileHandle_t file)
{
    if (file == nullptr) return 0;

    if (size < 0 || sizeDest < 0)
        return 0;

    std::fstream* f = static_cast<std::fstream*>(file);
    if (!f->good())
        return 0;

    int sizeToRead = std::min(sizeDest, size);
    f->read(static_cast<char*>(pOutput), sizeToRead);

    return f->good() ? sizeToRead : 0;
}

bool CFileSystem_STL::GetOptimalIOConstraints(FileHandle_t hFile, unsigned* pOffsetAlign, unsigned* pSizeAlign, unsigned* pBufferAlign)
{
    if (pOffsetAlign)
        *pOffsetAlign = 1;

    if (pSizeAlign)
        *pSizeAlign = 1;

    if (pBufferAlign)
        *pBufferAlign = 1;

    return false;
}
