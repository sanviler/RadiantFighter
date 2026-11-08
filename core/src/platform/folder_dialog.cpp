#include "platform/folder_dialog.h"
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <vector>

namespace dm {

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.resize(size_t(n - 1));
    return w;
}
std::string narrow(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.resize(size_t(n - 1));
    return s;
}
} // namespace

bool folderDialogAvailable() { return true; }

bool chooseFolder(const std::string& title, const std::string& startFolder, std::string& chosen) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool ok = false;
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        const std::wstring wt = widen(title);
        dlg->SetTitle(wt.c_str());
        if (!startFolder.empty()) {
            IShellItem* start = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(widen(startFolder).c_str(), nullptr, IID_PPV_ARGS(&start)))) {
                dlg->SetFolder(start);
                start->Release();
            }
        }
        if (SUCCEEDED(dlg->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    chosen = narrow(path);
                    CoTaskMemFree(path);
                    ok = true;
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}

} // namespace dm

#else
#include <string>

namespace dm {

namespace {
// runs a command and returns its first line of output; false if it failed or printed nothing
bool runForLine(const std::string& cmd, std::string& line) {
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return false;
    char buf[4096];
    line.clear();
    if (std::fgets(buf, sizeof buf, p)) line = buf;
    const int rc = pclose(p);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    while (line.size() > 1 && line.back() == '/') line.pop_back();
    return rc == 0 && !line.empty();
}
std::string quote(const std::string& s) {                // single quotes for the shell
    std::string q = "'";
    for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return q + "'";
}
bool have(const char* tool) { return std::system((std::string("command -v ") + tool + " >/dev/null 2>&1").c_str()) == 0; }
} // namespace

bool folderDialogAvailable() {
#ifdef __APPLE__
    return true;
#else
    return have("zenity") || have("kdialog");
#endif
}

bool chooseFolder(const std::string& title, const std::string& startFolder, std::string& chosen) {
#ifdef __APPLE__
    std::string script = "POSIX path of (choose folder with prompt \"" + title + "\")";
    return runForLine("osascript -e " + quote(script) + " 2>/dev/null", chosen);
#else
    const std::string start = startFolder.empty() ? std::string(".") : startFolder;
    if (have("zenity"))
        return runForLine("zenity --file-selection --directory --title=" + quote(title) + " --filename=" + quote(start + "/") + " 2>/dev/null", chosen);
    if (have("kdialog"))
        return runForLine("kdialog --title " + quote(title) + " --getexistingdirectory " + quote(start) + " 2>/dev/null", chosen);
    return false;
#endif
}

} // namespace dm
#endif
