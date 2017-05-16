/*
 * LegacyClonk
 *
 * Copyright (c) 1998-2000, Matthes Bender (RedWolf Design)
 * Copyright (c) 2017-2019, The LegacyClonk Team and contributors
 *
 * Distributed under the terms of the ISC license; see accompanying file
 * "COPYING" for details.
 *
 * "Clonk" is a registered trademark of Matthes Bender, used with permission.
 * See accompanying file "TRADEMARK" for details.
 *
 * To redistribute this file separately, substitute the full license texts
 * for the above references.
 */

/* A handy wrapper class to gzio files */

#include <Standard.h>
#include <StdFile.h>
#include <CStdFile.h>

#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

CStdFile::CStdFile()
{
	Status = FALSE;
	hFile = NULL;
	ClearBuffer();
	ModeWrite = FALSE;
	Name[0] = 0;
}

CStdFile::~CStdFile()
{
	Close();
}

bool CStdFile::Create(const char *szFilename, bool fCompressed, bool fExecutable)
{
	SCopy(szFilename, Name, _MAX_PATH);
	// Set modes
	ModeWrite = TRUE;
	// Open standard file
	if (fCompressed)
	{
		try
		{
			writeCompressedFile.reset(new StdGzCompressedFile::Write{szFilename});
		}
		catch (const StdGzCompressedFile::Exception &)
		{
			return false;
		}
	}
	else
	{
		if (fExecutable)
		{
			// Create an executable file
#ifdef _WIN32
			int mode = _S_IREAD | _S_IWRITE;
			int flags = _O_BINARY | _O_CREAT | _O_WRONLY | _O_TRUNC;
#else
			mode_t mode = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH;
			int flags = O_CREAT | O_WRONLY | O_TRUNC;
#endif
			int fd = open(Name, flags, mode);
			if (fd == -1) return false;
			if (!(hFile = fdopen(fd, "wb"))) return false;
		}
		else
		{
			if (!(hFile = fopen(Name, "wb"))) return false;
		}
	}
	// Reset buffer
	ClearBuffer();
	// Set status
	Status = TRUE;
	return true;
}

bool CStdFile::Open(const char *szFilename, bool fCompressed)
{
	SCopy(szFilename, Name, _MAX_PATH);
	// Set modes
	ModeWrite = FALSE;
	// Open standard file
	if (fCompressed)
	{
		try
		{
			readCompressedFile.reset(new StdGzCompressedFile::Read{szFilename});
		}
		catch (const StdGzCompressedFile::Exception &)
		{
			return false;
		}
	}
	else
	{
		if (!(hFile = fopen(Name, "rb"))) return false;
	}
	// Reset buffer
	ClearBuffer();
	// Set status
	Status = TRUE;
	return true;
}

bool CStdFile::Append(const char *szFilename)
{
	SCopy(szFilename, Name, _MAX_PATH);
	// Set modes
	ModeWrite = TRUE;
	// Open standard file
	if (!(hFile = fopen(Name, "ab"))) return FALSE;
	// Reset buffer
	ClearBuffer();
	// Set status
	Status = TRUE;
	return TRUE;
}

bool CStdFile::Close()

{
	BOOL rval = TRUE;
	Status = FALSE;
	Name[0] = 0;
	// Save buffer if in write mode
	if (ModeWrite && BufferLoad) if (!SaveBuffer()) rval = FALSE;
	// Close file(s)
	readCompressedFile.reset();
	writeCompressedFile.reset();
	if (hFile) if (fclose(hFile) != 0) rval = FALSE;
	hFile = NULL;
	return !!rval;
}

bool CStdFile::Default()
{
	Status = FALSE;
	Name[0] = 0;
	readCompressedFile.reset();
	writeCompressedFile.reset();
	hFile = NULL;
	BufferLoad = BufferPtr = 0;
	return TRUE;
}

bool CStdFile::Read(void *pBuffer, size_t iSize, size_t *ipFSize)
{
	int transfer;
	if (!pBuffer) return FALSE;
	if (ModeWrite) return FALSE;
	BYTE *bypBuffer = (BYTE *)pBuffer;
	if (ipFSize) *ipFSize = 0;
	while (iSize > 0)
	{
		// Valid data in the buffer: Transfer as much as possible
		if (BufferLoad > BufferPtr)
		{
			transfer = Min<size_t>(BufferLoad - BufferPtr, iSize);
			memmove(bypBuffer, Buffer + BufferPtr, transfer);
			BufferPtr += transfer;
			bypBuffer += transfer;
			iSize -= transfer;
			if (ipFSize) *ipFSize += transfer;
		}
		// Buffer empty: Load
		else if (LoadBuffer() <= 0) return FALSE;
	}
	return TRUE;
}

int CStdFile::LoadBuffer()
{
	if (hFile) BufferLoad = fread(Buffer, 1, CStdFileBufSize, hFile);
	if (readCompressedFile)
	{
		try
		{
			BufferLoad = readCompressedFile->ReadData(Buffer, CStdFileBufSize);
		}
		catch (const StdGzCompressedFile::Exception &)
		{
			BufferLoad = 0;
		}
	}
	BufferPtr = 0;
	return BufferLoad;
}

bool CStdFile::SaveBuffer()
{
	int saved = 0;
	if (hFile) saved = fwrite(Buffer, 1, BufferLoad, hFile);
	if (writeCompressedFile)
	{
		try
		{
			writeCompressedFile->WriteData(Buffer, BufferLoad);
			saved = BufferLoad;
		}
		catch (const StdGzCompressedFile::Exception &)
		{
			return false;
		}
	}
	if (saved != BufferLoad) return FALSE;
	BufferLoad = 0;
	return TRUE;
}

void CStdFile::ClearBuffer()
{
	BufferLoad = BufferPtr = 0;
}

bool CStdFile::Write(const void *pBuffer, int iSize)
{
	int transfer;
	if (!pBuffer) return FALSE;
	if (!ModeWrite) return FALSE;
	BYTE *bypBuffer = (BYTE *)pBuffer;
	while (iSize > 0)
	{
		// Space in buffer: Transfer as much as possible
		if (BufferLoad < CStdFileBufSize)
		{
			transfer = Min(CStdFileBufSize - BufferLoad, iSize);
			memcpy(Buffer + BufferLoad, bypBuffer, transfer);
			BufferLoad += transfer;
			bypBuffer += transfer;
			iSize -= transfer;
		}
		// Buffer full: Save
		else if (!SaveBuffer()) return FALSE;
	}
	return TRUE;
}

bool CStdFile::WriteString(const char *szStr)
{
	BYTE nl[2] = { 0x0D, 0x0A };
	if (!szStr) return FALSE;
	int size = SLen(szStr);
	if (!Write((void *)szStr, size)) return FALSE;
	if (!Write(nl, 2)) return FALSE;
	return TRUE;
}

bool CStdFile::Rewind()
{
	if (ModeWrite) return FALSE;
	ClearBuffer();
	if (hFile) rewind(hFile);
	if (readCompressedFile)
	{
		readCompressedFile->Rewind();
	}
	return TRUE;
}

bool CStdFile::Advance(int iOffset)
{
	if (ModeWrite) return FALSE;
	while (iOffset > 0)
	{
		// Valid data in the buffer: Transfer as much as possible
		if (BufferLoad > BufferPtr)
		{
			int transfer = Min(BufferLoad - BufferPtr, iOffset);
			BufferPtr += transfer;
			iOffset -= transfer;
		}
		// Buffer empty: Load or skip
		else
		{
			if (hFile) return !fseek(hFile, iOffset, SEEK_CUR); // uncompressed: Just skip
			if (LoadBuffer() <= 0) return FALSE; // compressed: Read...
		}
	}
	return TRUE;
}

bool CStdFile::Save(const char *szFilename, const BYTE *bpBuf,
	int iSize, bool fCompressed)
{
	if (!bpBuf || (iSize < 1)) return FALSE;
	if (!Create(szFilename, fCompressed)) return FALSE;
	if (!Write(bpBuf, iSize)) return FALSE;
	if (!Close()) return FALSE;
	return TRUE;
}

bool CStdFile::Load(const char *szFilename, BYTE **lpbpBuf,
	int *ipSize, int iAppendZeros,
	bool fCompressed)
{
	int iSize = fCompressed ? UncompressedFileSize(szFilename) : FileSize(szFilename);
	if (!lpbpBuf || (iSize < 1)) return FALSE;
	if (!Open(szFilename, fCompressed)) return FALSE;
	if (!(*lpbpBuf = new BYTE[iSize + iAppendZeros])) return FALSE;
	if (!Read(*lpbpBuf, iSize)) { delete[] *lpbpBuf; return FALSE; }
	if (iAppendZeros) ZeroMem((*lpbpBuf) + iSize, iAppendZeros);
	if (ipSize) *ipSize = iSize;
	Close();
	return TRUE;
}

int UncompressedFileSize(const char *szFilename)
{
	try
	{
		StdGzCompressedFile::Read file{szFilename};
		return file.UncompressedSize();
	}
	catch (const StdGzCompressedFile::Exception &)
	{
		return 0;
	}
}

int CStdFile::AccessedEntrySize()
{
	if (hFile)
	{
		long pos = ftell(hFile);
		fseek(hFile, 0, SEEK_END);
		int r = ftell(hFile);
		fseek(hFile, pos, SEEK_SET);
		return r;
	}
	assert(!readCompressedFile);
	return 0;
}
