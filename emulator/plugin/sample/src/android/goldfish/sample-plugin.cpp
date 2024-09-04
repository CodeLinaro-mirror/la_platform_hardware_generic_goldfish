// Copyright 2024 The Android Open Source Project
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
#include "android/goldfish/sample-devices.h"

extern "C" {
#include "qom/object.h"
}

#define TYPE_SAMPLE "sample"
#define SAMPLE_DEV(obj) OBJECT_CHECK(SampleDev, (obj), TYPE_SAMPLE)
#define SAMPLE_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(SampleDev, obj, TYPE_SAMPLE)

static void sample_realize(DeviceState* dev, Error** errp) {
    SampleDev* sample = SAMPLE_DEV(dev);
    printf("sample_realize: with %s\n", sample->amessage.c_str());
}

static void sample_set_message(Object* obj, const char* value, Error** errp) {
    SampleDev* sample = SAMPLE_DEV(obj);
    sample->amessage = value;
    printf("You set the property message to: %s\n", sample->amessage.c_str());
}

static void sample_class_init(ObjectClass* oc, void* data) {
    object_class_property_add_str(oc, "message", NULL, sample_set_message);

    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = sample_realize;
}

static const TypeInfo char_sample_type_info = {
        .name = TYPE_SAMPLE,
        .parent = "sample-base",
        .instance_size = sizeof(SampleDev),
        .class_init = sample_class_init,
};

static void register_types(void) {
    type_register_static(&char_sample_type_info);
}

type_init(register_types);
