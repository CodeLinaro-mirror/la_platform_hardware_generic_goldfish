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

/*
 * NOTE: Types like ACall, ACallRec, SmsPDU, ADataNetworkType, ARegistrationState, and SysFile
 * are defined in the included C headers (modem.h, sms.h, sysdeps.h) and are available in the global
 * namespace.
 */

#include "ModemSimulator.h"

#include <string>
#include <unordered_map>

#include "android/telephony/modem.h"
#include "android/telephony/sms.h"
#include "android/telephony/sysdeps.h"
#include "modem_main.h"

namespace android {
namespace modem {

int ModemSimulator::number_of_calls() {
    return mCalls.size();
}

ACall ModemSimulator::call_by_index(int idx) {
    if (idx < 0 || idx >= mCalls.size()) return nullptr;
    for (auto iter = mCalls.begin(); iter != mCalls.end(); ++iter) {
        if (idx == 0) {
            return &(iter->second);
        }
        idx--;
    }
    return nullptr;
}

void ModemSimulator::receive_sms(SmsPDU sms) {
    cuttlefish::send_sms_msg(std::string(amodem_sms_to_string(nullptr, sms)));
}

int ModemSimulator::add_inbound_call(const char* args) {
    if (!args) return 0;
    int res = cuttlefish::receive_inbound_call(std::string(args));
    if (res == A_CALL_OP_OK) {
        ACallRec rec = {.dir = ACallDir::A_CALL_INBOUND,
                        .state = ACallState::A_CALL_INCOMING,
                        .mode = ACallMode::A_CALL_VOICE};
        snprintf(rec.number, sizeof(rec.number), "%s", args);
        mCalls[std::string(args)] = rec;
    }
    return res;
}

ACall ModemSimulator::find_call_by_number(const char* args) {
    auto iter = mCalls.find(std::string(args));
    if (iter == mCalls.end()) {
        return nullptr;
    } else {
        return &(iter->second);
    }
}

int ModemSimulator::disconnect_call(const char* args) {
    if (!args) return 0;
    cuttlefish::disconnect_call(std::string(args));
    mCalls.erase(std::string(args));
    return 0;
}

int ModemSimulator::update_call(const char* args, int state) {
    cuttlefish::update_call(std::string(args), state);
    auto iter = mCalls.find(std::string(args));
    if (iter != mCalls.end()) {
        iter->second.state = (ACallState)state;
    }
    return 0;
}

void ModemSimulator::set_data_network_type(ADataNetworkType type) {
    cuttlefish::set_data_network_type(type);
}

void ModemSimulator::set_signal_strength_profile(int quality) {
    cuttlefish::set_signal_strength_profile(quality);
}

void ModemSimulator::update_time() {
    cuttlefish::update_time();
}

int ModemSimulator::set_phone_number(const char* number) {
    return cuttlefish::set_phone_number(number);
}

void ModemSimulator::set_data_registration(ARegistrationState state) {
    mDataState = state;
    cuttlefish::set_data_registration(state);
}

ARegistrationState ModemSimulator::get_data_registration() {
    return mDataState;
}

ARegistrationState ModemSimulator::get_voice_registration() {
    return mVoiceState;
}

void ModemSimulator::set_voice_registration(ARegistrationState state) {
    mVoiceState = state;
    cuttlefish::set_voice_registration(state);
}

void ModemSimulator::save_sate(SysFile* file) {
    cuttlefish::save_state(file);
}

int ModemSimulator::load_sate(SysFile* file, int version_id) {
    int res = 0;
    res = cuttlefish::load_state(file, version_id);
    return res;
}

void ModemSimulator::modem_callback_forwarder(void* user_data, int numActiveCalls) {
    ModemSimulator* me = (ModemSimulator*)user_data;
    if (numActiveCalls == 0) {
        me->mCalls.clear();
    }
    me->mCallbackFunc(me->mUserData, numActiveCalls);
}

void ModemSimulator::set_notification_callback_vx(ModemCallback callbackFunc, void* userData) {
    mUserData = userData;
    mCallbackFunc = callbackFunc;
    cuttlefish::set_notification_callback(reinterpret_cast<void*>(&modem_callback_forwarder), this);
}

}  // namespace modem
}  // namespace android
