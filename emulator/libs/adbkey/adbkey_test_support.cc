// Copyright 2026 The Android Open Source Project
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

#include "goldfish/adb/testing/adbkey_test_support.h"

#include <openssl/base.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <ios>
#include <memory>
#include <optional>
#include <streambuf>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

#include "android/process/command.h"
#include "goldfish/adb/adbkey.h"
#include "goldfish/file/file.h"
#include "goldfish/file/file_atomic.h"

namespace goldfish::adb::testing {
namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
constexpr std::string_view kAdbExeName = "adb.exe";
#else
constexpr std::string_view kAdbExeName = "adb";
#endif

class FakeStringOverseer : public android::base::NullOverseer {
  public:
    FakeStringOverseer(std::string out, std::string err)
            : out_(std::move(out)), err_(std::move(err)) {}

    void Start(std::basic_streambuf<char>* out, std::basic_streambuf<char>* err) override {
        if (out != nullptr && !out_.empty()) {
            out->sputn(out_.data(), static_cast<std::streamsize>(out_.size()));
        }
        if (err != nullptr && !err_.empty()) {
            err->sputn(err_.data(), static_cast<std::streamsize>(err_.size()));
        }
    }

  private:
    std::string out_;
    std::string err_;
};

class FakeCallbackProcess : public android::base::ObservableProcess {
  public:
    explicit FakeCallbackProcess(ScopedTestProcessFactory::Handler handler)
            : handler_(std::move(handler)) {}

    std::string Exe() const override { return exe_; }
    bool IsAlive() const override { return false; }
    bool Terminate() override { return true; }

    std::future_status WaitForKernel(
            const std::chrono::milliseconds /*timeout_duration*/) const override {
        return std::future_status::ready;
    }

    std::optional<android::base::ProcessExitCode> GetExitCode() const override {
        return exit_code_;
    }

    std::optional<android::base::Pid> CreateProcess(const android::base::CommandArguments& args,
                                                    bool /*capture_output*/,
                                                    bool /*replace*/) override {
        if (!args.empty()) {
            exe_ = args.front();
        }
        exit_code_ = handler_(args, &stdout_text_, &stderr_text_);
        return 4242;
    }

    std::unique_ptr<android::base::ProcessOverseer> CreateOverseer() override {
        return std::make_unique<FakeStringOverseer>(stdout_text_, stderr_text_);
    }

  private:
    ScopedTestProcessFactory::Handler handler_;
    std::string exe_;
    std::string stdout_text_;
    std::string stderr_text_;
    android::base::ProcessExitCode exit_code_ = 0;
};

}  // namespace

bool GenerateTestAdbPrivateKey(const fs::path& file) {
    const bssl::UniquePtr<BIGNUM> exponent(BN_new());
    if (!exponent || !BN_set_word(exponent.get(), RSA_F4)) {
        LOG(WARNING) << "Failed to allocate RSA exponent";
        return false;
    }

    const bssl::UniquePtr<RSA> rsa(RSA_new());
    if (!rsa || !RSA_generate_key_ex(rsa.get(), 2048, exponent.get(), nullptr)) {
        LOG(WARNING) << "Failed to generate RSA key";
        return false;
    }

    const bssl::UniquePtr<EVP_PKEY> pkey(EVP_PKEY_new());
    if (!pkey || !EVP_PKEY_set1_RSA(pkey.get(), rsa.get())) {
        LOG(WARNING) << "Failed to wrap RSA key in EVP_PKEY";
        return false;
    }

    const bssl::UniquePtr<BIO> bio(BIO_new(BIO_s_mem()));
    if (!bio ||
        !PEM_write_bio_PrivateKey(bio.get(), pkey.get(), nullptr, nullptr, 0, nullptr, nullptr)) {
        LOG(WARNING) << "Failed to serialize private key to PEM";
        return false;
    }

    const uint8_t* pem_data = nullptr;
    size_t pem_len = 0;
    if (!BIO_mem_contents(bio.get(), &pem_data, &pem_len)) {
        LOG(WARNING) << "Failed to read PEM BIO buffer";
        return false;
    }

    android::base::file::rm(file).IgnoreError();
    const std::string_view pem_view(reinterpret_cast<const char*>(pem_data), pem_len);
    if (const absl::Status s = android::base::file::CreatePrivateFileExclusive(file, pem_view);
        !s.ok()) {
        LOG(WARNING) << "Failed to write private key to " << file << ": " << s;
        return false;
    }
    return true;
}

fs::path CreateStubAdbExecutable(const fs::path& dir) {
    android::base::file::mkdir_recursive(dir, 0755).IgnoreError();
    const fs::path adb_path = dir / kAdbExeName;
    {
        std::ofstream out(adb_path, std::ios::out | std::ios::trunc);
        out << "stub";
    }
    android::base::file::chmod(adb_path, 0755).IgnoreError();
    return adb_path;
}

ScopedTestProcessFactory::ScopedTestProcessFactory(Handler handler) {
    android::base::Command::SetTestProcessFactory(
            [h = std::move(handler)](const android::base::CommandArguments& /*args*/,
                                     bool /*daemon*/, bool /*inherit*/) {
                return std::make_unique<FakeCallbackProcess>(h);
            });
}

ScopedTestProcessFactory::~ScopedTestProcessFactory() {
    android::base::Command::SetTestProcessFactory(nullptr);
}

ScopedFakeAdbKeygen::ScopedFakeAdbKeygen(std::chrono::milliseconds delay)
        : factory_([this, delay](const android::base::CommandArguments& args,
                                 std::string* /*std_out*/,
                                 std::string* std_err) -> android::base::ProcessExitCode {
            invocations_.fetch_add(1, std::memory_order_acq_rel);
            {
                absl::MutexLock lock(mu_);
                last_args_ = args;
            }
            if (delay > std::chrono::milliseconds::zero()) {
                std::this_thread::sleep_for(delay);
            }
            if (args.size() != 3 || args[1] != "keygen") {
                *std_err = "unexpected arguments";
                return 1;
            }
            const fs::path target_priv(args[2]);
            if (!GenerateTestAdbPrivateKey(target_priv)) {
                *std_err = "keygen failed";
                return 1;
            }
            std::string pub;
            if (!PubkeyFromPrivkey(target_priv, &pub)) {
                *std_err = "pubkey derivation failed";
                return 1;
            }
            fs::path target_pub = target_priv;
            target_pub += ".pub";
            std::ofstream(target_pub, std::ios::binary | std::ios::trunc) << pub;
            return 0;
        }) {}

android::base::CommandArguments ScopedFakeAdbKeygen::last_args() const {
    absl::MutexLock lock(mu_);
    return last_args_;
}

}  // namespace goldfish::adb::testing
