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
#include "snapshot_commands.h"

#include <chrono>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include "android/emulation/control/absl_status_translate.h"
#include "android/status/status_macros.h"
#include "console_context.h"
#include "snapshot_service.grpc.pb.h"

namespace goldfish::telnet {

using android::emulation::control::GrpcStatusToAbslStatus;

void RegisterSnapshotCommands(CommandRegistryBuilder::NodeBuilder& snapshot) {
    snapshot.On("list" /* do_snapshot_list */, "list available state snapshots",
                [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
                    ASSIGN_OR_RETURN(auto stub, ctx.SnapshotStub());
                    ASSIGN_OR_RETURN(auto context, ctx.NewContext());

                    android::emulation::control::SnapshotFilter request;
                    request.set_statusfilter(android::emulation::control::SnapshotFilter::All);
                    android::emulation::control::SnapshotList response;

                    RETURN_IF_ERROR(GrpcStatusToAbslStatus(
                            stub->ListSnapshots(context.get(), request, &response)));

                    if (response.snapshots_size() == 0) {
                        return "There is no snapshot available.";
                    }

                    std::string out = "List of snapshots present on all disks:\r\n";
                    for (const auto& snap : response.snapshots()) {
                        const std::string& name = !snap.details().logical_name().empty()
                                                          ? snap.details().logical_name()
                                                          : snap.snapshot_id();
                        absl::StrAppend(&out, name, "\r\n");
                    }
                    return out;
                });
    snapshot.On("save" /* do_snapshot_save */, "save state snapshot",
                [](ConsoleContext& ctx, const std::string& name) -> absl::Status {
                    if (name.empty()) {
                        return absl::InvalidArgumentError(
                                "Argument missing, try 'avd snapshot save <name>'");
                    }
                    ASSIGN_OR_RETURN(auto stub, ctx.SnapshotStub());
                    ASSIGN_OR_RETURN(auto context, ctx.NewContext(std::chrono::system_clock::now() +
                                                                  std::chrono::seconds(30)));

                    android::emulation::control::SnapshotPackage request;
                    request.set_snapshot_id(name);
                    android::emulation::control::SnapshotPackage response;

                    return GrpcStatusToAbslStatus(
                            stub->SaveSnapshot(context.get(), request, &response));
                });
    snapshot.On("load" /* do_snapshot_load */, "load state snapshot",
                [](ConsoleContext& ctx, const std::string& name) -> absl::Status {
                    if (name.empty()) {
                        return absl::InvalidArgumentError(
                                "Argument missing, try 'avd snapshot load <name>'");
                    }
                    ASSIGN_OR_RETURN(auto stub, ctx.SnapshotStub());
                    ASSIGN_OR_RETURN(auto context, ctx.NewContext(std::chrono::system_clock::now() +
                                                                  std::chrono::seconds(30)));

                    android::emulation::control::SnapshotPackage request;
                    request.set_snapshot_id(name);
                    android::emulation::control::SnapshotPackage response;

                    return GrpcStatusToAbslStatus(
                            stub->LoadSnapshot(context.get(), request, &response));
                });
    snapshot.On("del|delete" /* do_snapshot_del */, "delete state snapshot",
                [](ConsoleContext& ctx, const std::string& name) -> absl::Status {
                    if (name.empty()) {
                        return absl::InvalidArgumentError(
                                "Argument missing, try 'avd snapshot del <name>'");
                    }
                    ASSIGN_OR_RETURN(auto stub, ctx.SnapshotStub());
                    ASSIGN_OR_RETURN(auto context, ctx.NewContext(std::chrono::system_clock::now() +
                                                                  std::chrono::seconds(30)));

                    android::emulation::control::SnapshotPackage request;
                    request.set_snapshot_id(name);
                    android::emulation::control::SnapshotPackage response;

                    return GrpcStatusToAbslStatus(
                            stub->DeleteSnapshot(context.get(), request, &response));
                });
    snapshot.On("remap" /* do_snapshot_remap */, "remap current snapshot RAM",
                "'avd snapshot remap <auto-save>' will activate or shut off Quickboot "
                "auto-saving\r\n"
                "while the emulator is running.\r\n"
                "<auto-save> value of 0: deactivate auto-save\r\n"
                "<auto-save> value of 1: activate auto-save\r\n"
                "- It is required that the current loaded snapshot be the Quickboot "
                "snapshot (default_boot).\r\n"
                "- If auto-saving is currently active and gets deactivated, a "
                "snapshot will be saved\r\n"
                "  to establish the last state.\r\n"
                "- If the emulator is not currently auto-saving and a remap command "
                "is issued,\r\n"
                "  the Quickboot snapshot will be reloaded with auto-saving enabled "
                "or disabled\r\n"
                "  according to the value of the <auto-save> argument.\r\n"
                "- This allows the user to set a checkpoint in the middle of running "
                "the emulator:\r\n"
                "  by starting the emulator with auto-save enabled, then issuing 'avd "
                "snapshot remap 0'\r\n"
                "  to disable auto-save and thus set the checkpoint. Subsequent 'avd "
                "snapshot remap 0'\r\n"
                "  commands will then repeatedly rewind to that checkpoint.\r\n"
                "  Issuing 'avd snapshot remap 1' after that will rewind again but "
                "activate auto-saving.\r\n",
                [](ConsoleContext& /*ctx*/, int /*auto_save*/) {
                    return absl::UnimplementedError("not implemented");
                });
}

}  // namespace goldfish::telnet
