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

    const NumberField kNumbers[] = {
        { L"ToggleFingers", &Settings::ToggleFingers }, { L"ToggleMaxMs", &Settings::ToggleMaxMs },
        { L"Opacity", &Settings::Opacity },             { L"EdgeGap", &Settings::EdgeGap },
        { L"PointerHole", &Settings::PointerHole },     { L"KeyboardCorner", &Settings::KeyboardCorner },
        { L"HoldKey", &Settings::HoldKey },
    };
    const BoolField kBools[] = {
        { L"StartEnabled", &Settings::StartEnabled },
        { L"ShowOsd", &Settings::ShowOsd },
        { L"ShowKeyboardForTextFields", &Settings::ShowKeyboardForTextFields },
        { L"CornerWhenOff", &Settings::CornerWhenOff },
        { L"LogTouch", &Settings::LogTouch },
    };

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
    {
        std::wifstream file{ std::filesystem::path(FilePath()) };
        std::wstring line;
        while (file && std::getline(file, line))
        {
            line = Trim(line);
            const size_t eq = line.find(L'=');
            if (line.empty() || line[0] == L'#' || eq == std::wstring::npos) continue;
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
                if (_wcsicmp(f.name, key.c_str()) == 0)
                    s.*f.member = _wcsicmp(value.c_str(), L"true") == 0 || value == L"1";
        }
    }
    // Keep hand-edited numbers sane (this also turns NaN into the lower bound).
    auto clamp = [](double& v, double lo, double hi) { v = (v >= lo) ? (v <= hi ? v : hi) : lo; };
    clamp(s.ToggleFingers, 2, 5);
    clamp(s.ToggleMaxMs, 100, 3000);
    clamp(s.Opacity, 1, 255);
    clamp(s.EdgeGap, 0, 50);
    clamp(s.PointerHole, 1, 50);
    clamp(s.KeyboardCorner, 0, 200);
    clamp(s.HoldKey, 0, 254);

    s.Save(); // writes defaults, and drops settings from older versions
    return s;
}

void Settings::Save() const
{
    const std::filesystem::path path(FilePath());
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::wostringstream out;
    out << L"# Virtual Touchpad settings. Restart the app after editing.\n";
    for (const auto& f : kBools) out << f.name << L'=' << (this->*f.member ? L"true" : L"false") << L'\n';
    for (const auto& f : kNumbers) out << f.name << L'=' << this->*f.member << L'\n';
    std::wofstream file(path, std::ios::trunc);
    file << out.str();
}
