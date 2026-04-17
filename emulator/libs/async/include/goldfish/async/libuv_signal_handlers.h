// Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/signal_handlers.h"
#include "goldfish/async/uv_to_absl.h"
#include "uv.h"

namespace goldfish::async {

class UvSignalHandlers : public SignalHandlers {
  public:
    UvSignalHandlers(LibuvEventLoop& uv_loop, Callback signal_cb = nullptr)
            : mUvLoop(uv_loop)
            , mSignalCallback(std::move(signal_cb))
#ifdef _WIN32
            , mSignalHandlerBreak(uv_loop, SIGBREAK, this, signal_handler)
#endif
            , mSignalHandlerHup(uv_loop, SIGHUP, this, signal_handler)
            , mSignalHandlerInt(uv_loop, SIGINT, this, signal_handler)
            , mSignalHandlerQuit(uv_loop, SIGQUIT, this, signal_handler)
            , mSignalHandlerTerm(uv_loop, SIGTERM, this, signal_handler) {
    }

    ~UvSignalHandlers() override { close(); }

    void SetCallback(Callback signal_cb) override {
        mSignalCallback = std::move(signal_cb);
    }

    Callback GetCallback() const {
        // copy
        return mSignalCallback;
    }

    void close() override {
        if (mClosed) {
            return;
        }
        LOG_IF(FATAL, mUvLoop.GetState() != LooperStatusEvent::State::kRunning)
                << "event loop is not running but signal handlers are being closed";
        mUvLoop.PostAndWait([this] {
#ifdef _WIN32
                   mSignalHandlerBreak.close();
#endif
                   mSignalHandlerHup.close();
                   mSignalHandlerInt.close();
                   mSignalHandlerQuit.close();
                   mSignalHandlerTerm.close();
               })
                .IgnoreError();

#ifdef _WIN32
        mSignalHandlerBreak.waitForClosed();
#endif
        mSignalHandlerHup.waitForClosed();
        mSignalHandlerInt.waitForClosed();
        mSignalHandlerQuit.waitForClosed();
        mSignalHandlerTerm.waitForClosed();
        mClosed = true;
    }

  private:
    static void signal_handler(uv_signal_t* handle, int signum) {
        auto* self = static_cast<UvSignalHandlers*>(handle->data);

        if (auto callback = self->GetCallback(); callback) {
            callback(signum);
        }
    }

    class UvSignalHandler {
      public:
        UvSignalHandler(LibuvEventLoop& uv_loop, int signal, void* data, uv_signal_cb signal_cb) {
            if (int res = uv_signal_init(static_cast<uv_loop_t*>(uv_loop.GetRawLoop()),
                                         &mSignalHandler);
                res < 0) {
                LOG(FATAL) << "couldn't create signal handler: " << UvErrToAbslStatus(res);
            }
            mSignalHandler.data = data;
            if (int res = uv_signal_start(&mSignalHandler, signal_cb, signal); res < 0) {
                LOG(FATAL) << "couldn't start signal handler: " << UvErrToAbslStatus(res);
            }
        }

        void close() {
            if (mClosedNotification.HasBeenNotified()) {
                return;
            }

            if (int res = uv_signal_stop(&mSignalHandler); res < 0) {
                LOG(ERROR) << "couldn't stop signal handler: " << UvErrToAbslStatus(res);
            }

            mSignalHandler.data = this;
            uv_close((uv_handle_t*)&mSignalHandler, close_cb);
        }

        void waitForClosed() { mClosedNotification.WaitForNotification(); }

      private:
        static void close_cb(uv_handle_t* handle) {
            auto* self = static_cast<UvSignalHandler*>(handle->data);
            self->mClosedNotification.Notify();
        }

        uv_signal_t mSignalHandler;
        absl::Notification mClosedNotification;
    };

    LibuvEventLoop& mUvLoop;
    Callback mSignalCallback;

#ifdef _WIN32
    UvSignalHandler mSignalHandlerBreak;
#endif
    UvSignalHandler mSignalHandlerHup;
    UvSignalHandler mSignalHandlerInt;
    UvSignalHandler mSignalHandlerQuit;
    UvSignalHandler mSignalHandlerTerm;
    bool mClosed = false;
};

}  // namespace goldfish::async
