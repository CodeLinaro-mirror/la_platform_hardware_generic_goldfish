#include "emulator/crashreport/tool/annotation_extractor.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "client/annotation.h"
#include "snapshot/minidump/process_snapshot_minidump.h"

namespace android {
namespace crashreport {

namespace {
// Translate vector to hex string.
std::string vectorToHexString(const std::vector<uint8_t>& data) {
    std::stringstream ss;
    ss << "[";
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < data.size(); i++) {
        ss << "0x" << std::setw(2) << static_cast<int>(data[i]);
    }
    ss << "]";
    return ss.str();
}
}  // namespace

nlohmann::json AnnotationExtractor::Extract(crashpad::FileReader* reader) {
    crashpad::ProcessSnapshotMinidump snapshot;
    if (!snapshot.Initialize(reader)) {
        LOG(ERROR) << "Failed to initialize annotation parser";
        return nullptr;
    }

    nlohmann::json modules;
    for (const crashpad::ModuleSnapshot* module : snapshot.Modules()) {
        if (module->AnnotationsSimpleMap().empty() && module->AnnotationsVector().empty() &&
            module->AnnotationObjects().empty()) {
            continue;
        }
        nlohmann::json json_module;
        json_module["name"] = module->Name();
        json_module["id"] = module->BuildID();
        json_module["address"] = module->Address();
        if (!module->AnnotationsSimpleMap().empty()) {
            json_module["simple_annotations"] = std::vector<nlohmann::json>();
            for (const auto& kv : module->AnnotationsSimpleMap()) {
                json_module["simple_annotations"].push_back({"name", kv.first, "value", kv.second});
            }
        }
        if (!module->AnnotationsVector().empty()) {
            json_module["vectored_annotations"] = module->AnnotationsVector();
        }
        if (!module->AnnotationObjects().empty()) {
            json_module["annotation_objects"] = std::vector<nlohmann::json>();
            for (const crashpad::AnnotationSnapshot& annotation : module->AnnotationObjects()) {
                nlohmann::json json_annotation;
                json_annotation["name"] = annotation.name;
                if (annotation.type != static_cast<uint16_t>(crashpad::Annotation::Type::kString)) {
                    json_annotation["value"] = vectorToHexString(annotation.value);
                } else {
                    std::string value(reinterpret_cast<const char*>(annotation.value.data()),
                                      annotation.value.size());
                    json_annotation["value"] = value;
                }
                json_module["annotation_objects"].push_back(json_annotation);
            }
        }
        modules.push_back(json_module);
    }
    return modules;
}

}  // namespace crashreport
}  // namespace android
