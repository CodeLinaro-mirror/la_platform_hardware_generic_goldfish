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
#include "legacy_console_bridge.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include "android/base/system.h"
#include "android/emulation/control/absl_status_translate.h"
#include "android/goldfish/ini_file.h"
#include "android/status/status_macros.h"
#include "emulator_controller.grpc.pb.h"
#include "emulator_controller.pb.h"
#include "goldfish/discovery/emulator_advertisement.h"
#include "gsm_commands.h"
#include "screen_record_commands.h"
#include "snapshot_commands.h"
#include "telnet_auth.h"

namespace goldfish::parsing {

struct SensorValues {
    std::vector<float> data;
};

template <>
struct ArgExtractor<SensorValues> {
    static absl::StatusOr<SensorValues> Extract(ArgStream& args) {
        auto remaining = args.Remaining();
        while (!args.Empty()) {
            args.Next();
        }
        std::vector<std::string_view> tokens =
                absl::StrSplit(remaining, absl::ByAnyChar(" :,\t"), absl::SkipEmpty());

        if (tokens.empty()) {
            return absl::InvalidArgumentError(
                    "Usage: \"set <sensorname> <value-a>[:<value-b>[:<value-c>]]\"");
        }

        SensorValues result;
        for (auto token : tokens) {
            float v;
            if (!absl::SimpleAtof(token, &v)) {
                return absl::InvalidArgumentError(
                        "Usage: \"set <sensorname> <value-a>[:<value-b>[:<value-c>]]\"");
            }
            result.data.push_back(v);
        }
        return result;
    }
};

}  // namespace goldfish::parsing

namespace goldfish::telnet {

using android::emulation::control::GrpcStatusToAbslStatus;
using android::goldfish::IniFile;

namespace {
absl::StatusOr<std::string> GetPlatformConfigProperty(LegacyConsoleBridge::ConsoleContext& ctx,
                                                      const std::string& key) {
    ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
    ASSIGN_OR_RETURN(auto context, ctx.NewContext());

    google::protobuf::Empty request;
    android::emulation::control::EmulatorStatus response;
    RETURN_IF_ERROR(GrpcStatusToAbslStatus(stub->getStatus(context.get(), request, &response)));

    auto map = response.platformconfig();
    if (auto it = map.find(key); it != map.end()) {
        return it->second;
    }

    LOG(WARNING) << key << " not found in platform config for port " << ctx.Port();
    return absl::InternalError(absl::StrCat(key, " not found in platform config"));
}
}  // namespace

LegacyConsoleBridge::LegacyConsoleBridge(int port, std::filesystem::path token_path)
        : port_(port), token_path_(std::move(token_path)) {
    if (auto status = TelnetAuth::LoadOrCreateToken(16, token_path_); !status.ok()) {
        LOG(WARNING) << "Failed to load or create telnet auth token at " << token_path_ << ": "
                     << status.status();
    }
    CommandRegistryBuilder builder(token_path_);

    // --- Safe Root Commands ---

    builder.On("ping" /* do_ping */, "check if the emulator is alive",
               [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
                   // We check the stub is connected and the service is ready.
                   RETURN_IF_ERROR(ctx.EmulatorControllerStub().status());
                   return "I am alive!";
               });
    builder.Command("ping", "").Safe();

    builder.On("auth" /* do_auth */, "user authentication for the emulator console",
               "use 'auth <auth_token>' to get extended console functionality\r\n",
               [token_path = token_path_](ConsoleContext& ctx,
                                          const std::string& token) -> absl::StatusOr<std::string> {
                   ASSIGN_OR_RETURN(auto expected_token, TelnetAuth::ReadToken(token_path));
                   if (!expected_token.SecureEquals(token)) {
                       return absl::InvalidArgumentError(absl::StrCat(
                               "authentication token does not match ", token_path.string()));
                   }
                   ctx.authenticated = true;
                   return "Android Console: type 'help' for a list of commands";
               });
    builder.Command("auth", "").Safe();

    builder.On("quit|exit" /* do_quit */, "quit control session",
               [](ConsoleContext& /*ctx*/) { return absl::AbortedError("quit"); });
    builder.Command("quit|exit", "").Safe();

    // --- AVD Commands ---
    auto avd = builder.Command("avd", "control virtual device execution");
    avd.On("stop" /* do_avd_stop */, "stop the virtual device",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("start" /* do_avd_start */, "start/restart the virtual device",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("status" /* do_avd_status */, "query virtual device status",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("heartbeat" /* do_avd_heartbeat */,
           "query the heart heartbeat number of the guest system",
           [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
               ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());

               google::protobuf::Empty request;
               android::emulation::control::EmulatorStatus response;
               RETURN_IF_ERROR(
                       GrpcStatusToAbslStatus(stub->getStatus(context.get(), request, &response)));
               return absl::StrCat("heartbeat: ", response.heartbeat());
           });
    avd.On("rewindaudio" /* do_avd_rewind_audio */, "rewind the input audio to the beginning",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("pause" /* do_avd_pause */, "pause the virtual device",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("resume" /* do_avd_resume */, "resume the virtual device",
           [](ConsoleContext& ctx) -> absl::Status {
               ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());

               android::emulation::control::VmRunState request;
               request.set_state(android::emulation::control::VmRunState::RUNNING);
               google::protobuf::Empty response;

               return GrpcStatusToAbslStatus(stub->setVmState(context.get(), request, &response));
           });
    avd.On("hostmicon" /* do_avd_hostmicon */, "activate the host audio input device",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("hostmicoff" /* do_avd_hostmicoff */, "deactivate the host audio input device",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("bugreport" /* do_avd_bugreport */, "generate bug report info.",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("id" /* do_avd_id */, "query virtual device ID",
           [](ConsoleContext& ctx) { return GetPlatformConfigProperty(ctx, "avd.id"); });
    avd.On("windowtype" /* do_avd_windowtype */, "query virtual device headless or qtwindow",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });

    avd.On("path" /* do_avd_path */, "query AVD path",
           [](ConsoleContext& ctx) { return GetPlatformConfigProperty(ctx, "avd.content_path"); });

    avd.On("discoverypath" /* do_avd_discoverypath */, "query AVD discovery path",
           [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
               ASSIGN_OR_RETURN(auto discovery,
                                ctx.DiscoverEmulatorWithProperties(
                                        {{"port.serial", std::to_string(ctx.Port())}}));
               return discovery.discovery_file.string();
           });
    avd.On("snapshotspath" /* do_avd_snapshotspath */, "query AVD snapshots path",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("snapshotpath" /* do_avd_snapshotpath */, "query path to a particular AVD snapshot",
           [](ConsoleContext& /*ctx*/, const std::string& /*name*/) {
               return absl::UnimplementedError("not implemented");
           });

    // name and grpc are safe sub-commands
    avd.On("name" /* do_avd_name */, "query virtual device name",
           [](ConsoleContext& ctx) { return GetPlatformConfigProperty(ctx, "avd.name"); });
    avd.Sub("name", "").Safe();

    avd.On("grpc" /* do_avd_grpc_port */, "query the grpc port",
           [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
               ASSIGN_OR_RETURN(auto discovery,
                                ctx.DiscoverEmulatorWithProperties(
                                        {{"port.serial", std::to_string(ctx.Port())}}));
               if (auto it = discovery.properties.find("grpc.port");
                   it != discovery.properties.end()) {
                   return it->second;
               }
               return absl::NotFoundError("No active gRPC service.");
           });
    avd.Sub("grpc", "").Safe();

    auto snapshot = avd.Sub("snapshot", "state snapshot commands");
    RegisterSnapshotCommands(snapshot);

    avd.On("snapshotspath" /* do_snapshotspath */, "query AVD snapshots path",
           [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
               ASSIGN_OR_RETURN(auto avd_path, GetPlatformConfigProperty(ctx, "avd.content_path"));
               return (std::filesystem::path(avd_path) / "snapshots").string();
           });
    avd.On("snapshotpath" /* do_snapshotpath */, "query path to a particular AVD snapshot",
           [](ConsoleContext& ctx, const std::string& name) -> absl::StatusOr<std::string> {
               if (name.empty()) {
                   return absl::InvalidArgumentError("Usage: 'avd snapshotpath <name>'");
               }
               ASSIGN_OR_RETURN(auto avd_path, GetPlatformConfigProperty(ctx, "avd.content_path"));
               return (std::filesystem::path(avd_path) / "snapshots" / name).string();
           });
    avd.Safe();

    // --- Automation Commands ---
    auto automation = builder.Command("automation", "manage emulator automation");
    automation.On("record" /* do_automation_record */, "start recording a macro.",
                  [](ConsoleContext& /*ctx*/, const std::string& /*filename*/) {
                      return absl::UnimplementedError("not implemented");
                  });
    automation.On(
            "stop-record" /* do_automation_stop_recording */, "stop recording a macro.",
            [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    automation.On("play" /* do_automation_play */, "playback macro.",
                  [](ConsoleContext& /*ctx*/, const std::string& /*filename*/) {
                      return absl::UnimplementedError("not implemented");
                  });
    automation.On(
            "stop-play" /* do_automation_stop_playback */, "stop playing macro.",
            [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });

    // --- Event Commands ---
    auto event = builder.Command("event", "simulate hardware events");
    event.On("send" /* do_event_send */, "send a series of events to the kernel",
             [](ConsoleContext& /*ctx*/, const std::string& /*events*/) {
                 /* parse event sequence (type:code:value repeated) */
                 return absl::UnimplementedError("not implemented");
             });
    event.On("types" /* do_event_types */, "list all <type> aliases",
             [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    event.On("codes" /* do_event_codes */, "list all <code> aliases for a given <type>",
             [](ConsoleContext& /*ctx*/, const std::string& /*type*/) {
                 return absl::UnimplementedError("not implemented");
             });
    event.On("text" /* do_event_text */, "simulate keystrokes from a given text",
             [](ConsoleContext& /*ctx*/, const std::string& /*message*/) {
                 /* parse message (rest of line) */
                 return absl::UnimplementedError("not implemented");
             });
    event.On("mouse" /* do_event_mouse */, "simulate a mouse event",
             [](ConsoleContext& /*ctx*/, int /*x*/, int /*y*/, int /*device*/,
                int /*buttonstate*/) { return absl::UnimplementedError("not implemented"); });

    // --- Geo Commands ---
    auto geo = builder.Command("geo", "Geo-location commands");
    geo.On("nmea" /* do_geo_nmea */, "send a GPS NMEA sentence",
           "'geo nema <sentence>' sends an NMEA 0183 sentence to the emulated "
           "device, as\r\n"
           "if it came from an emulated GPS modem. <sentence> must begin with "
           "'$GP'. Only\r\n"
           "'$GPGGA' and '$GPRMC' sentences are supported at the moment.\r\n",
           [](ConsoleContext& /*ctx*/, const std::string& /*sentence*/) {
               /* parse NMEA sentence (rest of line) */
               return absl::UnimplementedError("not implemented");
           });
    geo.On("fix" /* do_geo_fix */, "send a simple GPS fix",
           "'geo fix <longitude> <latitude> [<altitude> [<satellites> "
           "[<velocity> [<heading>]]]]'\r\n"
           " allows you to send a simple GPS fix to the emulated system.\r\n"
           " The parameters are:\r\n\r\n"
           "  <longitude>   longitude, in decimal degrees\r\n"
           "  <latitude>    latitude, in decimal degrees\r\n"
           "  <altitude>    optional altitude in meters\r\n"
           "  <satellites>  number of satellites being tracked (1-12)\r\n"
           "  <velocity>    optional velocity in knots\r\n"
           "  <heading>     optional heading in degrees [0.0, 360.0]\r\n"
           "\r\n",
           [](ConsoleContext& ctx, double longitude, double latitude,
              std::optional<double> altitude, std::optional<int> satellites,
              std::optional<double> velocity, std::optional<double> heading) {
               // Input validation
               if (latitude < -90.0 || latitude > 90.0) {
                   return absl::InvalidArgumentError("Latitude must be between -90 and 90.");
               }
               if (longitude < -180.0 || longitude > 180.0) {
                   return absl::InvalidArgumentError("Longitude must be between -180 and 180.");
               }

               ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());

               android::emulation::control::GpsState request;
               request.set_longitude(longitude);
               request.set_latitude(latitude);
               if (altitude) {
                   request.set_altitude(*altitude);
               }
               if (satellites) {
                   if (*satellites < 0 || *satellites > 12) {
                       return absl::InvalidArgumentError("Satellites must be between 0 and 12.");
                   }
                   request.set_satellites(*satellites);
               }
               if (velocity) {
                   // Convert knots to m/s
                   request.set_speed(*velocity * 0.514444);
               }
               if (heading) {
                   if (*heading < 0.0 || *heading > 360.0) {
                       return absl::InvalidArgumentError("Heading must be between 0 and 360.");
                   }
                   request.set_bearing(*heading);
               }
               ASSIGN_OR_RETURN(auto context, ctx.NewContext());
               google::protobuf::Empty unused;
               return GrpcStatusToAbslStatus(stub->setGps(context.get(), request, &unused));
           });
    geo.On("gnss" /* do_geo_gnss */, "send a GNSS sentence",
           "'geo gnss <sentence>' sends a GNSS sentence to the emulated "
           "device.\r\n"
           "<sentence> has fields separated by ',', and it starts with 8 "
           "fields\r\n"
           "for clock data, 1 field for measurement count, and followed by\r\n"
           "count * 9 (each measurement data has 9 fields).\r\n"
           "e.g. geo gnss "
           "TimeNanos,FullBiasNanos,BiasNanos,BiasUncertaintyNanos,\r\n"
           "DriftNanosPerSecond,DriftUncertaintyNanosPerSecond,"
           "HardwareClockDiscontinuityCount,\r\n",
           [](ConsoleContext& /*ctx*/, const std::string& /*sentence*/) {
               /* parse GNSS sentence (rest of line) */
               return absl::UnimplementedError("not implemented");
           });

    // --- GSM Commands ---
    auto gsm = builder.Command("gsm", "GSM related commands");
    RegisterGsmCommands(gsm);

    // --- CDMA Commands ---
    auto cdma = builder.Command("cdma", "CDMA related commands");
    cdma.On("ssource" /* do_cdma_ssource */, "Set the current CDMA subscription source",
            [](ConsoleContext& /*ctx*/, const std::string& /*source*/) {
                return absl::UnimplementedError("not implemented");
            });
    cdma.On("prl_version" /* do_cdma_prl_version */, "Dump the current PRL version",
            [](ConsoleContext& /*ctx*/, int /*version*/) {
                return absl::UnimplementedError("not implemented");
            });

    // --- Network Commands ---
    auto network = builder.Command("network", "manage network settings");
    network.On("status" /* do_network_status */, "dump network status",
               [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    network.On("speed" /* do_network_speed */, "change network speed",
               [](ConsoleContext& /*ctx*/, const std::string& /*speed*/) {
                   return absl::UnimplementedError("not implemented");
               });
    network.On("delay" /* do_network_delay */, "change network latency",
               [](ConsoleContext& /*ctx*/, const std::string& /*delay*/) {
                   return absl::UnimplementedError("not implemented");
               });
    auto capture = network.Sub("capture", "dump network packets to file");
    capture.On("start" /* do_network_capture_start */, "start network capture",
               [](ConsoleContext& /*ctx*/, const std::string& /*file*/) {
                   return absl::UnimplementedError("not implemented");
               });
    capture.On("stop" /* do_network_capture_stop */, "stop network capture",
               [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });

    // --- WiFi Commands ---
    auto wifi = builder.Command("wifi", "manage wifi settings");
    wifi.On("add" /* do_wifi_add */, "add new WiFi SSID",
            [](ConsoleContext& /*ctx*/, const std::string& /*ssid*/,
               const std::optional<std::string>& /*password*/) {
                return absl::UnimplementedError("not implemented");
            });
    wifi.On("block" /* do_wifi_block */, "block network access on SSID",
            [](ConsoleContext& /*ctx*/, const std::string& /*ssid*/) {
                return absl::UnimplementedError("not implemented");
            });
    wifi.On("unblock" /* do_wifi_unblock */, "unblock network access on SSID",
            [](ConsoleContext& /*ctx*/, const std::string& /*ssid*/) {
                return absl::UnimplementedError("not implemented");
            });

    // --- Power Commands ---
    auto power = builder.Command("power", "power related commands");
    power.On("display" /* do_power_display */, "display battery and charger state",
             [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    power.On("ac" /* do_ac_state */, "set AC charging state",
             [](ConsoleContext& /*ctx*/, const std::string& /*state*/) {
                 return absl::UnimplementedError("not implemented");
             });
    power.On("status" /* do_battery_status */, "set battery status",
             [](ConsoleContext& /*ctx*/, const std::string& /*status*/) {
                 return absl::UnimplementedError("not implemented");
             });
    power.On("present" /* do_battery_present */, "set battery present state",
             [](ConsoleContext& /*ctx*/, const std::string& /*state*/) {
                 return absl::UnimplementedError("not implemented");
             });
    power.On("health" /* do_battery_health */, "set battery health state",
             [](ConsoleContext& /*ctx*/, const std::string& /*health*/) {
                 return absl::UnimplementedError("not implemented");
             });
    power.On("capacity" /* do_battery_capacity */, "set battery capacity state",
             [](ConsoleContext& /*ctx*/, int /*percentage*/) {
                 return absl::UnimplementedError("not implemented");
             });

    // --- Redir Commands ---
    auto redir = builder.Command("redir", "manage port redirections");
    redir.On("list" /* do_redir_list */, "list current redirections",
             [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    redir.On("add" /* do_redir_add_ipv4 */, "add new ipv4 redirection",
             [](ConsoleContext& /*ctx*/, const std::string& /*protocol_host_guest*/) {
                 /* parse (tcp|udp):hostport:guestport */
                 return absl::UnimplementedError("not implemented");
             });
    redir.On("del" /* do_redir_del_ipv4 */, "remove existing ipv4 redirection",
             [](ConsoleContext& /*ctx*/, const std::string& /*protocol_host*/) {
                 /* parse (tcp|udp):hostport */
                 return absl::UnimplementedError("not implemented");
             });
    redir.On("add-ipv6" /* do_redir_add_ipv6 */, "add new ipv6 redirection",
             [](ConsoleContext& /*ctx*/, const std::string& /*protocol_host_guest*/) {
                 /* parse (tcp|udp):hostport:guestport */
                 return absl::UnimplementedError("not implemented");
             });
    redir.On("del-ipv6" /* do_redir_del_ipv6 */, "remove existing ipv6 redirection",
             [](ConsoleContext& /*ctx*/, const std::string& /*protocol_host*/) {
                 /* parse (tcp|udp):hostport */
                 return absl::UnimplementedError("not implemented");
             });

    // --- SMS Commands ---
    auto sms = builder.Command("sms", "SMS related commands");
    sms.On("send" /* do_sms_send */, "send inbound SMS text message",
           [](ConsoleContext& /*ctx*/, const std::string& /*phonenumber*/,
              const std::string& /*message*/) {
               /* parse message (rest of line) */
               return absl::UnimplementedError("not implemented");
           });
    sms.On("pdu" /* do_sms_sendpdu */, "send inbound SMS PDU",
           [](ConsoleContext& /*ctx*/, const std::string& /*hexstring*/) {
               return absl::UnimplementedError("not implemented");
           });

    // --- Sensor Commands ---
    struct SensorDefinition {
        std::string name;
        std::vector<std::string> aliases;
        int sensor_id;
        size_t value_count;
    };

    static const std::vector<SensorDefinition> kSupportedSensors = {
        {"acceleration", {"accelerometer"}, 0, 3},
        {"gyroscope", {"gyro"}, 1, 3},
        {"magnetic-field", {"magnetic", "magnetometer"}, 2, 3},
        {"orientation", {}, 3, 3},
        {"temperature", {}, 4, 1},
        {"proximity", {}, 5, 1},
        {"light", {"lux"}, 6, 1},
        {"pressure", {"barometer"}, 7, 1},
        {"humidity", {}, 8, 1},
        {"magnetic-field-uncalibrated", {}, 9, 6},
        {"gyroscope-uncalibrated", {}, 10, 6},
        {"hinge-angle0", {}, 11, 1},
        {"hinge-angle1", {}, 12, 1},
        {"hinge-angle2", {}, 13, 1},
        {"heart-rate", {}, 14, 1},
        {"rgbc-light", {}, 15, 4},
        {"wrist-tilt", {}, 16, 1},
        {"acceleration-uncalibrated", {}, 17, 6},
    };

    auto find_sensor = [](std::string_view name) -> const SensorDefinition* {
        for (const auto& def : kSupportedSensors) {
            if (absl::EqualsIgnoreCase(def.name, name)) {
                return &def;
            }
            for (const auto& alias : def.aliases) {
                if (absl::EqualsIgnoreCase(alias, name)) {
                    return &def;
                }
            }
        }
        return nullptr;
    };

    auto sensor = builder.Command("sensor", "manage emulator sensors");
    sensor.On("status" /* do_sensors_status */, "list available sensors and their status",
              [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
                  ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
                  ASSIGN_OR_RETURN(auto context, ctx.NewContext());

                  std::string out;
                  for (const auto& def : kSupportedSensors) {
                      android::emulation::control::SensorValue request;
                      request.set_target(
                              static_cast<android::emulation::control::SensorValue::SensorType>(
                                      def.sensor_id));
                      android::emulation::control::SensorValue reply;
                      auto status = stub->getSensor(context.get(), request, &reply);
                      if (status.ok()) {
                          absl::StrAppendFormat(&out, "%s: enabled.\r\n", def.name);
                      } else {
                          absl::StrAppendFormat(&out, "%s: disabled.\r\n", def.name);
                      }
                  }
                  return out;
              });

    sensor.On("get" /* do_sensors_get */, "get sensor values",
              "'get <sensorname>' get the values of a given sensor.\r\n",
              [find_sensor](ConsoleContext& ctx,
                            std::optional<std::string> sensor_name) -> absl::StatusOr<std::string> {
                  if (!sensor_name || sensor_name->empty()) {
                      return absl::InvalidArgumentError("Usage: \"get <sensorname>\"");
                  }
                  const auto* def = find_sensor(*sensor_name);
                  if (!def) {
                      return absl::NotFoundError(absl::StrFormat(
                              "unknown sensor name: %s, run 'sensor status' to get available "
                              "sensors.",
                              *sensor_name));
                  }
                  ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
                  ASSIGN_OR_RETURN(auto context, ctx.NewContext());
                  android::emulation::control::SensorValue request;
                  request.set_target(
                          static_cast<android::emulation::control::SensorValue::SensorType>(
                                  def->sensor_id));
                  android::emulation::control::SensorValue reply;
                  RETURN_IF_ERROR(
                          GrpcStatusToAbslStatus(stub->getSensor(context.get(), request, &reply)));

                  std::string out = absl::StrCat(*sensor_name, " = ");
                  const auto& data = reply.value().data();
                  for (size_t i = 0; i < def->value_count; ++i) {
                      float val = i < static_cast<size_t>(data.size()) ? data[i] : 0.0f;
                      absl::StrAppendFormat(&out, "%g%s", val,
                                            (i == def->value_count - 1) ? "" : ":");
                  }
                  return out;
              });

    sensor.On(
            "set" /* do_sensors_set */, "set sensor values",
            "'set <sensorname> <value-a>[:<value-b>[:<value-c>[...]]]' set the values of a given "
            "sensor.\r\n",
            [find_sensor](
                    ConsoleContext& ctx, std::optional<std::string> sensor_name,
                    std::optional<goldfish::parsing::SensorValues> parsed_values) -> absl::Status {
                if (!sensor_name || sensor_name->empty() || !parsed_values ||
                    parsed_values->data.empty()) {
                    return absl::InvalidArgumentError(
                            "Usage: \"set <sensorname> <value-a>[:<value-b>[:<value-c>]]\"");
                }
                const auto* def = find_sensor(*sensor_name);
                if (!def) {
                    return absl::NotFoundError(absl::StrFormat(
                            "unknown sensor name: %s, run 'sensor status' to get available "
                            "sensors.",
                            *sensor_name));
                }

                ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
                ASSIGN_OR_RETURN(auto context, ctx.NewContext());

                android::emulation::control::SensorValue request;
                request.set_target(
                        static_cast<android::emulation::control::SensorValue::SensorType>(
                                def->sensor_id));

                for (float v : parsed_values->data) {
                    request.mutable_value()->add_data(v);
                }

                google::protobuf::Empty response;
                return GrpcStatusToAbslStatus(stub->setSensor(context.get(), request, &response));
            });
    // --- Physics Commands ---
    auto physics = builder.Command("physics", "manage physical model");
    physics.On("record-gt" /* do_physics_record_ground_truth */,
               "start recording ground truth of the physical model's 6dof poses",
               [](ConsoleContext& /*ctx*/, const std::string& /*filename*/) {
                   return absl::UnimplementedError("not implemented");
               });
    physics.On("stop" /* do_physics_stop */, "stop recording ground truth",
               [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });

    // --- Finger Commands ---
    auto send_fingerprint = [](ConsoleContext& ctx, bool is_touching,
                               int touch_id = 0) -> absl::Status {
        ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
        ASSIGN_OR_RETURN(auto context, ctx.NewContext());
        android::emulation::control::Fingerprint request;
        request.set_istouching(is_touching);
        request.set_touchid(touch_id);
        google::protobuf::Empty unused;
        return GrpcStatusToAbslStatus(stub->sendFingerprint(context.get(), request, &unused));
    };

    auto finger = builder.Command("finger", "manage emulator finger print");
    finger.On("touch" /* do_fingerprint_touch */, "touch finger print sensor with <fingerid>",
              [send_fingerprint](ConsoleContext& ctx, int fingerid) {
                  return send_fingerprint(ctx, true, fingerid);
              });
    finger.On("remove" /* do_fingerprint_remove */, "remove finger from the fingerprint sensor",
              [send_fingerprint](ConsoleContext& ctx) { return send_fingerprint(ctx, false); });

    // --- Multi-display Commands ---
    auto multidisplay = builder.Command("multidisplay", "configure the multi-display");
    multidisplay.On(
            "add" /* do_multi_display_add */, "add new or modify existing display",
            [](ConsoleContext& /*ctx*/, int /*id*/, int /*width*/, int /*height*/, int /*dpi*/,
               int /*flag*/) { return absl::UnimplementedError("not implemented"); });
    multidisplay.On("del" /* do_multi_display_del */, "remove existing display",
                    [](ConsoleContext& /*ctx*/, int /*id*/) {
                        return absl::UnimplementedError("not implemented");
                    });

    // --- Proxy Commands ---
    auto proxy = builder.Command("proxy", "manage network proxy server settings");
    proxy.On("set" /* do_proxy_set */, "set the proxy server to use",
             [](ConsoleContext& /*ctx*/, const std::string& /*url*/) {
                 return absl::UnimplementedError("not implemented");
             });
    proxy.On("clear" /* do_proxy_clear */, "clear proxy server setting",
             [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });

    // --- Other Terminal Root Commands ---
    builder.On("crash" /* do_crash */, "crash the emulator instance",
               [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    builder.On("crash-on-exit" /* do_crash_on_exit */,
               "simulate crash on exit for the emulator instance",
               [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    builder.On(
            "kill" /* do_kill */, "kill the emulator instance",
            [](ConsoleContext& ctx) -> absl::Status {
                ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
                ASSIGN_OR_RETURN(auto context, ctx.NewContext());

                android::emulation::control::VmRunState request;
                request.set_state(android::emulation::control::VmRunState::SHUTDOWN);
                google::protobuf::Empty response;

                return GrpcStatusToAbslStatus(stub->setVmState(context.get(), request, &response));
            });
    builder.On("restart" /* do_restart */, "restart the emulator instance",
               [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });

    builder.Command("grpc", "enable the grpc endpoint")
            .On("start" /* do_start_grpc */, "start the grpc endpoint",
                [](ConsoleContext& /*ctx*/, int /*port*/) {
                    return absl::UnimplementedError("not implemented");
                });

    builder.On("debug" /* do_debug */, "control the emulator debug output tags",
               [](ConsoleContext& /*ctx*/, const std::string& /*tags*/) {
                   return absl::UnimplementedError("not implemented");
               });

    builder.On("rotate" /* do_rotate_90_clockwise */, "rotate the screen clockwise by 90 degrees",
               [](ConsoleContext& ctx) -> absl::Status {
                   ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
                   ASSIGN_OR_RETURN(auto context_get, ctx.NewContext());

                   android::emulation::control::PhysicalModelValue get_req;
                   get_req.set_target(android::emulation::control::PhysicalModelValue::ROTATION);
                   android::emulation::control::PhysicalModelValue current_state;
                   auto status = stub->getPhysicalModel(context_get.get(), get_req, &current_state);
                   if (!status.ok()) {
                       current_state.set_target(
                               android::emulation::control::PhysicalModelValue::ROTATION);
                       current_state.mutable_value()->add_data(0.0f);
                       current_state.mutable_value()->add_data(0.0f);
                       current_state.mutable_value()->add_data(0.0f);
                   }

                   float current_z = current_state.value().data_size() > 2
                                             ? current_state.value().data(2)
                                             : 0.0f;
                   float new_z = std::fmod(current_z - 90.0f, 360.0f);

                   ASSIGN_OR_RETURN(auto context_set, ctx.NewContext());
                   android::emulation::control::PhysicalModelValue request;
                   request.set_target(android::emulation::control::PhysicalModelValue::ROTATION);
                   request.mutable_value()->add_data(0.0f);
                   request.mutable_value()->add_data(0.0f);
                   request.mutable_value()->add_data(new_z);

                   google::protobuf::Empty response;
                   return GrpcStatusToAbslStatus(
                           stub->setPhysicalModel(context_set.get(), request, &response));
               });

    auto set_fold = [](ConsoleContext& ctx, bool is_fold) -> absl::Status {
        ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
        ASSIGN_OR_RETURN(auto context, ctx.NewContext());

        android::emulation::control::PhysicalModelValue request;
        request.set_target(android::emulation::control::PhysicalModelValue::POSTURE);
        request.mutable_value()->add_data(is_fold ? 1.0f : 3.0f);
        google::protobuf::Empty response;

        return GrpcStatusToAbslStatus(stub->setPhysicalModel(context.get(), request, &response));
    };

    builder.On("fold" /* do_fold */, "fold the device",
               [set_fold](ConsoleContext& ctx) { return set_fold(ctx, true); });
    builder.On("unfold" /* do_unfold */, "unfold the device",
               [set_fold](ConsoleContext& ctx) { return set_fold(ctx, false); });

    builder.On("posture" /* do_set_posture */, "set the device posture",
               "Usage: posture <posture_id>\r\n"
               "  1: closed\t2: half-opened\t3: opened\t4: flipped\t5: tent\r\n",
               [](ConsoleContext& ctx, std::optional<int> posture_val) -> absl::Status {
                   if (!posture_val.has_value() || *posture_val < 1 || *posture_val > 5) {
                       return absl::InvalidArgumentError(
                               "Usage: \"posture <posture_id>\" "
                               "1: closed\t2: half-opened\t3: opened\t4: flipped\t5: tent");
                   }
                   ASSIGN_OR_RETURN(auto stub, ctx.EmulatorControllerStub());
                   ASSIGN_OR_RETURN(auto context, ctx.NewContext());

                   android::emulation::control::PhysicalModelValue request;
                   request.set_target(android::emulation::control::PhysicalModelValue::POSTURE);
                   request.mutable_value()->add_data(static_cast<float>(*posture_val));
                   google::protobuf::Empty response;

                   return GrpcStatusToAbslStatus(
                           stub->setPhysicalModel(context.get(), request, &response));
               });
    builder.Command("icebox", "auto-snapshot on uncaught exceptions")
            .On("track" /* do_icebox_track */,
                "(experimental) track exceptions in <pid> and take (up to [max_snapshots]) "
                "snapshots when there are assert failures",
                [](ConsoleContext& /*ctx*/, int /*pid*/, std::optional<int> /*max_snapshots*/) {
                    return absl::UnimplementedError("not implemented");
                });

    builder.On("nodraw" /* do_no_draw */, "turn on/off NoDraw mode. (experimental)",
               [](ConsoleContext& /*ctx*/, const std::string& /*state*/) {
                   return absl::UnimplementedError("not implemented");
               });

    builder.On("resize-display" /* do_resize_display */,
               "resize the display resolution to the preset size",
               [](ConsoleContext& /*ctx*/, int /*index*/) {
                   return absl::UnimplementedError("not implemented");
               });

    builder.On("virtualscene-image" /* do_set_virtualscene_image */,
               "customize virtualscene image for virtulscene camera",
               "Usage: virtualscene-image <wall|table> [path-to-image].\n"
               "If path-to-image is void, restore default image.\n",
               [](ConsoleContext& /*ctx*/, const std::string& /*target*/,
                  const std::optional<std::string>& /*path*/) {
                   return absl::UnimplementedError("not implemented");
               });

    builder.On("phonenumber" /* do_set_phone_number */, "set phone number for the device",
               "Usage: phonenumber <gsm-formatted number>.\n",
               [](ConsoleContext& /*ctx*/, const std::string& /*number*/) {
                   return absl::UnimplementedError("not implemented");
               });

    auto screenrecord = builder.Command("screenrecord", "Records the emulator's display");
    RegisterScreenRecordCommands(screenrecord);

    // Monitor is a root group in console.cpp but it's under qemu group usually
    builder.Command("qemu", "QEMU-specific commands")
            .On("monitor" /* do_qemu_monitor */, "enter QEMU monitor", [](ConsoleContext& /*ctx*/) {
                return absl::UnimplementedError("not implemented");
            });

    registry_ = builder.Build();
}

absl::StatusOr<std::string> LegacyConsoleBridge::operator()(std::string line, Context& ctx) {
    return (*registry_)(std::move(line), ctx);
}

std::string LegacyConsoleBridge::WelcomeMessage(const Context& ctx) const {
    return registry_->WelcomeMessage(ctx);
}

std::unique_ptr<LineCommandHandler::Context> LegacyConsoleBridge::CreateContext() const {
    auto ctx = std::make_unique<ConsoleContext>(port_);
    if (TelnetAuth::GetStatus(token_path_) == AuthStatus::kDisabled) {
        ctx->authenticated = true;
    }
    return ctx;
}

}  // namespace goldfish::telnet
