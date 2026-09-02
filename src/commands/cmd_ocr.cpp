/**
 * @file cmd_ocr.cpp
 * @brief `rcli ocr <image.ppm> --model <ref>` — read the text on a page.
 *
 * Mirrors cmd_segment's shape (bootstrap → resolve model → one commons path → render), against
 * the OCR service promoted to `ocr_ops` in ABI v11:
 *
 *   rac_ocr_create(model) → rac_ocr_initialize(path) → rac_ocr_read_page(image, &result)
 *     → rac_ocr_result_free / _cleanup / _destroy
 *
 * `read_page` and NOT `recognize`. Full-page OCR is two models, and for the family this serves
 * the recognizer's input is a grid-sampled crop of the DETECTOR's feature map rather than a line
 * image — so `recognize` returns NOT_SUPPORTED and there is no line-level verb to expose here.
 *
 * PPM (P6) for the same reason cmd_segment uses it: the dependency-free image format the CLI can
 * decode without linking a codec.
 */
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "commands/commands.h"
#include "commands/model_setup.h"
#include "io/image_io.h"
#include "io/output.h"
#include "rac/features/ocr/rac_ocr_service.h"

namespace rcli::commands {

namespace {

int resolve_model_path(const GlobalOptions& options, const std::string& ref,
                       std::string* out_path) {
    ResolvedModelPaths model;
    const int setup = ensure_model_ready(options, ref, &model);
    if (setup != 0) {
        return setup;
    }
    *out_path = model.primary_path;
    return 0;
}

void print_result(const GlobalOptions& options, const std::string& model_ref,
                  const rac_ocr_result_t& result) {
    if (options.json) {
        out::JsonWriter json;
        json.begin_object()
            .field("model", model_ref)
            .field("region_count", static_cast<int64_t>(result.num_regions))
            .field("processing_time_ms", static_cast<int64_t>(result.processing_time_ms));
        json.begin_array("regions");
        for (size_t i = 0; i < result.num_regions; ++i) {
            const rac_ocr_region_t& r = result.regions[i];
            json.begin_array_object()
                .field("text", r.text ? r.text : "")
                .field("confidence", static_cast<double>(r.confidence));
            // The quad, not just the text. Geometry is half of what OCR
            // produces -- dropping it here would make the JSON strictly less
            // useful than the C API it wraps, and it is what a caller needs to
            // crop, sort into reading order, or draw a box.
            json.begin_array("quad");
            for (int q = 0; q < 8; ++q) {
                json.value(static_cast<double>(r.quad[q]));
            }
            json.end_array().end_object();
        }
        json.end_array().end_object();
        out::result_line(json.str());
        return;
    }

    if (result.num_regions == 0) {
        out::result_line("(no text found)");
    } else {
        std::vector<std::vector<std::string>> rows;
        rows.reserve(result.num_regions);
        for (size_t i = 0; i < result.num_regions; ++i) {
            const rac_ocr_region_t& r = result.regions[i];
            char conf[16];
            std::snprintf(conf, sizeof(conf), "%.2f", static_cast<double>(r.confidence));
            rows.push_back({conf, r.text ? r.text : ""});
        }
        out::table({"conf", "text"}, rows);
    }
    if (options.verbose) {
        out::status_line("(" + std::to_string(result.processing_time_ms) + " ms)");
    }
}

int run_ocr(const GlobalOptions& options, const std::string& image_path,
            const std::string& model_ref) {
    Bootstrapped env;
    if (bootstrap(options, &env) != RAC_SUCCESS) {
        return 1;
    }
    if (model_ref.empty()) {
        out::error_line("--model is required (an OCR model id or on-disk path)");
        return 2;
    }

    std::string model_path;
    const int resolve = resolve_model_path(options, model_ref, &model_path);
    if (resolve != 0) {
        return resolve;
    }

    image::RgbImage src;
    std::string err;
    if (!image::read_ppm(image_path, &src, &err)) {
        out::error_line(err);
        return 1;
    }

    rac_handle_t handle = nullptr;
    rac_result_t rc = rac_ocr_create(model_path.c_str(), &handle);
    if (rc != RAC_SUCCESS) {
        out::error_line("could not create the OCR service: " + out::describe_result(rc));
        return 1;
    }
    rc = rac_ocr_initialize(handle, model_path.c_str());
    if (rc != RAC_SUCCESS) {
        // The manifest refuses a recognizer-only bundle BY NAME, which is what every
        // nemotron-ocr bundle published before nemotron-ocr-v1-full_ANE is.
        out::error_line("could not initialize the OCR model: " + out::describe_result(rc));
        rac_ocr_destroy(handle);
        return 1;
    }

    rac_ocr_image_t image = {};
    image.format = RAC_OCR_FORMAT_RGB8;
    image.pixels = src.rgb.data();
    image.width = src.width;
    image.height = src.height;

    rac_ocr_result_t result = {};
    rc = rac_ocr_read_page(handle, &image, &result);
    if (rc != RAC_SUCCESS) {
        out::error_line("read_page failed: " + out::describe_result(rc));
        rac_ocr_result_free(&result);
        rac_ocr_cleanup(handle);
        rac_ocr_destroy(handle);
        return 1;
    }

    print_result(options, model_ref, result);
    rac_ocr_result_free(&result);
    rac_ocr_cleanup(handle);
    rac_ocr_destroy(handle);
    return 0;
}

}  // namespace

void register_ocr(CLI::App& app, GlobalOptions& options) {
    CLI::App* cmd = app.add_subcommand("ocr", "Read the text on a page image");
    auto image_path = std::make_shared<std::string>();
    auto model = std::make_shared<std::string>();
    cmd->add_option("image", *image_path, "Input image (binary PPM / P6)")
        ->required()
        ->check(CLI::ExistingFile);
    cmd->add_option("--model,-m", *model, "OCR model id or on-disk path")->required();
    cmd->callback([&options, image_path, model]() {
        const int exit_code = run_ocr(options, *image_path, *model);
        if (exit_code != 0) {
            throw CLI::RuntimeError(exit_code);
        }
    });
}

}  // namespace rcli::commands
