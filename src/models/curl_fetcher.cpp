#include "zoal_atc/models/fetcher.hpp"

#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace zoal_atc::models {
namespace {

constexpr auto kPoll = std::chrono::milliseconds(250);
constexpr const char *kConfigName = ".download.curl";
constexpr const char *kLogName = ".download.log";

// A curl config file string: quoted, with its two special characters escaped.
std::string quoted(const std::string &text) {
  std::string out = "\"";
  for (const char c : text) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

// On Windows curl runs in the models folder and is given paths relative to
// it, because curl.exe opens what a config file names in the ANSI code page
// and an X-Plane folder under a user's own name need not fit in it. The
// manifest's paths are ASCII. Elsewhere a path is UTF-8 bytes either way.
std::string path_for_curl(const std::filesystem::path &path, const std::filesystem::path &work_dir) {
#if defined(_WIN32)
  return path.lexically_relative(work_dir).generic_string();
#else
  (void)work_dir;
  return path.string();
#endif
}

std::string last_line(const std::filesystem::path &file) {
  std::ifstream in(file, std::ios::binary);
  std::string line;
  std::string last;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (!line.empty()) {
      last = line;
    }
  }
  return last;
}

// The options every transfer shares: https only, redirects followed (both
// hosts hand off to a CDN), resumed, and given up on when they stall rather
// than left hanging for the rest of the session.
std::vector<std::string> curl_options(const std::string &config, const std::string &log) {
  return {"--silent",        "--show-error",   "--fail",          "--location",
          "--proto",         "=https",         "--proto-redir",   "=https",
          "--retry",         "3",              "--retry-delay",   "2",
          "--connect-timeout", "30",           "--speed-limit",   "1024",
          "--speed-time",    "60",             "--create-dirs",   "--continue-at",
          "-",               "--stderr",       log,               "--config",
          config};
}

#if defined(_WIN32)

std::wstring widen(const std::string &text) {
  if (text.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

// run_curl starts curl, waits for it, and returns its exit code; -1 when it
// could not be started, -2 when cancelled.
int run_curl(const std::vector<std::string> &args, const std::filesystem::path &work_dir,
             const std::atomic_bool &cancel, const std::function<void()> &tick, std::string *why) {
  wchar_t system_dir[MAX_PATH] = {};
  const UINT length = GetSystemDirectoryW(system_dir, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    *why = "cannot find the Windows system folder";
    return -1;
  }
  const std::wstring curl = std::wstring(system_dir) + L"\\curl.exe";
  std::wstring command = L"\"" + curl + L"\"";
  for (const std::string &arg : args) {
    command += L" \"" + widen(arg) + L"\"";
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::wstring dir = work_dir.wstring();
  if (!CreateProcessW(curl.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, dir.c_str(), &startup, &process)) {
    *why = "cannot run " + std::filesystem::path(curl).string() + " (error " +
           std::to_string(GetLastError()) + ")";
    return -1;
  }
  CloseHandle(process.hThread);
  for (;;) {
    const DWORD wait = WaitForSingleObject(process.hProcess, static_cast<DWORD>(kPoll.count()));
    if (wait == WAIT_OBJECT_0) {
      break;
    }
    if (cancel.load()) {
      TerminateProcess(process.hProcess, 1);
      WaitForSingleObject(process.hProcess, 5000);
      CloseHandle(process.hProcess);
      return -2;
    }
    if (tick) {
      tick();
    }
  }
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hProcess);
  return static_cast<int>(code);
}

#else

std::string find_curl() {
  for (const char *candidate : {"/usr/bin/curl", "/usr/local/bin/curl", "/bin/curl", "/opt/homebrew/bin/curl"}) {
    if (access(candidate, X_OK) == 0) {
      return candidate;
    }
  }
  return "curl";
}

int run_curl(const std::vector<std::string> &args, const std::filesystem::path &,
             const std::atomic_bool &cancel, const std::function<void()> &tick, std::string *why) {
  const std::string curl = find_curl();
  std::vector<std::string> owned;
  owned.push_back(curl);
  owned.insert(owned.end(), args.begin(), args.end());
  std::vector<char *> argv;
  for (std::string &arg : owned) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int spawned = posix_spawnp(&pid, curl.c_str(), nullptr, nullptr, argv.data(), environ);
  if (spawned != 0) {
    *why = "cannot run curl: " + std::string(std::strerror(spawned));
    return -1;
  }
  int status = 0;
  for (;;) {
    const pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid) {
      break;
    }
    if (done < 0) {
      if (errno == EINTR) {
        continue;
      }
      // X-Plane or another plugin ignores SIGCHLD, so the child was reaped
      // for us and its code is gone. What it downloaded is checked anyway.
      return 0;
    }
    if (cancel.load()) {
      kill(pid, SIGTERM);
      waitpid(pid, &status, 0);
      return -2;
    }
    if (tick) {
      tick();
    }
    std::this_thread::sleep_for(kPoll);
  }
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  return 1;
}

#endif

class CurlFetcher final : public Fetcher {
public:
  bool fetch(const std::vector<Transfer> &transfers, const std::filesystem::path &work_dir,
             const std::atomic_bool &cancel, const std::function<void()> &tick,
             std::string *why) override {
    std::string ignored;
    std::string &reason = why != nullptr ? *why : ignored;
    if (transfers.empty()) {
      return true;
    }
    std::error_code error;
    std::filesystem::create_directories(work_dir, error);
    const auto config = work_dir / kConfigName;
    const auto log = work_dir / kLogName;
    {
      std::ofstream out(config, std::ios::binary | std::ios::trunc);
      for (const Transfer &transfer : transfers) {
        out << "url = " << quoted(transfer.url) << "\n"
            << "output = " << quoted(path_for_curl(transfer.destination, work_dir)) << "\n";
      }
      if (!out) {
        reason = "cannot write " + config.string();
        return false;
      }
    }
    std::filesystem::remove(log, error);
    const int code = run_curl(curl_options(path_for_curl(config, work_dir), path_for_curl(log, work_dir)),
                              work_dir, cancel, tick, &reason);
    std::filesystem::remove(config, error);
    if (code == 0) {
      std::filesystem::remove(log, error);
      return true;
    }
    if (code == -2) {
      reason = "cancelled";
    } else if (code == 127) {
      reason = "curl is not installed";
    } else if (code > 0) {
      const std::string detail = last_line(log);
      reason = "curl exited " + std::to_string(code) + (detail.empty() ? "" : ": " + detail);
    }
    return false;
  }
};

} // namespace

std::unique_ptr<Fetcher> make_curl_fetcher() { return std::make_unique<CurlFetcher>(); }

} // namespace zoal_atc::models
