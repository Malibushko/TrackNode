#ifndef FATFS_CPP_HPP
#define FATFS_CPP_HPP

#include "ff.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fatfs {

inline const char* to_string(FRESULT r) noexcept
{
    switch (r) {
        case FR_OK:                  return "OK";
        case FR_DISK_ERR:            return "DISK_ERR";
        case FR_INT_ERR:             return "INT_ERR";
        case FR_NOT_READY:           return "NOT_READY";
        case FR_NO_FILE:             return "NO_FILE";
        case FR_NO_PATH:             return "NO_PATH";
        case FR_INVALID_NAME:        return "INVALID_NAME";
        case FR_DENIED:              return "DENIED";
        case FR_EXIST:               return "EXIST";
        case FR_INVALID_OBJECT:      return "INVALID_OBJECT";
        case FR_WRITE_PROTECTED:     return "WRITE_PROTECTED";
        case FR_INVALID_DRIVE:       return "INVALID_DRIVE";
        case FR_NOT_ENABLED:         return "NOT_ENABLED";
        case FR_NO_FILESYSTEM:       return "NO_FILESYSTEM";
        case FR_MKFS_ABORTED:        return "MKFS_ABORTED";
        case FR_TIMEOUT:             return "TIMEOUT";
        case FR_LOCKED:              return "LOCKED";
        case FR_NOT_ENOUGH_CORE:     return "NOT_ENOUGH_CORE";
        case FR_TOO_MANY_OPEN_FILES: return "TOO_MANY_OPEN_FILES";
        case FR_INVALID_PARAMETER:   return "INVALID_PARAMETER";
        default:                     return "UNKNOWN";
    }
}

#if FF_USE_MKFS && !FF_FS_READONLY
struct MkfsOptions {
    BYTE  format       = FM_ANY;
    bool  sfd          = false;
    BYTE  n_fat        = 0;
    UINT  align        = 0;
    UINT  n_root       = 0;
    DWORD cluster_size = 0;
};

namespace detail {

inline BYTE mkfs_format(const MkfsOptions& o) noexcept
{
    return static_cast<BYTE>(o.format | (o.sfd ? FM_SFD : 0));
}

template <typename Param>
FRESULT mkfs_impl(FRESULT (*fn)(const TCHAR*, const Param*, void*, UINT),
                  const TCHAR* path, const MkfsOptions& o, void* work, UINT len) noexcept
{
    Param p = {};
    p.fmt     = mkfs_format(o);
    p.n_fat   = o.n_fat;
    p.align   = o.align;
    p.n_root  = o.n_root;
    p.au_size = o.cluster_size;
    return fn(path, &p, work, len);
}

inline FRESULT mkfs_impl(FRESULT (*fn)(const TCHAR*, BYTE, DWORD, void*, UINT),
                         const TCHAR* path, const MkfsOptions& o, void* work, UINT len) noexcept
{
    return fn(path, mkfs_format(o), o.cluster_size, work, len);
}

}
#endif

class Volume {
public:
    explicit Volume(const TCHAR* path = _T("")) noexcept : path_(path) {}

    template <typename Card>
    explicit Volume(Card& card, const TCHAR* path = _T("")) noexcept
        : path_(path),
          card_(&card),
          bind_(&bind_thunk<Card>),
          unbind_(&unbind_thunk<Card>)
    {
    }

    ~Volume() { unmount(); }

    Volume(const Volume&)            = delete;
    Volume& operator=(const Volume&) = delete;

    FRESULT mount(bool force = true) noexcept
    {
        ensure_bound();
        last_ = f_mount(&fs_, path_, force ? 1 : 0);
        if (last_ != FR_INVALID_DRIVE)
        {
	        registered_ = true;
        }

    	mounted_ = last_ == FR_OK;

        return last_;
    }

    FRESULT unmount() noexcept
    {
        FRESULT r = FR_OK;
        if (registered_)
        {
            registered_ = false;
            r = last_ = f_mount(nullptr, path_, 0);
        }

    	mounted_ = false;

        if (bound_)
        {
            unbind_(card_);
            bound_ = false;
        }
        return r;
    }

#if FF_USE_MKFS && !FF_FS_READONLY
    FRESULT mkfs(const MkfsOptions& opts, void* work, UINT len) noexcept
    {
        ensure_bound();
        return last_ = detail::mkfs_impl(&f_mkfs, path_, opts, work, len);
    }

    template <typename T, std::size_t N>
    FRESULT mkfs(const MkfsOptions& opts, T (&work)[N]) noexcept
    {
        return mkfs(opts, static_cast<void*>(work), static_cast<UINT>(sizeof(T) * N));
    }
#endif

    bool          registered() const noexcept { return registered_; }
    bool          mounted() const noexcept { return mounted_; }
    bool          bound()      const noexcept { return bound_; }
    FRESULT       result()     const noexcept { return last_; }
    const TCHAR*  path()       const noexcept { return path_; }
    FATFS&        native()           noexcept { return fs_; }

#if !FF_FS_READONLY && FF_FS_MINIMIZE == 0
    FRESULT space(std::uint64_t& free_bytes, std::uint64_t* total_bytes = nullptr) noexcept
    {
        DWORD  nclst = 0;
        FATFS* pfs   = nullptr;
        last_ = f_getfree(path_, &nclst, &pfs);
        if (last_ != FR_OK) return last_;

#if FF_MAX_SS != FF_MIN_SS
        const std::uint64_t ss = pfs->ssize;
#else
        const std::uint64_t ss = FF_MAX_SS;
#endif
        const std::uint64_t cluster_bytes = ss * pfs->csize;
        free_bytes = static_cast<std::uint64_t>(nclst) * cluster_bytes;
        if (total_bytes)
        {
	        *total_bytes = static_cast<std::uint64_t>(pfs->n_fatent - 2) * cluster_bytes;
        }

        return FR_OK;
    }
#endif

#if FF_FS_MINIMIZE == 0
    static FRESULT stat(const TCHAR* path, FILINFO& info) noexcept { return f_stat(path, &info); }

    static bool exists(const TCHAR* path) noexcept { return f_stat(path, nullptr) == FR_OK; }
#endif

#if !FF_FS_READONLY && FF_FS_MINIMIZE == 0
    static FRESULT mkdir(const TCHAR* path) noexcept { return f_mkdir(path); }

    static FRESULT remove(const TCHAR* path) noexcept { return f_unlink(path); }

    static FRESULT rename(const TCHAR* from, const TCHAR* to) noexcept { return f_rename(from, to); }
#endif

private:
    void ensure_bound() noexcept
    {
        if (card_ && !bound_) {
            bind_(card_);
            bound_ = true;
        }
    }

    template <typename Card>
    static void bind_thunk(void* p) noexcept
    {
        bind_sd_card(*static_cast<Card*>(p));
    }

    template <typename Card>
    static void unbind_thunk(void* p) noexcept
    {
        unbind_sd_card(*static_cast<Card*>(p));
    }

    FATFS        fs_{};
    const TCHAR* path_;
    void*        card_   = nullptr;
    void       (*bind_)(void*)   = nullptr;
    void       (*unbind_)(void*) = nullptr;
    FRESULT      last_       = FR_OK;
    bool         registered_ = false;
    bool         mounted_    = false;
    bool         bound_      = false;
};

class File {
public:
    File() noexcept = default;

    File(const TCHAR* path, BYTE mode) noexcept { open(path, mode); }

    ~File() { close(); }

    File(const File&)            = delete;
    File& operator=(const File&) = delete;

    File(File&& o) = delete;
    File& operator=(File&& o) = delete;

    FRESULT open(const TCHAR* path, BYTE mode) noexcept
    {
        close();
        return last_ = f_open(&fil_, path, mode);
    }

    FRESULT close() noexcept
    {
        if (is_open())
            last_ = f_close(&fil_);
        return last_;
    }

    bool    is_open() const noexcept { return fil_.obj.fs != nullptr; }
    explicit operator bool() const noexcept { return is_open(); }

    FRESULT result() const noexcept { return last_; }

    FRESULT read(void* buf, UINT len, UINT& br) noexcept
    {
        return last_ = f_read(&fil_, buf, len, &br);
    }

#if FF_USE_STRFUNC
    bool read_line(TCHAR* buf, int len) noexcept
    {
        return f_gets(buf, len, &fil_) != nullptr;
    }
#endif

#if !FF_FS_READONLY
    FRESULT write(const void* data, UINT len, UINT* written = nullptr) noexcept
    {
        UINT bw = 0;
        last_ = f_write(&fil_, data, len, &bw);
        if (written)
            *written = bw;
        else if (last_ == FR_OK && bw != len)
            last_ = FR_DENIED;
        return last_;
    }

    FRESULT write_string(const char* str) noexcept
    {
        return write(str, static_cast<UINT>(std::strlen(str)));
    }

    FRESULT sync() noexcept { return last_ = f_sync(&fil_); }

#if FF_FS_MINIMIZE == 0
    FRESULT truncate() noexcept { return last_ = f_truncate(&fil_); }
#endif
#endif

#if FF_FS_MINIMIZE <= 2
    FRESULT seek(FSIZE_t pos) noexcept { return last_ = f_lseek(&fil_, pos); }
    FRESULT rewind()          noexcept { return seek(0); }
    FRESULT seek_end()        noexcept { return seek(size()); }
#endif

    FSIZE_t tell()  const noexcept { return f_tell(&fil_); }
    FSIZE_t size()  const noexcept { return f_size(&fil_); }
    bool    eof()   const noexcept { return f_eof(&fil_) != 0; }
    BYTE    error() const noexcept { return f_error(&fil_); }

    FIL& native() noexcept { return fil_; }

private:

    FIL     fil_{};
    FRESULT last_ = FR_OK;
};

#if FF_FS_MINIMIZE <= 1
class Dir {
public:
    Dir() noexcept = default;

    explicit Dir(const TCHAR* path) noexcept { open(path); }

    ~Dir() { close(); }

    Dir(const Dir&)            = delete;
    Dir& operator=(const Dir&) = delete;

    Dir(Dir&& o) noexcept = delete;
    Dir& operator=(Dir&& o) noexcept = delete;

    FRESULT open(const TCHAR* path) noexcept
    {
        close();
        return last_ = f_opendir(&dir_, path);
    }

    FRESULT close() noexcept
    {
        if (is_open()) last_ = f_closedir(&dir_);
        return last_;
    }

    bool    is_open() const noexcept { return dir_.obj.fs != nullptr; }
    explicit operator bool() const noexcept { return is_open(); }
    FRESULT result() const noexcept { return last_; }

    FRESULT next(FILINFO& info) noexcept { return last_ = f_readdir(&dir_, &info); }

    static bool is_end(const FILINFO& info) noexcept { return info.fname[0] == 0; }
    static bool is_dir(const FILINFO& info) noexcept { return (info.fattrib & AM_DIR) != 0; }

    FRESULT rewind() noexcept { return last_ = f_readdir(&dir_, nullptr); }

    template <typename Fn>
    FRESULT for_each(Fn&& fn)
    {
        FILINFO fno;
        FRESULT r;
        while ((r = next(fno)) == FR_OK && !is_end(fno))
            fn(static_cast<const FILINFO&>(fno));
        return r;
    }

    DIR& native() noexcept { return dir_; }

private:

    DIR     dir_{};
    FRESULT last_ = FR_OK;
};
#endif

}

#endif
