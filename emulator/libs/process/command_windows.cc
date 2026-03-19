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
// clang-format off
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

#include "android/base/scoped_file_handle.h"
#include "android/base/win32_unicode_string.h"
#include "android/process/command.h"
#include "exec.h"

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
    Win32UnicodeString wstr(str);
    return {wstr.c_str(), wstr.size()};
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

    ScopedFileHandle read_pipe_handle(
            CreateNamedPipeA(pipe_name_buffer, PIPE_ACCESS_INBOUND | read_mode,
                             PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_READMODE_BYTE,
                             1,           // Number of pipes_
                             size,        // Out buffer size
                             size,        // In buffer size
                             120 * 1000,  // Timeout in ms
                             pipe_attributes));

    if (!read_pipe_handle) {
        return FALSE;
    }

    ScopedFileHandle write_pipe_handle(CreateFileA(pipe_name_buffer, GENERIC_WRITE,
                                                   0,  // No sharing
                                                   pipe_attributes, OPEN_EXISTING,
                                                   FILE_ATTRIBUTE_NORMAL | write_mode,
                                                   nullptr  // Template file
                                                   ));

    if (!write_pipe_handle) {
        return FALSE;
    }

    *read_pipe = read_pipe_handle.release();
    *write_pipe = write_pipe_handle.release();
    return TRUE;
}

#define BUFSIZE 4096

#if DEBUG >= 1
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
#endif

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
    HANDLE Event() const { return overlap_event.get(); }

    void Close() {
        DD("Closing pipe.");
        pending_io = false;
        closed = true;
    }

    void FlushToStreambuf() {
        if (buffer) {
            buffer->sputn(ch_read, cb_read);
            buffer->pubsync();
        }
        pending_io = false;
    }

    ScopedFileHandle overlap_event;
    OVERLAPPED overlap = {0};
    ScopedFileHandle read;
    ScopedFileHandle write;
    CHAR ch_read[BUFSIZE] = {0};
    DWORD cb_read = 0;
    BOOL pending_io = FALSE;
    BOOL closed = FALSE;
    std::basic_streambuf<char>* buffer = nullptr;
};
}  // namespace

class WindowsOverseer : public ProcessOverseer {
  public:
    WindowsOverseer(ScopedFileHandle process, std::vector<std::unique_ptr<WindwsPipe>> pipes)
            : pipes_(std::move(pipes)), process_(std::move(process)) {
        stop_event_.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    }

    ~WindowsOverseer() override { DD("~WindowsOverseer"); }

    bool ChildIsAlive() {
        return process_ && WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT;
    }

    // Waits for a read finished event on any of the active pipes or the stop event.
    // returns the pipe with the event or nullptr if there are no
    // events to wait for
    WindwsPipe* WaitForPipeEvents() {
        std::vector<HANDLE> events;
        std::vector<WindwsPipe*> event_pipes;
        for (const auto& pipe : pipes_) {
            if (pipe->pending_io && !pipe->closed) {
                DD("-- Wait for %p", pipe->read.get());
                events.push_back(pipe->overlap_event.get());
                event_pipes.push_back(pipe.get());
            }
        }

        if (events.empty()) return nullptr;

        events.push_back(stop_event_.get());

        DD("Waiting for %d events", events.size());
        DWORD wait = WaitForMultipleObjects(events.size(),  // number of event objects
                                            events.data(),  // array of event objects
                                            FALSE,          // does not wait for all
                                            INFINITE);      // waits indefinitely

        if (wait == WAIT_FAILED || wait == WAIT_OBJECT_0 + events.size() - 1) {
            DD("Stop event or wait failed. %s", FormatLastErr().c_str());
            return nullptr;
        }

        // dwWait shows which pipe completed the operation.
        auto i = wait - WAIT_OBJECT_0;
        if (i < event_pipes.size()) {
            ResetEvent(events[i]);
            return event_pipes[i];
        }
        return nullptr;
    }

    void Start(std::basic_streambuf<char>* out, std::basic_streambuf<char>* err) override {
        pipes_[0]->buffer = out;
        pipes_[1]->buffer = err;

        // A pipe can be:
        // - pending (we are waiting for a read complete)
        // - closed (no more events will come in)
        while (!stop_) {
            for (const auto& pipe : pipes_) {
                if (pipe->closed) {
                    DD("Skipping %p", pipe->read.get());
                    continue;
                }

                if (!pipe->pending_io) {
                    DWORD bytes_to_read = BUFSIZE * sizeof(char);
                    BOOL success = ReadFile(pipe->read.get(), pipe->ch_read, bytes_to_read, nullptr,
                                            &pipe->overlap);

                    DD("Readfile: (%p), %s, read: %d (%s)", pipe->read.get(),
                       success ? "success" : "fail", pipe->cb_read, FormatLastErr().c_str());

                    // The read might still be pending.
                    if (success) {
                        // Read completed immediately.
                        if (GetOverlappedResult(pipe->read.get(), &pipe->overlap, &pipe->cb_read,
                                                FALSE)) {
                            pipe->FlushToStreambuf();
                        }
                    } else if (GetLastError() == ERROR_IO_PENDING) {
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
                DD("No events or stopped!");
                pipes_.clear();
                return;
            }

            DD("Event for %p", pipe->read.get());
            if (pipe->pending_io) {
                DWORD byte_count = 0;
                bool success = GetOverlappedResult(pipe->read.get(),  // handle to pipe
                                                   &pipe->overlap,    // OVERLAPPED structure
                                                   &byte_count,       // bytes transferred
                                                   FALSE);            // do not wait

                DD("Overlapped: %s, bytes available: %d, (%d:%s)", success ? "success" : "fail",
                   byte_count, GetLastError(), success ? "" : FormatLastErr().c_str());

                if (!success && GetLastError() == ERROR_BROKEN_PIPE) {
                    // remove this pipe..
                    pipe->Close();
                } else if (success) {
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
    void Stop() override {
        stop_ = true;
        SetEvent(stop_event_.get());
    };

  private:
    std::vector<std::unique_ptr<WindwsPipe>> pipes_;
    ScopedFileHandle process_;
    ScopedEventHandle stop_event_;
    bool stop_{false};
};

class WinProcess : public ObservableProcess {
  public:
    ~WinProcess() override {
        if (!daemon_ && owner_) WinProcess::Terminate();
    }

    WinProcess(bool daemon, bool inherit) : ObservableProcess(daemon, inherit) {}

    explicit WinProcess(ScopedFileHandle process_handle) : ObservableProcess(true) {
        SetHandle(std::move(process_handle));
    }

    void SetHandle(ScopedFileHandle process_handle) {
        if (process_handle) {
            pid_ = static_cast<Pid>(GetProcessId(process_handle.get()));
        } else {
            pid_ = 0;
        }
        process_ = std::move(process_handle);
    }

    std::future_status WaitForKernel(
            const std::chrono::milliseconds timeout_duration) const override {
        if (!process_) {
            LOG(WARNING) << "Invalid process handle, assuming it is not running.";
            return std::future_status::ready;
        }

        auto state = WaitForSingleObject(process_.get(), timeout_duration.count());
        if (state == WAIT_FAILED) {
            PLOG(ERROR) << "Failed to wait for process due to " << GetLastError();
        }
        return state == WAIT_TIMEOUT ? std::future_status::timeout : std::future_status::ready;
    }

    bool Terminate() override {
        if (!process_) return false;
        TerminateProcess(process_.get(), 1);
        // 100ms to shut down.
        return WaitForSingleObject(process_.get(), 100) != WAIT_TIMEOUT;
    }

    std::string Exe() const override {
        std::string name(MAX_PATH, '\000');
        if (process_) {
            DWORD size = GetModuleFileNameExA(process_.get(), nullptr, name.data(), name.size());
            if (size == 0) {
                return "";
            }
            if (size == name.size() && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                name.resize(UNICODE_STRING_MAX_CHARS);
                size = GetModuleFileNameExA(process_.get(), nullptr, name.data(), name.size());
            }
            name.resize(size);
        }
        return name;
    }

    bool IsAlive() const override {
        if (!process_) return false;

        return WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT;
    }

    std::future_status WaitFor(const std::chrono::milliseconds timeout_duration) const override {
        if (!process_) return std::future_status::ready;

        auto state = WaitForSingleObject(process_.get(), timeout_duration.count());
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
                if (!CreateNamedPipe(&read_handle, &write_handle, &security_attributes, 0,
                                     FILE_FLAG_OVERLAPPED, FILE_FLAG_OVERLAPPED)) {
                    // ("Unable to create pipe: " + std::to_string(i));
                    return std::nullopt;
                }
                pipe->read.reset(read_handle);
                pipe->write.reset(write_handle);

                if (!SetHandleInformation(pipe->read.get(), HANDLE_FLAG_INHERIT, 0))
                    return std::nullopt;
                // "SetHandleInformation failed for pipe: " +
                //         std::to_string(i));

                pipe->overlap_event.reset(CreateEvent(nullptr,    // default security attribute
                                                      TRUE,       // manual-reset event
                                                      TRUE,       // initial state = signaled
                                                      nullptr));  // unnamed event object

                if (!pipe->overlap_event) {
                    return std::nullopt;
                }
                pipe->overlap.hEvent = pipe->overlap_event.get();

                pipe->pending_io = FALSE;
                pipes_.push_back(std::move(pipe));
            }

            startup_info.dwFlags |= STARTF_USESTDHANDLES;
            startup_info.hStdOutput = pipes_[0]->write.get();
            startup_info.hStdError = pipes_[1]->write.get();
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
        PROCESS_INFORMATION proc_info = {0};
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
                                       &proc_info);      // receives PROCESS_INFORMATION
        } else {
            assert(!inherit_);
            // We explicitly inherit our pipes.
            std::vector<HANDLE> handles{pipes_[0]->write.get(), pipes_[1]->write.get()};
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
                                                     &proc_info, handles.size(), handles.data());
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
            pipe->write.reset();
        }
        ScopedFileHandle thread_handle(proc_info.hThread);
        SetHandle(ScopedFileHandle(proc_info.hProcess));
        owner_ = true;
        return pid_;
    }

    std::unique_ptr<ProcessOverseer> CreateOverseer() override {
        // We need to duplicate the process handle for the overseer.
        HANDLE dup_handle;
        if (!DuplicateHandle(GetCurrentProcess(), process_.get(), GetCurrentProcess(), &dup_handle,
                             0, FALSE, DUPLICATE_SAME_ACCESS)) {
            return nullptr;
        }
        return std::make_unique<WindowsOverseer>(ScopedFileHandle(dup_handle), std::move(pipes_));
    }

  protected:
    std::optional<ProcessExitCode> GetExitCode() const override {
        if (!process_ || IsAlive()) {
            return std::nullopt;
        }
        DWORD exit = STILL_ACTIVE;
        DWORD n;

        // When we are poking another process than our own that just
        // terminated (i.e. not alive), it might not have yet updated
        // its exit status.
        GetExitCodeProcess(process_.get(), &exit);
        for (n = 0; n < 20 && exit == STILL_ACTIVE; n++) {
            std::this_thread::sleep_for(1ms);
            GetExitCodeProcess(process_.get(), &exit);
        }

        DD("Looped %d times", n);
        return exit;
    }

  private:
    ScopedFileHandle process_;
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
    if (process_handle) {
        return std::make_unique<WinProcess>(std::move(process_handle));
    }
    return nullptr;
}

std::vector<std::unique_ptr<Process>> Process::FromName(const std::string& name) {
    std::vector<std::unique_ptr<Process>> processes;

    ScopedFileHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) {
        return processes;
    }
    PROCESSENTRY32W process = {0};
    process.dwSize = sizeof(process);

    if (!Process32FirstW(snapshot.get(), &process)) {
        return processes;
    }
    do {
        if (Win32UnicodeString::convertToUtf8(process.szExeFile).find(name) !=
            std::string::npos) {  // NOLINT(bugprone-signed-char-arg)
            auto proc = FromPid(static_cast<Pid>(process.th32ProcessID));
            if (proc) {
                processes.push_back(std::move(proc));
            }
        }
    } while (Process32NextW(snapshot.get(), &process));

    return processes;
}

std::unique_ptr<Process> Process::Me() {
    return FromPid(static_cast<Pid>(GetCurrentProcessId()));
}

}  // namespace android::base
