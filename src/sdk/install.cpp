#include "sdk/install.h"

#include <string>

#include "model_types.pb.h"

#include "rac/core/rac_core.h"
#include "rac/core/rac_model_lifecycle.h"
#include "rac/foundation/rac_proto_buffer.h"
#include "rac/infrastructure/model_management/rac_model_registry.h"

namespace rcli::sdk {
namespace {

namespace v1 = runanywhere::v1;

v1::InferenceFramework Framework(catalog::Backend backend) {
    switch (backend) {
        case catalog::Backend::LlamaCpp: return v1::INFERENCE_FRAMEWORK_LLAMA_CPP;
        case catalog::Backend::Mlx: return v1::INFERENCE_FRAMEWORK_MLX;
        case catalog::Backend::NeuRT: return v1::INFERENCE_FRAMEWORK_COREML;
        case catalog::Backend::Onnx: return v1::INFERENCE_FRAMEWORK_ONNX;
        case catalog::Backend::Sherpa: return v1::INFERENCE_FRAMEWORK_SHERPA;
    }
    return v1::INFERENCE_FRAMEWORK_UNSPECIFIED;
}

v1::ModelCategory Category(catalog::Category category) {
    switch (category) {
        case catalog::Category::Language: return v1::MODEL_CATEGORY_LANGUAGE;
        case catalog::Category::Multimodal: return v1::MODEL_CATEGORY_MULTIMODAL;
        case catalog::Category::SpeechRecognition: return v1::MODEL_CATEGORY_SPEECH_RECOGNITION;
        case catalog::Category::SpeechSynthesis: return v1::MODEL_CATEGORY_SPEECH_SYNTHESIS;
        case catalog::Category::VoiceActivityDetection:
            return v1::MODEL_CATEGORY_VOICE_ACTIVITY_DETECTION;
        case catalog::Category::SpeakerDiarization: return v1::MODEL_CATEGORY_SPEAKER_DIARIZATION;
        case catalog::Category::SemanticSegmentation:
            return v1::MODEL_CATEGORY_SEMANTIC_SEGMENTATION;
        case catalog::Category::Embedding: return v1::MODEL_CATEGORY_EMBEDDING;
        case catalog::Category::ImageGeneration: return v1::MODEL_CATEGORY_IMAGE_GENERATION;
    }
    return v1::MODEL_CATEGORY_UNSPECIFIED;
}

v1::ModelFormat FileFormat(catalog::Format format) {
    switch (format) {
        case catalog::Format::Gguf: return v1::MODEL_FORMAT_GGUF;
        case catalog::Format::Onnx: return v1::MODEL_FORMAT_ONNX;
        case catalog::Format::Safetensors: return v1::MODEL_FORMAT_SAFETENSORS;
        case catalog::Format::Mlpackage: return v1::MODEL_FORMAT_MLPACKAGE;
        case catalog::Format::Unspecified: return v1::MODEL_FORMAT_UNSPECIFIED;
    }
    return v1::MODEL_FORMAT_UNSPECIFIED;
}

}  // namespace

bool Register(const catalog::Model& model, std::string* error) {
    if (!catalog::Installable(model)) {
        if (error != nullptr) {
            *error = std::string(model.id) + " is a multi-file model; not supported yet";
        }
        return false;
    }

    v1::ModelInfo info;
    info.set_id(std::string(model.id));
    info.set_name(std::string(model.name));
    info.set_category(Category(model.category));
    info.set_framework(Framework(model.backend));
    info.set_format(FileFormat(model.format));
    info.set_download_url(std::string(model.url));
    info.set_download_size_bytes(model.bytes);
    info.set_source(v1::MODEL_SOURCE_REMOTE);
    if (model.context_length > 0) {
        info.set_context_length(model.context_length);
    }

    std::string bytes;
    if (!info.SerializeToString(&bytes)) {
        if (error != nullptr) {
            *error = "could not serialize the registration";
        }
        return false;
    }
    const rac_result_t rc = rac_model_registry_register_proto(
        rac_get_model_registry(), reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    if (rc != RAC_SUCCESS) {
        if (error != nullptr) {
            *error = "register failed (" + std::to_string(rc) + ")";
        }
        return false;
    }
    return true;
}

bool Install(const catalog::Model& model, std::function<void(Progress)> on_progress,
             std::string* error) {
    if (!Register(model, error)) {
        return false;
    }
    if (on_progress) {
        on_progress({"starting", 0});
    }

    v1::ModelLoadRequest request;
    request.set_model_id(std::string(model.id));
    // The whole point of this flag: a missing model is fetched rather than
    // reported absent.
    request.set_validate_availability(true);

    std::string bytes;
    request.SerializeToString(&bytes);

    rac_proto_buffer_t out;
    rac_proto_buffer_init(&out);
    const rac_result_t rc = rac_model_lifecycle_load_proto(
        rac_get_model_registry(), reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(),
        &out);

    bool ok = rc == RAC_SUCCESS;
    if (ok) {
        v1::ModelLoadResult result;
        if (result.ParseFromArray(out.data, static_cast<int>(out.size)) && result.has_error()) {
            ok = false;
            if (error != nullptr) {
                *error = result.error().message().empty() ? "install failed"
                                                          : result.error().message();
            }
        }
    } else if (error != nullptr) {
        *error = "install failed (" + std::to_string(rc) + ")";
    }
    rac_proto_buffer_free(&out);

    if (on_progress) {
        on_progress({ok ? "done" : "failed", ok ? 100 : 0});
    }
    return ok;
}

}  // namespace rcli::sdk
