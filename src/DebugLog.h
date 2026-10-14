#pragma once

#include <cstdio>
#include <string>

// Optional diagnostic log at %LOCALAPPDATA%\VirtualTouchpad\debug.log, enabled with DebugLog=true in
// settings.ini. Off by default; meant for tracking down how Windows delivers touch to the app.
class DebugLog
{
public:
    ~DebugLog() { Close(); }

    void Open();
    void Close();
    bool On() const { return file_ != nullptr; }
    void Write(const char* format, ...);

    static std::wstring FilePath();

private:
    FILE* file_ = nullptr;
};
