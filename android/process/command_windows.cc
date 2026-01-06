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

namespace android {
namespace base {

using namespace std::chrono_literals;

namespace {
// Converts a std::string (utf-8) -> utf-16
std::wstring toWide(const std::string& str) {
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

std::string quoteParameter(const std::string& commandLine) {
    // Therefore, the function will return the length of str1 if none of the characters of str2 are
    // found in str1.
    if (::strcspn(commandLine.c_str(), " \t\v\n\"") == commandLine.size()) {
        return commandLine;
    }

    using namespace std::literals;

    // Otherwise, we need to quote some of the characters.
    std::string out = "\""s;

    for (const char c : commandLine) {
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
BOOL createNamedPipe(LPHANDLE lpReadPipe, LPHANDLE lpWritePipe,
                     LPSECURITY_ATTRIBUTES lpPipeAttributes, DWORD nSize, DWORD dwReadMode,
                     DWORD dwWriteMode) {
    HANDLE ReadPipeHandle;
    HANDLE WritePipeHandle;
    DWORD dwError;

    char PipeNameBuffer[MAX_PATH];
    static std::atomic_int counter(0);

    if ((dwReadMode | dwWriteMode) & (~FILE_FLAG_OVERLAPPED)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (nSize == 0) {
        nSize = 4096;
    }

    sprintf(PipeNameBuffer, R"(\\.\Pipe\android.%08x.%08x)", GetCurrentProcessId(), counter++);

    ReadPipeHandle = CreateNamedPipeA(PipeNameBuffer, PIPE_ACCESS_INBOUND | dwReadMode,
                                      PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_READMODE_BYTE,
                                      1,           // Number of mPipes
                                      nSize,       // Out buffer size
                                      nSize,       // In buffer size
                                      120 * 1000,  // Timeout in ms
                                      lpPipeAttributes);

    if (!ReadPipeHandle) {
        return FALSE;
    }

    WritePipeHandle =
            CreateFileA(PipeNameBuffer, GENERIC_WRITE,
                        0,  // No sharing
                        lpPipeAttributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | dwWriteMode,
                        NULL  // Template file
            );

    if (INVALID_HANDLE_VALUE == WritePipeHandle) {
        dwError = GetLastError();
        CloseHandle(ReadPipeHandle);
        SetLastError(dwError);
        return FALSE;
    }

    *lpReadPipe = ReadPipeHandle;
    *lpWritePipe = WritePipeHandle;
    return (TRUE);
}

#define BUFSIZE 4096

// Human readable string of the last error.
std::string formatLastErr() {
    DWORD error = GetLastError();
    if (error) {
        constexpr size_t max_str_len = 512;
        char lpMsgBuf[max_str_len];
        DWORD bufLen = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                      lpMsgBuf, max_str_len, nullptr);
        if (bufLen) {
            std::string result(lpMsgBuf, lpMsgBuf + bufLen);
            return result;
        }
    }
    return "";
}

// From https://devblogs.microsoft.com/oldnewthing/20111216-00/?p=8873
// Calls create process with explicitly inheriting the set of handles
// that are in rgHandlesToInherit.
// Note that you must poass bInheritHandles as true and
// make sure to set the individual handle to inherit.
BOOL CreateProcessWithExplicitHandles(
        __in_opt LPCWSTR lpApplicationName, __inout_opt LPWSTR lpCommandLine,
        __in_opt LPSECURITY_ATTRIBUTES lpProcessAttributes,
        __in_opt LPSECURITY_ATTRIBUTES lpThreadAttributes, __in BOOL bInheritHandles,
        __in DWORD dwCreationFlags, __in_opt LPVOID lpEnvironment,
        __in_opt LPCWSTR lpCurrentDirectory, __in LPSTARTUPINFOW lpStartupInfo,
        __out LPPROCESS_INFORMATION lpProcessInformation,
        // here is the new stuff
        __in DWORD cHandlesToInherit, __in_ecount(cHandlesToInherit) HANDLE* rgHandlesToInherit) {
    BOOL fSuccess;
    BOOL fInitialized = FALSE;
    SIZE_T size = 0;
    LPPROC_THREAD_ATTRIBUTE_LIST lpAttributeList = NULL;
    fSuccess = cHandlesToInherit < 0xFFFFFFFF / sizeof(HANDLE) &&
               lpStartupInfo->cb == sizeof(*lpStartupInfo);
    if (!fSuccess) {
        SetLastError(ERROR_INVALID_PARAMETER);
    }
    if (fSuccess) {
        fSuccess = InitializeProcThreadAttributeList(NULL, 1, 0, &size) ||
                   GetLastError() == ERROR_INSUFFICIENT_BUFFER;
    }
    if (fSuccess) {
        lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
                HeapAlloc(GetProcessHeap(), 0, size));
        fSuccess = lpAttributeList != NULL;
    }
    if (fSuccess) {
        fSuccess = InitializeProcThreadAttributeList(lpAttributeList, 1, 0, &size);
    }
    if (fSuccess) {
        fInitialized = TRUE;
        fSuccess = UpdateProcThreadAttribute(lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                             rgHandlesToInherit, cHandlesToInherit * sizeof(HANDLE),
                                             NULL, NULL);
    }
    if (fSuccess) {
        STARTUPINFOEXW info;
        ZeroMemory(&info, sizeof(info));
        info.StartupInfo = *lpStartupInfo;
        info.StartupInfo.cb = sizeof(info);
        info.lpAttributeList = lpAttributeList;

        DD("Creating proc.");
        fSuccess = CreateProcessW(lpApplicationName, lpCommandLine, lpProcessAttributes,
                                  lpThreadAttributes, bInheritHandles,
                                  dwCreationFlags | EXTENDED_STARTUPINFO_PRESENT, lpEnvironment,
                                  lpCurrentDirectory, &info.StartupInfo, lpProcessInformation);
    }
    if (fInitialized) DeleteProcThreadAttributeList(lpAttributeList);
    if (lpAttributeList) HeapFree(GetProcessHeap(), 0, lpAttributeList);
    return fSuccess;
}

struct WindwsPipe {
    ~WindwsPipe() {
        CloseHandle(oOverlap.hEvent);
        CloseHandle(hRead);
        CloseHandle(hWrite);
    }

    HANDLE event() const { return oOverlap.hEvent; }

    void close() {
        DD("Closing pipe.");
        fPendingIO = false;
        fClosed = true;
    }

    void flushToStreambuf() {
        buffer->sputn(chRead, cbRead);
        buffer->pubsync();
        fPendingIO = false;
    }

    OVERLAPPED oOverlap = {0};
    HANDLE hRead;
    HANDLE hWrite;
    CHAR chRead[BUFSIZE] = {0};
    DWORD cbRead = 0;
    BOOL fPendingIO = FALSE;
    BOOL fClosed = FALSE;
    std::basic_streambuf<char>* buffer = nullptr;
};
}  // namespace

class WindowsOverseer : public ProcessOverseer {
  public:
    WindowsOverseer(HANDLE process, std::vector<std::unique_ptr<WindwsPipe>> mPipes)
            : mProcess(process), mPipes(std::move(mPipes)) {}

    ~WindowsOverseer() override { DD("~WindowsOverseer"); }

    bool childIsAlive() { return WaitForSingleObject(mProcess, 0) == WAIT_TIMEOUT; }

    // Waits for a read finished event on any of the active pipes.
    // returns the pipe with the event or nullptr if there are no
    // events to wait for
    WindwsPipe* waitForPipeEvents() {
        std::vector<HANDLE> events;
        for (const auto& pipe : mPipes) {
            if (pipe->fPendingIO && !pipe->fClosed) {
                DD("-- Wait for %p", pipe->hRead);
                events.push_back(pipe->oOverlap.hEvent);
            }
        }

        DD("Waiting for %d events", events.size());
        if (events.empty()) return nullptr;

        DWORD dwWait = WaitForMultipleObjects(events.size(),  // number of event objects
                                              events.data(),  // array of event objects
                                              FALSE,          // does not wait for all
                                              INFINITE);      // waits indefinitely

        if (dwWait == WAIT_TIMEOUT || dwWait == WAIT_FAILED) {
            DD("Failed or abandoned. %s", formatLastErr().c_str());
            return nullptr;
        }
        // dwWait shows which pipe completed the operation.
        auto i = dwWait - WAIT_OBJECT_0;  // determines which pipe
        assert(i >= 0 && i < mPipes.size());
        ResetEvent(events[i]);

        for (const auto& pipe : mPipes) {
            if (pipe->event() == events[i]) return pipe.get();
        }
        assert(false);
        return nullptr;
    }

    void start(std::basic_streambuf<char>* out, std::basic_streambuf<char>* err) override {
        mPipes[0]->buffer = out;
        mPipes[1]->buffer = err;

        // A pipe can be:
        // - pending (we are waiting for a read complete)
        // - closed (no more events will come in)
        while (!mStop) {
            DWORD dwErr = 0;
            for (const auto& pipe : mPipes) {
                if (pipe->fClosed) {
                    DD("Skipping %p", pipe->hRead);
                    continue;
                }

                if (!pipe->fPendingIO) {
                    DWORD bytesToRead = BUFSIZE * sizeof(char);
                    BOOL fSuccess =
                            ReadFile(pipe->hRead, pipe->chRead, bytesToRead, NULL, &pipe->oOverlap);

                    DD("Readfile: (%p), %s, read: %d (%s)", pipe->hRead,
                       fSuccess ? "success" : "fail", pipe->cbRead, formatLastErr().c_str());

                    // The read might still be pending.
                    dwErr = GetLastError();
                    if (dwErr == ERROR_IO_PENDING) {
                        DD("I/O Pending");
                        pipe->fPendingIO = TRUE;
                    } else {
                        // Some other unknown error..
                        pipe->close();
                    }
                }
            }

            // Eventually all pipes move to the closed state
            // which will result in them not being scheduled for
            // events.
            auto* pipe = waitForPipeEvents();

            if (pipe == nullptr) {
                DD("No events!");
                mPipes.clear();
                return;
            }

            DD("Event for %p", pipe->hRead);
            if (pipe->fPendingIO) {
                DWORD cbRet = 0;
                bool fSuccess = GetOverlappedResult(pipe->hRead,      // handle to pipe
                                                    &pipe->oOverlap,  // OVERLAPPED structure
                                                    &cbRet,           // bytes transferred
                                                    FALSE);           // do not wait

                DD("Overlapped: %s, bytes available: %d, (%d:%s)", fSuccess ? "success" : "fail",
                   cbRet, GetLastError(), fSuccess ? "" : formatLastErr().c_str());

                if (GetLastError() == ERROR_BROKEN_PIPE) {
                    // remove this pipe..
                    pipe->close();
                }
                if (fSuccess) {
                    pipe->cbRead = cbRet;

                    pipe->flushToStreambuf();
                }
            }
        }

        // Close and destroy pipe objects.
        mPipes.clear();
    }

    // Cancel the observation of the process, no callbacks
    // should be invoked.
    // no writes to std_out, std_err should happen.
    void stop() override { mStop = true; };

  private:
    std::vector<std::unique_ptr<WindwsPipe>> mPipes;
    HANDLE mProcess;
    bool mStop{false};
};

class WinProcess : public ObservableProcess {
  public:
    ~WinProcess() override {
        if (!mDeamon && mOwner) WinProcess::terminate();

        if (mProcess != nullptr && mProcess != INVALID_HANDLE_VALUE) {
            CloseHandle(mProcInfo.hProcess);
        }
    }

    WinProcess(bool deamon, bool inherit) {
        mDeamon = deamon;
        mInherit = inherit;
    }

    explicit WinProcess(HANDLE process_handle) { setHandle(process_handle); }

    void setHandle(HANDLE hProcess) {
        mPid = static_cast<Pid>(GetProcessId(hProcess));
        mProcess = hProcess;
    }

    std::future_status wait_for_kernel(
            const std::chrono::milliseconds timeout_duration) const override {
        if (mProcess == INVALID_HANDLE_VALUE) {
            LOG(WARNING) << "Invalid process handle, assuming it is not running.";
            return std::future_status::ready;
        }

        auto state = WaitForSingleObject(mProcess, timeout_duration.count());
        if (state == WAIT_FAILED) {
            PLOG(ERROR) << "Failed to wait for process due to " << GetLastError();
        }
        return state == WAIT_TIMEOUT ? std::future_status::timeout : std::future_status::ready;
    }

    bool terminate() override {
        if (mProcess == INVALID_HANDLE_VALUE) return false;
        TerminateProcess(mProcess, 1);
        // 100ms to shut down.
        return WaitForSingleObject(mProcess, 100) != WAIT_TIMEOUT;
    }

    std::string exe() const override {
        std::string name(MAX_PATH, '\000');
        if (mProcess != INVALID_HANDLE_VALUE) {
            int size = GetModuleFileNameExA(mProcess, nullptr, name.data(), name.size());
            name.reserve(size + 1);
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                size = GetModuleFileNameExA(mProcess, nullptr, name.data(), size + 1);
                name.resize(size + 1, '\000');
            }
        }
        return name;
    }

    bool isAlive() const override {
        if (mProcess == INVALID_HANDLE_VALUE) return false;

        return WaitForSingleObject(mProcess, 0) == WAIT_TIMEOUT;
    }

    std::future_status wait_for(const std::chrono::milliseconds timeout_duration) const override {
        if (mProcess == INVALID_HANDLE_VALUE) return std::future_status::ready;

        auto state = WaitForSingleObject(mProcess, timeout_duration.count());
        return state == WAIT_TIMEOUT ? std::future_status::timeout : std::future_status::ready;
    }

    virtual std::optional<Pid> createProcess(const CommandArguments& args, bool captureOutput,
                                             bool replace) override {
        if (replace) {
            std::vector<std::string> quoted_arguments;
            quoted_arguments.reserve(args.size());
            for (const std::string& arg : args) {
                quoted_arguments.push_back(quoteParameter(arg));
            }

            std::vector<char*> cmdline;
            cmdline.reserve(args.size() + 1);
            for (std::string& arg : quoted_arguments) {
                cmdline.push_back(const_cast<char*>(arg.c_str()));
            }
            cmdline.push_back(nullptr);

            // The exec() functions only return if an error has occurred.
            safe_execv(cmdline[0], cmdline.data());
            return std::nullopt;
        }

        STARTUPINFOW siStartInfo = {.cb = sizeof(STARTUPINFOW)};
        if (captureOutput) {
            DD("Installing mPipes for stdout & stderr");
            // Setup named mPipes to stderr/stdout..
            // https://docs.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output
            SECURITY_ATTRIBUTES saAttr;
            saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
            saAttr.bInheritHandle = TRUE;
            saAttr.lpSecurityDescriptor = nullptr;

            for (int i = 0; i < 2; i++) {
                auto pipe = std::make_unique<WindwsPipe>();
                if (!createNamedPipe(&pipe->hRead, &pipe->hWrite, &saAttr, 0, FILE_FLAG_OVERLAPPED,
                                     FILE_FLAG_OVERLAPPED)) {
                    // ("Unable to create pipe: " + std::to_string(i));
                    return std::nullopt;
                }
                if (!SetHandleInformation(pipe->hRead, HANDLE_FLAG_INHERIT, 0)) return std::nullopt;
                // "SetHandleInformation failed for pipe: " +
                //         std::to_string(i));

                pipe->oOverlap.hEvent = CreateEvent(nullptr,   // default security attribute
                                                    TRUE,      // manual-reset event
                                                    TRUE,      // initial state = signaled
                                                    nullptr);  // unnamed event object

                if (pipe->oOverlap.hEvent == nullptr) {
                    return std::nullopt;
                }

                pipe->fPendingIO = FALSE;
                mPipes.push_back(std::move(pipe));
            }

            siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
            siStartInfo.hStdOutput = mPipes[0]->hWrite;
            siStartInfo.hStdError = mPipes[1]->hWrite;
        }

        // Setup the child process
        std::string cmdline;
        for (const auto& param : args) {
            cmdline += quoteParameter(param) + " ";
        }
        cmdline.pop_back();
        std::wstring wCmdline = toWide(cmdline);
        auto* szCmdline = const_cast<LPWSTR>(wCmdline.c_str());

        BOOL bSuccess;
        if (mInherit || mPipes.empty()) {
            DD("CreateProcessW(%s)", mInherit ? "Inherit handles" : "Do not inherit");
            bSuccess = ::CreateProcessW(nullptr,
                                        szCmdline,     // command line
                                        nullptr,       // process security attributes
                                        nullptr,       // primary thread security attributes
                                        mInherit,      // handles could be inherited
                                        0,             // creation flags
                                        nullptr,       // use parent's environment
                                        nullptr,       // use parent's current directory
                                        &siStartInfo,  // STARTUPINFO pointer
                                        &mProcInfo);   // receives PROCESS_INFORMATION
        } else {
            assert(!mInherit);
            // We explicitly inherit our pipes.
            std::vector<HANDLE> handles{mPipes[0]->hWrite, mPipes[1]->hWrite};
            bSuccess =
                    CreateProcessWithExplicitHandles(nullptr,
                                                     szCmdline,  // command line
                                                     nullptr,    // process security attributes
                                                     nullptr,  // primary thread security attributes
                                                     TRUE,  // handles will be explicitly inherited
                                                     0,     // creation flags
                                                     nullptr,  // use parent's environment
                                                     nullptr,  // use parent's current directory
                                                     &siStartInfo,  // STARTUPINFO pointer
                                                     &mProcInfo, handles.size(), handles.data());
        }
        DD("Create process: %d, %s", bSuccess, formatLastErr().c_str());

        // If an error occurs, exit the application.
        if (!bSuccess) {
            DD("Create process failed: %s", formatLastErr().c_str());
            return std::nullopt;
        }

        // Close handles to the  mPipes no longer needed by the child
        // process. If they are not explicitly closed, there is no way
        // to recognize that the child process has ended.
        for (const auto& pipe : mPipes) {
            CloseHandle(pipe->hWrite);
            pipe->hWrite = INVALID_HANDLE_VALUE;
        }
        CloseHandle(mProcInfo.hThread);
        mProcInfo.hThread = INVALID_HANDLE_VALUE;

        std::unique_ptr<ProcessOverseer> overseer;

        setHandle(mProcInfo.hProcess);
        mOwner = true;
        return mPid;
    }

    std::unique_ptr<ProcessOverseer> createOverseer() override {
        return std::make_unique<WindowsOverseer>(mProcInfo.hProcess, std::move(mPipes));
    }

  protected:
    std::optional<ProcessExitCode> getExitCode() const override {
        if (mProcess == INVALID_HANDLE_VALUE || isAlive()) {
            return std::nullopt;
        }
        DWORD exit = STILL_ACTIVE;
        DWORD n;

        // When we are poking another process than our own that just
        // terminated (i.e. not alive), it might not have yet updated
        // its exit status.
        GetExitCodeProcess(mProcess, &exit);
        for (n = 0; n < 20 && exit == STILL_ACTIVE; n++) {
            std::this_thread::sleep_for(1ms);
            GetExitCodeProcess(mProcess, &exit);
        }

        DD("Looped %d times", n);
        return exit;
    }

  private:
    HANDLE mProcess;
    PROCESS_INFORMATION mProcInfo = {.hProcess = INVALID_HANDLE_VALUE,
                                     .hThread = INVALID_HANDLE_VALUE};
    bool mOwner{false};
    std::vector<std::unique_ptr<WindwsPipe>> mPipes;
};

Command::ProcessFactory Command::sProcessFactory = [](const CommandArguments& /* args */,
                                                      bool deamon, bool inherit) {
    return std::make_unique<WinProcess>(deamon, inherit);
};

std::unique_ptr<Process> Process::fromPid(Pid pid) {
    ScopedFileHandle process_handle(OpenProcess(
            PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, false, pid));
    if (process_handle.valid()) {
        return std::make_unique<WinProcess>(process_handle.release());
    }
    return nullptr;
}

std::vector<std::unique_ptr<Process>> Process::fromName(std::string name) {
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
            processes.push_back(fromPid(static_cast<Pid>(process.th32ProcessID)));
        }
    } while (Process32NextW(snapshot, &process));

    CloseHandle(snapshot);
    return processes;
}

std::unique_ptr<Process> Process::me() {
    return fromPid(static_cast<Pid>(GetCurrentProcessId()));
}

}  // namespace base
}  // namespace android
