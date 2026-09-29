// Cross-platform child process management for spawning dedicated game servers.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace backend {

struct ChildProcess {
    long long pid = 0;   // POSIX pid
    void* handle = nullptr; // Windows process handle
};

// Launches `exe` with arguments; environment variables in `env` are added to the child.
bool spawnProcess(const std::string& exe, const std::vector<std::string>& args, const std::map<std::string, std::string>& env,
                  ChildProcess& out, std::string& error);
bool processAlive(ChildProcess& p);
void killProcess(ChildProcess& p);
std::string executableDir();
bool fileExists(const std::string& path);

} // namespace backend
