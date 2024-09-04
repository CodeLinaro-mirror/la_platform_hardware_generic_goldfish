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
#include <iostream>

#include "android/goldfish/sample-devices.h"

#define TYPE_SAMPLE "sample-base"
#define SAMPLE_BASE_DEV(obj) OBJECT_CHECK(SampleBaseDev, (obj), TYPE_SAMPLE)
#define SAMPLE_BASE_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(SampleBaseDev, obj, TYPE_SAMPLE)

static void sample_base_realize(DeviceState* dev, Error** errp) {
    SampleBaseDev* sample = SAMPLE_BASE_DEV(dev);
    std::cout << "You realized sample-base with: " << sample->base << std::endl;
}

static void sample_base_set_base(Object* obj, const char* value, Error** errp) {
    SampleBaseDev* sample = SAMPLE_BASE_DEV(obj);
    printf("sample_base_set_base %s\n", value);
    sample->base = value;
    std::cout << "You set the property base to: " << sample->base << std::endl;
}

static void sample_base_class_init(ObjectClass* oc, void* data) {
    printf("sample_base_class_init\n");
    object_class_property_add_str(oc, "base", NULL, sample_base_set_base);

    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = sample_base_realize;
}

static void sample_base_instance_init(Object* obj) {
    printf("sample_base_instance_init\n");
    SampleBaseDev* sample = SAMPLE_BASE_DEV(obj);
    // Construct the std::string member in the pre-allocated memory using
    // placement new
    // new (&sample->base) std::string();
}

static const TypeInfo char_sample_base_type_info = {
        .name = TYPE_SAMPLE,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(SampleBaseDev),
        .instance_init = sample_base_instance_init,
        .class_init = sample_base_class_init,
};

static void register_types(void) {
    printf("Registering sample_base\n");
    type_register_static(&char_sample_base_type_info);
}

type_init(register_types);
