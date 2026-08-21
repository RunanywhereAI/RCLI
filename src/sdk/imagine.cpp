#include "sdk/imagine.h"

#include <filesystem>
#include <vector>

#include "diffusion_options.pb.h"
#include "model_types.pb.h"

#include "rac/core/rac_core.h"
#include "rac/core/rac_model_lifecycle.h"
#include "rac/features/diffusion/rac_diffusion_service.h"
#include "rac/foundation/rac_proto_buffer.h"

#include "catalog/catalog.h"
#include "media/png.h"
#include "sdk/install.h"
#include "sdk/llm.h"
#include "sdk/session.h"

namespace rcli::sdk {
namespace {

namespace fs = std::filesystem;
namespace v1 = runanywhere::v1;

/// The engines return raw pixels here, whatever `media_type` claims: a 512x512
/// result arrives as exactly width*height*4 bytes of RGBA. Writing those into a
/// .png produces a file no viewer opens, so the payload is measured rather than
/// believed and encoded on the way out.
std::vector<std::uint8_t> ToRgb(const std::string& data, int width, int height) {
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (pixels == 0) {
        return {};
    }
    const std::size_t stride = data.size() / pixels;
    if (stride != 3 && stride != 4) {
        return {};
    }
    std::vector<std::uint8_t> rgb(pixels * 3);
    for (std::size_t i = 0; i < pixels; ++i) {
        rgb[i * 3 + 0] = static_cast<std::uint8_t>(data[i * stride + 0]);
        rgb[i * 3 + 1] = static_cast<std::uint8_t>(data[i * stride + 1]);
        rgb[i * 3 + 2] = static_cast<std::uint8_t>(data[i * stride + 2]);
    }
    return rgb;
}

}  // namespace

std::string Imagine::DefaultModel() {
    for (const LocalModel& local : LocalModels()) {
        for (const catalog::Model& entry : catalog::All()) {
            if (entry.id == local.id && entry.category == catalog::Category::ImageGeneration) {
                return local.id;
            }
        }
    }
    return {};
}

bool Imagine::Load(const std::string& model_id, std::string* error) {
    for (const catalog::Model& model : catalog::All()) {
        if (model.id == model_id && catalog::Installable(model)) {
            Register(model, nullptr);
            break;
        }
    }
    v1::ModelLoadRequest request;
    request.set_model_id(model_id);
    request.set_category(v1::MODEL_CATEGORY_IMAGE_GENERATION);
    request.set_validate_availability(true);

    std::string bytes;
    if (!request.SerializeToString(&bytes)) {
        if (error != nullptr) {
            *error = "could not serialize the load request";
        }
        return false;
    }
    rac_proto_buffer_t out;
    rac_proto_buffer_init(&out);
    const rac_result_t rc = rac_model_lifecycle_load_proto(
        rac_get_model_registry(), reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(),
        &out);
    bool ok = rc == RAC_SUCCESS;
    std::string message = ok ? "" : "load failed (" + std::to_string(rc) + ")";
    if (ok) {
        v1::ModelLoadResult result;
        if (result.ParseFromArray(out.data, static_cast<int>(out.size)) && result.has_error()) {
            ok = false;
            message = result.error().message();
        }
    }
    rac_proto_buffer_free(&out);
    if (error != nullptr) {
        *error = message;
    }
    if (ok) {
        model_id_ = model_id;
    }
    return ok;
}

Imagine::Result Imagine::Draw(const std::string& prompt, std::function<void(float)> on_step,
                              std::string* error) {
    Result result;
    if (model_id_.empty()) {
        if (error != nullptr) {
            *error = "no image model loaded";
        }
        return result;
    }
    // The lifecycle entry point takes no progress callback, so steps are not
    // reported on this path. Saying so once beats a bar that never moves.
    if (on_step) {
        on_step(0.0F);
    }

    v1::DiffusionGenerationRequest request;
    request.set_model_id(model_id_);
    request.mutable_options()->set_prompt(prompt);

    std::string bytes;
    if (!request.SerializeToString(&bytes)) {
        if (error != nullptr) {
            *error = "could not serialize the prompt";
        }
        return result;
    }
    rac_proto_buffer_t out;
    rac_proto_buffer_init(&out);
    const rac_result_t rc = rac_diffusion_generate_lifecycle_proto(
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &out);

    std::string message;
    v1::DiffusionResult reply;
    if (rc != RAC_SUCCESS) {
        message = "generation failed (" + std::to_string(rc) + ")";
    } else if (!reply.ParseFromArray(out.data, static_cast<int>(out.size))) {
        message = "the engine returned something unreadable";
    } else if (reply.images().empty()) {
        message = "the engine produced no image";
    }
    rac_proto_buffer_free(&out);

    if (message.empty()) {
        const v1::DiffusionImage& image = reply.images(0);
        const std::vector<std::uint8_t> rgb =
            ToRgb(image.data(), image.width(), image.height());
        if (rgb.empty()) {
            message = "the engine returned " + std::to_string(image.data().size()) +
                      " bytes for a " + std::to_string(image.width()) + "x" +
                      std::to_string(image.height()) + " image, which is neither RGB nor RGBA";
        } else {
            std::error_code ec;
            const fs::path dir = fs::path(std::string(Session::Instance().home())) / "Images";
            fs::create_directories(dir, ec);
            // The seed comes back 0 from this engine, so it cannot name the
            // file on its own: every picture would overwrite the last one.
            fs::path path = dir / ("rcli-" + std::to_string(image.seed_used()) + ".png");
            for (int n = 2; fs::exists(path, ec); ++n) {
                path = dir / ("rcli-" + std::to_string(image.seed_used()) + "-" +
                              std::to_string(n) + ".png");
            }
            if (!media::WritePng(path.string(), rgb.data(), image.width(), image.height(),
                                 &message)) {
                // WritePng filled in the reason.
            } else {
                result.path = path.string();
                result.width = image.width();
                result.height = image.height();
                result.seed = image.seed_used();
            }
        }
    }
    if (on_step) {
        on_step(1.0F);
    }
    if (error != nullptr) {
        *error = message;
    }
    return result;
}

}  // namespace rcli::sdk
