#include "folder_picker.hpp"

#include <cstdio>
#include <mutex>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#endif

namespace zoal_atc::xplane {

struct FolderPicker::State {
  std::mutex mutex;
  bool busy = false;
  std::optional<Result> result;
};

namespace {

std::string trim_line(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  return text;
}

#if !defined(_WIN32)
// Runs a dialog program and reads the folder it prints. `ran` is false when
// the program could not be started at all.
std::string run_dialog(const std::string &command, bool *ran) {
  *ran = false;
  std::FILE *pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return {};
  }
  std::string out;
  char buffer[512];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    out += buffer;
  }
  const int status = pclose(pipe);
  // 127: the shell found no such program.
  *ran = status != -1 && !(WIFEXITED(status) && WEXITSTATUS(status) == 127);
  return trim_line(out);
}

// Single quotes for the shell: the prompt is ours, but quote it anyway.
std::string shell_quoted(const std::string &text) {
  std::string out = "'";
  for (char c : text) {
    out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  }
  return out + "'";
}
#endif

FolderPicker::Result ask(const std::string &prompt) {
  FolderPicker::Result result;
#if defined(__APPLE__)
  std::string script = prompt;
  for (char &c : script) {
    if (c == '"' || c == '\\') {
      c = ' ';
    }
  }
  bool ran = false;
  result.path = run_dialog("osascript -e " +
                               shell_quoted("POSIX path of (choose folder with prompt \"" +
                                            script + "\")") +
                               " 2>/dev/null",
                           &ran);
  if (!ran) {
    result.error = "the folder dialog could not be opened";
  }
  // "/path/" from AppleScript: the trailing slash is dropped.
  while (result.path.size() > 1 && result.path.back() == '/') {
    result.path.pop_back();
  }
#elif defined(_WIN32)
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  IFileOpenDialog *dialog = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&dialog)))) {
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    const int wide_len = MultiByteToWideChar(CP_UTF8, 0, prompt.c_str(), -1, nullptr, 0);
    std::wstring title(static_cast<std::size_t>(wide_len > 0 ? wide_len : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, prompt.c_str(), -1, title.data(), wide_len);
    dialog->SetTitle(title.c_str());
    if (SUCCEEDED(dialog->Show(GetForegroundWindow()))) {
      IShellItem *item = nullptr;
      if (SUCCEEDED(dialog->GetResult(&item))) {
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
          const int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
          if (len > 1) {
            std::string utf8(static_cast<std::size_t>(len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), len, nullptr, nullptr);
            utf8.resize(static_cast<std::size_t>(len - 1));
            result.path = std::move(utf8);
          }
          CoTaskMemFree(path);
        }
        item->Release();
      }
    }
    dialog->Release();
  } else {
    result.error = "the folder dialog could not be opened";
  }
  if (SUCCEEDED(init)) {
    CoUninitialize();
  }
#else
  bool ran = false;
  result.path = run_dialog("zenity --file-selection --directory --title=" +
                               shell_quoted(prompt) + " 2>/dev/null",
                           &ran);
  if (!ran) {
    result.path = run_dialog("kdialog --getexistingdirectory ~ --title " +
                                 shell_quoted(prompt) + " 2>/dev/null",
                             &ran);
  }
  if (!ran) {
    result.error = "no folder dialog is installed (zenity or kdialog); type the path instead";
  }
#endif
  return result;
}

} // namespace

FolderPicker::FolderPicker() : state_(std::make_shared<State>()) {}

FolderPicker::~FolderPicker() = default;

bool FolderPicker::open(const std::string &prompt) {
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->busy) {
      return false;
    }
    state_->busy = true;
    state_->result.reset();
  }
  std::thread([state = state_, prompt] {
    Result result = ask(prompt);
    std::lock_guard<std::mutex> lock(state->mutex);
    state->result = std::move(result);
    state->busy = false;
  }).detach();
  return true;
}

bool FolderPicker::busy() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->busy;
}

std::optional<FolderPicker::Result> FolderPicker::take() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return std::exchange(state_->result, std::nullopt);
}

} // namespace zoal_atc::xplane
