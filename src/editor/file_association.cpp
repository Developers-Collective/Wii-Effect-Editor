#include "file_association.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#elif defined(__APPLE__)
#include <CoreServices/CoreServices.h>
#else
#include <SDL3/SDL.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
    namespace fs = std::filesystem;

#ifdef _WIN32
    void registerString(const std::wstring& path, const wchar_t* name, const std::wstring& value) {
        HKEY key;
        auto status =
            RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (status != ERROR_SUCCESS)
            throw std::runtime_error("Could not register the BREFF file type.");
        status = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                DWORD((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        if (status != ERROR_SUCCESS)
            throw std::runtime_error("Could not register the BREFF file type.");
    }

    void associate() {
        std::wstring executable(32768, L'\0');
        const auto length = GetModuleFileNameW(nullptr, executable.data(), DWORD(executable.size()));
        if (!length || length >= executable.size())
            throw std::runtime_error("Could not find the editor executable.");
        executable.resize(length);
        const std::wstring progId = L"Software\\Classes\\EffectEditor.BREFF";
        registerString(progId, nullptr, L"BREFF effect archive");
        registerString(progId + L"\\DefaultIcon", nullptr, L"\"" + executable + L"\",0");
        registerString(progId + L"\\shell\\open\\command", nullptr, L"\"" + executable + L"\" \"%1\"");
        registerString(L"Software\\Classes\\.breff\\OpenWithProgids", L"EffectEditor.BREFF", L"");
        registerString(L"Software\\Classes\\.breff", nullptr, L"EffectEditor.BREFF");
        const std::wstring capabilities = L"Software\\EffectEditor\\Capabilities";
        registerString(capabilities, L"ApplicationName", L"Effect Editor");
        registerString(capabilities, L"ApplicationDescription", L"Edit and preview Wii effect archives");
        registerString(capabilities + L"\\FileAssociations", L".breff", L"EffectEditor.BREFF");
        registerString(L"Software\\RegisteredApplications", L"Effect Editor", capabilities);
        // The user accepted our default handler. Remove the per-user override so
        // Windows uses the extension mapping above, including after a previous choice.
        const auto status = RegDeleteKeyW(
            HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.breff\\UserChoice");
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
            throw std::runtime_error("Windows could not update the default application for .breff files.");

        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr);
        std::wstring selected(32768, L'\0');
        DWORD size = DWORD(selected.size());
        if (AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L".breff", L"open", selected.data(), &size) != S_OK ||
            _wcsicmp(selected.c_str(), executable.c_str()) != 0)
            throw std::runtime_error("Windows did not apply the default application for .breff files.");
    }
#elif defined(__APPLE__)
    void associate() {
        auto* bundle = CFBundleGetMainBundle();
        if (!bundle)
            throw std::runtime_error("Launch the installed Effect Editor.app to register BREFF files.");
        auto* identifier = CFBundleGetIdentifier(bundle);
        auto* url = CFBundleCopyBundleURL(bundle);
        if (!url)
            throw std::runtime_error("Could not locate Effect Editor.app.");
        const auto registration = LSRegisterURL(url, true);
        CFRelease(url);
        if (!identifier || registration != noErr ||
            LSSetDefaultRoleHandlerForContentType(CFSTR("org.effect-editor.breff"), kLSRolesAll, identifier) != noErr)
            throw std::runtime_error(
                "Could not set the default application. In Finder, use Get Info > Open with > Effect Editor > Change All.");
    }
#else
    void run(const std::vector<std::string>& arguments) {
        std::vector<char*> argv;
        for (const auto& argument : arguments)
            argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        const auto child = fork();
        if (child == 0) {
            execvp(argv[0], argv.data());
            _exit(127);
        }
        int status = 0;
        if (child < 0 || waitpid(child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw std::runtime_error("Could not register the BREFF file type. Check that xdg-utils, shared-mime-info "
                                     "and desktop-file-utils are installed.");
    }

    std::string desktopArgument(const std::string& value) {
        std::string result = "\"";
        for (const char c : value) {
            if (c == '\\')
                result += "\\\\\\\\";
            else if (c == '"' || c == '$' || c == '`') {
                result += "\\\\";
                result += c;
            } else if (c == '%')
                result += "%%";
            else if (c == '\n' || c == '\r')
                throw std::runtime_error("The executable path cannot contain a newline.");
            else
                result += c;
        }
        return result + '"';
    }

    void associate() {
        const char* home = SDL_getenv("HOME");
        const char* dataHome = SDL_getenv("XDG_DATA_HOME");
        if ((!dataHome || !*dataHome) && !home)
            throw std::runtime_error("Could not locate the user application directory.");
        const fs::path data = dataHome && *dataHome ? fs::path(dataHome) : fs::path(home) / ".local/share";
        const auto applications = data / "applications";
        const auto packages = data / "mime/packages";
        const auto executable = fs::canonical("/proc/self/exe");
        const auto icons = data / "icons/hicolor/256x256";
        const auto iconSource = executable.parent_path() / "icons/hicolor/256x256/apps/org.effect-editor.png";
        fs::create_directories(icons / "apps");
        fs::create_directories(icons / "mimetypes");
        fs::copy_file(iconSource, icons / "apps/org.effect-editor.png", fs::copy_options::overwrite_existing);
        fs::copy_file(iconSource, icons / "mimetypes/application-x-breff.png", fs::copy_options::overwrite_existing);
        fs::create_directories(applications);
        fs::create_directories(packages);
        std::ofstream desktop(applications / "org.effect-editor.desktop");
        desktop.exceptions(std::ios::badbit | std::ios::failbit);
        desktop << "[Desktop Entry]\nType=Application\nName=Effect Editor\nExec="
                << desktopArgument(executable.string())
                << " %F\nIcon=org.effect-editor\nTerminal=false\nMimeType=application/x-breff;\nCategories=Graphics;\n";
        desktop.close();
        std::ofstream mime(packages / "org.effect-editor.xml");
        mime.exceptions(std::ios::badbit | std::ios::failbit);
        mime << "<?xml version=\"1.0\"?>\n<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">"
                "<mime-type type=\"application/x-breff\"><comment>BREFF effect archive</comment>"
                "<glob pattern=\"*.breff\"/></mime-type></mime-info>\n";
        mime.close();
        run({"update-mime-database", (data / "mime").string()});
        run({"update-desktop-database", applications.string()});
        run({"xdg-icon-resource", "forceupdate", "--mode", "user"});
        run({"xdg-mime", "default", "org.effect-editor.desktop", "application/x-breff"});
    }
#endif
}

bool fileAssociationPromptNeeded(const char* userPath) {
    if (!userPath || !*userPath)
        return false;
    std::ifstream marker(fs::u8path(userPath) / "file-association-asked");
    std::string response;
    std::getline(marker, response);
    // Older builds recorded "asked" even when registration failed.
    return response != "answered";
}

void rememberFileAssociationPrompt(const char* userPath) {
    std::ofstream marker;
    marker.exceptions(std::ios::badbit | std::ios::failbit);
    marker.open(fs::u8path(userPath) / "file-association-asked");
    marker << "answered\n";
    marker.close();
}

void registerFileAssociation() {
    associate();
}
