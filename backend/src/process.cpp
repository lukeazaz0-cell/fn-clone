#include "process.h"

#include <cstdlib>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace backend {

bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

#ifdef _WIN32

static std::string quoteArg(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
    std::string q = "\"";
    for (char c : a) {
        if (c == '"') q += "\\\"";
        else q += c;
    }
    return q + "\"";
}

bool spawnProcess(const std::string& exe, const std::vector<std::string>& args, const std::map<std::string, std::string>& env,
                  ChildProcess& out, std::string& error) {
    std::string cmd = quoteArg(exe);
    for (auto& a : args) cmd += " " + quoteArg(a);
    // Children inherit the parent's environment; set extra variables before spawning.
    for (auto& [k, v] : env) SetEnvironmentVariableA(k.c_str(), v.c_str());
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    BOOL ok = CreateProcessA(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    for (auto& [k, v] : env) SetEnvironmentVariableA(k.c_str(), nullptr);
    if (!ok) {
        error = "CreateProcess failed with error " + std::to_string(GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    out.handle = pi.hProcess;
    out.pid = (long long)pi.dwProcessId;
    return true;
}

bool processAlive(ChildProcess& p) {
    if (!p.handle) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess((HANDLE)p.handle, &code)) return false;
    if (code == STILL_ACTIVE) return true;
    CloseHandle((HANDLE)p.handle);
    p.handle = nullptr;
    return false;
}

void killProcess(ChildProcess& p) {
    if (!p.handle) return;
    TerminateProcess((HANDLE)p.handle, 1);
    CloseHandle((HANDLE)p.handle);
    p.handle = nullptr;
}

std::string executableDir() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf, n);
    auto pos = s.find_last_of("\\/");
    return pos == std::string::npos ? "." : s.substr(0, pos);
}

#else

bool spawnProcess(const std::string& exe, const std::vector<std::string>& args, const std::map<std::string, std::string>& env,
                  ChildProcess& out, std::string& error) {
    pid_t pid = fork();
    if (pid < 0) {
        error = "fork failed";
        return false;
    }
    if (pid == 0) {
        for (auto& [k, v] : env) setenv(k.c_str(), v.c_str(), 1);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(exe.c_str()));
        for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(exe.c_str(), argv.data());
        _exit(127);
    }
    out.pid = pid;
    return true;
}

bool processAlive(ChildProcess& p) {
    if (p.pid <= 0) return false;
    int status = 0;
    pid_t r = waitpid((pid_t)p.pid, &status, WNOHANG);
    if (r == 0) return true;
    p.pid = 0;
    return false;
}

void killProcess(ChildProcess& p) {
    if (p.pid <= 0) return;
    kill((pid_t)p.pid, SIGTERM);
    int status = 0;
    for (int i = 0; i < 20; i++) {
        if (waitpid((pid_t)p.pid, &status, WNOHANG) != 0) { p.pid = 0; return; }
        usleep(50000);
    }
    kill((pid_t)p.pid, SIGKILL);
    waitpid((pid_t)p.pid, &status, 0);
    p.pid = 0;
}

std::string executableDir() {
    char buf[4096];
#ifdef __APPLE__
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return ".";
    std::string s(buf);
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    std::string s(buf, (size_t)n);
#endif
    auto pos = s.find_last_of('/');
    return pos == std::string::npos ? "." : s.substr(0, pos);
}

#endif

} // namespace backend
