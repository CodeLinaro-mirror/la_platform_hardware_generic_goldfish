// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/aquarium/aquarium_ui_server.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "goldfish/http/http_response.h"

namespace fs = std::filesystem;

namespace goldfish::aquarium {
namespace {

TEST(AquariumUiServerTest, GetMimeTypeDetectsStandardWebFormats) {
    EXPECT_EQ(GetMimeType("index.html"), "text/html; charset=utf-8");
    EXPECT_EQ(GetMimeType("app.js"), "application/javascript; charset=utf-8");
    EXPECT_EQ(GetMimeType("module.mjs"), "application/javascript; charset=utf-8");
    EXPECT_EQ(GetMimeType("bundle.ts"), "application/javascript; charset=utf-8");
    EXPECT_EQ(GetMimeType("component.tsx"), "application/javascript; charset=utf-8");
    EXPECT_EQ(GetMimeType("component.jsx"), "application/javascript; charset=utf-8");
    EXPECT_EQ(GetMimeType("styles.css"), "text/css; charset=utf-8");
    EXPECT_EQ(GetMimeType("data.json"), "application/json; charset=utf-8");
    EXPECT_EQ(GetMimeType("bundle.js.map"), "application/json; charset=utf-8");
    EXPECT_EQ(GetMimeType("icon.svg"), "image/svg+xml");
    EXPECT_EQ(GetMimeType("photo.png"), "image/png");
    EXPECT_EQ(GetMimeType("photo.jpg"), "image/jpeg");
    EXPECT_EQ(GetMimeType("photo.jpeg"), "image/jpeg");
    EXPECT_EQ(GetMimeType("favicon.ico"), "image/x-icon");
    EXPECT_EQ(GetMimeType("module.wasm"), "application/wasm");
    EXPECT_EQ(GetMimeType("unknown.bin"), "application/octet-stream");
}

class ServeStaticFileTest : public ::testing::Test {
  protected:
    void SetUp() override {
        temp_dir_ = fs::temp_directory_path() / ("aquarium_test_" + std::to_string(std::rand()));
        fs::create_directories(temp_dir_);
        fs::create_directories(temp_dir_ / "assets");

        // Write index.html
        std::ofstream index_file(temp_dir_ / "index.html");
        index_file << "<!DOCTYPE html><html><body>Aquarium Test</body></html>";
        index_file.close();

        // Write sample asset
        std::ofstream js_file(temp_dir_ / "assets" / "app.js");
        js_file << "console.log(\"aquarium\");";
        js_file.close();
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    fs::path temp_dir_;
};

TEST_F(ServeStaticFileTest, ServesRootAsIndexHtml) {
    auto res = ServeStaticFile(temp_dir_, "/");
    EXPECT_EQ(res.Status(), goldfish::http::HttpStatus::kOk);
    EXPECT_NE(res.Headers().find("content-type"), res.Headers().end());
    EXPECT_EQ(res.Headers().at("content-type"), "text/html; charset=utf-8");
}

TEST_F(ServeStaticFileTest, ServesExistingAsset) {
    auto res = ServeStaticFile(temp_dir_, "/assets/app.js");
    EXPECT_EQ(res.Status(), goldfish::http::HttpStatus::kOk);
    EXPECT_NE(res.Headers().find("content-type"), res.Headers().end());
    EXPECT_EQ(res.Headers().at("content-type"), "application/javascript; charset=utf-8");
}

TEST_F(ServeStaticFileTest, FallbackToIndexHtmlForSpaRouting) {
    auto res = ServeStaticFile(temp_dir_, "/devices/emulator-5554/display");
    EXPECT_EQ(res.Status(), goldfish::http::HttpStatus::kOk);
    EXPECT_EQ(res.Headers().at("content-type"), "text/html; charset=utf-8");
}

TEST_F(ServeStaticFileTest, MissingAssetReturns404InsteadOfHtmlFallback) {
    auto res = ServeStaticFile(temp_dir_, "/assets/nonexistent.js");
    EXPECT_EQ(res.Status(), goldfish::http::HttpStatus::kNotFound);
}

TEST_F(ServeStaticFileTest, PreventsPathTraversal) {
    auto res = ServeStaticFile(temp_dir_, "/../../../../etc/passwd");
    // Should fallback to index.html within root or 404, never expose outside
    EXPECT_EQ(res.Status(), goldfish::http::HttpStatus::kOk);
    EXPECT_EQ(res.Headers().at("content-type"), "text/html; charset=utf-8");
}

TEST(AquariumUiServerTest, LocateStaticDirReturnsCustomDirIfValid) {
    auto tmp = fs::temp_directory_path();
    EXPECT_EQ(LocateStaticDir(tmp.string()), fs::canonical(tmp));
}

}  // namespace
}  // namespace goldfish::aquarium
