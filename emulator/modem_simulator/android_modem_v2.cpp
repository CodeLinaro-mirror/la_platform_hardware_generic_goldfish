/* Copyright (C) 2020 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#include "android_modem_v2.h"

// #include "android/emulation/control/adb/AdbInterface.h"
#include <functional>
#include <mutex>
#include <thread>

#include "absl/log/log.h"

#include "ModemSimulator.h"
#include "android/telephony/modem.h"
#include "android/telephony/phone_number.h"
#include "android/telephony/sms.h"
#include "android/telephony/sysdeps.h"

static std::unique_ptr<android::modem::ModemSimulator> s_modem{
    new android::modem::ModemSimulator()};

android::modem::ModemSimulator* getModemSimulator() {
    return s_modem.get();
}

extern int amodem_number_of_calls_vx(AModem modem) {
    return s_modem->number_of_calls();
}

extern ACall amodem_call_by_idx_vx(AModem modem, int idx) {
    return s_modem->call_by_index(idx);
}

void amodem_receive_sms_vx(AModem modem, SmsPDU sms) {
    s_modem->receive_sms(sms);
}

ACall amodem_find_call_by_number_vx(AModem modem, const char* args) {
    return s_modem->find_call_by_number(args);
}

int amodem_add_inbound_call_vx(AModem modem, const char* args) {
    return s_modem->add_inbound_call(args);
}

int amodem_disconnect_call_vx(AModem modem, const char* args) {
    return s_modem->disconnect_call(args);
}

int amodem_update_call_vx(AModem modem, const char* args, int state) {
    return s_modem->update_call(args, state);
}

void amodem_set_data_network_type_vx(AModem modem, ADataNetworkType type) {
    s_modem->set_data_network_type(type);
}

void amodem_set_signal_strength_profile_vx(AModem modem, int quality) {
    s_modem->set_signal_strength_profile(quality);
}

void amodem_update_time(AModem modem) {
    s_modem->update_time();
}

static std::string parseAndValidatePhoneNumber(const std::string& input_phone_number) {
    // Max possible MSISDN length as per E.164 recommendation
    constexpr size_t kMaxMsisdnLength = 15;

    std::string parsed_phone_number;
    parsed_phone_number.reserve(kMaxMsisdnLength);

    for (char c : input_phone_number) {
        if (c == '-') {
            continue;  // Ignore hyphens
        }

        if (!std::isdigit(static_cast<unsigned char>(c))) {
            LOG(ERROR) << "Phone number contains invalid character: '" << c
                       << "'. Only digits and hyphens are allowed.";
            return "";
        }

        // Check length *before* appending to ensure we don't exceed kMaxMsisdnLength
        if (parsed_phone_number.length() == kMaxMsisdnLength) {
            LOG(ERROR) << "Phone number exceeds maximum allowed length of " << kMaxMsisdnLength
                       << " digits. Input was: '" << input_phone_number << "'";
            return "";
        }

        parsed_phone_number.push_back(c);  // Append valid digit
    }

    return parsed_phone_number;
}

int amodem_update_phone_number(AModem modem, const char* number) {
    auto phone_number = parseAndValidatePhoneNumber(number);
    if (phone_number.empty()) {
        LOG(WARNING) << "bad phone number format: " << number << ", use digits, # and + only";
        return -1;
    }

    LOG(FATAL) << "Not yet implemented";
    // TODO: enable adb interface
    // auto adbInterface = android::emulation::AdbInterface::getGlobal();
    // if (!adbInterface) {
    // LOG(WARNING) << "No adb binary found, cannot set the phone number.";
    // return -1;
    // }
    int res = s_modem->set_phone_number(phone_number.c_str());
    // adbInterface->enqueueCommand(
    //         {"shell", "cmd", "connectivity", "airplane-mode", "enable"});
    // adbInterface->enqueueCommand(
    //         {"shell", "cmd", "connectivity", "airplane-mode", "disable"});
    return res;
}

void amodem_set_data_registration_vx(AModem modem, ARegistrationState state) {
    s_modem->set_data_registration(state);
}

ARegistrationState amodem_get_data_registration_vx(AModem modem) {
    return s_modem->get_data_registration();
}

ARegistrationState amodem_get_voice_registration_vx(AModem modem) {
    return s_modem->get_voice_registration();
}

void amodem_set_voice_registration_vx(AModem modem, ARegistrationState state) {
    s_modem->set_voice_registration(state);
}

void amodem_state_save_vx(AModem modem, SysFile* file) {
    s_modem->save_sate(file);
}

int amodem_state_load_vx(AModem modem, SysFile* file, int version_id) {
    return s_modem->load_sate(file, version_id);
}

void amodem_set_notification_callback_vx(AModem modem, ModemCallback callbackFunc, void* userData) {
    s_modem->set_notification_callback_vx(callbackFunc, userData);
}
