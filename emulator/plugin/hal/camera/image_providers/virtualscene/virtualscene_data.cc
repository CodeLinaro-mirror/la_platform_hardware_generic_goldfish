/* Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "android/base/bazel_info.h"
#include "tiny_gltf.h"
#include "tiny_obj_loader.h"

TEST(virtualscene_data_is_parsable, Toren1BD) {
    tinyobj::ObjReader reader;

    EXPECT_TRUE(reader.ParseFromFile(::android::base::Bazel::RunfilesPath(
            "goldfish_prebuilts_common+/virtualscene/Toren1BD/Toren1BD.obj")));
}

TEST(virtualscene_data_is_parsable, LoadGltf) {
    const std::filesystem::path temp_gltf_path =
            std::filesystem::path(testing::TempDir()) / "test_mesh.gltf";
    {
        std::ofstream out(temp_gltf_path);
        out << R"({
  "asset": { "version": "2.0" },
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ {
    "primitives": [ {
      "attributes": { "POSITION": 0 },
      "indices": 1
    } ]
  } ],
  "accessors": [
    {
      "bufferView": 0,
      "componentType": 5126,
      "count": 3,
      "type": "VEC3",
      "max": [1.0, 1.0, 0.0],
      "min": [0.0, 0.0, 0.0]
    },
    {
      "bufferView": 1,
      "componentType": 5123,
      "count": 3,
      "type": "SCALAR"
    }
  ],
  "bufferViews": [
    {
      "buffer": 0,
      "byteOffset": 0,
      "byteLength": 36
    },
    {
      "buffer": 0,
      "byteOffset": 36,
      "byteLength": 6
    }
  ],
  "buffers": [
    {
      "byteLength": 42,
      "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA"
    }
  ]
})";
    }

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    EXPECT_TRUE(loader.LoadASCIIFromFile(&model, &err, &warn, temp_gltf_path.string()))
            << "err: " << err << ", warn: " << warn;
    EXPECT_TRUE(err.empty()) << err;
    EXPECT_TRUE(warn.empty()) << warn;
    EXPECT_EQ(model.meshes.size(), 1u);

    std::filesystem::remove(temp_gltf_path);
}
