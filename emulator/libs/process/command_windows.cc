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
#if defined(UNICODE) || defined(_UNICODE)
#error "This file does not support UNICODE builds. It must be compiled with the ANSI/UTF-8 codepage."
#endif
#include <windows.h>

// Process entry
#include <Tlhelp32.h>
#include <psapi.h>
// IWYU pragma: end_keep
// clang-format on

#include <ios>
#include <streambuf>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"

#include "android/base/scoped_file_handle.h"
#include "android/process/command.h"
#include "exec.h"

namespace android::base {

using namespace std::chrono_literals;

namespace {

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
            // Remove trailing newlines from FormatMessage
            while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
                result.pop_back();
            }
            return std::to_string(error) + ": " + result;
        }
        return std::to_string(error);
    }
    return "0: Success";
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
        LOG(ERROR) << "Unable to establish communication channel with child process (server side) "
                   << "at " << pipe_name_buffer << ". This might be due to system resource "
                   << "limits. (Error: " << FormatLastErr() << ")";
        return FALSE;
    }

    ScopedFileHandle write_pipe_handle(CreateFileA(pipe_name_buffer, GENERIC_WRITE,
                                                   0,  // No sharing
                                                   pipe_attributes, OPEN_EXISTING,
                                                   FILE_ATTRIBUTE_NORMAL | write_mode,
                                                   nullptr  // Template file
                                                   ));

    if (!write_pipe_handle) {
        LOG(ERROR) << "Unable to establish communication channel with child process (client side) "
                   << "at " << pipe_name_buffer << ". This might be due to security software "
                   << "or system limits. (Error: " << FormatLastErr() << ")";
        return FALSE;
    }
    *read_pipe = read_pipe_handle.release();
    *write_pipe = write_pipe_handle.release();
    return TRUE;
}

#define BUFSIZE 4096

// From https://devblogs.microsoft.com/oldnewthing/20111216-00/?p=8873
// Calls create process with explicitly inheriting the set of handles
// that are in rgHandlesToInherit.
// Note that you must poass inherit_handles as true and
// make sure to set the individual handle to inherit.
BOOL CreateProcessWithExplicitHandles(LPCSTR application_name, LPSTR command_line,
                                      LPSECURITY_ATTRIBUTES process_attributes,
                                      LPSECURITY_ATTRIBUTES thread_attributes, BOOL inherit_handles,
                                      DWORD creation_flags, LPVOID environment,
                                      LPCSTR current_directory, LPSTARTUPINFOA startup_info,
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
        STARTUPINFOEXA info;
        ZeroMemory(&info, sizeof(info));
        info.StartupInfo = *startup_info;
        info.StartupInfo.cb = sizeof(info);
        info.lpAttributeList = attribute_list;

        DVLOG(1) << "Creating process with explicit handles: "
                 << (command_line ? command_line : "null");
        success = CreateProcessA(application_name, command_line, process_attributes,
                                 thread_attributes, inherit_handles,
                                 creation_flags | EXTENDED_STARTUPINFO_PRESENT, environment,
                                 current_directory, &info.StartupInfo, process_information);
    }
    if (initialized) DeleteProcThreadAttributeList(attribute_list);
    if (attribute_list) HeapFree(GetProcessHeap(), 0, attribute_list);
    return success;
}

struct WindowsPipe {
    HANDLE Event() const { return overlap_event.get(); }

    void Close() {
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
    WindowsOverseer(ScopedFileHandle process, std::vector<std::unique_ptr<WindowsPipe>> pipes)
            : pipes_(std::move(pipes)), process_(std::move(process)) {
        stop_event_.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
    }

    ~WindowsOverseer() override {
        DVLOG(1) << "~WindowsOverseer for process handle " << process_.get();
    }

    bool ChildIsAlive() {
        return process_ && WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT;
    }

    // Waits for a read finished event on any of the active pipes or the stop event.
    // returns the pipe with the event or nullptr if there are no
    // events to wait for
    WindowsPipe* WaitForPipeEvents() {
        std::vector<HANDLE> events;
        std::vector<WindowsPipe*> event_pipes;
        for (const auto& pipe : pipes_) {
            if (pipe->pending_io && !pipe->closed) {
                events.push_back(pipe->overlap_event.get());
                event_pipes.push_back(pipe.get());
            }
        }

        if (events.empty()) return nullptr;

        events.push_back(stop_event_.get());

        DWORD wait = WaitForMultipleObjects(events.size(),  // number of event objects
                                            events.data(),  // array of event objects
                                            FALSE,          // does not wait for all
                                            INFINITE);      // waits indefinitely

        if (wait == WAIT_FAILED || wait == WAIT_OBJECT_0 + events.size() - 1) {
            if (wait == WAIT_FAILED) {
                LOG(ERROR) << "An error occurred while monitoring child process output. "
                           << "Communication may be interrupted. (Error: " << FormatLastErr()
                           << ")";
            }
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

        DVLOG(1) << "Starting WindowsOverseer I/O loop.";

        // A pipe can be:
        // - pending (we are waiting for a read complete)
        // - closed (no more events will come in)
        while (!stop_) {
            for (const auto& pipe : pipes_) {
                if (pipe->closed) {
                    continue;
                }

                if (!pipe->pending_io) {
                    DWORD bytes_to_read = BUFSIZE * sizeof(char);
                    BOOL success = ReadFile(pipe->read.get(), pipe->ch_read, bytes_to_read, nullptr,
                                            &pipe->overlap);

                    // The read might still be pending.
                    if (success) {
                        // Read completed immediately.
                        if (GetOverlappedResult(pipe->read.get(), &pipe->overlap, &pipe->cb_read,
                                                FALSE)) {
                            pipe->FlushToStreambuf();
                        }
                    } else if (GetLastError() == ERROR_IO_PENDING) {
                        pipe->pending_io = TRUE;
                    } else {
                        // Some other unknown error..
                        DVLOG(1) << "ReadFile failed: " << FormatLastErr()
                                 << ". Marking pipe as closed.";
                        pipe->Close();
                    }
                }
            }

            // Eventually all pipes move to the closed state
            // which will result in them not being scheduled for
            // events.
            auto* pipe = WaitForPipeEvents();

            if (pipe == nullptr) {
                DVLOG(1) << "Overseer I/O loop terminating (no more events or stopped).";
                pipes_.clear();
                return;
            }

            if (pipe->pending_io) {
                DWORD byte_count = 0;
                bool success = GetOverlappedResult(pipe->read.get(),  // handle to pipe
                                                   &pipe->overlap,    // OVERLAPPED structure
                                                   &byte_count,       // bytes transferred
                                                   FALSE);            // do not wait

                if (!success && GetLastError() == ERROR_BROKEN_PIPE) {
                    // remove this pipe..
                    DVLOG(1) << "Pipe " << pipe->read.get()
                             << " broken (process likely exited). Closing.";
                    pipe->Close();
                } else if (success) {
                    pipe->cb_read = byte_count;
                    pipe->FlushToStreambuf();
                }
            }
        }

        DVLOG(1) << "Overseer I/O loop stopped.";
        // Close and destroy pipe objects.
        pipes_.clear();
    }

    // Cancel the observation of the process, no callbacks
    // should be invoked.
    // no writes to std_out, std_err should happen.
    void Stop() override {
        DVLOG(1) << "Signaling WindowsOverseer to stop.";
        stop_ = true;
        SetEvent(stop_event_.get());
    };

  private:
    std::vector<std::unique_ptr<WindowsPipe>> pipes_;
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
            return std::future_status::ready;
        }

        auto state = WaitForSingleObject(process_.get(), timeout_duration.count());
        if (state == WAIT_FAILED) {
            PLOG(ERROR) << "Timed out or encountered an error while waiting for the process "
                        << "to respond or exit";
        }
        return state == WAIT_TIMEOUT ? std::future_status::timeout : std::future_status::ready;
    }

    bool Terminate() override {
        if (!process_) return false;
        DVLOG(1) << "Terminating process PID: " << pid_;
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
                name.resize(32767);
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

            DVLOG(1) << "Replacing current process with: " << cmdline[0];
            // The exec() functions only return if an error has occurred.
            SafeExecv(cmdline[0], cmdline.data());
            LOG(ERROR) << "Failed to replace current process with: " << cmdline[0];
            return std::nullopt;
        }

        STARTUPINFOA startup_info = {.cb = sizeof(STARTUPINFOA)};
        if (capture_output) {
            // Setup named pipes to stderr/stdout..
            // https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output
            SECURITY_ATTRIBUTES security_attributes;
            security_attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
            security_attributes.bInheritHandle = TRUE;
            security_attributes.lpSecurityDescriptor = nullptr;

            for (int i = 0; i < 2; i++) {
                auto pipe = std::make_unique<WindowsPipe>();
                HANDLE read_handle;
                HANDLE write_handle;
                if (!CreateNamedPipe(&read_handle, &write_handle, &security_attributes, 0,
                                     FILE_FLAG_OVERLAPPED, FILE_FLAG_OVERLAPPED)) {
                    LOG(ERROR) << "Unable to prepare output redirection for the new process "
                               << "(channel " << i
                               << "). Output might be lost. (Error: " << FormatLastErr() << ")";
                    return std::nullopt;
                }
                pipe->read.reset(read_handle);
                pipe->write.reset(write_handle);

                if (!SetHandleInformation(pipe->read.get(), HANDLE_FLAG_INHERIT, 0)) {
                    LOG(ERROR) << "Unable to share communication handles with the new process. "
                               << "This prevents the emulator from capturing its output. "
                               << "(Error: " << FormatLastErr() << ")";
                    return std::nullopt;
                }

                pipe->overlap_event.reset(CreateEvent(nullptr,    // default security attribute
                                                      TRUE,       // manual-reset event
                                                      TRUE,       // initial state = signaled
                                                      nullptr));  // unnamed event object

                if (!pipe->overlap_event) {
                    LOG(ERROR) << "Unable to initialize asynchronous monitoring for process "
                               << "output. (Error: " << FormatLastErr() << ")";
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
        auto* sz_command_line = const_cast<LPSTR>(cmdline.c_str());

        BOOL success;
        PROCESS_INFORMATION proc_info = {0};
        if (inherit_ || pipes_.empty()) {
            DVLOG(1) << "Launching: " << cmdline << " (Inherit: " << (inherit_ ? "True" : "False")
                     << ")";
            success = ::CreateProcessA(nullptr,
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
            DCHECK(!inherit_);
            // We explicitly inherit our pipes.
            std::vector<HANDLE> handles{pipes_[0]->write.get(), pipes_[1]->write.get()};
            DVLOG(1) << "Launching (explicit handles): " << cmdline;
            success = CreateProcessWithExplicitHandles(
                    nullptr,
                    sz_command_line,  // command line
                    nullptr,          // process security attributes
                    nullptr,          // primary thread security attributes
                    TRUE,             // handles will be explicitly inherited
                    0,                // creation flags
                    nullptr,          // use parent's environment
                    nullptr,          // use parent's current directory
                    &startup_info,    // STARTUPINFO pointer
                    &proc_info, (DWORD)handles.size(), handles.data());
        }

        if (!success) {
            LOG(ERROR) << "Failed to start the process: " << cmdline << ". Please check if "
                       << "the path is correct and you have sufficient permissions. (Error: "
                       << FormatLastErr() << ")";
            return std::nullopt;
        }

        DVLOG(1) << "Successfully launched process. PID: " << proc_info.dwProcessId;

        // Close handles to the pipes no longer needed by the child
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
            LOG(ERROR) << "Unable to monitor the new process because its internal handle "
                       << "could not be duplicated. (Error: " << FormatLastErr() << ")";
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

        DVLOG(1) << "Process PID: " << pid_ << " exited with status " << exit
                 << " (Retrieved after " << n << " poll attempts)";
        return (ProcessExitCode)exit;
    }

  private:
    ScopedFileHandle process_;
    bool owner_{false};
    std::vector<std::unique_ptr<WindowsPipe>> pipes_;
};

Command::ProcessFactory Command::s_process_factory = [](const CommandArguments& /* args */,
                                                        bool daemon, bool inherit) {
    return std::make_unique<WinProcess>(daemon, inherit);
};

std::unique_ptr<Process> Process::FromPid(Pid pid) {
    ScopedFileHandle process_handle(
            OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, false,
                        (DWORD)pid));
    if (process_handle) {
        return std::make_unique<WinProcess>(std::move(process_handle));
    }
    return nullptr;
}

std::vector<std::unique_ptr<Process>> Process::FromName(const std::string& name) {
    std::vector<std::unique_ptr<Process>> processes;

    ScopedFileHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot) {
        LOG(ERROR) << "Unable to scan the list of running processes to find \"" << name
                   << "\". (Error: " << FormatLastErr() << ")";
        return processes;
    }
    PROCESSENTRY32 process = {0};
    process.dwSize = sizeof(process);

    if (!Process32First(snapshot.get(), &process)) {
        LOG(ERROR) << "Unable to read process information while searching for \"" << name
                   << "\". (Error: " << FormatLastErr() << ")";
        return processes;
    }
    do {
        if (absl::StrContains(process.szExeFile, name)) {
            auto proc = FromPid(static_cast<Pid>(process.th32ProcessID));
            if (proc) {
                processes.push_back(std::move(proc));
            }
        }
    } while (Process32Next(snapshot.get(), &process));

    return processes;
}

std::unique_ptr<Process> Process::Me() {
    return FromPid(static_cast<Pid>(GetCurrentProcessId()));
}

}  // namespace android::base
