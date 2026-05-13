#!/usr/bin/env python3
# Copyright 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""A build-time utility to generate gRPC breadcrumb resolution logic.

This script parses Protobuf FileDescriptorSets to discover gRPC services and methods.
It calculates CRC32 hashes for method paths and generates C++ code that can
translate these hashes back into human-readable names and deserialize their
associated Protobuf payloads.
"""

import argparse
import binascii
import sys
import textwrap
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Set, Tuple

from google.protobuf import descriptor_pb2


@dataclass(frozen=True)
class GrpcMethod:
    """Represents a discovered gRPC method and its associated metadata.

    Attributes:
        hash: The CRC32 hash of the method path (e.g., "/package.Service/Method").
        method_name: The simple name of the method.
        input_type_cpp: Scoped C++ type name for the request message.
        output_type_cpp: Scoped C++ type name for the response message.
    """

    hash: int
    method_name: str
    input_type_cpp: str
    output_type_cpp: str


def calculate_crc32(path: str) -> int:
    """Calculates the CRC32 of a string, matching zlib.crc32.

    This hash is used as a compact identifier for gRPC methods in the
    binary breadcrumb log.
    """
    return binascii.crc32(path.encode()) & 0xFFFFFFFF


def get_cpp_type(proto_type: str) -> str:
    """Converts a proto message type to a C++ scoped type.

    Example:
        ".android.emulation.control.KeyboardEvent" ->
        "android::emulation::control::KeyboardEvent"
    """
    return proto_type.lstrip(".").replace(".", "::")


class MetadataGatherer:
    """Extracts gRPC service metadata from binary FileDescriptorSets."""

    def __init__(self, descriptor_paths: List[str]):
        """Initializes the gatherer with a list of descriptor file paths."""
        self.descriptor_paths = [Path(p) for p in descriptor_paths]

    def gather(self) -> Tuple[Dict[int, GrpcMethod], Set[str]]:
        """Parses the descriptor sets and returns discovered methods and headers.

        Returns:
            A tuple of (methods_dict, headers_set), where:
                methods_dict: Mapping of CRC32 hashes to GrpcMethod objects.
                headers_set: Set of original .proto filenames containing services.
        """
        methods: Dict[int, GrpcMethod] = {}
        proto_headers: Set[str] = set()

        for desc_path in self.descriptor_paths:
            # Only process files that look like binary descriptor sets.
            # Genrules often pass source .proto files in $(locations) as well.
            if not desc_path.suffix in (".bin", ".desc"):
                continue

            with open(desc_path, "rb") as f:
                fds = descriptor_pb2.FileDescriptorSet()
                fds.ParseFromString(f.read())

                for proto_file in fds.file:
                    # If this file defines gRPC services, we'll need its C++ header.
                    if proto_file.service:
                        proto_headers.add(proto_file.name)

                    package = proto_file.package
                    for service in proto_file.service:
                        for method in service.method:
                            path = f"/{package}.{service.name}/{method.name}"
                            hash_val = calculate_crc32(path)
                            assert hash_val not in methods, f"Collision detected for hash 0x{hash_val:08X}"
                            methods[hash_val] = GrpcMethod(
                                hash=hash_val,
                                method_name=method.name,
                                input_type_cpp=get_cpp_type(method.input_type),
                                output_type_cpp=get_cpp_type(method.output_type),
                            )

        return methods, proto_headers


class CppGenerator:
    """Generates C++ header and source files from gathered metadata."""

    def __init__(
        self,
        header_path: str,
        source_path: str,
        methods: Dict[int, GrpcMethod],
        proto_headers: Set[str],
    ):
        """Initializes the generator with output paths and metadata."""
        self.header_path = Path(header_path)
        self.source_path = Path(source_path)
        self.methods = methods
        self.proto_headers = proto_headers

    def generate_header(self):
        """Writes the C++ header file defining the metadata lookup interface."""
        content = textwrap.dedent("""\
            // GENERATED CODE - DO NOT EDIT
            #pragma once
            #include <cstdint>
            #include <string>
            #include <string_view>
            #include "grpc_diagnostic.pb.h"

            namespace android::crashreport::breadcrumbs {

            using android::control::interceptor::GrpcBreadcrumb;

            /** @brief Returns the human-readable name for a given CRC32 method hash. */
            std::string_view GetMethodName(uint32_t hash);

            /**
             * @brief Deserializes the payload based on the method type and RPC phase.
             */
            std::string ResolvePayload(uint32_t hash, const std::string& bytes, GrpcBreadcrumb::Phase phase);

            } // namespace android::crashreport::breadcrumbs
        """)
        self.header_path.write_text(content)

    def generate_source(self):
        """Writes the C++ source file containing the static resolution engine."""
        sorted_hashes = sorted(self.methods.keys())

        # Build Includes
        includes = [
            f'#include "{self.header_path.name}"',
            "#include <unordered_map>",
            '#include "google/protobuf/util/json_util.h"',
        ]

        for h in sorted(self.proto_headers):
            header_base = Path(h).with_suffix(".pb.h").name
            includes.append(f'#include "{header_base}"')

        # Build GetMethodName cases
        method_cases = []
        for h in sorted_hashes:
            m = self.methods[h]
            method_cases.append(f'        case 0x{h:08X}u: return "{m.method_name}";')

        # Build ResolvePayload cases
        payload_cases = []
        for h in sorted_hashes:
            m = self.methods[h]
            case = textwrap.dedent(f"""\
                case 0x{h:08X}u: {{
                    const bool is_request = (phase == GrpcBreadcrumb::PRE_SEND_MESSAGE || phase == GrpcBreadcrumb::POST_RECV_MESSAGE);
                    if (is_request) {{
                        {m.input_type_cpp} msg;
                        if (msg.ParseFromString(bytes)) return msg.ShortDebugString();
                    }} else {{
                        {m.output_type_cpp} msg;
                        if (msg.ParseFromString(bytes)) return msg.ShortDebugString();
                    }}
                    break;
                }}""")
            payload_cases.append(textwrap.indent(case, "        "))

        source_template = textwrap.dedent("""\
            {includes}

            namespace android::crashreport::breadcrumbs {{

            std::string_view GetMethodName(uint32_t hash) {{
                switch (hash) {{
            {method_name_cases}
                    default: return "unknown_method";
                }}
            }}

            std::string ResolvePayload(uint32_t hash, const std::string& bytes, GrpcBreadcrumb::Phase phase) {{
                if (bytes.empty()) return "";

                switch (hash) {{
            {payload_resolution_cases}
                    default: break;
                }}
                return "";
            }}

            }} // namespace android::crashreport::breadcrumbs
        """).format(
            includes="\n".join(includes),
            method_name_cases="\n".join(method_cases),
            payload_resolution_cases="\n".join(payload_cases),
        )

        self.source_path.write_text(source_template)


def main():
    """Main entry point for the metadata generator."""
    parser = argparse.ArgumentParser(
        description="Generate gRPC breadcrumb metadata C++ code."
    )
    parser.add_argument(
        "--descriptors",
        nargs="+",
        required=True,
        help="Path to FileDescriptorSet binary files.",
    )
    parser.add_argument("--header", required=True, help="Output header path.")
    parser.add_argument("--source", required=True, help="Output source path.")
    args = parser.parse_args()

    # 1. Gather semantic data from descriptors
    gatherer = MetadataGatherer(args.descriptors)
    methods, headers = gatherer.gather()

    # 2. Generate the C++ resolution engine
    generator = CppGenerator(args.header, args.source, methods, headers)
    generator.generate_header()
    generator.generate_source()


if __name__ == "__main__":
    main()
