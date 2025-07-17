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
#pragma once

#include <string>
#include <unordered_map>

#include "android/telephony/modem.h"
#include "android/telephony/sms.h"
#include "android/telephony/sysdeps.h"

namespace android {
namespace modem {

class ModemSimulator {
  public:
    ModemSimulator() = default;
    virtual ~ModemSimulator() = default;

  public:
    virtual int number_of_calls();
    virtual ACall call_by_index(int idx);
    virtual void receive_sms(SmsPDU);
    virtual int add_inbound_call(const char*);
    virtual int disconnect_call(const char*);
    virtual ACall find_call_by_number(const char*);
    virtual int update_call(const char*, int);
    virtual void set_data_network_type(ADataNetworkType);
    virtual void set_signal_strength_profile(int);
    virtual void update_time();
    virtual int set_phone_number(const char*);
    virtual void set_data_registration(ARegistrationState);
    virtual void set_voice_registration(ARegistrationState);
    virtual ARegistrationState get_data_registration();
    virtual ARegistrationState get_voice_registration();
    virtual void save_sate(SysFile*);
    virtual int load_sate(SysFile* file, int version_id);
    virtual void set_notification_callback_vx(ModemCallback callbackFunc, void* userData);

  private:
    static void modem_callback_forwarder(void* user_data, int numActiveCalls);

    std::unordered_map<std::string, ACallRec> mCalls;
    void* mUserData;
    ModemCallback* mCallbackFunc;
    ARegistrationState mDataState{A_REGISTRATION_HOME};
    ARegistrationState mVoiceState{A_REGISTRATION_HOME};
};

}  // namespace modem
}  // namespace android
