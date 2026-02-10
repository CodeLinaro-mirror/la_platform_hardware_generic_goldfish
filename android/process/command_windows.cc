// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// IWYU pragma: end_keep
// clang-format on
#include <windows.h>

// Process entry
#include <Tlhelp32.h>
#include <psapi.h>
// IWYU pragma: end_keep
// clang-format on

#include <cassert>
#include <ios>
#include <streambuf>

#include "absl/log/log.h"

#include "aemu/base/files/ScopedFileHandle.h"
#include "android/base/win32_unicode_string.h"
#include "android/process/command.h"
#include "android/process/exec.h"

#define DEBUG 0

#if DEBUG >= 1
#define DD(fmt, ...) \
    printf("%d| %s:%d %s| " fmt "\n", GetTickCount(), __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define DD(...) (void)0
#endif

namespace android::base {

using namespace std::chrono_literals;

namespace {
// Converts a std::string (utf-8) -> utf-16
std::wstring ToWide(const std::string& str) {
    std::mbstate_t state = std::mbstate_t();
    std::wstring ws(str.size(), L' ');  // Overestimate number of code points.
    const char* src = str.c_str();
    size_t x = std::mbsrtowcs(ws.data(), &src, str.size(), &state);
    if (x < 0) {
        return {};
    }
    ws.resize(x);
    return ws;
    // Utf8 -> Utf16, so width will always be smaller.
    wchar_t rsp_buffer[str.size() + 1];
    size_t size = 0;
    if (mbstowcs_s(&size, rsp_buffer, sizeof(rsp_buffer), str.c_str(), str.size()) != 0) {
        return L"";
    }
    return {rsp_buffer, size - 1};
}

std::string QuoteParameter(const std::string& command_line) {
    // Therefore, the function will return the length of str1 if none of the characters of str2 are
    // found in str1.
    if (::strcspn(command_line.c_str(), " \t\v\n\"") == command_line.size()) {
        return command_line;
    }

    using namespace std::literals;

    // Otherwise, we need to quote some of the characters.
    std::string out = "\""s;

    for (const char c : command_line) {
        switch (c) {
        case '\\':
            out.append("\\\\"sv);
            break;

        case '\n':
            out.append("\\n"sv);
            break;

        case '"':
            out.append("\\\""sv);
            break;

        default:
            out.append(1, c);
            break;
        }
    }

    out.append("\""sv);

    return out;
}

// Creates a named pipe under /Pipe/android.%ProcessId%.%Counter%
// For doing overlapped I/O
BOOL CreateNamedPipe(LPHANDLE read_pipe, LPHANDLE write_pipe, LPSECURITY_ATTRIBUTES pipe_attributes,
                     DWORD size, DWORD read_mode, DWORD write_mode) {
    HANDLE read_pipe_handle;
    HANDLE write_pipe_handle;
    DWORD error_code;  // Not used after initialization.

    char pipe_name_buffer[MAX_PATH];
    static std::atomic_int counter(0);

    if ((read_mode | write_mode) & (~FILE_FLAG_OVERLAPPED)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (size == 0) {
        size = 4096;
    }

    sprintf(pipe_name_buffer, R"(\\.\Pipe\android.%08lx.%08x)", GetCurrentProcessId(), counter++);

    read_pipe_handle = CreateNamedPipeA(pipe_name_buffer, PIPE_ACCESS_INBOUND | read_mode,
                                        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_READMODE_BYTE,
                                        1,           // Number of pipes_
                                        size,        // Out buffer size
                                        size,        // In buffer size
                                        120 * 1000,  // Timeout in ms
                                        pipe_attributes);

    if (!read_pipe_handle) {
        return FALSE;
    }

    write_pipe_handle =
            CreateFileA(pipe_name_buffer, GENERIC_WRITE,
                        0,  // No sharing
                        pipe_attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | write_mode,
                        nullptr  // Template file
            );

    if (INVALID_HANDLE_VALUE == write_pipe_handle) {
        error_code = GetLastError();
        CloseHandle(read_pipe_handle);
        SetLastError(error_code);
        return FALSE;
    }

    *read_pipe = read_pipe_handle;
    *write_pipe = write_pipe_handle;
    return TRUE;
}

#define BUFSIZE 4096

// Human readable string of the last error.
std::string FormatLastErr() {
    DWORD error = GetLastError();
    if (error) {
        constexpr size_t kMaxStrLen = 512;
        char msg_buf[kMaxStrLen];
        DWORD buffer_len = FormatMessageA(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msg_buf, kMaxStrLen, nullptr);
        if (buffer_len) {
            std::string result(msg_buf, msg_buf + buffer_len);
            return result;
        }
    }
    return "";
}

// From https://devblogs.microsoft.com/oldnewthing/20111216-00/?p=8873
// Calls create process with explicitly inheriting the set of handles
// that are in rgHandlesToInherit.
// Note that you must poass inherit_handles as true and
// make sure to set the individual handle to inherit.
BOOL CreateProcessWithExplicitHandles(LPCWSTR application_name, LPWSTR command_line,
                                      LPSECURITY_ATTRIBUTES process_attributes,
                                      LPSECURITY_ATTRIBUTES thread_attributes, BOOL inherit_handles,
                                      DWORD creation_flags, LPVOID environment,
                                      LPCWSTR current_directory, LPSTARTUPINFOW startup_info,
                                      LPPROCESS_INFORMATION process_information,
                                      // here is the new stuff
                                      DWORD count_handles_to_inherit, HANDLE* handles_to_inherit) {
    BOOL success;
    BOOL initialized = FALSE;
    SIZE_T size = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST attribute_list = nullptr;
    success = count_handles_to_inherit < 0xFFFFFFFF / sizeof(HANDLE) &&
              startup_info->cb == sizeof(*startup_info);
    if (!success) {
        SetLastError(ERROR_INVALID_PARAMETER);
    }
    if (success) {
        success = InitializeProcThreadAttributeList(nullptr, 1, 0, &size) ||
                  GetLastError() == ERROR_INSUFFICIENT_BUFFER;
    }
    if (success) {
        attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
                HeapAlloc(GetProcessHeap(), 0, size));
        success = attribute_list != nullptr;
    }
    if (success) {
        success = InitializeProcThreadAttributeList(attribute_list, 1, 0, &size);
    }
    if (success) {
        initialized = TRUE;
        success = UpdateProcThreadAttribute(attribute_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                            static_cast<PVOID>(handles_to_inherit),
                                            count_handles_to_inherit * sizeof(HANDLE), nullptr,
                                            nullptr);
    }
    if (success) {
        STARTUPINFOEXW info;
        ZeroMemory(&info, sizeof(info));
        info.StartupInfo = *startup_info;
        info.StartupInfo.cb = sizeof(info);
        info.lpAttributeList = attribute_list;

        DD("Creating proc.");
        success = CreateProcessW(application_name, command_line, process_attributes,
                                 thread_attributes, inherit_handles,
                                 creation_flags | EXTENDED_STARTUPINFO_PRESENT, environment,
                                 current_directory, &info.StartupInfo, process_information);
    }
    if (initialized) DeleteProcThreadAttributeList(attribute_list);
    if (attribute_list) HeapFree(GetProcessHeap(), 0, attribute_list);
    return success;
}

struct WindwsPipe {
    ~WindwsPipe() {
        CloseHandle(overlap.hEvent);
        CloseHandle(read);
        CloseHandle(write);
    }

    HANDLE Event() const { return overlap.hEvent; }

    void Close() {
        DD("Closing pipe.");
        pending_io = false;
        closed = true;
    }

    void FlushToStreambuf() {
        buffer->sputn(ch_read, cb_read);
        buffer->pubsync();
        pending_io = false;
    }

    OVERLAPPED overlap = {0};
    HANDLE read;
    HANDLE write;
    CHAR ch_read[BUFSIZE] = {0};
    DWORD cb_read = 0;
    BOOL pending_io = FALSE;
    BOOL closed = FALSE;
    std::basic_streambuf<char>* buffer = nullptr;
};
}  // namespace

class WindowsOverseer : public ProcessOverseer {
  public:
    WindowsOverseer(HANDLE process, std::vector<std::unique_ptr<WindwsPipe>> pipes)
            : process_(process), pipes_(std::move(pipes)) {}

    ~WindowsOverseer() override { DD("~WindowsOverseer"); }

    bool ChildIsAlive() { return WaitForSingleObject(process_, 0) == WAIT_TIMEOUT; }

    // Waits for a read finished event on any of the active pipes.
    // returns the pipe with the event or nullptr if there are no
    // events to wait for
    WindwsPipe* WaitForPipeEvents() {
        std::vector<HANDLE> events;
        for (const auto& pipe : pipes_) {
            if (pipe->pending_io && !pipe->closed) {
                DD("-- Wait for %p", pipe->hRead);
                events.push_back(pipe->overlap.hEvent);
            }
        }

        DD("Waiting for %d events", events.size());
        if (events.empty()) return nullptr;

        DWORD wait = WaitForMultipleObjects(events.size(),  // number of event objects
                                            events.data(),  // array of event objects
                                            FALSE,          // does not wait for all
                                            INFINITE);      // waits indefinitely

        if (wait == WAIT_TIMEOUT || wait == WAIT_FAILED) {
            DD("Failed or abandoned. %s", FormatLastErr().c_str());
            return nullptr;
        }
        // dwWait shows which pipe completed the operation.
        auto i = wait - WAIT_OBJECT_0;  // determines which pipe
        assert(i >= 0 && i < pipes_.size());
        ResetEvent(events[i]);

        for (const auto& pipe : pipes_) {
            if (pipe->Event() == events[i]) return pipe.get();
        }
        assert(false);
        return nullptr;
    }

    void Start(std::basic_streambuf<char>* out, std::basic_streambuf<char>* err) override {
        pipes_[0]->buffer = out;
        pipes_[1]->buffer = err;

        // A pipe can be:
        // - pending (we are waiting for a read complete)
        // - closed (no more events will come in)
        while (!stop_) {
            DWORD error = 0;
            for (const auto& pipe : pipes_) {
                if (pipe->closed) {
                    DD("Skipping %p", pipe->hRead);
                    continue;
                }

                if (!pipe->pending_io) {
                    DWORD bytes_to_read = BUFSIZE * sizeof(char);
                    BOOL success = ReadFile(pipe->read, pipe->ch_read, bytes_to_read, nullptr,
                                            &pipe->overlap);

                    DD("Readfile: (%p), %s, read: %d (%s)", pipe->hRead,
                       success ? "success" : "fail", pipe->cbRead, FormatLastErr().c_str());

                    // The read might still be pending.
                    error = GetLastError();
                    if (error == ERROR_IO_PENDING) {
                        DD("I/O Pending");
                        pipe->pending_io = TRUE;
                    } else {
                        // Some other unknown error..
                        pipe->Close();
                    }
                }
            }

            // Eventually all pipes move to the closed state
            // which will result in them not being scheduled for
            // events.
            auto* pipe = WaitForPipeEvents();

            if (pipe == nullptr) {
                DD("No events!");
                pipes_.clear();
                return;
            }

            DD("Event for %p", pipe->hRead);
            if (pipe->pending_io) {
                DWORD byte_count = 0;
                bool success = GetOverlappedResult(pipe->read,      // handle to pipe
                                                   &pipe->overlap,  // OVERLAPPED structure
                                                   &byte_count,     // bytes transferred
                                                   FALSE);          // do not wait

                DD("Overlapped: %s, bytes available: %d, (%d:%s)", success ? "success" : "fail",
                   cbRet, GetLastError(), success ? "" : FormatLastErr().c_str());

                if (GetLastError() == ERROR_BROKEN_PIPE) {
                    // remove this pipe..
                    pipe->Close();
                }
                if (success) {
                    pipe->cb_read = byte_count;
                    pipe->FlushToStreambuf();
                }
            }
        }

        // Close and destroy pipe objects.
        pipes_.clear();
    }

    // Cancel the observation of the process, no callbacks
    // should be invoked.
    // no writes to std_out, std_err should happen.
    void Stop() override { stop_ = true; };

  private:
    std::vector<std::unique_ptr<WindwsPipe>> pipes_;
    HANDLE process_;
    bool stop_{false};
};

class WinProcess : public ObservableProcess {
  public:
    ~WinProcess() override {
        if (!daemon_ && owner_) WinProcess::Terminate();

        if (process_ != nullptr && process_ != INVALID_HANDLE_VALUE) {
            CloseHandle(proc_info_.hProcess);
        }
    }

    WinProcess(bool daemon, bool inherit) : ObservableProcess(daemon, inherit) {}

    explicit WinProcess(HANDLE process_handle) : ObservableProcess(true) {
        SetHandle(process_handle);
    }

    void SetHandle(HANDLE process_handle) {
        pid_ = static_cast<Pid>(GetProcessId(process_handle));
        process_ = process_handle;
    }

    std::future_status WaitForKernel(
            const std::chrono::milliseconds timeout_duration) const override {
        if (process_ == INVALID_HANDLE_VALUE) {
            LOG(WARNING) << "Invalid process handle, assuming it is not running.";
            return std::future_status::ready;
        }

        auto state = WaitForSingleObject(process_, timeout_duration.count());
        if (state == WAIT_FAILED) {
            PLOG(ERROR) << "Failed to wait for process due to " << GetLastError();
        }
        return state == WAIT_TIMEOUT ? std::future_status::timeout : std::future_status::ready;
    }

    bool Terminate() override {
        if (process_ == INVALID_HANDLE_VALUE) return false;
        TerminateProcess(process_, 1);
        // 100ms to shut down.
        return WaitForSingleObject(process_, 100) != WAIT_TIMEOUT;
    }

    std::string Exe() const override {
        std::string name(MAX_PATH, '\000');
        if (process_ != INVALID_HANDLE_VALUE) {
            int size = GetModuleFileNameExA(process_, nullptr, name.data(), name.size());
            name.reserve(size + 1);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                size = GetModuleFileNameExA(process_, nullptr, name.data(), size + 1);
                name.resize(size + 1, '\000');
            }
        }
        return name;
    }

    bool IsAlive() const override {
        if (process_ == INVALID_HANDLE_VALUE) return false;

        return WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
    }

    std::future_status WaitFor(const std::chrono::milliseconds timeout_duration) const override {
        if (process_ == INVALID_HANDLE_VALUE) return std::future_status::ready;

        auto state = WaitForSingleObject(process_, timeout_duration.count());
        return state == WAIT_TIMEOUT ? std::future_status::timeout : std::future_status::ready;
    }

    virtual std::optional<Pid> CreateProcess(const CommandArguments& args, bool capture_output,
                                             bool replace) override {
        if (replace) {
            std::vector<std::string> quoted_arguments;
            quoted_arguments.reserve(args.size());
            for (const std::string& arg : args) {
                quoted_arguments.push_back(QuoteParameter(arg));
            }

            std::vector<char*> cmdline;
            cmdline.reserve(args.size() + 1);
            for (std::string& arg : quoted_arguments) {
                cmdline.push_back(const_cast<char*>(arg.c_str()));
            }
            cmdline.push_back(nullptr);

            // The exec() functions only return if an error has occurred.
            SafeExecv(cmdline[0], cmdline.data());
            return std::nullopt;
        }

        STARTUPINFOW startup_info = {.cb = sizeof(STARTUPINFOW)};
        if (capture_output) {
            DD("Installing pipes_ for stdout & stderr");
            // Setup named pipes_ to stderr/stdout..
            // https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output
            SECURITY_ATTRIBUTES security_attributes;
            security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
            security_attributes.bInheritHandle = TRUE;
            security_attributes.lpSecurityDescriptor = nullptr;

            for (int i = 0; i < 2; i++) {
                auto pipe = std::make_unique<WindwsPipe>();
                HANDLE read_handle;
                HANDLE write_handle;
                if (!CreateNamedPipe(&pipe->read, &pipe->write, &security_attributes, 0,
                                     FILE_FLAG_OVERLAPPED, FILE_FLAG_OVERLAPPED)) {
                    // ("Unable to create pipe: " + std::to_string(i));
                    return std::nullopt;
                }
                if (!SetHandleInformation(pipe->read, HANDLE_FLAG_INHERIT, 0)) return std::nullopt;
                // "SetHandleInformation failed for pipe: " +
                //         std::to_string(i));

                pipe->overlap.hEvent = CreateEvent(nullptr,   // default security attribute
                                                   TRUE,      // manual-reset event
                                                   TRUE,      // initial state = signaled
                                                   nullptr);  // unnamed event object

                if (pipe->overlap.hEvent == nullptr) {
                    return std::nullopt;
                }

                pipe->pending_io = FALSE;
                pipes_.push_back(std::move(pipe));
            }

            startup_info.dwFlags |= STARTF_USESTDHANDLES;
            startup_info.hStdOutput = pipes_[0]->write;
            startup_info.hStdError = pipes_[1]->write;
        }

        // Setup the child process
        std::string cmdline;
        for (const auto& param : args) {
            cmdline += QuoteParameter(param) + " ";
        }
        cmdline.pop_back();
        std::wstring command_line_w = ToWide(cmdline);
        auto* sz_command_line = const_cast<LPWSTR>(command_line_w.c_str());

        BOOL success;
        if (inherit_ || pipes_.empty()) {
            DD("CreateProcessW(%s)", inherit_ ? "Inherit handles" : "Do not inherit");
            success = ::CreateProcessW(nullptr,
                                       sz_command_line,  // command line
                                       nullptr,          // process security attributes
                                       nullptr,          // primary thread security attributes
                                       inherit_,         // handles could be inherited
                                       0,                // creation flags
                                       nullptr,          // use parent's environment
                                       nullptr,          // use parent's current directory
                                       &startup_info,    // STARTUPINFO pointer
                                       &proc_info_);     // receives PROCESS_INFORMATION
        } else {
            assert(!inherit_);
            // We explicitly inherit our pipes.
            std::vector<HANDLE> handles{pipes_[0]->write, pipes_[1]->write};
            success =
                    CreateProcessWithExplicitHandles(nullptr,
                                                     sz_command_line,  // command line
                                                     nullptr,  // process security attributes
                                                     nullptr,  // primary thread security attributes
                                                     TRUE,  // handles will be explicitly inherited
                                                     0,     // creation flags
                                                     nullptr,  // use parent's environment
                                                     nullptr,  // use parent's current directory
                                                     &startup_info,  // STARTUPINFO pointer
                                                     &proc_info_, handles.size(), handles.data());
        }
        DD("Create process: %d, %s", success, FormatLastErr().c_str());

        // If an error occurs, exit the application.
        if (!success) {
            DD("Create process failed: %s", FormatLastErr().c_str());
            return std::nullopt;
        }

        // Close handles to the  pipes_ no longer needed by the child
        // process. If they are not explicitly closed, there is no way
        // to recognize that the child process has ended.
        for (const auto& pipe : pipes_) {
            CloseHandle(pipe->write);
            pipe->write = INVALID_HANDLE_VALUE;
        }
        CloseHandle(proc_info_.hThread);
        proc_info_.hThread = INVALID_HANDLE_VALUE;

        std::unique_ptr<ProcessOverseer> overseer;

        SetHandle(proc_info_.hProcess);
        owner_ = true;
        return pid_;
    }

    std::unique_ptr<ProcessOverseer> CreateOverseer() override {
        return std::make_unique<WindowsOverseer>(proc_info_.hProcess, std::move(pipes_));
    }

  protected:
    std::optional<ProcessExitCode> GetExitCode() const override {
        if (process_ == INVALID_HANDLE_VALUE || IsAlive()) {
            return std::nullopt;
        }
        DWORD exit = STILL_ACTIVE;
        DWORD n;

        // When we are poking another process than our own that just
        // terminated (i.e. not alive), it might not have yet updated
        // its exit status.
        GetExitCodeProcess(process_, &exit);
        for (n = 0; n < 20 && exit == STILL_ACTIVE; n++) {
            std::this_thread::sleep_for(1ms);
            GetExitCodeProcess(process_, &exit);
        }

        DD("Looped %d times", n);
        return exit;
    }

  private:
    HANDLE process_;
    PROCESS_INFORMATION proc_info_ = {.hProcess = INVALID_HANDLE_VALUE,
                                      .hThread = INVALID_HANDLE_VALUE};
    bool owner_{false};
    std::vector<std::unique_ptr<WindwsPipe>> pipes_;
};

Command::ProcessFactory Command::s_process_factory = [](const CommandArguments& /* args */,
                                                        bool daemon, bool inherit) {
    return std::make_unique<WinProcess>(daemon, inherit);
};

std::unique_ptr<Process> Process::FromPid(Pid pid) {
    ScopedFileHandle process_handle(OpenProcess(
            PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, false, pid));
    if (process_handle.valid()) {
        return std::make_unique<WinProcess>(process_handle.release());
    }
    return nullptr;
}

std::vector<std::unique_ptr<Process>> Process::FromName(const std::string& name) {
    std::vector<std::unique_ptr<Process>> processes;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W process = {0};
    process.dwSize = sizeof(process);

    if (!Process32FirstW(snapshot, &process)) {
        return processes;
    }
    do {
        if (Win32UnicodeString::convertToUtf8(process.szExeFile).find(name) !=
            std::string::npos) {  // NOLINT(bugprone-signed-char-arg)
            processes.push_back(FromPid(static_cast<Pid>(process.th32ProcessID)));
        }
    } while (Process32NextW(snapshot, &process));

    CloseHandle(snapshot);
    return processes;
}

std::unique_ptr<Process> Process::Me() {
    return FromPid(static_cast<Pid>(GetCurrentProcessId()));
}

}  // namespace android::base
