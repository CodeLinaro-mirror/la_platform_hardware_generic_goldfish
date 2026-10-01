import os
import subprocess
import tempfile

MESH_VERT = """#version 450
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 vUV;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float posterMask;
    vec2 minUV;
    vec2 scaleUV;
} pc;

void main() {
    vUV = uv;
    gl_Position = pc.mvp * vec4(pos, 1.0);
}
"""

MESH_FRAG = """#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D baseSampler;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float posterMask;
    vec2 minUV;
    vec2 scaleUV;
} pc;

void main() {
    vec2 newUV = (vUV - pc.minUV) / pc.scaleUV;
    float mask = pc.posterMask;
    if (newUV.x < 0.0 || newUV.x > 1.0 || newUV.y < 0.0 || newUV.y > 1.0) {
        mask = 0.0;
    }
    if (mask <= 0.0) {
        discard;
    }
    outColor = texture(baseSampler, newUV);
}
"""

SKYBOX_VERT = """#version 450
layout(location = 0) in vec3 pos;
layout(location = 0) out vec3 TexCoords;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float posterMask;
    vec2 minUV;
    vec2 scaleUV;
} pc;

void main() {
    TexCoords = pos;
    vec4 p = pc.mvp * vec4(pos, 1.0);
    gl_Position = p.xyww;
}
"""

SKYBOX_FRAG = """#version 450
layout(location = 0) in vec3 TexCoords;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube skybox;

void main() {
    outColor = texture(skybox, TexCoords);
}
"""


def compile_shader(source, stage):
  with tempfile.NamedTemporaryFile(
      suffix=f".{stage}", mode="w", delete=False
  ) as f:
    f.write(source)
    src_path = f.name
  spv_path = src_path + ".spv"

  cmd = ["glslangValidator", "-V", src_path, "-o", spv_path]
  subprocess.check_call(cmd)

  with open(spv_path, "rb") as f:
    data = f.read()

  os.unlink(src_path)
  os.unlink(spv_path)
  return data


def to_c_array(name, data):
  # Convert bytes to uint32_t array
  words = []
  for i in range(0, len(data), 4):
    word = int.from_bytes(data[i : i + 4], byteorder="little")
    words.append(f"0x{word:08x}")

  lines = []
  for i in range(0, len(words), 8):
    lines.append("    " + ", ".join(words[i : i + 8]) + ",")

  return f"static const uint32_t {name}[] = {{\n" + "\n".join(lines) + f"\n}};\nstatic const size_t {name}Size = {len(data)};\n"


out = """// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>
#include <cstddef>

namespace android {
namespace ver {

// Auto-generated SPIR-V binaries for VER Vulkan backend

"""

out += to_c_array("kMeshVertSpv", compile_shader(MESH_VERT, "vert")) + "\n"
out += to_c_array("kMeshFragSpv", compile_shader(MESH_FRAG, "frag")) + "\n"
out += to_c_array("kSkyboxVertSpv", compile_shader(SKYBOX_VERT, "vert")) + "\n"
out += to_c_array("kSkyboxFragSpv", compile_shader(SKYBOX_FRAG, "frag")) + "\n"

out += """} // namespace ver
} // namespace android
"""

target_path = os.path.join(
    os.path.dirname(__file__), "../src/VulkanShaders.h"
)
with open(target_path, "w") as f:
  f.write(out)
print("Successfully generated", target_path)
