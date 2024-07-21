/*******************************************************

   CoolReader Engine

   lvstream.cpp:  stream classes implementation

   (c) Vadim Lopatin, 2000-2009
   This source code is distributed under the terms of
   GNU General Public License
   See LICENSE file for details

 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * UNRAR library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.

*******************************************************/

#include "../include/lvstream.h"
#include "../include/lvptrvec.h"
#include "../include/crtxtenc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

//#define USE_UNRAR 1

#if (USE_ZLIB==1)
#include <zlib.h>
#endif

#if (USE_UNRAR==1)
#include <rar.hpp>
#endif

#if !defined(__SYMBIAN32__) && defined(_WIN32)
extern "C" {
#include <windows.h>
}
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#endif

#ifdef _LINUX
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dlfcn.h>
#include <errno.h>
#endif


#ifndef USE_ANSI_FILES

#if !defined(__SYMBIAN32__) && defined(_WIN32)
#define USE_ANSI_FILES 0
#else
#define USE_ANSI_FILES 1
#endif

#endif

// To support "large files" on 32-bit platforms
// Since we have defined own types 'lvoffset_t', 'lvpos_t' and do not use the system type 'off_t'
// it is logical to define our own wrapper function 'lseek'.
static inline lvpos_t cr3_lseek(int fd, lvoffset_t offset, int whence) {
#if LVLONG_FILE_SUPPORT == 1
    return (lvpos_t)::lseek64(fd, (off64_t)offset, whence);
#else
    return (lvpos_t)::lseek(fd, (off_t)offset, whence);
#endif
}

static LVAssetContainerFactory * _assetContainerFactory = NULL;

/// set container to handle filesystem access for paths started with ASSET_PATH_PREFIX (@ sign)
void LVSetAssetContainerFactory(LVAssetContainerFactory * asset) {
	_assetContainerFactory = asset;
}

lString32 LVExtractAssetPath(lString32 fn) {
	if (fn.length() < 2 || fn[0] != ASSET_PATH_PREFIX)
		return lString32();
	if (fn[1] == '/' || fn[1] == '\\')
		return fn.substr(2);
	return fn.substr(1);
}

// LVStorageObject stubs
const lChar32 * LVStorageObject::GetName()
{
    return NULL;
}

LVContainer * LVStorageObject::GetParentContainer()
{
    return NULL;
}

void LVStorageObject::SetName(const lChar32 *)
{
}

bool LVStorageObject::IsContainer()
{
    return false;
}

lvsize_t LVStorageObject::GetSize( )
{
    lvsize_t sz;
    if ( GetSize( &sz )!=LVERR_OK )
        return LV_INVALID_SIZE;
    return sz;
}


/// calculate crc32 code for stream, if possible
lverror_t LVNamedStream::getcrc32( lUInt32 & dst )
{
    if ( _crc!=0 ) {
        dst = _crc;
        return LVERR_OK;
    } else {
        if ( !_crcFailed ) {
            lverror_t res = LVStream::getcrc32( dst );
            if ( res==LVERR_OK ) {
                _crc = dst;
                return LVERR_OK;
            }
            _crcFailed = true;
        }
        dst = 0;
        return LVERR_FAIL;
    }
}
/// returns stream/container name, may be NULL if unknown
const lChar32 * LVNamedStream::GetName()
{
    if (m_fname.empty())
        return NULL;
    return m_fname.c_str();
}
/// sets stream/container name, may be not implemented for some objects
void LVNamedStream::SetName(const lChar32 * name)
{
    m_fname = name;
    m_filename.clear();
    m_path.clear();
    if (m_fname.empty())
        return;
    const lChar32 * fn = m_fname.c_str();

    const lChar32 * p = fn + m_fname.length() - 1;
    for ( ;p>fn; p--) {
        if (p[-1] == '/' || p[-1]=='\\')
            break;
    }
    int pos = (int)(p - fn);
    if (p>fn)
        m_path = m_fname.substr(0, pos);
    m_filename = m_fname.substr(pos, m_fname.length() - pos);
}

/// Universal Read or write buffer for stream region for non-meped streams
// default implementation, with RAM buffer
class LVDefStreamBuffer : public LVStreamBuffer
{
protected:
    LVStreamRef m_stream;
    lUInt8 * m_buf;
    lvpos_t m_pos;
    lvsize_t m_size;
    bool m_readonly;
    bool m_writeonly;
public:
    static LVStreamBufferRef create( LVStreamRef stream, lvpos_t pos, lvsize_t size, bool readonly )
    {
        LVStreamBufferRef res;
        switch ( stream->GetMode() ) {
        case LVOM_ERROR:       ///< to indicate error state
        case LVOM_CLOSED:        ///< to indicate closed state
            return res;
        case LVOM_READ:          ///< readonly mode, use for r/o
            if ( !readonly )
                return res;
            break;
        case LVOM_WRITE:         ///< writeonly mode
        case LVOM_APPEND:        ///< append (readwrite) mode, use for r/w
        case LVOM_READWRITE:      ///< readwrite mode
            if ( readonly )
                return res;
            break;
        }
        lvsize_t sz;
        if ( stream->GetSize(&sz)!=LVERR_OK )
            return res;
        if ( pos + size > sz )
            return res; // wrong position/size
        LVDefStreamBuffer * buf = new LVDefStreamBuffer( stream, pos, size, readonly );
        if ( !buf->m_buf ) {
            delete buf;
            return res;
        }
        if ( stream->SetPos( pos )!=LVERR_OK ) {
            delete buf;
            return res;
        }
        lvsize_t bytesRead = 0;
        if ( stream->Read( buf->m_buf, size, &bytesRead )!=LVERR_OK || bytesRead!=size ) {
            delete buf;
            return res;
        }
        return LVStreamBufferRef( buf );
    }

    LVDefStreamBuffer( LVStreamRef stream, lvpos_t pos, lvsize_t size, bool readonly )
    : m_stream( stream ), m_buf( NULL ), m_pos(pos), m_size( size ), m_readonly( readonly )
    {
        m_buf = (lUInt8*)malloc( size );
        m_writeonly = (m_stream->GetMode()==LVOM_WRITE);
    }
    /// get pointer to read-only buffer, returns NULL if unavailable
    virtual const lUInt8 * getReadOnly()
    {
        return m_writeonly ? NULL : m_buf;
    }
    /// get pointer to read-write buffer, returns NULL if unavailable
    virtual lUInt8 * getReadWrite()
    {
        return m_readonly ? NULL : m_buf;
    }

    /// get buffer size
    virtual lvsize_t getSize()
    {
        return m_size;
    }

    /// write on close
    virtual bool close()
    {
        bool res = true;
        if ( m_buf ) {
            if ( !m_readonly ) {
                if ( m_stream->SetPos( m_pos )!=LVERR_OK ) {
                    res = false;
                } else {
                    lvsize_t bytesWritten = 0;
                    if ( m_stream->Write( m_buf, m_size, &bytesWritten )!=LVERR_OK || bytesWritten!=m_size ) {
                        res = false;
                    }
                }
            }
            free( m_buf );
        }
        m_buf = NULL;
        m_stream = NULL;
        m_size = 0;
        m_pos = 0;
        return res;
    }
    /// flush on destroy
    virtual ~LVDefStreamBuffer()
    {
        close(); // NOLINT: Call to virtual function during destruction
    }
};

/// Get read buffer - default implementation, with RAM buffer
LVStreamBufferRef LVStream::GetReadBuffer( lvpos_t pos, lvpos_t size )
{
    LVStreamBufferRef res;
    res = LVDefStreamBuffer::create( LVStreamRef(this), pos, size, true );
    return res;
}

/// Get read/write buffer - default implementation, with RAM buffer
LVStreamBufferRef LVStream::GetWriteBuffer( lvpos_t pos, lvpos_t size )
{
    LVStreamBufferRef res;
    res = LVDefStreamBuffer::create( LVStreamRef(this), pos, size, false );
    return res;
}


#define CRC_BUF_SIZE 16384

/// calculate crc32 code for stream, if possible
lverror_t LVStream::getcrc32( lUInt32 & dst )
{
    dst = 0;
    if ( GetMode() == LVOM_READ || GetMode() == LVOM_APPEND ) {
        lvpos_t savepos = GetPos();
        lvsize_t size = GetSize();
        lUInt8 buf[CRC_BUF_SIZE];
        SetPos( 0 );
        lvsize_t bytesRead = 0;
        for ( lvpos_t pos = 0; pos<size; pos+=CRC_BUF_SIZE ) {
            lvsize_t sz = size - pos;
            if ( sz > CRC_BUF_SIZE )
                sz = CRC_BUF_SIZE;
            Read( buf, sz, &bytesRead );
            if ( bytesRead!=sz ) {
                SetPos(savepos);
                return LVERR_FAIL;
            }
            dst = lStr_crc32( dst, buf, sz );
        }
        SetPos( savepos );
        return LVERR_OK;
    } else {
        // not supported
        return LVERR_NOTIMPL;
    }
}


//#if USE__FILES==1
#if defined(_LINUX) || defined(_WIN32)

class LVFileMappedStream : public LVNamedStream
{
private:
#if defined(_WIN32)
    HANDLE m_hFile;
    HANDLE m_hMap;
#else
    int m_fd;
#endif
    lUInt8* m_map;
    lvsize_t m_size;
    lvpos_t m_pos;

    /// Read or write buffer for stream region
    class LVBuffer : public LVStreamBuffer
    {
    protected:
        LVStreamRef m_stream;
        lUInt8 * m_buf;
        lvsize_t m_size;
        bool m_readonly;
    public:
        LVBuffer( LVStreamRef stream, lUInt8 * buf, lvsize_t size, bool readonly )
        : m_stream( stream ), m_buf( buf ), m_size( size ), m_readonly( readonly )
        {
        }

        /// get pointer to read-only buffer, returns NULL if unavailable
        virtual const lUInt8 * getReadOnly()
        {
            return m_buf;
        }

        /// get pointer to read-write buffer, returns NULL if unavailable
        virtual lUInt8 * getReadWrite()
        {
            return m_readonly ? NULL : m_buf;
        }

        /// get buffer size
        virtual lvsize_t getSize()
        {
            return m_size;
        }

        /// flush on destroy
        virtual ~LVBuffer() { }

    };


public:

    /// Get read buffer (optimal for )
    virtual LVStreamBufferRef GetReadBuffer( lvpos_t pos, lvpos_t size )
    {
        LVStreamBufferRef res;
        if ( !m_map )
            return res;
        if ( (m_mode!=LVOM_APPEND && m_mode!=LVOM_READ) || pos + size > m_size || size==0 )
            return res;
        return LVStreamBufferRef ( new LVBuffer( LVStreamRef(this), m_map + pos, size, true ) );
    }

    /// Get read/write buffer (optimal for )
    virtual LVStreamBufferRef GetWriteBuffer( lvpos_t pos, lvpos_t size )
    {
        LVStreamBufferRef res;
        if ( !m_map )
            return res;
        if ( m_mode!=LVOM_APPEND || pos + size > m_size || size==0 )
            return res;
        return LVStreamBufferRef ( new LVBuffer( LVStreamRef(this), m_map + pos, size, false ) );
    }

    virtual lverror_t Seek( lvoffset_t offset, lvseek_origin_t origin, lvpos_t * pNewPos )
    {
        //
        lvpos_t newpos = m_pos;
        switch ( origin )
        {
        case LVSEEK_SET:
            newpos = offset;
            break;
        case LVSEEK_CUR:
            newpos += offset;
            break;
        case LVSEEK_END:
            newpos = m_size + offset;
            break;
        }
        if ( newpos>m_size )
            return LVERR_FAIL;
        if ( pNewPos!=NULL )
            *pNewPos = newpos;
        m_pos = newpos;
        return LVERR_OK;
    }

    /// Tell current file position
    /**
        \param pNewPos points to place to store file position
        \return lverror_t status: LVERR_OK if success
    */
    virtual lverror_t Tell( lvpos_t * pPos )
    {
        *pPos = m_pos;
        return LVERR_OK;
    }

    virtual lvpos_t SetPos(lvpos_t p)
    {
        if ( p<=m_size ) {
            m_pos = p;
            return m_pos;
        }
        return (lvpos_t)(~0);
    }

    /// Get file position
    /**
        \return lvpos_t file position
    */
    virtual lvpos_t   GetPos()
    {
        return m_pos;
    }

    /// Get file size
    /**
        \return lvsize_t file size
    */
    virtual lvsize_t  GetSize()
    {
        return m_size;
    }

    virtual lverror_t GetSize( lvsize_t * pSize )
    {
        *pSize = m_size;
        return LVERR_OK;
    }

    lverror_t error()
    {
#if defined(_WIN32)
		if ( m_hFile!=NULL ) {
			UnMap();
			if ( !CloseHandle(m_hFile) )
				CRLog::error("Error while closing file handle");
			m_hFile = NULL;
		}
#else
		if ( m_fd!= -1 ) {
            CRLog::trace("Closing mapped file %s", UnicodeToUtf8(GetName()).c_str() );
			UnMap();
            close(m_fd);
		}
        m_fd = -1;
#endif
        m_map = NULL;
        m_size = 0;
        m_mode = LVOM_ERROR;
        return LVERR_FAIL;
    }

    virtual lverror_t Read( void * buf, lvsize_t count, lvsize_t * nBytesRead )
    {
        if ( !m_map )
            return LVERR_FAIL;
        int cnt = (int)count;
        if ( m_pos + cnt > m_size )
            cnt = (int)(m_size - m_pos);
        if ( cnt <= 0 )
            return LVERR_FAIL;
        memcpy( buf, m_map + m_pos, cnt );
        m_pos += cnt;
        if (nBytesRead)
            *nBytesRead = cnt;
        return LVERR_OK;
    }

    virtual bool Read( lUInt8 * buf )
    {
        if ( m_pos < m_size ) {
            *buf = m_map[ m_pos++ ];
            return true;
        }
        return false;
    }

    virtual bool Read( lUInt16 * buf )
    {
        if ( m_pos+1 < m_size ) {
            *buf = m_map[ m_pos ] | ( ( (lUInt16)m_map[ m_pos+1 ] )<<8 );
            m_pos += 2;
            return true;
        }
        return false;
    }

    virtual bool Read( lUInt32 * buf )
    {
        if ( m_pos+3 < m_size ) {
            *buf = m_map[ m_pos ] | ( ( (lUInt32)m_map[ m_pos+1 ] )<<8 )
                | ( ( (lUInt32)m_map[ m_pos+2 ] )<<16 )
                | ( ( (lUInt32)m_map[ m_pos+3 ] )<<24 )
                ;
            m_pos += 4;
            return true;
        }
        return false;
    }

    virtual int ReadByte()
    {
        if ( m_pos < m_size ) {
            return m_map[ m_pos++ ];
        }
        return -1;
    }

    virtual lverror_t Write( const void * buf, lvsize_t count, lvsize_t * nBytesWritten )
    {
        if ( m_mode!=LVOM_APPEND )
            return LVERR_FAIL;
        lvsize_t maxSize = (lvsize_t)(m_size - m_pos);
        if ( maxSize<=0 )
            return LVERR_FAIL; // end of file reached: resize is not supported yet
        if ( count > maxSize || count > m_size )
            count = maxSize;
        memcpy( m_map + m_pos, buf, count );
        m_pos += count;
        if ( nBytesWritten )
            *nBytesWritten = count;
        return LVERR_OK;
    }

    virtual bool Eof()
    {
        return (m_pos >= m_size);
    }

    static LVFileMappedStream * CreateFileStream( lString32 fname, lvopen_mode_t mode, int minSize )
    {
        LVFileMappedStream * f = new LVFileMappedStream();
        if ( f->OpenFile( fname, mode, minSize )==LVERR_OK ) {
            return f;
        } else {
            delete f;
            return NULL;
        }
    }

	lverror_t Map()
	{
#if defined(_WIN32)
		m_hMap = CreateFileMapping(
			m_hFile,
			NULL,
			(m_mode==LVOM_READ)?PAGE_READONLY:PAGE_READWRITE, //flProtect,
			0,
			0,
			NULL
		);
		if ( m_hMap==NULL ) {
			DWORD err = GetLastError();
            CRLog::error( "LVFileMappedStream::Map() -- Cannot map file to memory, err=%08x, hFile=%08x", err, (lUInt32)m_hFile );
            return error();
		}
		m_map = (lUInt8*) MapViewOfFile(
			m_hMap,
			m_mode==LVOM_READ ? FILE_MAP_READ : FILE_MAP_READ|FILE_MAP_WRITE,
			0,
			0,
			m_size
		);
		if ( m_map==NULL ) {
            CRLog::error( "LVFileMappedStream::Map() -- Cannot map file to memory" );
            return error();
		}
		return LVERR_OK;
#else
        int mapFlags = (m_mode==LVOM_READ) ? PROT_READ : PROT_READ | PROT_WRITE;
        m_map = (lUInt8*)mmap( 0, m_size, mapFlags, MAP_SHARED, m_fd, 0 );
        if ( m_map == MAP_FAILED ) {
            CRLog::error( "LVFileMappedStream::Map() -- Cannot map file to memory" );
            return error();
        }
        return LVERR_OK;
#endif
	}

	lverror_t UnMap()
	{
#if defined(_WIN32)
		lverror_t res = LVERR_OK;
		if ( m_map!=NULL ) {
			if ( !UnmapViewOfFile( m_map ) ) {
	            CRLog::error("LVFileMappedStream::UnMap() -- Error while unmapping file");
				res = LVERR_FAIL;
			}
			m_map = NULL;
		}
		if ( m_hMap!=NULL ) {
			if ( !CloseHandle( m_hMap ) ) {
	            CRLog::error("LVFileMappedStream::UnMap() -- Error while unmapping file");
				res = LVERR_FAIL;
			}
			m_hMap = NULL;
		}
		if ( res!=LVERR_OK )
			return error();
		return res;
#else
        if ( m_map!=NULL && munmap( m_map, m_size ) == -1 ) {
            m_map = NULL;
            CRLog::error("LVFileMappedStream::UnMap() -- Error while unmapping file");
            return error();
        }
        return LVERR_OK;
#endif
	}

    virtual lverror_t SetSize( lvsize_t size )
    {
        // support only size grow
        if ( m_mode!=LVOM_APPEND )
            return LVERR_FAIL;
        if ( size == m_size )
            return LVERR_OK;
        //if ( size < m_size )
        //    return LVERR_FAIL;

		bool wasMapped = false;
        if ( m_map!=NULL ) {
			wasMapped = true;
			if ( UnMap()!=LVERR_OK )
	            return LVERR_FAIL;
        }
        m_size = size;

#if defined(_WIN32)
		// WIN32
		__int64 offset = size - 1;
        lUInt32 pos_low = (lUInt32)((__int64)offset & 0xFFFFFFFF);
        LONG pos_high = (long)(((__int64)offset >> 32) & 0xFFFFFFFF);
		pos_low = SetFilePointer(m_hFile, pos_low, &pos_high, FILE_BEGIN );
        if (pos_low == 0xFFFFFFFF) {
            lUInt32 err = GetLastError();
            if (err == ERROR_NOACCESS)
                pos_low = (lUInt32)offset;
            else if ( err != ERROR_SUCCESS)
                return error();
        }
		DWORD bytesWritten = 0;
		if ( !WriteFile( m_hFile, "", 1, &bytesWritten, NULL ) || bytesWritten!=1 )
			return error();
#else
		// LINUX
		if ( cr3_lseek( m_fd, size-1, SEEK_SET ) == (lvpos_t)-1 ) {
            CRLog::error("LVFileMappedStream::SetSize() -- Seek error");
            return error();
        }
        if ( write(m_fd, "", 1) != 1 ) {
            CRLog::error("LVFileMappedStream::SetSize() -- File resize error");
            return error();
        }
#endif
		if ( wasMapped ) {
			if ( Map() != LVERR_OK ) {
				return error();
			}
		}
        return LVERR_OK;
    }

    lverror_t OpenFile( lString32 fname, lvopen_mode_t mode, lvsize_t minSize = (lvsize_t)-1 )
    {
        m_mode = mode;
        if ( mode!=LVOM_READ && mode!=LVOM_APPEND )
            return LVERR_FAIL; // not supported
        if ( minSize==(lvsize_t)-1 ) {
            if ( !LVFileExists(fname) )
                return LVERR_FAIL;
        }
        //if ( mode==LVOM_APPEND && minSize<=0 )
        //    return LVERR_FAIL;
        SetName(fname.c_str());
        lString8 fn8 = UnicodeToUtf8( fname );
#if defined(_WIN32)
		//========================================================
		// WIN32 IMPLEMENTATION
        lUInt32 m = 0;
        lUInt32 s = 0;
        lUInt32 c = 0;
        SetName(fname.c_str());
        switch (mode) {
        case LVOM_READWRITE:
            m |= GENERIC_WRITE|GENERIC_READ;
            s |= FILE_SHARE_WRITE|FILE_SHARE_READ;
            c |= OPEN_ALWAYS;
            break;
        case LVOM_READ:
            m |= GENERIC_READ;
            s |= FILE_SHARE_READ;
            c |= OPEN_EXISTING;
            break;
        case LVOM_WRITE:
            m |= GENERIC_WRITE;
            s |= FILE_SHARE_WRITE;
            c |= CREATE_ALWAYS;
            break;
        case LVOM_APPEND:
            m |= GENERIC_WRITE|GENERIC_READ;
            s |= FILE_SHARE_WRITE;
            c |= OPEN_ALWAYS;
            break;
        case LVOM_CLOSED:
        case LVOM_ERROR:
            crFatalError();
            break;
        }
        m_hFile = CreateFileW( fname.c_str(), m, s, NULL, c, FILE_ATTRIBUTE_NORMAL, NULL);
        if (m_hFile == INVALID_HANDLE_VALUE || !m_hFile) {
			// unicode not implemented?
			lUInt32 err = GetLastError();
			if (err==ERROR_CALL_NOT_IMPLEMENTED)
				m_hFile = CreateFileA( fn8.c_str(), m, s, NULL, c, FILE_ATTRIBUTE_NORMAL, NULL);
			if ( (m_hFile == INVALID_HANDLE_VALUE) || (!m_hFile) ) {
                CRLog::error("Error opening file %s", UnicodeToUtf8(fname).c_str() );
                m_hFile = NULL;
				// error
				return error();
			}
		}
		// check size
        lUInt32 hw=0;
        m_size = GetFileSize( m_hFile, (LPDWORD)&hw );
#if LVLONG_FILE_SUPPORT
        if (hw)
            m_size |= (((lvsize_t)hw)<<32);
#endif

        if ( mode == LVOM_APPEND && m_size < minSize ) {
            if ( SetSize( minSize ) != LVERR_OK ) {
                CRLog::error( "Cannot set file size for %s", fn8.c_str() );
                return error();
            }
        }

		if ( Map()!=LVERR_OK )
			return error();

		return LVERR_OK;


#else
		//========================================================
		// LINUX IMPLEMENTATION
        m_fd = -1;

        int flags = (mode==LVOM_READ) ? O_RDONLY | O_CLOEXEC : O_RDWR | O_CREAT | O_CLOEXEC; // | O_SYNC
        m_fd = open( fn8.c_str(), flags, (mode_t)0666);
        if (m_fd == -1) {
            CRLog::error( "Error opening file %s for %s, errno=%d, msg=%s", fn8.c_str(), (mode==LVOM_READ) ? "reading" : "read/write",  (int)errno, strerror(errno) );
            return error();
        }
        struct stat stat;
        if ( fstat( m_fd, &stat ) < 0 ) {
#if defined(HAVE_STAT64) && defined(_LINUX)
            if (errno == EOVERFLOW) {
                CRLog::debug( "File require LFS support, fallback to stat64" );
                struct stat64 stat64;
                if ( fstat64( m_fd, &stat64 ) < 0 ) {
                    CRLog::error( "Cannot get file size for %s, errno=%d, msg=%s", fn8.c_str(), errno, strerror( errno )  );
                    return error();
                } else {
                    if (stat64.st_size >= INT_MIN && stat64.st_size <= INT_MAX) {
                        m_size = (lvsize_t) stat64.st_size;
                    } else {
                        CRLog::error( "File is too big to open %s");
                        return error();
                    }
                }
            } else {
                CRLog::error( "Cannot get file size for %s, errno=%d, msg=%s", fn8.c_str(), errno, strerror( errno )  );
                return error();
            }
#else
#ifdef _LINUX
            CRLog::error( "Cannot get file size for %s, errno=%d, msg=%s", fn8.c_str(), errno, strerror( errno )  );
#else
            CRLog::error( "Cannot get file size for %s", fn8.c_str()  );
#endif
            return error();
#endif
        } else {
            m_size = (lvsize_t) stat.st_size;
        }
        if ( mode == LVOM_APPEND && m_size < minSize ) {
            if ( SetSize( minSize ) != LVERR_OK ) {
                CRLog::error( "Cannot set file size for %s", fn8.c_str() );
                return error();
            }
        }

        int mapFlags = (mode==LVOM_READ) ? PROT_READ : PROT_READ | PROT_WRITE;
        m_map = (lUInt8*)mmap( 0, m_size, mapFlags, MAP_SHARED, m_fd, 0 );
        if ( m_map == MAP_FAILED ) {
            CRLog::error( "Cannot map file %s to memory", fn8.c_str() );
            return error();
        }
        return LVERR_OK;
#endif
    }
    LVFileMappedStream()
#if defined(_WIN32)
		: m_hFile(NULL), m_hMap(NULL),
#else
		: m_fd(-1),
#endif
		m_map(NULL), m_size(0), m_pos(0)
    {
        m_mode=LVOM_ERROR;
    }
    virtual ~LVFileMappedStream()
    {
		// reuse error() to close file
		error();
    }
};
#endif


/// Open memory mapped file
/**
    \param pathname is file name to open (unicode)
    \param mode is mode file should be opened in (LVOM_READ or LVOM_APPEND only)
	\param minSize is minimum file size for R/W mode
    \return reference to opened stream if success, NULL if error
*/
LVStreamRef LVMapFileStream( const lChar8 * pathname, lvopen_mode_t mode, lvsize_t minSize )
{
	lString32 fn = LocalToUnicode( lString8(pathname) );
	return LVMapFileStream( fn.c_str(), mode, minSize );
}


//#ifdef _LINUX
#undef USE_ANSI_FILES
//#endif

#if (USE_ANSI_FILES==1)

class LVFileStream : public LVNamedStream
{
private:
    FILE * m_file;
public:


    virtual lverror_t Seek( lvoffset_t offset, lvseek_origin_t origin, lvpos_t * pNewPos )
    {
       //
       int res = -1;
       switch ( origin )
       {
       case LVSEEK_SET:
           res = fseek( m_file, offset, SEEK_SET );
           break;
       case LVSEEK_CUR:
           res = fseek( m_file, offset, SEEK_CUR );
           break;
       case LVSEEK_END:
           res = fseek( m_file, offset, SEEK_END );
           break;
       }
       if (res==0)
       {
          if ( pNewPos )
              * pNewPos = ftell(m_file);
          return LVERR_OK;
       }
       CRLog::error("error setting file position to %d (%d)", (int)offset, (int)origin );
       return LVERR_FAIL;
    }
    virtual lverror_t SetSize( lvsize_t )
    {
        /*
        int64 sz = m_file->SetSize( size );
        if (sz==-1)
           return LVERR_FAIL;
        else
           return LVERR_OK;
        */
        return LVERR_FAIL;
    }
    virtual lverror_t Read( void * buf, lvsize_t count, lvsize_t * nBytesRead )
    {
        lvsize_t sz = fread( buf, 1, count, m_file );
        if (nBytesRead)
            *nBytesRead = sz;
        if ( sz==0 )
        {
            return LVERR_FAIL;
        }
        return LVERR_OK;
    }
    virtual lverror_t Write( const void * buf, lvsize_t count, lvsize_t * nBytesWritten )
    {
        lvsize_t sz = fwrite( buf, 1, count, m_file );
        if (nBytesWritten)
            *nBytesWritten = sz;
        handleAutoSync(sz);
        if (sz < count)
        {
            return LVERR_FAIL;
        }
        return LVERR_OK;
    }
    /// flushes unsaved data from buffers to file, with optional flush of OS buffers
    virtual lverror_t Flush( bool sync )
    {
        if ( !m_file )
            return LVERR_FAIL;
        fflush( m_file );
        return LVERR_OK;
    }
    virtual bool Eof()
    {
        return feof(m_file)!=0;
    }
    static LVFileStream * CreateFileStream( lString32 fname, lvopen_mode_t mode )
    {
        LVFileStream * f = new LVFileStream;
        if (f->OpenFile( fname, mode )==LVERR_OK) {
            return f;
        } else {
            delete f;
            return NULL;
        }
    }
    lverror_t OpenFile( lString32 fname, lvopen_mode_t mode )
    {
        m_mode = mode;
        m_file = NULL;
        SetName(fname.c_str());
        const char * modestr = "r" STDIO_CLOEXEC;
        switch (mode) {
        case LVOM_READ:
            modestr = "rb" STDIO_CLOEXEC;
            break;
        case LVOM_WRITE:
            modestr = "wb" STDIO_CLOEXEC;
            break;
        case LVOM_READWRITE:
        case LVOM_APPEND:
            modestr = "a+b" STDIO_CLOEXEC;
            break;
        case LVOM_CLOSED:
        case LVOM_ERROR:
            break;
        }
        FILE * file = fopen(UnicodeToLocal(fname).c_str(), modestr);
        if (!file)
        {
            //printf("cannot open file %s\n", UnicodeToLocal(fname).c_str());
            m_mode = LVOM_ERROR;
            return LVERR_FAIL;
        }
        m_file = file;
        //printf("file %s opened ok\n", UnicodeToLocal(fname).c_str());
        // set filename
        SetName( fname.c_str() );
        return LVERR_OK;
    }
    LVFileStream() : m_file(NULL)
    {
        m_mode=LVOM_ERROR;
    }
    virtual ~LVFileStream()
    {
        if (m_file)
            fclose(m_file);
    }
};

#else

class LVDirectoryContainer;
class LVFileStream : public LVNamedStream
{
    friend class LVDirectoryContainer;
protected:
#if defined(_WIN32)
    HANDLE m_hFile;
#else
    int m_fd;
#endif
    //LVDirectoryContainer * m_parent;
    lvsize_t               m_size;
    lvpos_t                m_pos;
public:
    /// flushes unsaved data from buffers to file, with optional flush of OS buffers
    virtual lverror_t Flush( bool sync )
    {
        CR_UNUSED(sync);
#ifdef _WIN32
        if ( m_hFile==INVALID_HANDLE_VALUE || !FlushFileBuffers( m_hFile ) )
            return LVERR_FAIL;
#else
        if ( m_fd==-1 )
            return LVERR_FAIL;
        if ( sync ) {
//            CRTimerUtil timer;
//            CRLog::trace("calling fsync");
            fsync( m_fd );
//            CRLog::trace("fsync took %d ms", (int)timer.elapsed());
        }
#endif
        return LVERR_OK;
    }

    virtual bool Eof()
    {
        return m_size<=m_pos;
    }
//    virtual LVContainer * GetParentContainer()
//    {
//        return (LVContainer*)m_parent;
//    }
    virtual lverror_t Read( void * buf, lvsize_t count, lvsize_t * nBytesRead )
    {
#ifdef _WIN32
        //fprintf(stderr, "Read(%08x, %d)\n", buf, count);

        if (m_hFile == INVALID_HANDLE_VALUE || m_mode==LVOM_WRITE ) // || m_mode==LVOM_APPEND
            return LVERR_FAIL;
        //
		if ( m_pos > m_size )
			return LVERR_FAIL; // EOF

        lUInt32 dwBytesRead = 0;
        if (ReadFile( m_hFile, buf, (lUInt32)count, (LPDWORD)&dwBytesRead, NULL )) {
            if (nBytesRead)
                *nBytesRead = dwBytesRead;
            m_pos += dwBytesRead;
	        return LVERR_OK;
        } else {
            //DWORD err = GetLastError();
			if (nBytesRead)
				*nBytesRead = 0;
            return LVERR_FAIL;
        }

#else
        if (m_fd == -1)
            return LVERR_FAIL;
        ssize_t res = read( m_fd, buf, count );
        if ( res!=(ssize_t)-1 ) {
            if (nBytesRead)
                *nBytesRead = res;
            m_pos += res;
            return LVERR_OK;
        }
        if (nBytesRead)
            *nBytesRead = 0;
        return LVERR_FAIL;
#endif
    }
    virtual lverror_t GetSize( lvsize_t * pSize )
    {
#ifdef _WIN32
        if (m_hFile == INVALID_HANDLE_VALUE || !pSize)
            return LVERR_FAIL;
#else
        if (m_fd == -1 || !pSize)
            return LVERR_FAIL;
#endif
        if (m_size<m_pos)
            m_size = m_pos;
        *pSize = m_size;
        return LVERR_OK;
    }
    virtual lvsize_t GetSize()
    {
#ifdef _WIN32
        if (m_hFile == INVALID_HANDLE_VALUE)
            return 0;
        if (m_size<m_pos)
            m_size = m_pos;
        return m_size;
#else
        if (m_fd == -1)
            return 0;
        if (m_size<m_pos)
            m_size = m_pos;
        return m_size;
#endif
    }
    virtual lverror_t SetSize( lvsize_t size )
    {
#ifdef _WIN32
        //
        if (m_hFile == INVALID_HANDLE_VALUE || m_mode==LVOM_READ )
            return LVERR_FAIL;
        lvpos_t oldpos = 0;
        if (!Tell(&oldpos))
            return LVERR_FAIL;
        if (!Seek(size, LVSEEK_SET, NULL))
            return LVERR_FAIL;
        SetEndOfFile( m_hFile);
        Seek(oldpos, LVSEEK_SET, NULL);
        return LVERR_OK;
#else
        if (m_fd == -1)
            return LVERR_FAIL;
        lvpos_t oldpos = 0;
        if (!Tell(&oldpos))
            return LVERR_FAIL;
        if (!Seek(size, LVSEEK_SET, NULL))
            return LVERR_FAIL;
        Seek(oldpos, LVSEEK_SET, NULL);
        return LVERR_OK;
#endif
    }
    virtual lverror_t Write( const void * buf, lvsize_t count, lvsize_t * nBytesWritten )
    {
#ifdef _WIN32
        if (m_hFile == INVALID_HANDLE_VALUE || m_mode==LVOM_READ )
            return LVERR_FAIL;
        //
        lUInt32 dwBytesWritten = 0;
        if (WriteFile( m_hFile, buf, (lUInt32)count, (LPDWORD)&dwBytesWritten, NULL )) {
            if (nBytesWritten)
                *nBytesWritten = dwBytesWritten;
            m_pos += dwBytesWritten;
            if ( m_size < m_pos )
                m_size = m_pos;
            handleAutoSync(dwBytesWritten);
            return LVERR_OK;
        }
        if (nBytesWritten)
            *nBytesWritten = 0;
        return LVERR_FAIL;

#else
        if (m_fd == -1)
            return LVERR_FAIL;
        ssize_t res = write( m_fd, buf, count );
        if ( res!=(ssize_t)-1 ) {
            if (nBytesWritten)
                *nBytesWritten = res;
            m_pos += res;
            if ( m_size < m_pos )
                m_size = m_pos;
            handleAutoSync(res);
            return LVERR_OK;
        }
        if (nBytesWritten)
            *nBytesWritten = 0;
        return LVERR_FAIL;
#endif
    }
    virtual lverror_t Seek( lvoffset_t offset, lvseek_origin_t origin, lvpos_t * pNewPos )
    {
#ifdef _WIN32
        //fprintf(stderr, "Seek(%d,%d)\n", offset, origin);
        if (m_hFile == INVALID_HANDLE_VALUE)
            return LVERR_FAIL;
        lUInt32 pos_low = (lUInt32)((__int64)offset & 0xFFFFFFFF);
        LONG pos_high = (LONG)(((__int64)offset >> 32) & 0xFFFFFFFF);
        lUInt32 m=0;
        switch (origin) {
        case LVSEEK_SET:
            m = FILE_BEGIN;
            break;
        case LVSEEK_CUR:
            m = FILE_CURRENT;
            break;
        case LVSEEK_END:
            m = FILE_END;
            break;
        }

        pos_low = SetFilePointer(m_hFile, pos_low, &pos_high, m );
		lUInt32 err;
        if (pos_low == INVALID_SET_FILE_POINTER && (err = GetLastError())!=ERROR_SUCCESS ) {
            //if (err == ERROR_NOACCESS)
            //    pos_low = (lUInt32)offset;
            //else if ( err != ERROR_SUCCESS)
            return LVERR_FAIL;
        }
        m_pos = pos_low
#if LVLONG_FILE_SUPPORT
         | ((lvpos_t)pos_high<<32)
#endif
          ;
        if (pNewPos)
            *pNewPos = m_pos;
        return LVERR_OK;
#else
        if (m_fd == -1)
            return LVERR_FAIL;
       //
       lvpos_t res = (lvpos_t)-1;
       switch ( origin )
       {
       case LVSEEK_SET:
           res = cr3_lseek( m_fd, offset, SEEK_SET );
           break;
       case LVSEEK_CUR:
           res = cr3_lseek( m_fd, offset, SEEK_CUR );
           break;
       case LVSEEK_END:
           res = cr3_lseek( m_fd, offset, SEEK_END );
           break;
       }
       if (res!=(lvpos_t)-1)
       {
           m_pos = res;
           if ( pNewPos )
               * pNewPos = res;
           return LVERR_OK;
       }
       CRLog::error("error setting file position to %d (%d)", (int)offset, (int)origin );
       return LVERR_FAIL;
#endif
    }
    lverror_t Close()
    {
#if defined(_WIN32)
        if (m_hFile == INVALID_HANDLE_VALUE)
            return LVERR_FAIL;
        CloseHandle( m_hFile );
        m_hFile = INVALID_HANDLE_VALUE;
#else
        if ( m_fd!= -1 ) {
            close(m_fd);
            m_fd = -1;
        }
#endif
        SetName(NULL);
        return LVERR_OK;
    }
    static LVFileStream * CreateFileStream( lString32 fname, lvopen_mode_t mode )
    {
        LVFileStream * f = new LVFileStream;
        if (f->OpenFile( fname, mode )==LVERR_OK) {
            return f;
        } else {
            delete f;
            return NULL;
        }
    }
    lverror_t OpenFile( lString32 fname, int mode )
    {
        mode = mode & LVOM_MASK;
#if defined(_WIN32)
        lUInt32 m = 0;
        lUInt32 s = 0;
        lUInt32 c = 0;
        SetName(fname.c_str());
        switch (mode) {
        case LVOM_READWRITE:
            m |= GENERIC_WRITE|GENERIC_READ;
            s |= FILE_SHARE_WRITE|FILE_SHARE_READ;
            c |= OPEN_ALWAYS;
            break;
        case LVOM_READ:
            m |= GENERIC_READ;
            s |= FILE_SHARE_READ;
            c |= OPEN_EXISTING;
            break;
        case LVOM_WRITE:
            m |= GENERIC_WRITE;
            s |= FILE_SHARE_WRITE;
            c |= CREATE_ALWAYS;
            break;
        case LVOM_APPEND:
            m |= GENERIC_WRITE|GENERIC_READ;
            s |= FILE_SHARE_WRITE|FILE_SHARE_READ;
            c |= OPEN_ALWAYS;
            break;
        case LVOM_CLOSED:
        case LVOM_ERROR:
            crFatalError();
            break;
        }
        m_hFile = CreateFileW( fname.c_str(), m, s, NULL, c, FILE_ATTRIBUTE_NORMAL, NULL);
        if (m_hFile == INVALID_HANDLE_VALUE || !m_hFile) {
         // unicode not implemented?
            lUInt32 err = GetLastError();
            if (err==ERROR_CALL_NOT_IMPLEMENTED)
                m_hFile = CreateFileA( UnicodeToLocal(fname).c_str(), m, s, NULL, c, FILE_ATTRIBUTE_NORMAL, NULL);
            if ( (m_hFile == INVALID_HANDLE_VALUE) || (!m_hFile) ) {
                // error
                return LVERR_FAIL;
            }
        }

        // set file size and position
        m_mode = (lvopen_mode_t)mode;
        lUInt32 hw=0;
        m_size = GetFileSize( m_hFile, (LPDWORD)&hw );
#if LVLONG_FILE_SUPPORT
        if (hw)
            m_size |= (((lvsize_t)hw)<<32);
#endif
        m_pos = 0;

        // set filename
        SetName( fname.c_str() );

        // move to end of file
        if (mode==LVOM_APPEND)
            Seek( 0, LVSEEK_END, NULL );
#else
        bool use_sync = (mode & LVOM_FLAG_SYNC)!=0;
        m_fd = -1;

        int flags = (mode==LVOM_READ) ? O_RDONLY | O_CLOEXEC : O_RDWR | O_CREAT | O_CLOEXEC | (use_sync ? O_SYNC : 0) | (mode==LVOM_WRITE ? O_TRUNC : 0);
        lString8 fn8 = UnicodeToUtf8(fname);
        m_fd = open( fn8.c_str(), flags, (mode_t)0666);
        if (m_fd == -1) {
#ifndef ANDROID
            CRLog::error( "Error opening file %s for %s", fn8.c_str(), (mode==LVOM_READ) ? "reading" : "read/write" );
            //CRLog::error( "Error opening file %s for %s, errno=%d, msg=%s", fn8.c_str(), (mode==LVOM_READ) ? "reading" : "read/write",  (int)errno, strerror(errno) );
#endif
            return LVERR_FAIL;
        }

        struct stat stat;
        if ( fstat( m_fd, &stat ) < 0 ) {
#if defined(HAVE_STAT64) && defined(_LINUX)
            if (errno == EOVERFLOW) {
                CRLog::debug( "File require LFS support, fallback to stat64" );
                struct stat64 stat64;
                if ( fstat64( m_fd, &stat64 ) < 0 ) {
                    CRLog::error( "Cannot get file size for %s, errno=%d, msg=%s", fn8.c_str(), errno, strerror( errno )  );
                    return LVERR_FAIL;
                } else {
                    if (stat64.st_size >= INT_MIN && stat64.st_size <= INT_MAX) {
                        m_mode = (lvopen_mode_t)mode;
                        m_size = (lvsize_t) stat64.st_size;
                    } else {
                        CRLog::error( "File is too big to open %s");
                        return LVERR_FAIL;
                    }
                }
            } else {
                CRLog::error( "Cannot get file size for %s, errno=%d, msg=%s", fn8.c_str(), errno, strerror( errno )  );
                return LVERR_FAIL;
            }
#else
#ifdef _LINUX
            CRLog::error( "Cannot get file size for %s, errno=%d, msg=%s", fn8.c_str(), errno, strerror( errno )  );
#else
            CRLog::error( "Cannot get file size for %s", fn8.c_str()  );
#endif
            return LVERR_FAIL;
#endif
        } else {
            m_mode = (lvopen_mode_t)mode;
            m_size = (lvsize_t) stat.st_size;
        }
#endif
        SetName(fname.c_str());
        return LVERR_OK;
    }
    LVFileStream() :
#if defined(_WIN32)
            m_hFile(INVALID_HANDLE_VALUE),
#else
            m_fd(-1),
#endif
            //m_parent(NULL),
            m_size(0), m_pos(0)
    {
    }
    virtual ~LVFileStream()
    {
        Close();
        //m_parent = NULL;
    }
};
#endif

/// tries to split full path name into archive name and file name inside archive using separator "@/" or "@\"
bool LVSplitArcName( lString32 fullPathName, lString32 & arcPathName, lString32 & arcItemPathName )
{
    int p = fullPathName.pos("@/");
    if ( p<0 )
        p = fullPathName.pos("@\\");
    if ( p<0 )
        return false;
    arcPathName = fullPathName.substr(0, p);
    arcItemPathName = fullPathName.substr(p + 2);
    return !arcPathName.empty() && !arcItemPathName.empty();
}

/// tries to split full path name into archive name and file name inside archive using separator "@/" or "@\"
bool LVSplitArcName( lString8 fullPathName, lString8 & arcPathName, lString8 & arcItemPathName )
{
    int p = fullPathName.pos("@/");
    if ( p<0 )
        p = fullPathName.pos("@\\");
    if ( p<0 )
        return false;
    arcPathName = fullPathName.substr(0, p);
    arcItemPathName = fullPathName.substr(p + 2);
    return !arcPathName.empty() && !arcItemPathName.empty();
}

// facility functions
LVStreamRef LVOpenFileStream( const lChar32 * pathname, int mode )
{
    lString32 fn(pathname);
    if (fn.length() > 1 && fn[0] == ASSET_PATH_PREFIX) {
    	if (!_assetContainerFactory || mode != LVOM_READ)
    		return LVStreamRef();
    	lString32 assetPath = LVExtractAssetPath(fn);
    	return _assetContainerFactory->openAssetStream(assetPath);
    }
#if 0
    //defined(_LINUX) || defined(_WIN32)
    if ( mode==LVOM_READ ) {
        LVFileMappedStream * stream = LVFileMappedStream::CreateFileStream( fn, mode, 0 );
        if ( stream != NULL )
        {
            return LVStreamRef( stream );
        }
        return LVStreamRef();
    }
#endif

    LVFileStream * stream = LVFileStream::CreateFileStream( fn, (lvopen_mode_t)mode );
    if ( stream!=NULL )
    {
        return LVStreamRef( stream );
    }
    return LVStreamRef();
}

LVStreamRef LVOpenFileStream( const lChar8 * pathname, int mode )
{
    lString32 fn = Utf8ToUnicode(lString8(pathname));
    return LVOpenFileStream( fn.c_str(), mode );
}


lvopen_mode_t LVTextStream::GetMode()
{
    return m_base_stream->GetMode();
}

lverror_t LVTextStream::Seek( lvoffset_t offset, lvseek_origin_t origin, lvpos_t * pNewPos )
{
    return m_base_stream->Seek(offset, origin, pNewPos);
}

lverror_t LVTextStream::Tell( lvpos_t * pPos )
{
    return m_base_stream->Tell(pPos);
}

lvpos_t   LVTextStream::SetPos(lvpos_t p)
{
    return m_base_stream->SetPos(p);
}

lvpos_t   LVTextStream::GetPos()
{
    return m_base_stream->GetPos();
}

lverror_t LVTextStream::SetSize( lvsize_t size )
{
    return m_base_stream->SetSize(size);
}

lverror_t LVTextStream::Read( void * buf, lvsize_t count, lvsize_t * nBytesRead )
{
    return m_base_stream->Read(buf, count, nBytesRead);
}

lverror_t LVTextStream::Write( const void * buf, lvsize_t count, lvsize_t * nBytesWritten )
{
    return m_base_stream->Write(buf, count, nBytesWritten);
}

bool LVTextStream::Eof()
{
    return m_base_stream->Eof();
}


class LVDirectoryContainerItemInfo : public LVCommonContainerItemInfo
{
    friend class LVDirectoryContainer;
};

class LVDirectoryContainer : public LVNamedContainer
{
protected:
    LVDirectoryContainer * m_parent;
public:
    virtual LVStreamRef OpenStream( const char32_t * fname, lvopen_mode_t mode )
    {
        LVDirectoryContainerItemInfo * item = (LVDirectoryContainerItemInfo*)GetObjectInfo(fname);
        if ( item && item->IsContainer() ) {
            // found directory with same name!!!
            return LVStreamRef();
        }
        // make filename
        lString32 fn = m_fname;
        fn << fname;
        //const char * fb8 = UnicodeToUtf8( fn ).c_str();
        //printf("Opening directory container file %s : %s fname=%s path=%s\n", UnicodeToUtf8( lString32(fname) ).c_str(), UnicodeToUtf8( fn ).c_str(), UnicodeToUtf8( m_fname ).c_str(), UnicodeToUtf8( m_path ).c_str());
        LVStreamRef stream( LVOpenFileStream( fn.c_str(), mode ) );
        if (!stream) {
            return stream;
        }
        //stream->m_parent = this;
        if (!item) {
            // add new info
            item = new LVDirectoryContainerItemInfo;
            item->m_name = fname;
            stream->GetSize(&item->m_size);
            Add(item);
        }
        return stream;
    }
    virtual LVContainer * GetParentContainer()
    {
        return (LVContainer*)m_parent;
    }
    virtual lverror_t GetSize( lvsize_t * pSize )
    {
        if (m_fname.empty())
            return LVERR_FAIL;
        *pSize = GetObjectCount();
        return LVERR_OK;
    }
    LVDirectoryContainer() : m_parent(NULL)
    {
    }
    virtual ~LVDirectoryContainer()
    {
        SetName(NULL);
        Clear();
    }
    static LVDirectoryContainer * OpenDirectory( const char32_t * path, const char32_t * mask = U"*.*" )
    {
        if (!path || !path[0])
            return NULL;


        // container object
        LVDirectoryContainer * dir = new LVDirectoryContainer;

        // make filename
        lString32 fn( path );
        lChar32 lastch = 0;
        if ( !fn.empty() )
            lastch = fn[fn.length()-1];
        if ( lastch!='\\' && lastch!='/' )
            fn << dir->m_path_separator;

        dir->SetName(fn.c_str());

#if !defined(__SYMBIAN32__) && defined(_WIN32)
        // WIN32 API
        fn << mask;
        WIN32_FIND_DATAW data = { 0 };
        WIN32_FIND_DATAA dataa = { 0 };
        //lString8 bs = DOMString(path).ToAnsiString();
        HANDLE hFind = FindFirstFileW(fn.c_str(), &data);
        bool unicode=true;
        if (hFind == INVALID_HANDLE_VALUE || !hFind) {
            lUInt32 err=GetLastError();
            if (err == ERROR_CALL_NOT_IMPLEMENTED) {
                hFind = FindFirstFileA(UnicodeToLocal(fn).c_str(), &dataa);
                unicode=false;
                if (hFind == INVALID_HANDLE_VALUE || !hFind)
                {
                    delete dir;
                    return NULL;
                }
            } else {
                delete dir;
                return NULL;
            }
        }

        if (unicode) {
            // unicode
            for (;;) {
                lUInt32 dwAttrs = data.dwFileAttributes;
                wchar_t * pfn = data.cFileName;
                for (int i=0; data.cFileName[i]; i++) {
                    if (data.cFileName[i]=='/' || data.cFileName[i]=='\\')
                        pfn = data.cFileName + i + 1;
                }

                if ( (dwAttrs & FILE_ATTRIBUTE_DIRECTORY) ) {
                    // directory
                    if (!lStr_cmp(pfn, L"..") || !lStr_cmp(pfn, L".")) {
                        // .. or .
                    } else {
                        // normal directory
                        LVDirectoryContainerItemInfo * item = new LVDirectoryContainerItemInfo;
                        item->m_name = pfn;
                        item->m_is_container = true;
                        dir->Add(item);
                    }
                } else {
                    // file
                    LVDirectoryContainerItemInfo * item = new LVDirectoryContainerItemInfo;
                    item->m_name = pfn;
                    item->m_size = data.nFileSizeLow;
                    item->m_flags = data.dwFileAttributes;
                    dir->Add(item);
                }

                if (!FindNextFileW(hFind, &data)) {
                    // end of list
                    break;
                }

            }
        } else {
            // ANSI
            for (;;) {
                lUInt32 dwAttrs = dataa.dwFileAttributes;
                char * pfn = dataa.cFileName;
                for (int i=0; dataa.cFileName[i]; i++) {
                    if (dataa.cFileName[i]=='/' || dataa.cFileName[i]=='\\')
                        pfn = dataa.cFileName + i + 1;
                }

                if ( (dwAttrs & FILE_ATTRIBUTE_DIRECTORY) ) {
                    // directory
                    if (!strcmp(pfn, "..") || !strcmp(pfn, ".")) {
                        // .. or .
                    } else {
                        // normal directory
                        LVDirectoryContainerItemInfo * item = new LVDirectoryContainerItemInfo;
                        item->m_name = LocalToUnicode( lString8( pfn ) );
                        item->m_is_container = true;
                        dir->Add(item);
                    }
                } else {
                    // file
                    LVDirectoryContainerItemInfo * item = new LVDirectoryContainerItemInfo;
                    item->m_name = LocalToUnicode( lString8( pfn ) );
                    item->m_size = data.nFileSizeLow;
                    item->m_flags = data.dwFileAttributes;
                    dir->Add(item);
                }

                if (!FindNextFileA(hFind, &dataa)) {
                    // end of list
                    break;
                }

            }
        }

        FindClose( hFind );
#else
        // POSIX
        lString32 p( fn );
        p.erase( p.length()-1, 1 );
        lString8 p8 = UnicodeToLocal( p );
        if ( p8.empty() )
            p8 = ".";
        const char * p8s = p8.c_str();
        DIR * d = opendir(p8s);
        if ( d ) {
            struct dirent * pde;
            while ( (pde = readdir(d))!=NULL ) {
                lString8 fpath = p8 + "/" + pde->d_name;
                struct stat st;
                stat( fpath.c_str(), &st );
                if ( S_ISDIR(st.st_mode) ) {
                    // dir
                    if ( strcmp(pde->d_name, ".") && strcmp(pde->d_name, "..") ) {
                        // normal directory
                        LVDirectoryContainerItemInfo * item = new LVDirectoryContainerItemInfo;
                        item->m_name = LocalToUnicode(lString8(pde->d_name));
                        item->m_is_container = true;
                        dir->Add(item);
                    }
                } else if ( S_ISREG(st.st_mode) ) {
                    // file
                    LVDirectoryContainerItemInfo * item = new LVDirectoryContainerItemInfo;
                    item->m_name = LocalToUnicode(lString8(pde->d_name));
                    item->m_size = st.st_size;
                    item->m_flags = st.st_mode;
                    dir->Add(item);
                }
            }
            closedir(d);
        } else {
            delete dir;
            return NULL;
        }


#endif
        return dir;
    }
};

class LVCachedStream : public LVNamedStream
{
private:

    #define CACHE_BUF_BLOCK_SHIFT 12
    #define CACHE_BUF_BLOCK_SIZE (1<<CACHE_BUF_BLOCK_SHIFT)
    class BufItem
    {
    public:
        lUInt32   start;
        lUInt32   size;
        BufItem * prev;
        BufItem * next;
        lUInt8    buf[CACHE_BUF_BLOCK_SIZE];

        int getIndex() { return start >> CACHE_BUF_BLOCK_SHIFT; }
        BufItem() : prev(NULL), next(NULL) { }
    };

    LVStreamRef m_stream;
    int m_bufSize;
    lvsize_t    m_size;
    lvpos_t     m_pos;
    BufItem * * m_buf;
    BufItem *   m_head;
    BufItem *   m_tail;
    int         m_bufItems;
    int         m_bufLen;

    /// add item to head
    BufItem * addNewItem( int start )
    {
        //
        int index = (start >> CACHE_BUF_BLOCK_SHIFT);
        BufItem * item = new BufItem();
        if (!m_head)
        {
            m_head = m_tail = item;
        }
        else
        {
            item->next = m_head;
            m_head->prev = item;
            m_head = item;
        }
        item->start = start;
        int sz = CACHE_BUF_BLOCK_SIZE;
        if ( start + sz > (int)m_size )
            sz = (int)(m_size - start);
        item->size = sz;
        m_buf[ index ] = item;
        m_bufLen++;
        assert( !(m_head && !m_tail) );
        return item;
    }
    /// move item to top
    void moveToTop( int index )
    {
        BufItem * item = m_buf[index];
        if ( !item || m_head == item )
            return;
        if ( m_tail == item )
            m_tail = item->prev;
        if ( item->next )
            item->next->prev = item->prev;
        if ( item->prev )
            item->prev->next = item->next;
        m_head->prev = item;
        item->next = m_head;
        item->prev = NULL;
        m_head = item;
        assert( !(m_head && !m_tail) );
    }
    /// reuse existing item from tail of list
    BufItem * reuseItem( int start )
    {
        //
        int rem_index = m_tail->start >> CACHE_BUF_BLOCK_SHIFT;
        if (m_tail->prev)
            m_tail->prev->next = NULL;
        m_tail = m_tail->prev;
        BufItem * item = m_buf[rem_index];
        m_buf[ rem_index ] = NULL;
        int index = (start >> CACHE_BUF_BLOCK_SHIFT);
        m_buf[ index ] = item;
        item->start = start;
        int sz = CACHE_BUF_BLOCK_SIZE;
        if ( start + sz > (int)m_size )
            sz = (int)(m_size - start);
        item->size = sz;
        item->next = m_head;
        item->prev = NULL;
        m_head->prev = item;
        m_head = item;
        assert( !(m_head && !m_tail) );
        return item;
    }
    /// read item content from base stream
    bool fillItem( BufItem * item )
    {
        //if ( m_stream->SetPos( item->start )==(lvpos_t)(~0) )
        if ( m_stream->SetPos( item->start )!=(lvpos_t)item->start )
            return false;
        //int streamSize=m_stream->GetSize(); int bytesLeft = m_stream->GetSize() - m_stream->GetPos();
        lvsize_t bytesRead = 0;
        if ( m_stream->Read( item->buf, item->size, &bytesRead )!=LVERR_OK || bytesRead!=item->size )
            return false;
        return true;
    }
    BufItem * addOrReuseItem( int start )
    {
        //assert( !(m_head && !m_tail) );
        if ( m_bufLen < m_bufSize )
            return addNewItem( start );
        else
            return reuseItem( start );
    }
    /// checks several items, loads if necessary
    bool fillFragment( int startIndex, int count )
    {
        if (count<=0 || startIndex<0 || startIndex+count>m_bufItems)
        {
            return false;
        }
        int firstne = -1;
        int lastne = -1;
        int i;
        for ( i=startIndex; i<startIndex+count; i++)
        {
            if ( m_buf[i] )
            {
                moveToTop( i );
            }
            else
            {
                if (firstne == -1)
                    firstne = i;
                lastne = i;
            }
        }
        if ( firstne<0 )
            return true;
        for ( i=firstne; i<=lastne; i++)
        {
            if ( !m_buf[i] )
            {
                BufItem * item = addOrReuseItem( i << CACHE_BUF_BLOCK_SHIFT );
                if ( !fillItem ( item ) )
                    return false;
            }
            else
            {
                moveToTop( i );
            }
        }
        return true;
    }
public:

    LVCachedStream( LVStreamRef stream, int bufSize ) : m_stream(stream), m_pos(0),
            m_head(NULL), m_tail(NULL), m_bufLen(0)
    {
        m_size = m_stream->GetSize();
        m_bufItems = (int)((m_size + CACHE_BUF_BLOCK_SIZE - 1) >> CACHE_BUF_BLOCK_SHIFT);
        if (!m_bufItems)
            m_bufItems = 1;
        m_bufSize = (bufSize + CACHE_BUF_BLOCK_SIZE - 1) >> CACHE_BUF_BLOCK_SHIFT;
        if (m_bufSize<3)
            m_bufSize = 3;
        m_buf = new BufItem* [m_bufItems]();
        SetName( stream->GetName() );
    }
    virtual ~LVCachedStream()
    {
        if (m_buf)
        {
            for (int i=0; i<m_bufItems; i++)
                if (m_buf[i])
                    delete m_buf[i];
            delete[] m_buf;
        }
    }

    /// fastly return already known CRC
    virtual lverror_t getcrc32( lUInt32 & dst )
    {
        return m_stream->getcrc32( dst );
    }

    virtual bool Eof()
    {
        return m_pos >= m_size;
    }
    virtual lvsize_t  GetSize()
    {
        return m_size;
    }

    virtual lverror_t Seek(lvoffset_t offset, lvseek_origin_t origin, lvpos_t* newPos)
    {
        lvpos_t npos = 0;
        lvpos_t currpos = m_pos;
        switch (origin) {
        case LVSEEK_SET:
            npos = offset;
            break;
        case LVSEEK_CUR:
            npos = currpos + offset;
            break;
        case LVSEEK_END:
            npos = m_size + offset;
            break;
        }
        if (npos > m_size)
            return LVERR_FAIL;
        m_pos = npos;
        if (newPos)
        {
            *newPos =  m_pos;
        }
        return LVERR_OK;
    }

    virtual lverror_t Write(const void*, lvsize_t, lvsize_t*)
    {
        return LVERR_NOTIMPL;
    }

    virtual lverror_t Read(void* buf, lvsize_t size, lvsize_t* pBytesRead)
    {
        if ( m_pos + size > m_size )
            size = m_size - m_pos;
        if ( size <= 0 ) {
            if ( pBytesRead )
                *pBytesRead = 0;
            return LVERR_FAIL;
        }
        int startIndex = (int)(m_pos >> CACHE_BUF_BLOCK_SHIFT);
        int endIndex = (int)((m_pos + size - 1) >> CACHE_BUF_BLOCK_SHIFT);
        int count = endIndex - startIndex + 1;
        int extraItems = (m_bufSize - count); // max move backward
        if (extraItems<0)
            extraItems = 0;
        char * flags = new char[ count ]();

        //if ( m_stream
        int start = (int)m_pos;
        lUInt8 * dst = (lUInt8 *) buf;
        int dstsz = (int)size;
        int i;
        int istart = start & (CACHE_BUF_BLOCK_SIZE - 1);
        for ( i=startIndex; i<=endIndex; i++ )
        {
            BufItem * item = m_buf[i];
            if (item)
            {
                int isz = item->size - istart;
                if (isz > dstsz)
                    isz = dstsz;
                memcpy( dst, item->buf + istart, isz );
                flags[i - startIndex] = 1;
            }
            dstsz -= CACHE_BUF_BLOCK_SIZE - istart;
            dst += CACHE_BUF_BLOCK_SIZE - istart;
            istart = 0;
        }

        dst = (lUInt8 *) buf;

        bool flgFirstNE = true;
        istart = start & (CACHE_BUF_BLOCK_SIZE - 1);
        dstsz = (int)size;
        for ( i=startIndex; i<=endIndex; i++ )
        {
            if (!flags[ i - startIndex])
            {
                if ( !m_buf[i] )
                {
                    int fillStart = i;
                    if ( flgFirstNE )
                    {
                        fillStart -= extraItems;
                    }
                    if (fillStart<0)
                        fillStart = 0;
                    int fillEnd = fillStart + m_bufSize - 1;
                    if (fillEnd>endIndex)
                        fillEnd = endIndex;
                    bool res = fillFragment( fillStart, fillEnd - fillStart + 1 );
                    if ( !res )
                    {
                        fprintf( stderr, "cannot fill fragment %d .. %d\n", fillStart, fillEnd );
                        exit(-1);
                    }
                    flgFirstNE = false;
                }
                BufItem * item = m_buf[i];
                int isz = item->size - istart;
                if (isz > dstsz)
                    isz = dstsz;
                memcpy( dst, item->buf + istart, isz );
            }
            dst += CACHE_BUF_BLOCK_SIZE - istart;
            dstsz -= CACHE_BUF_BLOCK_SIZE - istart;
            istart = 0;
        }
        delete[] flags;

        lvsize_t bytesRead = size;
        if ( m_pos + size > m_size )
            bytesRead = m_size - m_pos;
        m_pos += bytesRead;
        if (pBytesRead)
            *pBytesRead = bytesRead;
        return LVERR_OK;
    }

    virtual lverror_t SetSize(lvsize_t)
    {
        return LVERR_NOTIMPL;
    }
};

#define ZIPHDR_MAX_NM 4096 // maximum length of the name entry
#define ZIPHDR_MAX_XT 1024 // maximum length of the extra data

#pragma pack(push, 1)

typedef struct ZipLocalFileHdr {
    lUInt32  Mark;      // 0
    lUInt8   UnpVer;    // 4
    lUInt8   UnpOS;     // 5
    lUInt16  Flags;     // 6
    lUInt16  others[11]; //
    //lUInt16  Method;    // 8
    //lUInt32  ftime;     // A
    //lUInt32  CRC;       // E
    //lUInt32  PackSize;  //12
    //lUInt32  UnpSize;   //16
    //lUInt16  NameLen;   //1A
    //lUInt16  AddLen;    //1C

    lUInt16  getMethod() { return others[0]; }    // 8
    lUInt32  getftime() { return others[1] | ( (lUInt32)others[2] << 16); }     // A
    lUInt32  getCRC() { return others[3] | ( (lUInt32)others[4] << 16); }       // E
    lUInt32  getPackSize() { return others[5] | ( (lUInt32)others[6] << 16); }  //12
    lUInt32  getUnpSize() { return others[7] | ( (lUInt32)others[8] << 16); }   //16
    lUInt16  getNameLen() { return others[9]; }   //1A
    lUInt16  getAddLen() { return others[10]; }    //1C
    void byteOrderConv()
    {
        //
        lvByteOrderConv cnv;
        if ( cnv.msf() )
        {
            cnv.rev( &Mark );
            cnv.rev( &Flags );
            for ( int i=0; i<11; i++) {
                cnv.rev( &others[i] );
            }
            //cnv.rev( &Method );
            //cnv.rev( &ftime );
            //cnv.rev( &CRC );
            //cnv.rev( &PackSize );
            //cnv.rev( &UnpSize );
            //cnv.rev( &NameLen );
            //cnv.rev( &AddLen );
        }
    }
    //  Omitted fields (which follow this structure):
    // FileName (size = NameLen)
    // ExtraField (size = AddLen)
} ZipLocalFileHdr;
#pragma pack(pop)

#pragma pack(push, 1)
struct ZipHd2
{
    lUInt32  Mark;      // 0
    lUInt8   PackVer;   // 4
    lUInt8   PackOS;
    lUInt8   UnpVer;
    lUInt8   UnpOS;
    lUInt16     Flags;  // 8
    lUInt16     Method; // A
    lUInt32    ftime;   // C
    lUInt32    CRC;     // 10
    lUInt32    PackSize;// 14, ZIP64: if == 0xFFFFFFFF use Zip64ExtInfo
    lUInt32    UnpSize; // 18, ZIP64: if == 0xFFFFFFFF use Zip64ExtInfo
    lUInt16     NameLen;// 1C
    lUInt16     AddLen; // 1E
    lUInt16     CommLen;// 20
    lUInt16     DiskNum;// 22
    //lUInt16     ZIPAttr;// 24
    //lUInt32     Attr;   // 26
    //lUInt32     Offset; // 2A
    lUInt16     _Attr_and_Offset[5];   // 24
    lUInt16     getZIPAttr() { return _Attr_and_Offset[0]; }
    lUInt32     getAttr() { return _Attr_and_Offset[1] | ((lUInt32)_Attr_and_Offset[2]<<16); }
    lUInt32     getOffset() { return _Attr_and_Offset[3] | ((lUInt32)_Attr_and_Offset[4]<<16); }    // ZIP64: if == 0xFFFFFFFF use Zip64ExtInfo
    void        setOffset(lUInt32 offset) {
        _Attr_and_Offset[3] = (lUInt16)(offset & 0xFFFF);
        _Attr_and_Offset[4] = (lUInt16)(offset >> 16);
    }
    void byteOrderConv()
    {
        //
        lvByteOrderConv cnv;
        if ( cnv.msf() )
        {
            cnv.rev( &Mark );
            cnv.rev( &Flags );
            cnv.rev( &Method );
            cnv.rev( &ftime );
            cnv.rev( &CRC );
            cnv.rev( &PackSize );
            cnv.rev( &UnpSize );
            cnv.rev( &NameLen );
            cnv.rev( &AddLen );
            cnv.rev( &CommLen );
            cnv.rev( &DiskNum );
            cnv.rev( &_Attr_and_Offset[0] );
            cnv.rev( &_Attr_and_Offset[1] );
            cnv.rev( &_Attr_and_Offset[2] );
            cnv.rev( &_Attr_and_Offset[3] );
            cnv.rev( &_Attr_and_Offset[4] );
        }
    }
    //  Omitted fields (which follow this structure):
    // FileName (size = NameLen)
    // ExtraField (size = AddLen)
    // FileComment (size = CommLen)
};

struct Zip64ExtInfo
{
    lUInt16 Tag;        // 0x0001
    lUInt16 Size;       // 4-28
    lUInt8 data[28];

    void byteOrderConv() {
        lvByteOrderConv cnv;
        if ( cnv.msf() ) {
            cnv.rev( &Tag );
            cnv.rev( &Size );
        }
    }
    lUInt32 getField32(int pos) {
        if (pos >= 0 && pos + 3 < Size) {
            return (lUInt32)data[pos] | (((lUInt32)data[pos + 1]) << 8) |
                    (((lUInt32)data[pos + 2]) << 16) | (((lUInt32)data[pos + 3]) << 24);
        }
        return 0UL;
    }
    lUInt64 getField64(int pos) {
        if (pos >= 0 && pos + 7 < Size) {
            return (lUInt64)data[pos] | (((lUInt64)data[pos + 1]) << 8) |
                    (((lUInt64)data[pos + 2]) << 16) | (((lUInt64)data[pos + 3]) << 24) |
                    (((lUInt64)data[pos + 4]) << 32) | (((lUInt64)data[pos + 5]) << 40) |
                    (((lUInt64)data[pos + 6]) << 48) | (((lUInt64)data[pos + 7]) << 56);
        }
        return 0UL;
    }
};

#pragma pack(pop)

#define ARC_INBUF_SIZE  (8 * 1024)
#define ARC_OUTBUF_SIZE (16 * 1024)

#if (USE_ZLIB==1)

class LVZipDecodeStream : public LVNamedStream
{
private:
    LVStreamRef m_stream;
    lvsize_t    m_packsize;
    lvsize_t    m_unpacksize;
    bool        m_zInitialized;
    lUInt32     m_CRC;
    lUInt32     m_originalCRC;
    lvpos_t     m_pos;
    lvpos_t     m_inpos;
    lvpos_t     m_outpos;
    z_stream_s  m_zstream;
    lUInt8      m_inbuf[ARC_INBUF_SIZE];
    lUInt8      m_outbuf[ARC_OUTBUF_SIZE];

    LVZipDecodeStream( LVStreamRef stream, lvsize_t packsize, lvsize_t unpacksize, lUInt32 crc )
        : m_stream(stream), m_packsize(packsize), m_unpacksize(unpacksize), m_zInitialized(false)
        , m_CRC(0), m_originalCRC(crc), m_pos(0), m_inpos(0), m_outpos(0)
    {
        rewind();
    }

    ~LVZipDecodeStream()
    {
        zUninit();
    }

    /// Get stream open mode
    /** \return lvopen_mode_t open mode */
    virtual lvopen_mode_t GetMode()
    {
        return LVOM_READ;
    }

    void zUninit()
    {
        if (!m_zInitialized)
            return;
        inflateEnd(&m_zstream);
        m_zInitialized = false;
    }

    /// Fill input buffer: returns false on failure.
    bool fillInBuf()
    {
        assert(m_inpos <= m_packsize);
        assert(m_zstream.avail_in <= ARC_INBUF_SIZE);
        if (m_inpos >= m_packsize || m_zstream.avail_in >= (ARC_INBUF_SIZE / 2))
            return true;
        if (m_zstream.avail_in)
            memcpy(m_inbuf, m_zstream.next_in, m_zstream.avail_in);
        m_zstream.next_in = m_inbuf;
        lvsize_t count = ARC_INBUF_SIZE - m_zstream.avail_in;
        if (m_stream->Read(m_inbuf + m_zstream.avail_in, count, &count) != LVERR_OK)
            return false;
        m_inpos += count;
        m_zstream.avail_in += count;
        return m_zstream.avail_in > 0;
    }

    bool rewind()
    {
        zUninit();
        if (m_stream->Seek(0, LVSEEK_SET, NULL) != LVERR_OK)
            return false;
        m_CRC = m_outpos = m_inpos = 0;
        memset(&m_zstream, 0, sizeof (m_zstream));
        m_zstream.next_in = m_inbuf;
        m_zstream.avail_in = 0;
        m_zstream.next_out = m_outbuf;
        m_zstream.avail_out = ARC_OUTBUF_SIZE;
        int res = inflateInit2(&m_zstream, -15);
        if (res != Z_OK) {
            CRLog::error("ZIP stream: init error (%d)", res);
            return false;
        }
        m_zInitialized = true;
        return true;
    }
    // returns count of available decoded bytes in buffer
    inline int getAvailBytes()
    {
        return m_zstream.next_out - m_outbuf;
    }
    /// decode next portion of data, return false on failure
    bool decodeNext()
    {
        if (!fillInBuf())
            return false;

        m_outpos = m_zstream.total_out;
        m_zstream.next_out = m_outbuf;
        m_zstream.avail_out = ARC_OUTBUF_SIZE;

        int res = inflate(&m_zstream, m_inpos < m_packsize ? Z_NO_FLUSH : Z_FINISH);
        switch (res) {
        case Z_BUF_ERROR:
        case Z_STREAM_END:
        case Z_OK:
            break;
        default:
            CRLog::error("ZIP stream: decoding error (%d)", res);
            return false;
        }

#if 0
        m_CRC = lStr_crc32(m_CRC, m_outbuf, getAvailBytes());
        if (res == Z_STREAM_END && m_CRC != m_originalCRC) {
            CRLog::error("ZIP stream '%s': CRC doesn't match", LCSTR(lString32(GetName())));
            return false;
        }
#endif

        return true;
    }
    /// skip bytes from out stream
    bool skip( int bytesToSkip )
    {
        for ( ; ; ) {
            int avail = getAvailBytes();
            if (avail >= bytesToSkip)
                return true;
            bytesToSkip -= avail;
            if (!decodeNext())
                return false;
        }
    }
public:

    /// fastly return already known CRC
    virtual lverror_t getcrc32( lUInt32 & dst )
    {
        dst = m_originalCRC;
        return LVERR_OK;
    }

    virtual bool Eof()
    {
        return m_pos == m_unpacksize;
    }
    virtual lvsize_t GetSize()
    {
        return m_unpacksize;
    }
    virtual lvpos_t GetPos()
    {
        return m_pos;
    }
    virtual lverror_t GetPos( lvpos_t * pos )
    {
        if (pos)
            *pos = m_pos;
        return LVERR_OK;
    }
    virtual lverror_t Seek(lvoffset_t offset, lvseek_origin_t origin, lvpos_t* newPos)
    {
        if (!m_zInitialized)
            return LVERR_FAIL;

        lvpos_t abspos;
        switch (origin) {
        case LVSEEK_SET:
            abspos = offset;
            break;
        case LVSEEK_CUR:
            abspos = m_pos + offset;
            break;
        case LVSEEK_END:
            abspos = m_unpacksize + offset;
            break;
        default:
            return LVERR_FAIL;
        }
        if (abspos > m_unpacksize)
            return LVERR_FAIL;

        if (abspos > m_zstream.total_out) {
            if (!skip(abspos - m_zstream.total_out))
                return LVERR_FAIL;
        }
        else if (abspos < m_outpos) {
            if (!rewind() || !skip(abspos))
                return LVERR_FAIL;
        }

        m_pos = abspos;
        if (newPos)
            *newPos = abspos;

        return LVERR_OK;
    }
    virtual lverror_t Write(const void*, lvsize_t, lvsize_t*)
    {
        return LVERR_NOTIMPL;
    }
    virtual lverror_t Read(void* buf, lvsize_t count, lvsize_t* bytesRead)
    {
        if (!m_zInitialized)
            return LVERR_FAIL;

        if ((m_pos + count) > m_unpacksize)
            count = m_unpacksize - m_pos;

        lUInt8 * ptr = (lUInt8 *)buf;
        lUInt8 * end = ptr + count;

        for ( ; ; ) {
            int offset = m_pos - m_outpos;
            int avail = getAvailBytes() - offset;
            if ((ptr + avail) > end)
                avail = end - ptr;
            memcpy(ptr, m_outbuf + offset, avail);
            m_pos += avail;
            ptr += avail;
            if (ptr >= end)
                break;
            if (!decodeNext())
                break;
        }

        if (bytesRead)
            *bytesRead = ptr - (lUInt8 *)buf;

        return ptr != end ? LVERR_FAIL : LVERR_OK;
    }
    virtual lverror_t SetSize(lvsize_t)
    {
        return LVERR_NOTIMPL;
    }
    static LVStream * Create( LVStreamRef stream, lvpos_t pos, lString32 name, lvsize_t srcPackSize, lvsize_t srcUnpSize, lUInt32 srcCRC )
    {
        ZipLocalFileHdr hdr;
        unsigned hdr_size = 0x1E; //sizeof(hdr);
        if ( stream->Seek( pos, LVSEEK_SET, NULL )!=LVERR_OK )
            return NULL;
        lvsize_t ReadSize = 0;
        if ( stream->Read( &hdr, hdr_size, &ReadSize)!=LVERR_OK || ReadSize!=hdr_size )
            return NULL;
        hdr.byteOrderConv();

        pos += hdr_size + hdr.getNameLen() + hdr.getAddLen();
        if ((lvpos_t)(pos + srcPackSize) > (lvpos_t)stream->GetSize())
            return NULL;

        switch (hdr.getMethod()) {
        case 0:
            // store method, copy as is
            if (srcPackSize != srcUnpSize)
                return NULL;
            break;
        case 8:
            // deflate
            break;
        default:
            CRLog::error("Unimplemented compression method: 0x%02X", hdr.getMethod());
            return NULL;
        }

        LVNamedStream * res = new LVStreamFragment(stream, pos, srcPackSize);
        if (hdr.getMethod())
            res = new LVZipDecodeStream(LVStreamRef(res), srcPackSize, srcUnpSize, srcCRC);
        res->SetName(name.c_str());
        return res;
    }
};

class LVZipArc : public LVArcContainerBase
{
protected:
    // whether the alternative "truncated" method was used, or is to be used
    bool m_alt_reading_method = false;
public:
    bool usedAltReadingMethod() { return m_alt_reading_method; }
    void useAltReadingMethod() { m_alt_reading_method = true; }

    virtual LVStreamRef OpenStream( const char32_t * fname, lvopen_mode_t /*mode*/ )
    {
        if ( fname[0]=='/' )
            fname++;
        LVCommonContainerItemInfo * item = (LVCommonContainerItemInfo*)GetObjectInfo(fname);
        if ( !item )
            return LVStreamRef(); // not found
        // make filename
        lString32 fn = fname;
        LVStreamRef strm = m_stream; // fix strange arm-linux-g++ bug
        LVStreamRef stream(
            LVZipDecodeStream::Create(
                strm,
                item->GetSrcPos(),
                fn,
                item->GetSrcSize(),
                item->GetSize(),
                item->GetCRC())
        );
        if (!stream.isNull()) {
            stream->SetName(item->GetName());
            // Use buffering?
            return stream;
            //return LVCreateBufferedStream( stream, ZIP_STREAM_BUFFER_SIZE );
        }
        return stream;
    }
    LVZipArc( LVStreamRef stream ) : LVArcContainerBase(stream)
    {
        SetName(stream->GetName());
    }
    virtual ~LVZipArc()
    {
    }
    virtual int ReadContents() {
        lvByteOrderConv cnv;
        //bool arcComment = false;
        bool truncated = false;

        Clear();
        if (!m_stream || m_stream->Seek(0, LVSEEK_SET, NULL) != LVERR_OK)
            return -1;

        SetName(m_stream->GetName());

        lvsize_t fileSize = 0;
        if (m_stream->GetSize(&fileSize) != LVERR_OK)
            return -1;

        char ReadBuf[1024];
        lUInt32 NextPosition;
        lvoffset_t NextOffset;
        lvoffset_t CurPos;
        lvsize_t ReadSize;
        bool found = false;
        bool found64 = false;
        bool require64 = false;
        bool zip64 = false;
        lUInt64 NextPosition64 = 0;
        NextPosition = 0;
        if (fileSize < sizeof(ReadBuf) - 18)
            CurPos = (lvoffset_t)(-fileSize);
        else
            CurPos = (lvoffset_t)(-sizeof(ReadBuf) + 18);
        // Find End of central directory record (EOCD)
        for (int bufNo = 0; bufNo < 64; bufNo++) {
            if (m_stream->Seek(CurPos, LVSEEK_END, NULL) != LVERR_OK)
                break;
            if (m_stream->Read(ReadBuf, sizeof(ReadBuf), &ReadSize) != LVERR_OK)
                break;
            if (ReadSize == 0)
                break;
            for (int i = (int)ReadSize - 4; i >= 0; i--) {
                if (ReadBuf[i] == 0x50 && ReadBuf[i + 1] == 0x4b &&
                    ReadBuf[i + 2] == 0x05 && ReadBuf[i + 3] == 0x06) {
                    if (m_stream->Seek(CurPos + i + 16, LVSEEK_END, NULL) != LVERR_OK)
                        break;
                    if (m_stream->Read(&NextPosition, sizeof(NextPosition), &ReadSize) != LVERR_OK)
                        break;
                    cnv.lsf(&NextPosition);
                    found = true;
                    if (0xFFFFFFFFUL == NextPosition) {
                        require64 = true;
                        if (found64)
                            break;
                    } else {
                        break;
                    }
                }
                if (ReadBuf[i] == 0x50 && ReadBuf[i + 1] == 0x4b &&
                    ReadBuf[i + 2] == 0x06 && ReadBuf[i + 3] == 0x06) {
                    if (m_stream->Seek(CurPos + i + 48, LVSEEK_END, NULL) != LVERR_OK)
                        break;
                    if (m_stream->Read(&NextPosition64, sizeof(NextPosition64), &ReadSize) != LVERR_OK)
                        break;
                    cnv.lsf(&NextPosition64);
                    found64 = true;
                    break;
                }
            }
            if (fileSize < sizeof(ReadBuf) - 4)
                CurPos = (lvoffset_t)(-fileSize);
            else
                CurPos -= (lvoffset_t)sizeof(ReadBuf) - 4;
            if (CurPos <= (lvoffset_t)(-fileSize))
                break;
        }
        zip64 = found64 || require64;

#if LVLONG_FILE_SUPPORT == 1
        if (found64)
            NextOffset = NextPosition64;
        else if (found && !require64)
            NextOffset = NextPosition;
        else
            truncated = true;
#else
        if (zip64) {
            CRLog::error("zip64 signature found, but large file support is not enabled, stop processing.");
            return -1;
        }
        truncated = !found;
        NextOffset = NextPosition;
#endif


        // If the main reading method (using zip header at the end of the
        // archive) failed, we can try using the alternative method used
        // when this zip header is missing ("truncated"), which uses
        // local zip headers met along while scanning the zip.
        if (m_alt_reading_method)
            truncated = true; // do as if truncated
        else if (truncated) // archive detected as truncated
            // flag that, so there's no need to try that alt method,
            // as it was used on first scan
            m_alt_reading_method = true;

        if (truncated)
            NextOffset = 0;

        //================================================================
        // get file list


        lverror_t err;
        ZipLocalFileHdr ZipHd1;
        ZipHd2 ZipHeader = { 0 };
        unsigned ZipHeader_size = 0x2E; //sizeof(ZipHd2); //0x34; //
        unsigned ZipHd1_size = 0x1E; //sizeof(ZipHd1); //sizeof(ZipHd1)

        for (;;) {

            if (m_stream->Seek(NextOffset, LVSEEK_SET, NULL) != LVERR_OK)
                return 0;

            if (truncated) {
                // The offset (that we don't find in a local header, but
                // that we will store in the ZipHeader we're building)
                // happens to be the current position here.
                lUInt32 offset = (lUInt32)m_stream->GetPos();

                err = m_stream->Read(&ZipHd1, ZipHd1_size, &ReadSize);
                ZipHd1.byteOrderConv();

                if (err != LVERR_OK || ReadSize != ZipHd1_size) {
                    if (ReadSize == 0 && NextOffset == (lvoffset_t)fileSize)
                        return m_list.length();
                    if (ReadSize == 0)
                        return m_list.length();
                    return 0;
                }

                ZipHeader.UnpVer = ZipHd1.UnpVer;
                ZipHeader.UnpOS = ZipHd1.UnpOS;
                ZipHeader.Flags = ZipHd1.Flags;
                ZipHeader.ftime = ZipHd1.getftime();
                ZipHeader.CRC = ZipHd1.getCRC();
                ZipHeader.PackSize = ZipHd1.getPackSize();
                ZipHeader.UnpSize = ZipHd1.getUnpSize();
                ZipHeader.NameLen = ZipHd1.getNameLen();
                ZipHeader.AddLen = ZipHd1.getAddLen();
                ZipHeader.Method = ZipHd1.getMethod();
                ZipHeader.setOffset(offset);
                // We may get a last invalid record with NameLen=0, which shouldn't hurt.
                // If it does, use:
                // if (ZipHeader.NameLen == 0) break;
            } else {

                err = m_stream->Read(&ZipHeader, ZipHeader_size, &ReadSize);

                ZipHeader.byteOrderConv();
                if (err != LVERR_OK || ReadSize != ZipHeader_size) {
                    if (ReadSize > 16 && (ZipHeader.Mark == 0x06054B50 || ZipHeader.Mark == 0x06064b50)) {
                        break;
                    }
                    return 0;
                }
            }

            if (ReadSize == 0 || ZipHeader.Mark == 0x06054b50 || ZipHeader.Mark == 0x06064b50 ||
                (truncated && ZipHeader.Mark == 0x02014b50)) {
                //                if (!truncated && *(lUInt16 *)((char *)&ZipHeader+20)!=0)
                //                    arcComment=true;
                break; //(GETARC_EOF);
            }

#if LVLONG_FILE_SUPPORT == 1
            int extraPosUnpSize = -1;
            int extraPosPackSize = -1;
            int extraPosOffset = -1;
            int extraLastPos = 0;
            Zip64ExtInfo *zip64ExtInfo = NULL;
            if (0xFFFFFFFF == ZipHeader.UnpSize) {
                extraPosUnpSize = extraLastPos;
                extraLastPos += 8;
            }
            if (0xFFFFFFFF == ZipHeader.PackSize) {
                extraPosPackSize = extraLastPos;
                extraLastPos += 8;
            }
            if (0xFFFFFFFF == ZipHeader.getOffset()) {
                extraPosOffset = extraLastPos;
                extraLastPos += 8;
            }
            if (!zip64 && extraLastPos > 0)
                zip64 = true;
#endif

            if (ZipHeader.NameLen > ZIPHDR_MAX_NM) {
                CRLog::error("ZIP entry name is too long: %u, trunc to %u",
                             (unsigned int)ZipHeader.NameLen, (unsigned int)ZIPHDR_MAX_NM);
            }
            lvsize_t fnameSizeToRead = (ZipHeader.NameLen < ZIPHDR_MAX_NM) ? ZipHeader.NameLen : ZIPHDR_MAX_NM;
            lvoffset_t NM_skipped_sz = (ZipHeader.NameLen > ZIPHDR_MAX_NM) ? (lvoffset_t)(ZipHeader.NameLen - ZIPHDR_MAX_NM) : 0;
            char fnbuf[ZIPHDR_MAX_NM + 1];
            err = m_stream->Read(fnbuf, fnameSizeToRead, &ReadSize);
            if (err != LVERR_OK || ReadSize != fnameSizeToRead) {
                CRLog::error("error while reading zip entry name");
                return 0;
            }
            fnbuf[fnameSizeToRead] = 0;
            if (NM_skipped_sz > 0) {
                if (m_stream->Seek(NM_skipped_sz, LVSEEK_CUR, NULL) != LVERR_OK) {
                    CRLog::error("error while skipping the long zip entry name");
                    return 0;
                }
            }

            // read extra data
            if (ZipHeader.AddLen > ZIPHDR_MAX_XT) {
                CRLog::error("ZIP entry extra data is too long: %u, trunc to %u",
                             (unsigned int)ZipHeader.AddLen, (unsigned int)ZIPHDR_MAX_XT);
            }
            lvsize_t extraSizeToRead = (ZipHeader.AddLen < ZIPHDR_MAX_XT) ? ZipHeader.AddLen : ZIPHDR_MAX_XT;
            lvoffset_t XT_skipped_sz = (ZipHeader.AddLen > ZIPHDR_MAX_XT) ? (lvoffset_t)(ZipHeader.AddLen - ZIPHDR_MAX_XT) : 0;
            lUInt8 extra[ZIPHDR_MAX_XT];
            err = m_stream->Read(extra, extraSizeToRead, &ReadSize);
            if (err != LVERR_OK || ReadSize != extraSizeToRead) {
                CRLog::error("error while reading zip entry extra data");
                return 0;
            }
            if (XT_skipped_sz > 0) {
                if (m_stream->Seek(XT_skipped_sz, LVSEEK_CUR, NULL) != LVERR_OK) {
                    CRLog::error("error while skipping the long zip entry extra data");
                    return 0;
                }
            }
#if LVLONG_FILE_SUPPORT == 1
            // Find Zip64 extension if required
            lvsize_t offs = 0;
            Zip64ExtInfo *ext;
            if (zip64) {
                while (offs + 4 < extraSizeToRead) {
                    ext = (Zip64ExtInfo *)&extra[offs];
                    ext->byteOrderConv();
                    if (0x0001 == ext->Tag) {
                        zip64ExtInfo = ext;
                        break;
                    } else {
                        offs += 4 + ext->Size;
                    }
                }
            }
#endif

            lUInt32 SeekLen = ZipHeader.CommLen;
            if (truncated)
                SeekLen += ZipHeader.PackSize;

            NextOffset = (lvoffset_t)m_stream->GetPos();
            NextOffset += SeekLen;
            if (NextOffset >= (lvoffset_t)fileSize) {
                CRLog::error("invalid offset, stop to read contents.");
                break;
            }

            lString32 fName;
            if (ZipHeader.PackVer >= 63 && (ZipHeader.Flags & 0x0800) == 0x0800) {
                // Language encoding flag (EFS).  If this bit is set,
                // the filename and comment fields for this file
                // MUST be encoded using UTF-8. (InfoZip APPNOTE-6.3.0)
                //CRLog::trace("ZIP 6.3: Language encoding flag (EFS) enabled, using UTF-8 encoding.");
                fName = Utf8ToUnicode(fnbuf);
            } else {
                if (isValidUtf8Data((const unsigned char *)fnbuf, fnameSizeToRead)) {
                    //CRLog::trace("autodetected UTF-8 encoding.");
                    fName = Utf8ToUnicode(fnbuf);
                } else {
                    // {"DOS","Amiga","VAX/VMS","Unix","VM/CMS","Atari ST",
                    //  "OS/2","Mac-OS","Z-System","CP/M","TOPS-20",
                    //  "Win32","SMS/QDOS","Acorn RISC OS","Win32 VFAT","MVS",
                    //  "BeOS","Tandem"};
                    // TODO: try to detect proper charset using 0x0008 Extra Field (InfoZip APPNOTE-6.3.5, Appendix D.4).
                    const lChar32 *enc_name = (ZipHeader.PackOS == 0) ? U"cp866" : U"cp1251";
                    //CRLog::trace("detected encoding %s", LCSTR(enc_name));
                    const lChar32 *table = GetCharsetByte2UnicodeTable(enc_name);
                    fName = ByteToUnicode(lString8(fnbuf), table);
                }
            }

            LVCommonContainerItemInfo *item = new LVCommonContainerItemInfo();
#if LVLONG_FILE_SUPPORT == 1
            lvsize_t fileUnpSize = (lvsize_t)ZipHeader.UnpSize;
            lvsize_t filePackSize = (lvsize_t)ZipHeader.PackSize;
            lvpos_t fileOffset = (lvpos_t)ZipHeader.getOffset();
            if (zip64ExtInfo != NULL) {
                if (extraPosUnpSize >= 0)
                    fileUnpSize = zip64ExtInfo->getField64(extraPosUnpSize);
                if (extraPosPackSize >= 0)
                    filePackSize = zip64ExtInfo->getField64(extraPosPackSize);
                if (extraPosOffset >= 0)
                    fileOffset = zip64ExtInfo->getField64(extraPosOffset);
            }
            item->SetItemInfo(fName.c_str(), fileUnpSize, ZipHeader.getAttr() & 0x3f, ZipHeader.CRC);
            item->SetSrc(fileOffset, filePackSize, ZipHeader.Method);
#else
            item->SetItemInfo(fName.c_str(), ZipHeader.UnpSize, ZipHeader.getAttr() & 0x3f, ZipHeader.CRC);
            item->SetSrc(ZipHeader.getOffset(), ZipHeader.PackSize, ZipHeader.Method);
#endif
            Add(item);

//#define DUMP_ZIP_HEADERS
#ifdef DUMP_ZIP_HEADERS
#if LVLONG_FILE_SUPPORT == 1
            CRLog::trace("ZIP entry '%s' unpSz=%llu, pSz=%llu, m=%x, offs=%llu, zAttr=%x, flg=%x, addL=%d, commL=%d, dn=%d", LCSTR(fName), fileUnpSize, filePackSize, (int)ZipHeader.Method, fileOffset, (int)ZipHeader.getZIPAttr(), (int)ZipHeader.getAttr(), (int)ZipHeader.AddLen, (int)ZipHeader.CommLen, (int)ZipHeader.DiskNum);
#else
            CRLog::trace("ZIP entry '%s' unpSz=%d, pSz=%d, m=%x, offs=%x, zAttr=%x, flg=%x, addL=%d, commL=%d, dn=%d", LCSTR(fName), (int)ZipHeader.UnpSize, (int)ZipHeader.PackSize, (int)ZipHeader.Method, (int)ZipHeader.getOffset(), (int)ZipHeader.getZIPAttr(), (int)ZipHeader.getAttr(), (int)ZipHeader.AddLen, (int)ZipHeader.CommLen, (int)ZipHeader.DiskNum);
#endif
            //, addL=%d, commL=%d, dn=%d
            //, (int)ZipHeader.AddLen, (int)ZipHeader.CommLen, (int)ZipHeader.DiskNum
#define EXTRA_DEC_MAX (ZIPHDR_MAX_XT * 3 + 1)
            if (extraSizeToRead > 0) {
                char extra_buff[EXTRA_DEC_MAX];
                memset(extra_buff, 0, EXTRA_DEC_MAX);
                char* ptr = &extra_buff[0];
                for (lvsize_t i = 0; i < extraSizeToRead; i++) {
                    sprintf(ptr, ":%02X", extra[i]);
                    ptr += 3;
                }
                *ptr = 0;
                CRLog::trace("  ZIP entry extra data: %s", extra_buff);
            }
#endif
        }
        return m_list.length();
    }

    static LVArcContainerBase * OpenArchieve( LVStreamRef stream )
    {
        // read beginning of file
        const lvsize_t hdrSize = 4;
        char hdr[hdrSize];
        stream->SetPos(0);
        lvsize_t bytesRead = 0;
        if (stream->Read(hdr, hdrSize, &bytesRead)!=LVERR_OK || bytesRead!=hdrSize)
                return NULL;
        stream->SetPos(0);
        // detect arc type
        if (hdr[0]!='P' || hdr[1]!='K' || hdr[2]!=3 || hdr[3]!=4)
                return NULL;
        LVZipArc * arc = new LVZipArc( stream );
        int itemCount = arc->ReadContents();
        if ( itemCount > 0 && arc->usedAltReadingMethod() ) {
            printf("CRE WARNING: zip file truncated: going on with possibly partial content.\n");
        }
        else if ( itemCount == 0 && !arc->usedAltReadingMethod() ) {
            printf("CRE WARNING: zip file corrupted or invalid: trying alternative processing...\n");
            arc->useAltReadingMethod();
            itemCount = arc->ReadContents();
        }
        if ( itemCount <= 0 )
        {
            printf("CRE WARNING: zip file corrupted or invalid: processing failure.\n");
            delete arc;
            return NULL;
        }
        return arc;
    }

};
#endif

#if (USE_UNRAR==1)
class LVRarArc : public LVArcContainerBase
{
public:

    virtual LVStreamRef OpenStream( const lChar32 * fname, lvopen_mode_t mode )
    {
        int found_index = -1;
        for (int i=0; i<m_list.length(); i++) {
            if ( !lStr_cmp( fname, m_list[i]->GetName() ) ) {
                if ( m_list[i]->IsContainer() ) {
                    // found directory with same name!!!
                    return LVStreamRef();
                }
                found_index = i;
                break;
            }
        }
        if (found_index<0)
            return LVStreamRef(); // not found

        // TODO
        return LVStreamRef(); // not found
/*
        // make filename
        lString32 fn = fname;
        LVStreamRef strm = m_stream; // fix strange arm-linux-g++ bug
        LVStreamRef stream(
		LVZipDecodeStream::Create(
			strm,
			m_list[found_index]->GetSrcPos(), fn ) );
        if (!stream.isNull()) {
            return LVCreateBufferedStream( stream, ZIP_STREAM_BUFFER_SIZE );
        }
        stream->SetName(m_list[found_index]->GetName());
        return stream;
*/
    }
    LVRarArc( LVStreamRef stream ) : LVArcContainerBase(stream)
    {
    }
    virtual ~LVRarArc()
    {
    }

    virtual int ReadContents()
    {
        lvByteOrderConv cnv;

        m_list.clear();

        if (!m_stream || m_stream->Seek(0, LVSEEK_SET, NULL)!=LVERR_OK)
            return 0;

        SetName( m_stream->GetName() );

        lvsize_t sz = 0;
        if (m_stream->GetSize( &sz )!=LVERR_OK)
                return 0;
        lvsize_t m_FileSize = (unsigned)sz;

        return m_list.length();
    }

    static LVArcContainerBase * OpenArchieve( LVStreamRef stream )
    {
        // read beginning of file
        const lvsize_t hdrSize = 4;
        char hdr[hdrSize];
        stream->SetPos(0);
        lvsize_t bytesRead = 0;
        if (stream->Read(hdr, hdrSize, &bytesRead)!=LVERR_OK || bytesRead!=hdrSize)
            return NULL;
        stream->SetPos(0);
        // detect arc type
        if (hdr[0]!='R' || hdr[1]!='a' || hdr[2]!='r' || hdr[3]!='!')
            return NULL;
        LVRarArc * arc = new LVRarArc( stream );
        int itemCount = arc->ReadContents();
        if ( itemCount <= 0 )
        {
            delete arc;
            return NULL;
        }
        return arc;
    }

};
#endif // UNRAR





class LVMemoryStream : public LVNamedStream
{
protected:
	lUInt8 *               m_pBuffer;
	bool                   m_own_buffer;
	LVContainer *          m_parent;
	lvsize_t               m_size;
	lvsize_t               m_bufsize;
	lvpos_t                m_pos;
	lvopen_mode_t          m_mode;
public:
    /// Check whether end of file is reached
    /**
        \return true if end of file reached
    */
    virtual bool Eof()
    {
        return m_pos>=m_size;
    }
    virtual lvopen_mode_t GetMode()
    {
        return m_mode;
    }
    /** \return LVERR_OK if change is ok */
    virtual lverror_t SetMode( lvopen_mode_t mode )
    {
    	if ( m_mode==mode )
    		return LVERR_OK;
    	if ( m_mode==LVOM_WRITE && mode==LVOM_READ ) {
    		m_mode = LVOM_READ;
    		m_pos = 0;
    		return LVERR_OK;
    	}
    	// TODO: READ -> WRITE/APPEND
    	return LVERR_FAIL;
    }
	virtual LVContainer * GetParentContainer()
	{
		return (LVContainer*)m_parent;
	}
	virtual lverror_t Read( void * buf, lvsize_t count, lvsize_t * nBytesRead )
	{
		if (!m_pBuffer || m_mode==LVOM_WRITE || m_mode==LVOM_APPEND )
			return LVERR_FAIL;
		//
		int bytesAvail = (int)(m_size - m_pos);
		if (bytesAvail>0) {
			int bytesRead = bytesAvail;
			if (bytesRead>(int)count)
				bytesRead = (int)count;
			if (bytesRead>0)
				memcpy( buf, m_pBuffer+(int)m_pos, bytesRead );
			if (nBytesRead)
				*nBytesRead = bytesRead;
			m_pos += bytesRead;
		} else {
			if (nBytesRead)
				*nBytesRead = 0; // EOF
		}
		return LVERR_OK;
	}
    virtual lvsize_t  GetSize()
    {
        if (!m_pBuffer)
            return (lvsize_t)(-1);
        if (m_size<m_pos)
            m_size = m_pos;
        return m_size;
    }

    virtual lverror_t GetSize( lvsize_t * pSize )
	{
		if (!m_pBuffer || !pSize)
			return LVERR_FAIL;
		if (m_size<m_pos)
			m_size = m_pos;
		*pSize = m_size;
		return LVERR_OK;
	}
	// ensure that buffer is at least new_size long
	lverror_t SetBufSize( lvsize_t new_size )
	{
		if (!m_pBuffer || m_mode==LVOM_READ )
			return LVERR_FAIL;
		if (new_size<=m_bufsize)
			return LVERR_OK;
		if (m_own_buffer!=true)
			return LVERR_FAIL; // cannot resize foreign buffer
		//
		int newbufsize = (int)(new_size * 2 + 4096);
        m_pBuffer = cr_realloc( m_pBuffer, newbufsize );
		m_bufsize = newbufsize;
		return LVERR_OK;
	}
	virtual lverror_t SetSize( lvsize_t size )
	{
		//
		if (SetBufSize( size )!=LVERR_OK)
			return LVERR_FAIL;
		m_size = size;
		if (m_pos>m_size)
			m_pos = m_size;
		return LVERR_OK;
	}
	virtual lverror_t Write( const void * buf, lvsize_t count, lvsize_t * nBytesWritten )
	{
		if (!m_pBuffer || !buf || m_mode==LVOM_READ )
			return LVERR_FAIL;
		SetBufSize( m_pos+count ); // check buf size
		int bytes_avail = (int)(m_bufsize-m_pos);
		if (bytes_avail>(int)count)
			bytes_avail = (int)count;
		if (bytes_avail>0) {
			memcpy( m_pBuffer+m_pos, buf, bytes_avail );
			m_pos+=bytes_avail;
			if (m_size<m_pos)
				m_size = m_pos;
		}
		if (nBytesWritten)
			*nBytesWritten = bytes_avail;
		return LVERR_OK;
	}
	virtual lverror_t Seek( lvoffset_t offset, lvseek_origin_t origin, lvpos_t * pNewPos )
	{
		if (!m_pBuffer)
			return LVERR_FAIL;
		lvpos_t newpos;
		switch (origin) {
		case LVSEEK_SET:
			newpos = offset;
			break;
		case LVSEEK_CUR:
			newpos = m_pos + offset;
			break;
		case LVSEEK_END:
			newpos = m_size + offset;
			break;
		default:
			return LVERR_FAIL;
		}
		if (newpos>m_size)
			return LVERR_FAIL;
		m_pos = newpos;
		if (pNewPos)
			*pNewPos = m_pos;
		return LVERR_OK;
	}
	lverror_t Close()
	{
		if (!m_pBuffer)
			return LVERR_FAIL;
		if (m_pBuffer && m_own_buffer)
            free(m_pBuffer);
		m_pBuffer = NULL;
		m_size = 0;
		m_bufsize = 0;
		m_pos = 0;
		return LVERR_OK;
	}
	lverror_t Create( )
	{
		Close();
		m_bufsize = 4096;
		m_size = 0;
		m_pos = 0;
		m_pBuffer = (lUInt8*)malloc((int)m_bufsize);
		m_own_buffer = true;
		m_mode = LVOM_READWRITE;
		return LVERR_OK;
	}
    /// Creates memory stream as copy of another stream.
	lverror_t CreateCopy( LVStreamRef srcStream, lvopen_mode_t mode )
	{
		Close();
        if ( mode!=LVOM_READ || srcStream.isNull() )
            return LVERR_FAIL;
        lvsize_t sz = srcStream->GetSize();
        if ( (int)sz <= 0 || sz > 0x200000 )
            return LVERR_FAIL;
		m_bufsize = sz;
		m_size = 0;
		m_pos = 0;
		m_pBuffer = (lUInt8*)malloc((int)m_bufsize);
		if (m_pBuffer) {
            lvsize_t bytesRead = 0;
            srcStream->Read( m_pBuffer, m_bufsize, &bytesRead );
            if ( bytesRead!=m_bufsize ) {
                free(m_pBuffer);
                m_pBuffer = 0;
                m_size = 0;
                m_pos = 0;
                m_bufsize = 0;
                return LVERR_FAIL;
            }
		}
        m_size = sz;
		m_own_buffer = true;
		m_mode = mode;
		return LVERR_OK;
	}


	lverror_t CreateCopy( const lUInt8 * pBuf, lvsize_t size, lvopen_mode_t mode )
	{
		Close();
		m_bufsize = size;
		m_pos = 0;
		m_pBuffer = (lUInt8*) malloc((int)m_bufsize);
		if (m_pBuffer) {
			memcpy( m_pBuffer, pBuf, (int)size );
		}
		m_own_buffer = true;
		m_mode = mode;
        m_size = size;
		if (mode==LVOM_APPEND)
			m_pos = m_size;
		return LVERR_OK;
	}
	lverror_t Open( lUInt8 * pBuf, lvsize_t size )
	{
                if (!pBuf)
			return LVERR_FAIL;
		m_own_buffer = false;
		m_pBuffer = pBuf;
		m_bufsize = size;
		// set file size and position
		m_pos = 0;
		m_size = size;
		m_mode = LVOM_READ;

		return LVERR_OK;
	}
	LVMemoryStream() : m_pBuffer(NULL), m_own_buffer(false), m_parent(NULL), m_size(0), m_pos(0)
	{
	}
	virtual ~LVMemoryStream()
	{
		Close();
		m_parent = NULL;
	}
};

#if (USE_ZLIB==1)
LVContainerRef LVOpenArchieve( LVStreamRef stream )
{
    LVContainerRef ref;
    if (stream.isNull())
        return ref;

    // try ZIP
    ref = LVZipArc::OpenArchieve( stream );
    if (!ref.isNull())
        return ref;

#if USE_UNRAR==1
    // try RAR
    ref = LVRarArc::OpenArchieve( stream );
    if (!ref.isNull())
        return ref;
#endif
    // not found: return null ref
    return ref;
}
#endif

/// Creates memory stream as copy of string contents
LVStreamRef LVCreateStringStream( lString8 data )
{
    LVMemoryStream * stream = new LVMemoryStream();
    stream->CreateCopy( (const lUInt8*)data.c_str(), data.length(), LVOM_READ );
    return LVStreamRef( stream );
}

/// Creates memory stream as copy of string contents
LVStreamRef LVCreateStringStream( lString32 data )
{
    return LVCreateStringStream( UnicodeToUtf8( data ) );
}

LVStreamRef LVCreateMemoryStream( void * buf, int bufSize, bool createCopy, lvopen_mode_t mode )
{
    LVMemoryStream * stream = new LVMemoryStream();
    if ( !buf )
        stream->Create();
    else if ( createCopy )
        stream->CreateCopy( (lUInt8*)buf, bufSize, mode );
    else
        stream->Open( (lUInt8*)buf, bufSize );
    return LVStreamRef( stream );
}

LVStreamRef LVCreateMemoryStream( LVStreamRef srcStream )
{
    LVMemoryStream * stream = new LVMemoryStream();
    if ( stream->CreateCopy(srcStream, LVOM_READ)==LVERR_OK )
        return LVStreamRef( stream );
    else
        delete stream;
    return LVStreamRef();
}

/// Creates memory stream as copy of file contents.
LVStreamRef LVCreateMemoryStream( lString32 filename )
{
    LVStreamRef fs = LVOpenFileStream( filename.c_str(), LVOM_READ );
    if ( fs.isNull() )
        return fs;
    return LVCreateMemoryStream( fs );
}

LVStreamRef LVCreateBufferedStream( LVStreamRef stream, int bufSize )
{
    if ( stream.isNull() || bufSize < 512 )
        return stream;
    return LVStreamRef( new LVCachedStream( stream, bufSize ) );
}

lvsize_t LVPumpStream( LVStreamRef out, LVStreamRef in )
{
    return LVPumpStream( out.get(), in.get() );
}

lvsize_t LVPumpStream( LVStream * out, LVStream * in )
{
    char buf[5000];
    lvsize_t totalBytesWrite = 0;
    lvsize_t bytesRead = 0;
    lvsize_t bytesWrite = 0;
    in->SetPos(0);
    lvsize_t bytesToRead = in->GetSize();
    while ( bytesToRead>0 )
    {
        unsigned blockSize = 5000;
        if (blockSize > bytesToRead)
            blockSize = bytesToRead;
        bytesRead = 0;
        if ( in->Read( buf, blockSize, &bytesRead )!=LVERR_OK )
            break;
        if ( !bytesRead )
            break;
        bytesWrite = 0;
        if (out->Write(buf, bytesRead, &bytesWrite) != LVERR_OK)
            break;
        bytesToRead -= bytesRead;
        totalBytesWrite += bytesWrite;
        if (bytesWrite != bytesRead)
            break;
    }
    return totalBytesWrite;
}

bool LVDirectoryIsEmpty(const lString8& path) {
    return LVDirectoryIsEmpty(Utf8ToUnicode(path));
}

bool LVDirectoryIsEmpty(const lString32& path) {
    LVContainerRef dir = LVOpenDirectory(path);
    if (dir.isNull())
        return false;
    return dir->GetObjectCount() == 0;
}

LVContainerRef LVOpenDirectory(const lString32& path, const char32_t * mask) {
	return LVOpenDirectory(path.c_str(), mask);
}

LVContainerRef LVOpenDirectory(const lString8& path, const char32_t * mask) {
	return LVOpenDirectory(Utf8ToUnicode(path).c_str(), mask);
}

LVContainerRef LVOpenDirectory( const char32_t * path, const char32_t * mask )
{
	lString32 pathname(path);
    if (pathname.length() > 1 && pathname[0] == ASSET_PATH_PREFIX) {
    	if (!_assetContainerFactory)
    		return LVContainerRef();
    	lString32 assetPath = LVExtractAssetPath(pathname);
    	return _assetContainerFactory->openAssetContainer(assetPath);
    }
    LVContainerRef dir(LVDirectoryContainer::OpenDirectory(path, mask));
    return dir;
}

/// Stream base class
class LVTCRStream : public LVNamedStream
{
    class TCRCode {
    public:
        int len;
        char * str;
        TCRCode()
            : len(0), str(NULL)
        {
        }
        void set( const char * s, int sz )
        {
            if ( sz>0 ) {
                str = (char *)malloc( sz + 1 );
                memcpy( str, s, sz );
                str[sz] = 0;
                len = sz;
            }
        }
        ~TCRCode()
        {
            if ( str )
                free( str );
        }
    };
    LVStreamRef _stream;
    TCRCode _codes[256];
    lvpos_t _packedStart;
    lvsize_t _packedSize;
    lvsize_t _unpSize;
    lUInt32 * _index;
    lUInt8 * _decoded;
    int _decodedSize;
    int _decodedLen;
    unsigned _partIndex;
    lvpos_t _decodedStart;
    int _indexSize;
    lvpos_t _pos;
    //int _indexPos;
    #define TCR_READ_BUF_SIZE 4096
    lUInt8 _readbuf[TCR_READ_BUF_SIZE];
    LVTCRStream( LVStreamRef stream )
    : _stream(stream), _index(NULL), _decoded(NULL),
      _decodedSize(0), _decodedLen(0), _partIndex((unsigned)-1), _decodedStart(0), _indexSize(0), _pos(0)
    {
    }

    bool decodePart( unsigned index )
    {
        if ( _partIndex==index )
            return true;
        lvsize_t bytesRead;
        int bytesToRead = TCR_READ_BUF_SIZE;
        if ( (index+1)*TCR_READ_BUF_SIZE > _packedSize )
            bytesToRead = TCR_READ_BUF_SIZE - ((index+1)*TCR_READ_BUF_SIZE - _packedSize);
        if ( bytesToRead<=0 || bytesToRead>TCR_READ_BUF_SIZE )
            return false;
        if ( _stream->SetPos(_packedStart + index * TCR_READ_BUF_SIZE)==(lvpos_t)(~0) )
            return false;
        if ( _stream->Read( _readbuf, bytesToRead, &bytesRead )!=LVERR_OK )
            return false;
        if ( bytesToRead!=(int)bytesRead )
            return false;
        //TODO
        if ( !_decoded ) {
            _decodedSize = TCR_READ_BUF_SIZE * 2;
            _decoded = (lUInt8 *)malloc( _decodedSize );
        }
        _decodedLen = 0;
        for ( unsigned i=0; i<bytesRead; i++ ) {
            TCRCode * item = &_codes[_readbuf[i]];
            for ( int j=0; j<item->len; j++ )
                _decoded[_decodedLen++] = item->str[j];
            if ( _decodedLen >= _decodedSize - 256 ) {
                _decodedSize += TCR_READ_BUF_SIZE / 2;
                _decoded = cr_realloc( _decoded, _decodedSize );
            }
        }
        _decodedStart = _index[index];
        _partIndex = index;
        return true;
    }
public:
    ~LVTCRStream()
    {
        if ( _index )
            free(_index);
    }
    bool init()
    {
        lUInt8 sz;
        char buf[256];
        lvsize_t bytesRead;
        for ( int i=0; i<256; i++ ) {
            if ( _stream->Read( &sz, 1, &bytesRead )!=LVERR_OK || bytesRead!=1 )
                return false;
            if ( sz==0 && i!=0 )
                return false; // only first entry may be 0
            if ( sz && (_stream->Read( buf, sz, &bytesRead )!=LVERR_OK || bytesRead!=sz) )
                return false;
            _codes[i].set( buf, sz );
        }
        _packedStart = _stream->GetPos();
        if ( _packedStart==(lvpos_t)(~0) )
            return false;
        _packedSize = _stream->GetSize() - _packedStart;
        if ( _packedSize<10 || _packedSize>0x8000000 )
            return false;
        _indexSize = (_packedSize + TCR_READ_BUF_SIZE - 1) / TCR_READ_BUF_SIZE;
        _index = (lUInt32*)malloc( sizeof(lUInt32) * (_indexSize + 1) );
        lvpos_t pos = 0;
        lvsize_t size = 0;
        for (;;) {
            bytesRead = 0;
            int res = _stream->Read( _readbuf, TCR_READ_BUF_SIZE, &bytesRead );
            if ( res!=LVERR_OK && res!=LVERR_EOF )
                return false;
            if ( bytesRead>0 ) {
                for ( unsigned i=0; i<bytesRead; i++ ) {
                    int sz = _codes[_readbuf[i]].len;
                    if ( (pos & (TCR_READ_BUF_SIZE-1)) == 0 ) {
                        // add pos index
                        int index = pos / TCR_READ_BUF_SIZE;
                        _index[index] = size;
                    }
                    size += sz;
                    pos ++;
                }
            }
            if ( res==LVERR_EOF || bytesRead==0 ) {
                if ( _packedStart + pos != _stream->GetSize() )
                    return false;
                break;
            }
        }
        _index[ _indexSize ] = size;
        _unpSize = size;
        return decodePart( 0 );
    }
    static LVStreamRef create( LVStreamRef stream, int mode )
    {
        LVStreamRef res;
        if ( stream.isNull() || mode != LVOM_READ )
            return res;
        static const char * signature = "!!8-Bit!!";
        char buf[9];
        if ( stream->SetPos(0)!=0 )
            return res;
        lvsize_t bytesRead = 0;
        if ( stream->Read(buf, 9, &bytesRead)!=LVERR_OK
            || bytesRead!=9 )
            return res;
        if (memcmp(signature, buf, 9) != 0)
            return res;
        LVTCRStream * decoder = new LVTCRStream( stream );
        if ( !decoder->init() ) {
            delete decoder;
            return res;
        }
        return LVStreamRef ( decoder );
    }

    /// Get stream open mode
    /** \return lvopen_mode_t open mode */
    virtual lvopen_mode_t GetMode()
    {
        return LVOM_READ;
    }

    /// Seek (change file pos)
    /**
        \param offset is file offset (bytes) relateve to origin
        \param origin is offset base
        \param pNewPos points to place to store new file position
        \return lverror_t status: LVERR_OK if success
    */
    virtual lverror_t Seek( lvoffset_t offset, lvseek_origin_t origin, lvpos_t * pNewPos )
    {
        lvpos_t npos = 0;
        lvpos_t currpos = _pos;
        switch (origin) {
        case LVSEEK_SET:
            npos = offset;
            break;
        case LVSEEK_CUR:
            npos = currpos + offset;
            break;
        case LVSEEK_END:
            npos = _unpSize + offset;
            break;
        }
        if (npos >= _unpSize)
            return LVERR_FAIL;
        _pos = npos;
        if ( _pos < _decodedStart || _pos >= _decodedStart + _decodedLen ) {
            // load another part
            int a = 0;
            int b = _indexSize;
            int c;
            for (;;) {
                c = (a + b) / 2;
                if ( a >= b-1 )
                    break;
                if ( _index[c] > _pos )
                    b = c;
                else if ( _index[c+1] <= _pos )
                    a = c + 1;
                else
                    break;
            }
            if ( _index[c]>_pos || _index[c+1]<=_pos )
                return LVERR_FAIL; // wrong algorithm?
            if ( !decodePart( c ) )
                return LVERR_FAIL;
        }
        if (pNewPos)
        {
            *pNewPos =  _pos;
        }
        return LVERR_OK;
    }


    /// Get file position
    /**
        \return lvpos_t file position
    */
    virtual lvpos_t   GetPos()
    {
        return _pos;
    }

    /// Get file size
    /**
        \return lvsize_t file size
    */
    virtual lvsize_t  GetSize()
    {
        return _unpSize;
    }

    virtual lverror_t GetSize( lvsize_t * pSize )
    {
        *pSize = _unpSize;
        return LVERR_OK;
    }

    /// Set file size
    /**
        \param size is new file size
        \return lverror_t status: LVERR_OK if success
    */
    virtual lverror_t SetSize( lvsize_t )
    {
        return LVERR_FAIL;
    }

    /// Read
    /**
        \param buf is buffer to place bytes read from stream
        \param count is number of bytes to read from stream
        \param nBytesRead is place to store real number of bytes read from stream
        \return lverror_t status: LVERR_OK if success
    */
    virtual lverror_t Read( void * buf, lvsize_t count, lvsize_t * nBytesRead )
    {
        // TODO
        lvsize_t bytesRead = 0;
        lUInt8 * dst = (lUInt8*) buf;
        while (count) {
            int bytesLeft = _decodedLen - (int)(_pos - _decodedStart);
            if ( bytesLeft<=0 || bytesLeft>_decodedLen ) {
                SetPos(_pos);
                bytesLeft = _decodedLen - (int)(_pos - _decodedStart);
                if ( bytesLeft==0 && _pos==_decodedStart+_decodedLen) {
                    if (nBytesRead)
                        *nBytesRead = bytesRead;
                    return bytesRead ? LVERR_OK : LVERR_EOF;
                }
                if ( bytesLeft<=0 || bytesLeft>_decodedLen ) {
                    if (nBytesRead)
                        *nBytesRead = bytesRead;
                    return LVERR_FAIL;
                }
            }
            lUInt8 * src = _decoded + (_pos - _decodedStart);
            unsigned n = count;
            if ( n > (unsigned)bytesLeft )
                n = bytesLeft;
            for (unsigned i=0; i<n; i++) {
                *dst++ = *src++;
            }
            count -= n;
            // bytesLeft -= n;
            bytesRead += n;
            _pos += n;
        }
        if (nBytesRead)
            *nBytesRead = bytesRead;
        return LVERR_OK;
    }

    /// Write
    /**
        \param buf is data to write to stream
        \param count is number of bytes to write
        \param nBytesWritten is place to store real number of bytes written to stream
        \return lverror_t status: LVERR_OK if success
    */
    virtual lverror_t Write( const void *, lvsize_t, lvsize_t *)
    {
        return LVERR_FAIL;
    }

    /// Check whether end of file is reached
    /**
        \return true if end of file reached
    */
    virtual bool Eof()
    {
        //TODO
        return false;
    }
};

/// creates TCR decoder stream for stream
LVStreamRef LVCreateTCRDecoderStream( LVStreamRef stream )
{
    return LVTCRStream::create( stream, LVOM_READ );
}

/// returns path part of pathname (appended with / or \ delimiter)
lString8 LVExtractPath( lString8 pathName, bool appendEmptyPath) {
    return UnicodeToUtf8(LVExtractPath(Utf8ToUnicode(pathName), appendEmptyPath));
}

/// returns path part of pathname (appended with / or \ delimiter)
lString32 LVExtractPath( lString32 pathName, bool appendEmptyPath )
{
    int last_delim_pos = -1;
    for ( int i=0; i<pathName.length(); i++ )
        if ( pathName[i]=='/' || pathName[i]=='\\' )
            last_delim_pos = i;
    if ( last_delim_pos==-1 )
#ifdef _LINUX
        return lString32(appendEmptyPath ? U"./" : U"");
#else
        return lString32(appendEmptyPath ? U".\\" : U"");
#endif
    return pathName.substr( 0, last_delim_pos+1 );
}

/// returns filename part of pathname
lString8 LVExtractFilename( lString8 pathName ) {
    return UnicodeToUtf8(LVExtractFilename(Utf8ToUnicode(pathName)));
}

/// returns filename part of pathname
lString32 LVExtractFilename( lString32 pathName )
{
    int last_delim_pos = -1;
    for ( int i=0; i<pathName.length(); i++ )
        if ( pathName[i]=='/' || pathName[i]=='\\' )
            last_delim_pos = i;
    if ( last_delim_pos==-1 )
        return pathName;
    return pathName.substr( last_delim_pos+1 );
}

/// returns filename part of pathname without extension
lString32 LVExtractFilenameWithoutExtension( lString32 pathName )
{
    lString32 s = LVExtractFilename( pathName );
    int lastDot = -1;
    for ( int i=0; i<s.length(); i++ )
        if ( s[i]=='.' )
            lastDot = i;
    if ( lastDot<=0 || lastDot<(int)s.length()-7 )
        return s;
    return s.substr( 0, lastDot );
}

/// returns true if absolute path is specified
bool LVIsAbsolutePath( lString32 pathName )
{
    if ( pathName.empty() )
        return false;
    lChar32 c = pathName[0];
    if ( c=='\\' || c=='/' )
        return true;
#ifdef _WIN32
    if ( (c>='a' && c<='z') || (c>='A' && c<='Z') ) {
        return (pathName[1]==':');
    }
#endif
    return false;
}

/// removes first path part from pathname and returns it
lString32 LVExtractFirstPathElement( lString32 & pathName )
{
    if ( pathName.empty() )
        return lString32::empty_str;
    if ( pathName[0]=='/' || pathName[0]=='\\' )
        pathName.erase(0, 1);
    int first_delim_pos = -1;
    for ( int i=0; i<pathName.length(); i++ )
        if ( pathName[i]=='/' || pathName[i]=='\\' ) {
            first_delim_pos = i;
            break;
        }
    if ( first_delim_pos==-1 ) {
        lString32 res = pathName;
        pathName.clear();
        return res;
    }
    lString32 res = pathName.substr(0, first_delim_pos );
    pathName.erase(0, first_delim_pos+1 );
    return res;
}

/// appends path delimiter character to end of path, if absent
void LVAppendPathDelimiter( lString32 & pathName )
{
    if ( pathName.empty() || (pathName.length() == 1 && pathName[0] == ASSET_PATH_PREFIX))
        return;
    lChar32 delim = LVDetectPathDelimiter( pathName );
    if ( pathName[pathName.length()-1]!=delim )
        pathName << delim;
}

/// appends path delimiter character to end of path, if absent
void LVAppendPathDelimiter( lString8 & pathName )
{
    if ( pathName.empty() || (pathName.length() == 1 && pathName[0] == ASSET_PATH_PREFIX))
        return;
    lChar8 delim = LVDetectPathDelimiter(pathName);
    if ( pathName[pathName.length()-1]!=delim )
        pathName << delim;
}

/// removes path delimiter from end of path, if present
void LVRemoveLastPathDelimiter( lString32 & pathName ) {
    if (pathName.empty() || (pathName.length() == 1 && pathName[0] == ASSET_PATH_PREFIX))
        return;
    if (pathName.endsWith("/") || pathName.endsWith("\\"))
        pathName = pathName.substr(0, pathName.length() - 1);
}

/// removes path delimiter from end of path, if present
void LVRemoveLastPathDelimiter( lString8 & pathName )
{
    if (pathName.empty() || (pathName.length() == 1 && pathName[0] == ASSET_PATH_PREFIX))
        return;
    if (pathName.endsWith("/") || pathName.endsWith("\\"))
        pathName = pathName.substr(0, pathName.length() - 1);
}

/// replaces any found / or \\ separator with specified one
void LVReplacePathSeparator( lString32 & pathName, lChar32 separator )
{
    lChar32 * buf = pathName.modify();
    for ( ; *buf; buf++ )
        if ( *buf=='/' || *buf=='\\' )
            *buf = separator;
}

// resolve relative links
lString32 LVCombinePaths( lString32 basePath, lString32 newPath )
{
    if ( newPath[0]=='/' || newPath[0]=='\\' || (newPath.length()>0 && newPath[1]==':' && newPath[2]=='\\') )
        return newPath; // absolute path
    lChar32 separator = 0;
    if (!basePath.empty())
        LVAppendPathDelimiter(basePath);
    for ( int i=0; i<basePath.length(); i++ ) {
        if ( basePath[i]=='/' || basePath[i]=='\\' ) {
            separator = basePath[i];
            break;
        }
    }
    if ( separator == 0 )
        for ( int i=0; i<newPath.length(); i++ ) {
            if ( newPath[i]=='/' || newPath[i]=='\\' ) {
                separator = newPath[i];
                break;
            }
        }
    if ( separator == 0 )
        separator = '/';
    lString32 s = basePath;
    LVAppendPathDelimiter( s );
    s += newPath;
    //LVAppendPathDelimiter( s );
    LVReplacePathSeparator( s, separator );
    lString32 pattern;
    pattern << separator << ".." << separator;
    bool changed;
    do {
        changed = false;
        int lastElementStart = 0;
        for ( int i=0; i<(int)(s.length()-pattern.length()); i++ ) {
            if ( s[i]==separator && s[i+1]!='.' )
                lastElementStart = i + 1;
            else if ( s[i]==separator && s[i+1]=='.' && s[i+2]=='.' && s[i+3]==separator ) {
                if ( lastElementStart>=0 ) {
                    // /a/b/../c/
                    // 0123456789
                    //   ^ ^
                    s.erase( lastElementStart, i+4-lastElementStart );
                    changed = true;
                    // lastElementStart = -1;
                    break;
                }
            }
        }
    } while ( changed && s.length()>=pattern.length() );
    // Replace "/./" inside with "/"
    pattern.clear();
    pattern << separator << "." << separator;
    lString32 replacement;
    replacement << separator;
    while ( s.replace( pattern, replacement ) ) ;
    // Remove "./" at start
    if ( s.length()>2 && s[0]=='.' && s[1]==separator )
        s.erase(0, 2);
    return s;
}


/// removes last path part from pathname and returns it
lString32 LVExtractLastPathElement( lString32 & pathName )
{
    int l = pathName.length();
    if ( l==0 )
        return lString32::empty_str;
    if ( pathName[l-1]=='/' || pathName[l-1]=='\\' )
        pathName.erase(l-1, 1);
    int last_delim_pos = -1;
    for ( int i=0; i<pathName.length(); i++ )
        if ( pathName[i]=='/' || pathName[i]=='\\' )
            last_delim_pos = i;
    if ( last_delim_pos==-1 ) {
        lString32 res = pathName;
        pathName.clear();
        return res;
    }
    lString32 res = pathName.substr( last_delim_pos + 1, pathName.length()-last_delim_pos-1 );
    pathName.erase( last_delim_pos, pathName.length()-last_delim_pos );
    return res;
}

/// returns path delimiter character
lChar32 LVDetectPathDelimiter( lString32 pathName )
{
    for ( int i=0; i<pathName.length(); i++ )
        if ( pathName[i]=='/' || pathName[i]=='\\' )
            return pathName[i];
#ifdef _LINUX
        return '/';
#else
        return '\\';
#endif
}

/// returns path delimiter character
char LVDetectPathDelimiter( lString8 pathName ) {
    for ( int i=0; i<pathName.length(); i++ )
        if ( pathName[i]=='/' || pathName[i]=='\\' )
            return pathName[i];
#ifdef _LINUX
        return '/';
#else
        return '\\';
#endif
}

/// returns full path to file identified by pathName, with base directory == basePath
lString32 LVMakeRelativeFilename( lString32 basePath, lString32 pathName )
{
    if ( LVIsAbsolutePath( pathName ) )
        return pathName;
    lChar32 delim = LVDetectPathDelimiter( basePath );
    lString32 path = LVExtractPath( basePath );
    lString32 name = LVExtractFilename( pathName );
    lString32 dstpath = LVExtractPath( pathName );
    while ( !dstpath.empty() ) {
        lString32 element = LVExtractFirstPathElement( dstpath );
        if (element == ".") {
            // do nothing
        } else if (element == "..")
            LVExtractLastPathElement( path );
        else
            path << element << delim;
    }
    LVAppendPathDelimiter( path );
    path << name;
    return path;
}

/// removes path delimiter character from end of path, if exists
void LVRemovePathDelimiter( lString32 & pathName )
{
    int len = pathName.length();
    if ( len>0 && pathName != "/" && pathName != "\\" && !pathName.endsWith(":\\") && !pathName.endsWith("\\\\")) {
        if ( pathName.lastChar() == '/' || pathName.lastChar() == '\\' )
            pathName.erase( pathName.length()-1, 1 );
    }
}

/// removes path delimiter character from end of path, if exists
void LVRemovePathDelimiter( lString8 & pathName )
{
    int len = pathName.length();
    if ( len>0 && pathName != "/" && pathName != "\\" && !pathName.endsWith(":\\") && !pathName.endsWith("\\\\")) {
        if ( pathName.lastChar() == '/' || pathName.lastChar() == '\\' )
            pathName.erase( pathName.length()-1, 1 );
    }
}

/// returns true if specified file exists
bool LVFileExists( const lString8 & pathName ) {
    return LVFileExists(Utf8ToUnicode(pathName));
}

/// returns true if specified file exists
bool LVFileExists( const lString32 & pathName )
{
    lString32 fn(pathName);
    if (fn.length() > 1 && fn[0] == ASSET_PATH_PREFIX) {
    	if (!_assetContainerFactory)
    		return false;
    	lString32 assetPath = LVExtractAssetPath(fn);
    	return !_assetContainerFactory->openAssetStream(assetPath).isNull();
    }
#ifdef _WIN32
	LVStreamRef stream = LVOpenFileStream( pathName.c_str(), LVOM_READ );
	return !stream.isNull();
#else
    FILE * f = fopen(UnicodeToUtf8(pathName).c_str(), "rb" STDIO_CLOEXEC);
    if ( f ) {
        fclose( f );
        return true;
    }
    return false;
#endif
}

/// returns true if directory exists and your app can write to directory
bool LVDirectoryIsWritable(const lString32 & pathName) {
    lString32 fn = pathName;
    LVAppendPathDelimiter(fn);
    fn << ".cr3_directory_write_test";
    bool res = false;
    bool created = false;
    {
        LVStreamRef stream = LVOpenFileStream(fn.c_str(), LVOM_WRITE);
        if (!stream.isNull()) {
            created = true;
            lvsize_t bytesWritten = 0;
            if (stream->Write("TEST", 4, &bytesWritten) == LVERR_OK && bytesWritten == 4) {
                res = true;
            }
        }
    }
    if (created)
        LVDeleteFile(fn);
    return res;
}

/// returns true if specified directory exists
bool LVDirectoryExists( const lString32 & pathName )
{
    lString32 fn(pathName);
    if (fn.length() > 1 && fn[0] == ASSET_PATH_PREFIX) {
    	if (!_assetContainerFactory)
    		return false;
    	lString32 assetPath = LVExtractAssetPath(fn);
    	return !_assetContainerFactory->openAssetContainer(assetPath).isNull();
    }
    LVContainerRef dir = LVOpenDirectory( pathName.c_str() );
    return !dir.isNull();
}

/// returns true if specified directory exists
bool LVDirectoryExists( const lString8 & pathName )
{
    lString32 fn(Utf8ToUnicode(pathName));
    if (fn.length() > 1 && fn[0] == ASSET_PATH_PREFIX) {
    	if (!_assetContainerFactory)
    		return false;
    	lString32 assetPath = LVExtractAssetPath(fn);
    	return !_assetContainerFactory->openAssetContainer(assetPath).isNull();
    }
    LVContainerRef dir = LVOpenDirectory(fn);
    return !dir.isNull();
}

/// Create directory if not exist
bool LVCreateDirectory( lString32 path )
{
    CRLog::trace("LVCreateDirectory(%s)", UnicodeToUtf8(path).c_str() );
    //LVRemovePathDelimiter(path);
    if ( path.length() <= 1 )
        return false;
    if (path[0] == ASSET_PATH_PREFIX) {
    	// cannot create directory in asset
    	return false;
    }
    LVContainerRef dir = LVOpenDirectory( path.c_str() );
    if ( dir.isNull() ) {
        CRLog::trace("Directory %s not found", UnicodeToUtf8(path).c_str());
        LVRemovePathDelimiter(path);
        lString32 basedir = LVExtractPath( path );
        CRLog::trace("Checking base directory %s", UnicodeToUtf8(basedir).c_str());
        if ( !LVCreateDirectory( basedir ) ) {
            CRLog::error("Failed to create directory %s", UnicodeToUtf8(basedir).c_str());
            return false;
        }
#ifdef _WIN32
        return CreateDirectoryW( path.c_str(), NULL )!=0;
#else
        //LVRemovePathDelimiter( path );
        lString8 path8 = UnicodeToUtf8( path );
        CRLog::trace("Creating directory %s", path8.c_str() );
        if ( mkdir(path8.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) ) {
            CRLog::error("Cannot create directory %s", path8.c_str() );
            return false;
        }
        return true;
#endif
    }
    CRLog::trace("Directory %s exists", UnicodeToUtf8(path).c_str());
    return true;
}

/// Open memory mapped file
/**
    \param pathname is file name to open (unicode)
    \param mode is mode file should be opened in (LVOM_READ or LVOM_APPEND only)
        \param minSize is minimum file size for R/W mode
    \return reference to opened stream if success, NULL if error
*/
LVStreamRef LVMapFileStream( const lChar32 * pathname, lvopen_mode_t mode, lvsize_t minSize )
{
#if !defined(_WIN32) && !defined(_LINUX)
        // STUB for systems w/o mmap
    LVFileStream * stream = LVFileStream::CreateFileStream( pathname, mode );
    if ( stream!=NULL )
    {
        return LVStreamRef( stream );
    }
    return LVStreamRef();
#else
        LVFileMappedStream * stream = LVFileMappedStream::CreateFileStream( lString32(pathname), mode, (int)minSize );
        return LVStreamRef(stream);
#endif
}

/// delete file, return true if file found and successfully deleted
bool LVDeleteFile( lString32 filename )
{
#ifdef _WIN32
    return DeleteFileW( filename.c_str() ) ? true : false;
#else
    if ( unlink( UnicodeToUtf8( filename ).c_str() ) )
        return false;
    return true;
#endif
}

/// rename file
bool LVRenameFile(lString32 oldname, lString32 newname) {
    return LVRenameFile(UnicodeToUtf8(oldname), UnicodeToUtf8(newname));
}

/// rename file
bool LVRenameFile(lString8 oldname, lString8 newname) {
#ifdef _WIN32
    CRLog::trace("Renaming %s to %s", oldname.c_str(), newname.c_str());
    bool res = MoveFileW(Utf8ToUnicode(oldname).c_str(), Utf8ToUnicode(newname).c_str()) != 0;
    if (!res) {
        CRLog::error("Renaming result: %s for renaming of %s to %s", res ? "success" : "failed", oldname.c_str(), newname.c_str());
        CRLog::error("Last Error: %d", GetLastError());
    }
    return res;
#else
    return !rename(oldname.c_str(), newname.c_str());
#endif
}

/// delete file, return true if file found and successfully deleted
bool LVDeleteFile( lString8 filename ) {
    return LVDeleteFile(Utf8ToUnicode(filename));
}

/// delete directory, return true if directory is found and successfully deleted
bool LVDeleteDirectory( lString32 filename ) {
#ifdef _WIN32
    return RemoveDirectoryW( filename.c_str() ) ? true : false;
#else
    if ( rmdir( UnicodeToUtf8( filename ).c_str() ) )
        return false;
    return true;
#endif
}

/// delete directory, return true if directory is found and successfully deleted
bool LVDeleteDirectory( lString8 filename ) {
    return LVDeleteDirectory(Utf8ToUnicode(filename));
}

#define TRACE_BLOCK_WRITE_STREAM 0

class LVBlockWriteStream : public LVNamedStream
{
    LVStreamRef _baseStream;
    int _blockSize;
    int _blockCount;
    lvpos_t _pos;
    lvpos_t _size;


    struct Block
    {
        lvpos_t block_start;
        lvpos_t block_end;
        lvpos_t modified_start;
        lvpos_t modified_end;
        lUInt8 * buf;
        int size;
        Block * next;

        Block( lvpos_t start, lvpos_t end, int block_size )
            : block_start( start/block_size*block_size ), block_end( end )
            , modified_start((lvpos_t)-1), modified_end((lvpos_t)-1)
            , size( block_size ), next(NULL)
        {
            buf = (lUInt8*)calloc(size, sizeof(*buf));
            if ( buf ) {
    //            modified_start = 0;
    //            modified_end = size;
            }
            else {
                CRLog::error("buffer allocation failed");
            }
        }
        ~Block()
        {
            free(buf);
        }

        void save( const lUInt8 * ptr, lvpos_t pos, lvsize_t len )
        {
#if TRACE_BLOCK_WRITE_STREAM
            CRLog::trace("block %x save %x, %x", (int)block_start, (int)pos, (int)len);
#endif
            int offset = (int)(pos - block_start);
            if (offset > size || offset < 0 || (int)len > size || offset + (int)len > size) {
                CRLog::error("Unaligned access to block %x", (int)block_start);
            }
            for (unsigned i = 0; i < len; i++ ) {
                lUInt8 ch1 = buf[offset+i];
                if ( pos+i>block_end || ch1!=ptr[i] ) {
                    buf[offset+i] = ptr[i];
                    if ( modified_start==(lvpos_t)-1 ) {
                        modified_start = pos + i;
                        modified_end = modified_start + 1;
                    } else {
                        if ( modified_start>pos+i )
                            modified_start = pos+i;
                        if ( modified_end<pos+i+1)
                            modified_end = pos+i+1;
                        if ( block_end<pos+i+1)
                            block_end = pos+i+1;
                    }
                }
            }

        }

        bool containsPos( lvpos_t pos )
        {
            return pos>=block_start && pos<block_start+size;
        }
    };

    // list of blocks
    Block * _firstBlock;
    int _count;

    /// set write bytes limit to call flush(true) automatically after writing of each sz bytes
    void setAutoSyncSize(lvsize_t sz) {
        _baseStream->setAutoSyncSize(sz);
        handleAutoSync(0);
    }


    /// fills block with data existing in file
    lverror_t readBlock( Block * block )
    {
        if ( !block->size ) {
            CRLog::error("Invalid block size");
        }
        lvpos_t start = block->block_start;
        lvpos_t end = start + _blockSize;
        lvpos_t ssize = 0;
        lverror_t res = LVERR_OK;
        res = _baseStream->GetSize( &ssize);
        if ( res!=LVERR_OK )
            return res;
        if ( end>ssize )
            end = ssize;
        if ( end<=start )
            return LVERR_OK;
        _baseStream->SetPos( start );
        lvsize_t bytesRead = 0;
        block->block_end = end;
#if TRACE_BLOCK_WRITE_STREAM
        CRLog::trace("block %x filling from stream %x, %x", (int)block->block_start, (int)block->block_start, (int)(block->block_end-block->block_start));
#endif
        res = _baseStream->Read( block->buf, end-start, &bytesRead );
        if ( res!=LVERR_OK )
            CRLog::error("Error while reading block %x from file of size %x", block->block_start, ssize);
        return res;
    }

    lverror_t writeBlock( Block * block )
    {
        if ( block->modified_start < block->modified_end ) {
#if TRACE_BLOCK_WRITE_STREAM
            CRLog::trace("WRITE BLOCK %x (%x, %x)", (int)block->block_start, (int)block->modified_start, (int)(block->modified_end-block->modified_start));
#endif
            _baseStream->SetPos( block->modified_start );
            if (block->modified_end > _size) {
                block->modified_end = block->block_end;
            }
            lvpos_t bytesWritten = 0;
            lverror_t res = _baseStream->Write( block->buf + (block->modified_start-block->block_start), block->modified_end-block->modified_start, &bytesWritten );
            if ( res==LVERR_OK ) {
                if (_size < block->modified_end)
                    _size = block->modified_end;
            }
            block->modified_end = block->modified_start = (lvpos_t)-1;
            return res;
        } else
            return LVERR_OK;
    }

    Block * newBlock( lvpos_t start, int len )
    {
        Block * b = new Block( start, start+len, _blockSize );
        return b;
    }

    /// find block, move to top if found
    Block * findBlock( lvpos_t pos )
    {
        for ( Block ** p = &_firstBlock; *p; p=&(*p)->next ) {
            Block * item = *p;
            if ( item->containsPos(pos) ) {
                if ( item!=_firstBlock ) {
#if TRACE_BLOCK_WRITE_STREAM
                    dumpBlocks("before reorder");
#endif
                    *p = item->next;
                    item->next = _firstBlock;
                    _firstBlock = item;
#if TRACE_BLOCK_WRITE_STREAM
                    dumpBlocks("after reorder");
                    CRLog::trace("found block %x (%x, %x)", (int)item->block_start, (int)item->modified_start, (int)(item->modified_end-item->modified_start));
#endif
                }
                return item;
            }
        }
        return NULL;
    }

    // try read block-aligned fragment from cache
    bool readFromCache( void * buf, lvpos_t pos, lvsize_t count )
    {
        Block * p = findBlock( pos );
        if ( p ) {
#if TRACE_BLOCK_WRITE_STREAM
            CRLog::trace("read from cache block %x (%x, %x)", (int)p->block_start, (int)pos, (int)(count));
#endif
            memcpy( buf, p->buf + (pos-p->block_start), count );
            return true;
        }
        return false;
    }

    // write block-aligned fragment to cache
    lverror_t writeToCache( const void * buf, lvpos_t pos, lvsize_t count )
    {
        Block * p = findBlock( pos );
        if ( p ) {
#if TRACE_BLOCK_WRITE_STREAM
            CRLog::trace("saving data to existing block %x (%x, %x)", (int)p->block_start, (int)pos, (int)count);
#endif
            p->save( (const lUInt8 *)buf, pos, count );
            if ( pos + count > _size )
                _size = pos + count;
            return LVERR_OK;
        }
#if TRACE_BLOCK_WRITE_STREAM
        CRLog::trace("Block %x not found in cache", pos);
#endif
        if ( _count>=_blockCount-1 ) {
            // remove last
            for ( Block * p = _firstBlock; p; p=p->next ) {
                if ( p->next && !p->next->next ) {
#if TRACE_BLOCK_WRITE_STREAM
                    dumpBlocks("before remove last");
                    CRLog::trace("dropping block %x (%x, %x)", (int)p->next->block_start, (int)p->next->modified_start, (int)(p->next->modified_end-p->next->modified_start));
#endif
                    writeBlock( p->next );
                    delete p->next;
                    _count--;
                    p->next = NULL;
#if TRACE_BLOCK_WRITE_STREAM
                    dumpBlocks("after remove last");
#endif
                }
            }
        }
        p = newBlock( pos, count );
#if TRACE_BLOCK_WRITE_STREAM
        CRLog::trace("creating block %x", (int)p->block_start);
#endif
        if ( readBlock( p )!=LVERR_OK ) {
            delete p;
            return LVERR_FAIL;
        }
#if TRACE_BLOCK_WRITE_STREAM
        CRLog::trace("saving data to new block %x (%x, %x)", (int)p->block_start, (int)pos, (int)count);
#endif
        p->save( (const lUInt8 *)buf, pos, count );
        p->next = _firstBlock;
        _firstBlock = p;
        _count++;
        if ( pos + count > _size ) {
            _size = pos + count;
            p->modified_start = p->block_start;
            p->modified_end = p->block_end;
        }
        return LVERR_OK;
    }


public:
    virtual lverror_t Flush( bool sync ) {
        CRTimerUtil infinite;
        return Flush(sync, infinite); // NOLINT: Call to virtual function during destruction
    }
    /// flushes unsaved data from buffers to file, with optional flush of OS buffers
    virtual lverror_t Flush( bool sync, CRTimerUtil & timeout )
    {
#if TRACE_BLOCK_WRITE_STREAM
        CRLog::trace("flushing unsaved blocks");
#endif
        lverror_t res = LVERR_OK;
        for ( Block * p = _firstBlock; p; ) {
            Block * tmp = p;
            if ( writeBlock(p)!=LVERR_OK )
                res = LVERR_FAIL;
            p = p->next;
            delete tmp;
            if (!sync && timeout.expired()) {
                //CRLog::trace("LVBlockWriteStream::flush - timeout expired");
                _firstBlock = p;
                return LVERR_OK;
            }

        }
        _firstBlock = NULL;
        _baseStream->Flush( sync );
        return res;
    }


    virtual ~LVBlockWriteStream()
    {
        Flush( true ); // NOLINT: Call to virtual function during destruction
    }

    virtual const lChar32 * GetName()
            { return _baseStream->GetName(); }
    virtual lvopen_mode_t GetMode()
            { return _baseStream->GetMode(); }

    LVBlockWriteStream( LVStreamRef baseStream, int blockSize, int blockCount )
    : _baseStream( baseStream ), _blockSize( blockSize ), _blockCount( blockCount ), _firstBlock(NULL), _count(0)
    {
        _pos = _baseStream->GetPos();
        _size = _baseStream->GetSize();
    }

    virtual lvpos_t GetSize()
    {
        return _size;
    }

    virtual lverror_t Seek( lvoffset_t offset, lvseek_origin_t origin, lvpos_t * pNewPos )
    {
        if ( origin==LVSEEK_CUR ) {
            origin = LVSEEK_SET;
            offset = _pos + offset;
        } else if ( origin==LVSEEK_END ) {
            origin = LVSEEK_SET;
            offset = _size + offset;
        }

        lvpos_t newpos = 0;
        lverror_t res = _baseStream->Seek(offset, origin, &newpos);
        if ( res==LVERR_OK ) {
            if ( pNewPos )
                *pNewPos = newpos;
            _pos = newpos;
        } else {
            CRLog::error("baseStream->Seek(%d,%x) failed: %d", (int)origin, (int)offset, (int)res);
        }
        return res;
    }

    virtual lverror_t Tell( lvpos_t * pPos )
    {
        *pPos = _pos;
        return LVERR_OK;
    }
    //virtual lverror_t   SetPos(lvpos_t p)
    virtual lvpos_t   SetPos(lvpos_t p)
    {
        lvpos_t res = _baseStream->SetPos(p);
        _pos = _baseStream->GetPos();
//                if ( _size<_pos )
//                    _size = _pos;
        return res;
    }
    virtual lvpos_t   GetPos()
    {
        return _pos;
    }
    virtual lverror_t SetSize( lvsize_t size )
    {
        // TODO:
        lverror_t res = _baseStream->SetSize(size);
        if ( res==LVERR_OK )
            _size = size;
        return res;
    }

    void dumpBlocks( const char * context)
    {
        lString8 buf;
        for ( Block * p = _firstBlock; p; p = p->next ) {
            char s[1000];
            snprintf(s, 999, "%x ", (int)p->block_start);
            s[999] = 0;
            buf << s;
        }
        CRLog::trace("BLOCKS (%s): %s   count=%d", context, buf.c_str(), _count);
    }

    virtual lverror_t Read( void * buf, lvsize_t count, lvsize_t * nBytesRead )
    {
#if TRACE_BLOCK_WRITE_STREAM
        CRLog::trace("stream::Read(%x, %x)", (int)_pos, (int)count);
        dumpBlocks("before read");
#endif
        // slice by block bounds
        lvsize_t bytesRead = 0;
        lverror_t res = LVERR_OK;
		if ( _pos > _size ) {
			if ( nBytesRead )
				*nBytesRead = bytesRead;
			return LVERR_FAIL;
		}
        if ( _pos + count > _size )
            count = (int)(_size - _pos);
        while ( (int)count>0 && res==LVERR_OK ) {
            lvpos_t blockSpaceLeft = _blockSize - (_pos % _blockSize);
            if ( blockSpaceLeft > count )
                blockSpaceLeft = count;
            lvsize_t blockBytesRead = 0;

            // read from Write buffers if possible, otherwise - from base stream
            if ( readFromCache( buf, _pos, blockSpaceLeft ) ) {
                blockBytesRead = blockSpaceLeft;
                res = LVERR_OK;
            } else {
                lvpos_t fsize = _baseStream->GetSize();
                if ( _pos + blockSpaceLeft > fsize && fsize < _size) {
#if TRACE_BLOCK_WRITE_STREAM
                    CRLog::trace("stream::Read: inconsistent cache state detected: fsize=%d, _size=%d, force flush...", (int)fsize, (int)_size);
#endif
                    // Workaround to exclude fatal error in ldomTextStorageChunk::ensureUnpacked()
                    // Write cached data to a file stream if the required read block is larger than the rest of the file.
                    // This is a very rare case.
                    Flush(true);
                }
#if TRACE_BLOCK_WRITE_STREAM
                CRLog::trace("direct reading from stream (%x, %x)", (int)_pos, (int)blockSpaceLeft);
#endif
                _baseStream->SetPos(_pos);
                res = _baseStream->Read(buf, blockSpaceLeft, &blockBytesRead);
            }
            if ( res!=LVERR_OK )
                break;

            count -= blockBytesRead;
            buf = ((char*)buf) + blockBytesRead;
            _pos += blockBytesRead;
            bytesRead += blockBytesRead;
            if ( !blockBytesRead )
                break;
        }
        if ( nBytesRead && res==LVERR_OK )
            *nBytesRead = bytesRead;
        return res;
    }

    virtual lverror_t Write( const void * buf, lvsize_t count, lvsize_t * nBytesWritten )
    {
#if TRACE_BLOCK_WRITE_STREAM
        CRLog::trace("stream::Write(%x, %x)", (int)_pos, (int)count);
        dumpBlocks("before write");
#endif
        // slice by block bounds
        lvsize_t bytesRead = 0;
        lverror_t res = LVERR_OK;
        //if ( _pos + count > _size )
        //    count = _size - _pos;
        while ( count>0 && res==LVERR_OK ) {
            lvpos_t blockSpaceLeft = _blockSize - (_pos % _blockSize);
            if ( blockSpaceLeft > count )
                blockSpaceLeft = count;
            lvsize_t blockBytesWritten = 0;

            // write to Write buffers
            res = writeToCache(buf, _pos, blockSpaceLeft);
            if ( res!=LVERR_OK )
                break;

            blockBytesWritten = blockSpaceLeft;

            count -= blockBytesWritten;
            buf = ((char*)buf) + blockBytesWritten;
            _pos += blockBytesWritten;
            bytesRead += blockBytesWritten;
            if ( _pos>_size )
                _size = _pos;
            if ( !blockBytesWritten )
                break;
        }
        if ( nBytesWritten && res==LVERR_OK )
            *nBytesWritten = bytesRead;
#if TRACE_BLOCK_WRITE_STREAM
        dumpBlocks("after write");
#endif
        return res;
    }
    virtual bool Eof()
    {
        return _pos >= _size;
    }
};

LVStreamRef LVCreateBlockWriteStream( LVStreamRef baseStream, int blockSize, int blockCount )
{
    if ( baseStream.isNull() || baseStream->GetMode()==LVOM_READ )
        return baseStream;
    return LVStreamRef( new LVBlockWriteStream(baseStream, blockSize, blockCount) );
}




