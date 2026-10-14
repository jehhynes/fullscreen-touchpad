#include "DebugLog.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <filesystem>

std::wstring DebugLog::FilePath()
{
    PWSTR localAppData = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
        path = std::wstring(localAppData) + L"\\VirtualTouchpad\\debug.log";
    CoTaskMemFree(localAppData);
    return path;
}

void DebugLog::Open()
{
    if (file_) return;
    const std::filesystem::path path(FilePath());
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    file_ = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
}

void DebugLog::Close()
{
    if (!file_) return;
    fclose(file_);
    file_ = nullptr;
}

void DebugLog::Write(const char* format, ...)
{
    if (!file_) return;
    fprintf(file_, "%10llu ", static_cast<unsigned long long>(GetTickCount64()));
    va_list args;
    va_start(args, format);
    vfprintf(file_, format, args);
    va_end(args);
    fputc('\n', file_);
    fflush(file_);
}
