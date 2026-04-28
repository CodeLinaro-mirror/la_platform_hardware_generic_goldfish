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
#include "android/status/status_macros.h"
#include "emulator_controller.grpc.pb.h"
#include "emulator_controller.pb.h"
#include "goldfish/discovery/emulator_advertisement.h"
#include "telnet_auth.h"

namespace goldfish::telnet {

using android::emulation::control::GrpcStatusToAbslStatus;

absl::StatusOr<std::shared_ptr<android::emulation::control::BlockingEmulatorGrpcClient>>
LegacyConsoleBridge::ConsoleContext::Client() {
    absl::MutexLock lock(mutex_);
    if (client_) {
        return client_;
    }

    auto client = android::emulation::control::EmulatorGrpcClientBuilder()
                          .ForDiscoveredEmulator({{"port.serial", std::to_string(port_)}})
                          .BuildBlocking();
    if (!client.ok()) {
        LOG(ERROR) << "Failed to build gRPC client: " << client.status();
        return client.status();
    }

    VLOG(1) << "Connecting to gRPC server...";
    if (auto s = (*client)->Connect(absl::Seconds(2)); !s.ok()) {
        LOG(ERROR) << "Failed to connect to gRPC server: " << s;
        return s;
    }

    VLOG(1) << "Successfully connected to gRPC server.";
    client_ = std::move(*client);
    return client_;
}

LegacyConsoleBridge::LegacyConsoleBridge(int port, std::filesystem::path token_path)
        : port_(port), token_path_(std::move(token_path)) {
    CommandRegistryBuilder builder(token_path_);

    // --- Safe Root Commands ---

    builder.On("ping" /* do_ping */, "check if the emulator is alive",
               [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
                   if (auto s = ctx.Client(); !s.ok()) {
                       return s.status();
                   }
                   return "";
               });
    builder.Command("ping", "").Safe();

    builder.On("auth" /* do_auth */, "user authentication for the emulator console",
               "use 'auth <auth_token>' to get extended console functionality\r\n",
               [token_path = token_path_](ConsoleContext& ctx,
                                          const std::string& token) -> absl::StatusOr<std::string> {
                   auto expected_token = TelnetAuth::ReadToken(token_path);
                   if (!expected_token.ok()) {
                       return expected_token.status();
                   }

                   if ((*expected_token).SecureEquals(token)) {
                       ctx.authenticated = true;
                       return "Android Console: type 'help' for a list of commands";
                   } else {
                       return absl::InvalidArgumentError("authentication token does not match " +
                                                         token_path.string());
                   }
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
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("rewindaudio" /* do_avd_rewind_audio */, "rewind the input audio to the beginning",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("pause" /* do_avd_pause */, "pause the virtual device",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("resume" /* do_avd_resume */, "resume the virtual device",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("hostmicon" /* do_avd_hostmicon */, "activate the host audio input device",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("hostmicoff" /* do_avd_hostmicoff */, "deactivate the host audio input device",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("bugreport" /* do_avd_bugreport */, "generate bug report info.",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("id" /* do_avd_id */, "query virtual device ID",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("windowtype" /* do_avd_windowtype */, "query virtual device headless or qtwindow",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("path" /* do_avd_path */, "query AVD path",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("discoverypath" /* do_avd_discoverypath */, "query AVD discovery path",
           [](ConsoleContext& ctx) -> absl::StatusOr<std::string> {
               ASSIGN_OR_RETURN(auto discovery_path,
                                discovery::EmulatorAdvertisement().DiscoverEmulatorWithProperties(
                                        {{"port.serial", std::to_string(ctx.Port())}}));
               return discovery_path.string();
           });
    avd.On("snapshotspath" /* do_avd_snapshotspath */, "query AVD snapshots path",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.On("snapshotpath" /* do_avd_snapshotpath */, "query path to a particular AVD snapshot",
           [](ConsoleContext& /*ctx*/, const std::string& /*name*/) {
               return absl::UnimplementedError("not implemented");
           });

    // name and grpc are safe sub-commands
    avd.On("name" /* do_avd_name */, "query virtual device name",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.Sub("name", "").Safe();

    avd.On("grpc" /* do_avd_grpc_port */, "query the grpc port",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    avd.Sub("grpc", "").Safe();

    auto snapshot = avd.Sub("snapshot", "state snapshot commands");
    snapshot.On(
            "list" /* do_snapshot_list */, "list available state snapshots",
            [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    snapshot.On("save" /* do_snapshot_save */, "save state snapshot",
                [](ConsoleContext& /*ctx*/, const std::string& /*name*/) {
                    return absl::UnimplementedError("not implemented");
                });
    snapshot.On("load" /* do_snapshot_load */, "load state snapshot",
                [](ConsoleContext& /*ctx*/, const std::string& /*name*/) {
                    return absl::UnimplementedError("not implemented");
                });
    snapshot.On("del|delete" /* do_snapshot_del */, "delete state snapshot",
                [](ConsoleContext& /*ctx*/, const std::string& /*name*/) {
                    return absl::UnimplementedError("not implemented");
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

    // avd itself must be safe to allow access to safe sub-commands (name, grpc)
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
    gsm.On("list" /* do_gsm_list */, "list current phone calls",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    gsm.On("call" /* do_gsm_call */, "create inbound phone call",
           [](ConsoleContext& /*ctx*/, const std::string& /*phonenumber*/) {
               return absl::UnimplementedError("not implemented");
           });
    gsm.On("busy" /* do_gsm_busy */, "close waiting outbound call as busy",
           [](ConsoleContext& /*ctx*/, const std::string& /*remoteNumber*/) {
               return absl::UnimplementedError("not implemented");
           });
    gsm.On("hold" /* do_gsm_hold */, "change the state of an outbound call to 'held'",
           [](ConsoleContext& /*ctx*/, const std::string& /*remoteNumber*/) {
               return absl::UnimplementedError("not implemented");
           });
    gsm.On("accept" /* do_gsm_accept */, "change the state of an outbound call to 'active'",
           [](ConsoleContext& /*ctx*/, const std::string& /*remoteNumber*/) {
               return absl::UnimplementedError("not implemented");
           });
    gsm.On("cancel" /* do_gsm_cancel */, "disconnect an inbound or outbound phone call",
           [](ConsoleContext& /*ctx*/, const std::string& /*remoteNumber*/) {
               return absl::UnimplementedError("not implemented");
           });
    gsm.On("data" /* do_gsm_data */, "modify data connection state",
           [](ConsoleContext& /*ctx*/, const std::string& /*state*/) {
               return absl::UnimplementedError("not implemented");
           });
    gsm.On("meter" /* do_gsm_meter */, "modify mobile data meterness",
           [](ConsoleContext& /*ctx*/, const std::string& /*state*/) {
               return absl::UnimplementedError("not implemented");
           });
    gsm.On("voice" /* do_gsm_voice */, "modify voice connection state",
           [](ConsoleContext& /*ctx*/, const std::string& /*state*/) {
               return absl::UnimplementedError("not implemented");
           });
    gsm.On("status" /* do_gsm_status */, "display GSM status",
           [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    gsm.On("signal" /* do_gsm_signal */, "set sets the rssi and ber",
           [](ConsoleContext& /*ctx*/, int /*rssi*/, std::optional<int> /*ber*/) {
               return absl::UnimplementedError("not implemented");
           });
    gsm.On("signal-profile" /* do_gsm_signal_profile */, "set the signal strength profile",
           [](ConsoleContext& /*ctx*/, int /*strength*/) {
               return absl::UnimplementedError("not implemented");
           });

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
    auto sensor = builder.Command("sensor", "manage emulator sensors");
    sensor.On("status" /* do_sensors_status */, "list all sensors and their status.",
              [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    sensor.On("get" /* do_sensors_get */, "get sensor values",
              [](ConsoleContext& /*ctx*/, const std::string& /*sensorname*/) {
                  return absl::UnimplementedError("not implemented");
              });
    sensor.On("set" /* do_sensors_set */, "set sensor values",
              [](ConsoleContext& /*ctx*/, const std::string& /*sensorname*/,
                 const std::string& /*values*/) {
                  /* parse values (rest of line) */
                  return absl::UnimplementedError("not implemented");
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
    auto finger = builder.Command("finger", "manage emulator finger print");
    finger.On("touch" /* do_fingerprint_touch */, "touch finger print sensor with <fingerid>",
              [](ConsoleContext& /*ctx*/, int /*fingerid*/) {
                  return absl::UnimplementedError("not implemented");
              });
    finger.On("remove" /* do_fingerprint_remove */, "remove finger from the fingerprint sensor",
              [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });

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
    builder.On("kill" /* do_kill */, "kill the emulator instance",
               [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
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
               [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    builder.On("fold" /* do_fold */, "fold the device",
               [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    builder.On("unfold" /* do_unfold */, "unfold the device",
               [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });

    builder.On("posture" /* do_set_posture */, "set the device posture",
               [](ConsoleContext& /*ctx*/, const std::string& /*posture*/) {
                   return absl::UnimplementedError("not implemented");
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
    screenrecord.On("start" /* do_screenrecord_start */, "start screen recording",
                    "'screenrecord start [options] <filename>'\r\n"
                    "\r\nRecords the emulator's display to a .webm file.\r\n"
                    "\r\nOptions:\r\n"
                    "  --size WIDTHxHEIGHT\r\n"
                    "    Set the video size, e.g. \"1280x720\". Default is the device's main\r\n"
                    "    display resolution.\r\n"
                    "  --bit-rate RATE\r\n"
                    "    Set the video bit rate, in bits per second. Value may be specified as\r\n"
                    "    bits or megabits, e.g. '4000000' is equivalent to '4M'. Default 4Mbps.\r\n"
                    "    \r\n"
                    "  --time-limit TIME\r\n"
                    "    Set the maximum recording time, in seconds. Default/maximum is 180.\r\n"
                    "    \r\n"
                    "  --fps FPS\r\n"
                    "    Set the frames per second for the video recording. Default is 24 fps, "
                    "maximum is 60 fps.\r\n"
                    "    \r\n"
                    "  --display DISPLAY\r\n"
                    "    Set the display id for the video recording. Default is 0.\r\n"
                    "\r\nThe recording will stop with 'screenrecord stop' or when the time "
                    "limit\r\nis reached\r\n",
                    [](ConsoleContext& /*ctx*/) {
                        /* parse [options] <filename> */
                        return absl::UnimplementedError("not implemented");
                    });
    screenrecord.On(
            "stop" /* do_screenrecord_stop */, "stop screen recording",
            [](ConsoleContext& /*ctx*/) { return absl::UnimplementedError("not implemented"); });
    screenrecord.On("screenshot" /* do_screenrecord_screenshot */, "Take a screenshot",
                    "'screenrecord screenshot [options] <dirname>'\r\n"
                    "\r\nTakes a single screenshot of emulator's display "
                    "and saves the resulting PNG in <dirname>.\r\n"
                    "\r\nOptions:\r\n"
                    "  --display ID\r\n"
                    "    Set display to take screenshot. Default is the device's "
                    "main display ID = 0\r\n",
                    [](ConsoleContext& /*ctx*/, const std::string& /*filename*/) {
                        /* parse [options] <filename> */
                        return absl::UnimplementedError("not implemented");
                    });
    screenrecord.On(
            "webrtc" /* do_screenrecord_webrtc */, "start/stop the webrtc module",
            [](ConsoleContext& /*ctx*/, const std::string& /*state*/, std::optional<int> /*fps*/) {
                return absl::UnimplementedError("not implemented");
            });

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

}  // namespace goldfish::telnet
