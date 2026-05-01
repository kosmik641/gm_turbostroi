//------------------------------------------------------------------------------
// STL filesystem implementation for IFileSystem
// Done because of Linux's filesystem_stdio.so don't working o
//------------------------------------------------------------------------------
#include <filesystem.h>

class CFileSystem_STL : public CFileSystemPassThru
{
public:
	virtual int				Read(void* pOutput, int size, FileHandle_t file);
	virtual int				Write(void const* pInput, int size, FileHandle_t file);

	virtual FileHandle_t	Open(const char* pFileName, const char* pOptions, const char* pathID = 0);
	virtual void			Close(FileHandle_t file);


	virtual void				Seek(FileHandle_t file, long long pos, FileSystemSeek_t seekType);
	virtual unsigned long long	Tell(FileHandle_t file);
	virtual unsigned long long	Size(FileHandle_t file);
	virtual unsigned long long	Size(const char* pFileName, const char* pPathID = 0);

	virtual void			Flush(FileHandle_t file);
	virtual bool			Precache(const char* pFileName, const char* pPathID = 0);

	virtual bool			FileExists(const char* pFileName, const char* pPathID = 0);
	virtual bool			IsFileWritable(char const* pFileName, const char* pPathID = 0);
	virtual bool			SetFileWritable(char const* pFileName, bool writable, const char* pPathID = 0);

	virtual long			GetFileTime(const char* pFileName, const char* pPathID = 0);

	virtual bool			ReadFile(const char* pFileName, const char* pPath, CUtlBuffer& buf, int nMaxBytes = 0, int nStartingByte = 0, FSAllocFunc_t pfnAlloc = NULL);
	virtual bool			WriteFile(const char* pFileName, const char* pPath, CUtlBuffer& buf);
	virtual bool			UnzipFile(const char* pFileName, const char* pPath, const char* pDestination);

	virtual void			RemoveFile(char const* path, const char* pathID) override;
	virtual void			CreateDirHierarchy(const char* path, const char* pathID) override;

	// Methods redefined for KeyValues::LoadFromFile
	virtual int				ReadEx(void* pOutput, int sizeDest, int size, FileHandle_t file) override;

	virtual bool		    GetOptimalIOConstraints(FileHandle_t hFile, unsigned* pOffsetAlign, unsigned* pSizeAlign, unsigned* pBufferAlign) override;
	virtual void*           AllocOptimalReadBuffer(FileHandle_t hFile, unsigned nSize = 0, unsigned nOffset = 0) override { return malloc(nSize); };
	virtual void		    FreeOptimalReadBuffer(void* p) override { free(p); }
};