#include "Settings.h"

#include <windows.h>
#include <shlobj.h>

#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace
{
    struct NumberField { const wchar_t* name; double Settings::* member; };
    struct BoolField { const wchar_t* name; bool Settings::* member; };

#define NUM(f) NumberField{ L## #f, &Settings::f }
#define BOOL_(f) BoolField{ L## #f, &Settings::f }

    const NumberField kNumbers[] = {
        NUM(Sensitivity), NUM(Acceleration), NUM(MaxAccelSpeed),
        NUM(TapMaxMs), NUM(MultiTapMaxMs), NUM(TapSlop), NUM(DoubleTapWindowMs), NUM(LongPressMs),
        NUM(ScrollSpeed), NUM(KineticFriction), NUM(ZoomSpeed),
        NUM(SwipeThreshold), NUM(AltTabStep), NUM(BorderThickness),
    };

    const BoolField kBools[] = {
        BOOL_(TapToClick), BOOL_(LongPressRightClick), BOOL_(ShowKeyboardForTextFields), BOOL_(TypeDirectlyOnKeyboard),
        BOOL_(NaturalScrolling), BOOL_(KineticScrolling), BOOL_(PinchZoom),
        BOOL_(StartEnabled), BOOL_(ShowBorder), BOOL_(ShowOsd), BOOL_(DebugLog),
    };

#undef NUM
#undef BOOL_

    std::wstring Trim(const std::wstring& s)
    {
        size_t b = 0, e = s.size();
        while (b < e && std::iswspace(s[b])) ++b;
        while (e > b && std::iswspace(s[e - 1])) --e;
        return s.substr(b, e - b);
    }
}

std::wstring Settings::FilePath()
{
    PWSTR appData = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
        path = std::wstring(appData) + L"\\VirtualTouchpad\\settings.ini";
    CoTaskMemFree(appData);
    return path;
}

Settings Settings::Load()
{
    Settings s;
    std::wifstream file{ std::filesystem::path(FilePath()) };
    if (!file)
    {
        s.Save();
        return s;
    }

    std::wstring line;
    while (std::getline(file, line))
    {
        line = Trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        const std::wstring key = Trim(line.substr(0, eq));
        const std::wstring value = Trim(line.substr(eq + 1));

        for (const auto& f : kNumbers)
        {
            if (_wcsicmp(f.name, key.c_str()) != 0) continue;
            wchar_t* end = nullptr;
            const double v = wcstod(value.c_str(), &end);
            if (end != value.c_str()) s.*f.member = v;
        }
        for (const auto& f : kBools)
        {
            if (_wcsicmp(f.name, key.c_str()) != 0) continue;
            s.*f.member = _wcsicmp(value.c_str(), L"true") == 0 || value == L"1";
        }
    }
    file.close();

    // Rewrite so settings added in newer versions show up in the file.
    s.Save();
    return s;
}

void Settings::Save() const
{
    const std::filesystem::path path(FilePath());
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::wostringstream out;
    out << L"# Virtual Touchpad settings. Distances are pixels at 100% display scaling.\n"
        << L"# Edit, then choose \"Reload settings\" from the tray menu.\n";
    for (const auto& f : kNumbers) out << f.name << L'=' << this->*f.member << L'\n';
    for (const auto& f : kBools) out << f.name << L'=' << (this->*f.member ? L"true" : L"false") << L'\n';

    std::wofstream file(path, std::ios::trunc);
    file << out.str();
}
