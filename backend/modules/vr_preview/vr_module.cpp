#include "modules/vr_preview/vr_module.h"
#include "modules/common/module_factory.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <chrono>
#include <thread>
#include <algorithm>
#include <sstream>

namespace porcelain_monitor {
namespace modules {

using json = nlohmann::json;

static constexpr double PI_D = 3.14159265358979323846;

VRPreviewModule::VRPreviewModule(size_t num_workers) {
    if (num_workers == 0) {
        num_workers = std::max(1u, std::thread::hardware_concurrency());
    }
    worker_pool_ = std::make_unique<ThreadPool>(num_workers);
}

VRPreviewModule::~VRPreviewModule() { shutdown(); }

bool VRPreviewModule::initialize(const json& config) {
    try {
        if (config.contains("preview_config")) {
            const auto& pc = config["preview_config"];
            if (pc.contains("render_mode")) {
                int v = pc["render_mode"].get<int>();
                preview_config_.render_mode = static_cast<VRRenderMode>(std::clamp(v, 0, 2));
            }
            if (pc.contains("platform")) {
                int v = pc["platform"].get<int>();
                preview_config_.platform = static_cast<VRPlatform>(std::clamp(v, 0, 2));
            }
            if (pc.contains("render_resolution_width"))
                preview_config_.render_resolution_width = pc["render_resolution_width"].get<int>();
            if (pc.contains("render_resolution_height"))
                preview_config_.render_resolution_height = pc["render_resolution_height"].get<int>();
            if (pc.contains("enable_repair_visualization"))
                preview_config_.enable_repair_visualization = pc["enable_repair_visualization"].get<bool>();
            if (pc.contains("export_glb"))
                preview_config_.export_glb = pc["export_glb"].get<bool>();
            if (pc.contains("num_preview_angles"))
                preview_config_.num_preview_angles = pc["num_preview_angles"].get<int>();
        }
        status_ = ModuleStatus::IDLE;
        return true;
    } catch (const std::exception& e) {
        status_ = ModuleStatus::ERROR;
        return false;
    }
}

bool VRPreviewModule::supports_platform(VRPlatform p) const {
    switch (p) {
    case VRPlatform::OPENVR: return true;
    case VRPlatform::OPENXR: return true;
    case VRPlatform::WEBXR:  return true;
    }
    return false;
}

VRModelData VRPreviewModule::generate_porcelain_mesh(
    double thickness_mm, double diameter_mm, int resolution)
{
    VRModelData mesh;
    const double R = diameter_mm / 2.0;
    const double h = thickness_mm;

    resolution = std::max(8, resolution);

    mesh.vertices.reserve(2 * (resolution + 1) + 2 * resolution);
    mesh.indices.reserve(6 * resolution * 2);

    for (int ring = 0; ring < 2; ++ring) {
        double z = ring * h;
        for (int i = 0; i <= resolution; ++i) {
            double a = 2.0 * PI_D * i / resolution;
            float x = static_cast<float>(R * std::cos(a));
            float y = static_cast<float>(R * std::sin(a));
            float nx = static_cast<float>(std::cos(a));
            float ny = static_cast<float>(std::sin(a));
            VRVertexData v;
            v.x = x; v.y = y; v.z = static_cast<float>(z);
            v.nx = nx; v.ny = ny; v.nz = 0.0f;
            v.u = static_cast<float>(i) / static_cast<float>(resolution);
            v.v = static_cast<float>(ring);
            v.r = 0.92f; v.g = 0.90f; v.b = 0.85f; v.a = 1.0f;
            mesh.vertices.push_back(v);
        }
    }

    for (int i = 0; i < resolution; ++i) {
        int a = i, b = i + 1;
        int c = i + (resolution + 1), d = i + 1 + (resolution + 1);
        mesh.indices.push_back(a); mesh.indices.push_back(c); mesh.indices.push_back(b);
        mesh.indices.push_back(b); mesh.indices.push_back(c); mesh.indices.push_back(d);
    }

    for (int ring = 0; ring < 2; ++ring) {
        double z = ring * h;
        float nz = (ring == 0) ? -1.0f : 1.0f;
        VRVertexData center;
        center.x = 0.0f; center.y = 0.0f; center.z = static_cast<float>(z);
        center.nx = 0.0f; center.ny = 0.0f; center.nz = nz;
        center.u = 0.5f; center.v = 0.5f;
        center.r = 0.95f; center.g = 0.93f; center.b = 0.88f; center.a = 1.0f;
        mesh.vertices.push_back(center);
        int center_idx = static_cast<int>(mesh.vertices.size()) - 1;

        for (int i = 0; i <= resolution; ++i) {
            double a = 2.0 * PI_D * i / resolution;
            VRVertexData v;
            v.x = static_cast<float>(R * std::cos(a));
            v.y = static_cast<float>(R * std::sin(a));
            v.z = static_cast<float>(z);
            v.nx = 0.0f; v.ny = 0.0f; v.nz = nz;
            v.u = 0.5f + 0.5f * static_cast<float>(std::cos(a));
            v.v = 0.5f + 0.5f * static_cast<float>(std::sin(a));
            v.r = 0.95f; v.g = 0.93f; v.b = 0.88f; v.a = 1.0f;
            mesh.vertices.push_back(v);
        }

        for (int i = 0; i < resolution; ++i) {
            int p1 = center_idx + 1 + i;
            int p2 = center_idx + 1 + i + 1;
            if (ring == 0) {
                mesh.indices.push_back(center_idx);
                mesh.indices.push_back(p2);
                mesh.indices.push_back(p1);
            } else {
                mesh.indices.push_back(center_idx);
                mesh.indices.push_back(p1);
                mesh.indices.push_back(p2);
            }
        }
    }

    return mesh;
}

VRCrackRepresentation VRPreviewModule::generate_crack_mesh(
    const json& crack,
    const json& repair,
    double porcelain_diameter_mm)
{
    VRCrackRepresentation cr;
    cr.crack_id = crack.value("id", 0);
    cr.original_depth_um = crack.value("max_depth", 0.0);
    double sx = crack.value("start_x", 0.0);
    double sy = crack.value("start_y", 0.0);
    double ex = crack.value("end_x", 0.0);
    double ey = crack.value("end_y", 0.0);
    double depth_mm = std::max(0.001, cr.original_depth_um / 1000.0);
    double width_mm = std::max(0.0005, crack.value("max_width", 10.0) / 1000.0);

    bool is_repaired = preview_config_.enable_repair_visualization && !repair.empty();
    cr.repaired_depth_um = is_repaired ? repair.value("final_depth_um", 0.0) : cr.original_depth_um;
    cr.repair_quality = is_repaired ? repair.value("repair_quality", 0.0) : 0.0;

    double dx = ex - sx, dy = ey - sy;
    double len = std::sqrt(dx * dx + dy * dy);
    double ux = (len > 0) ? -dy / len : 0.0;
    double uy = (len > 0) ? dx / len : 1.0;

    double x1 = sx + ux * width_mm * 0.5;
    double y1 = sy + uy * width_mm * 0.5;
    double x2 = sx - ux * width_mm * 0.5;
    double y2 = sy - uy * width_mm * 0.5;
    double x3 = ex + ux * width_mm * 0.5;
    double y3 = ey + uy * width_mm * 0.5;
    double x4 = ex - ux * width_mm * 0.5;
    double y4 = ey - uy * width_mm * 0.5;

    float crack_r = is_repaired ? 0.35f : 0.6f;
    float crack_g = is_repaired ? 0.75f : 0.15f;
    float crack_b = is_repaired ? 0.95f : 0.10f;
    float crack_a = 0.9f;

    auto push_vert = [&](double x, double y, double z,
                         float nx, float ny, float nz) {
        VRVertexData v;
        v.x = static_cast<float>(x);
        v.y = static_cast<float>(y);
        v.z = static_cast<float>(z);
        v.nx = nx; v.ny = ny; v.nz = nz;
        v.u = 0.0f; v.v = 0.0f;
        v.r = crack_r; v.g = crack_g; v.b = crack_b; v.a = crack_a;
        cr.geometry.vertices.push_back(v);
    };

    push_vert(x1, y1, 0.0, 0.0f, 0.0f, 1.0f);
    push_vert(x2, y2, 0.0, 0.0f, 0.0f, 1.0f);
    push_vert(x3, y3, 0.0, 0.0f, 0.0f, 1.0f);
    push_vert(x4, y4, 0.0, 0.0f, 0.0f, 1.0f);
    push_vert(x1, y1, depth_mm, 0.0f, 0.0f, -1.0f);
    push_vert(x2, y2, depth_mm, 0.0f, 0.0f, -1.0f);
    push_vert(x3, y3, depth_mm, 0.0f, 0.0f, -1.0f);
    push_vert(x4, y4, depth_mm, 0.0f, 0.0f, -1.0f);

    cr.geometry.indices = {
        0, 1, 2,  1, 3, 2,
        4, 6, 5,  5, 6, 7,
        0, 4, 5,  0, 5, 1,
        2, 3, 7,  2, 7, 6,
        1, 5, 7,  1, 7, 3,
        0, 2, 6,  0, 6, 4
    };
    (void)porcelain_diameter_mm;
    return cr;
}

std::string VRPreviewModule::generate_stub_image(float r, float g, float b, const std::string& label) {
    std::ostringstream os;
    os << "stub_" << static_cast<int>(r * 255) << "_" << static_cast<int>(g * 255) << "_"
       << static_cast<int>(b * 255) << "_" << label << ".png";
    return os.str();
}

VRPreviewResult VRPreviewModule::build_preview_sync(
    int porcelain_id,
    const json& porcelain_geometry,
    const json& cracks,
    const json& repairs,
    const json& material_overrides)
{
    VRPreviewResult result;
    result.success = false;
    (void)material_overrides;

    auto t_start = std::chrono::high_resolution_clock::now();

    double thickness = porcelain_geometry.value("thickness_mm", 3.0);
    double diameter = porcelain_geometry.value("diameter_mm", 100.0);
    int resolution = porcelain_geometry.value("mesh_resolution", 32);

    result.porcelain_model = generate_porcelain_mesh(thickness, diameter, resolution);

    std::vector<std::future<VRCrackRepresentation>> crack_futures;
    crack_futures.reserve(cracks.size());

    for (size_t i = 0; i < cracks.size(); ++i) {
        const auto& c = cracks[i];
        json repair_j = (i < repairs.size()) ? repairs[i] : json{};
        crack_futures.push_back(worker_pool_->submit(
            [this, &c, repair_j, diameter]() -> VRCrackRepresentation {
                return generate_crack_mesh(c, repair_j, diameter);
            }
        ));
    }

    for (auto& f : crack_futures) {
        result.cracks.push_back(f.get());
    }

    result.total_triangles = static_cast<int>(
        result.porcelain_model.indices.size() / 3);
    for (const auto& c : result.cracks) {
        result.total_triangles += static_cast<int>(c.geometry.indices.size() / 3);
    }

    auto t_build_end = std::chrono::high_resolution_clock::now();
    result.build_ms = std::chrono::duration<double, std::milli>(t_build_end - t_start).count();

    if (preview_config_.export_glb) {
        std::ostringstream os;
        os << "vr_preview_" << porcelain_id << ".glb";
        result.glb_export_path = os.str();
    }

    int angles = preview_config_.num_preview_angles;
    for (int i = 0; i < angles; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(std::max(1, angles - 1));
        float hue_r = t;
        float hue_g = 1.0f - std::fabs(0.5f - t) * 2.0f;
        float hue_b = 1.0f - t;
        std::ostringstream label;
        label << porcelain_id << "_angle" << i;
        result.preview_image_paths.push_back(
            generate_stub_image(hue_r, hue_g, hue_b, label.str()));
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    result.render_ms = std::chrono::duration<double, std::milli>(t_end - t_build_end).count();
    result.export_ms = result.render_ms * 0.2;
    result.success = true;
    (void)cancelled_;
    return result;
}

std::future<VRPreviewResult> VRPreviewModule::build_preview_async(
    int porcelain_id,
    const json& porcelain_geometry,
    const json& cracks,
    const json& repairs,
    const json& material_overrides)
{
    return worker_pool_->submit(
        [this, porcelain_id, porcelain_geometry, cracks, repairs, material_overrides]()
            -> VRPreviewResult {
            return build_preview_sync(porcelain_id, porcelain_geometry,
                                      cracks, repairs, material_overrides);
        }
    );
}

std::vector<std::future<VRPreviewResult>> VRPreviewModule::build_batch_preview(
    const std::vector<std::tuple<int, json, json, json>>& batch)
{
    std::vector<std::future<VRPreviewResult>> futures;
    futures.reserve(batch.size());
    for (const auto& item : batch) {
        futures.push_back(worker_pool_->submit(
            [this, &item]() -> VRPreviewResult {
                return build_preview_sync(
                    std::get<0>(item), std::get<1>(item),
                    std::get<2>(item), std::get<3>(item));
            }
        ));
    }
    return futures;
}

void VRPreviewModule::run_async_impl(
    int porcelain_id,
    const json porcelain_geometry,
    const json cracks,
    const json repairs,
    const json material_overrides,
    ResultCallback callback)
{
    ModuleResult result;
    result.success = false;
    result.error_message = "";
    try {
        auto r = build_preview_sync(porcelain_id, porcelain_geometry,
                                    cracks, repairs, material_overrides);
        result.data = result_to_json(r);
        result.success = r.success;
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    status_ = cancelled_ ? ModuleStatus::IDLE : ModuleStatus::COMPLETED;
    if (callback) callback(result);
}

bool VRPreviewModule::execute(const json& input, ResultCallback callback) {
    if (status_ == ModuleStatus::RUNNING) return false;
    cancelled_ = false;
    status_ = ModuleStatus::RUNNING;

    int porcelain_id = input.value("porcelain_id", 0);
    json geo = input.contains("porcelain_geometry") ? input["porcelain_geometry"] : json{};
    json cracks = input.contains("cracks") ? input["cracks"] : json::array();
    json repairs = input.contains("repairs") ? input["repairs"] : json::array();
    json mat = input.contains("material_overrides") ? input["material_overrides"] : json{};

    worker_pool_->submit([this, porcelain_id, geo, cracks, repairs, mat, callback]() {
        run_async_impl(porcelain_id, geo, cracks, repairs, mat, callback);
    });
    return true;
}

bool VRPreviewModule::execute_sync(const json& input, ModuleResult& result) {
    if (status_ == ModuleStatus::RUNNING) return false;
    status_ = ModuleStatus::RUNNING;
    cancelled_ = false;

    try {
        int porcelain_id = input.value("porcelain_id", 0);
        json geo = input.contains("porcelain_geometry") ? input["porcelain_geometry"] : json{};
        json cracks = input.contains("cracks") ? input["cracks"] : json::array();
        json repairs = input.contains("repairs") ? input["repairs"] : json::array();
        json mat = input.contains("material_overrides") ? input["material_overrides"] : json{};

        auto r = build_preview_sync(porcelain_id, geo, cracks, repairs, mat);
        result.data = result_to_json(r);
        result.success = r.success;
        result.error_message = "";
        status_ = ModuleStatus::COMPLETED;
        return true;
    } catch (const std::exception& e) {
        result.error_message = e.what();
        result.success = false;
        status_ = ModuleStatus::ERROR;
        return false;
    }
}

json VRPreviewModule::result_to_json(const VRPreviewResult& r) const {
    json j;
    j["success"] = r.success;
    j["build_ms"] = r.build_ms;
    j["render_ms"] = r.render_ms;
    j["export_ms"] = r.export_ms;
    j["total_triangles"] = r.total_triangles;
    j["glb_export_path"] = r.glb_export_path;

    json previews = json::array();
    for (const auto& p : r.preview_image_paths) previews.push_back(p);
    j["preview_images"] = previews;

    j["porcelain_model"] = {
        {"vertex_count", static_cast<int>(r.porcelain_model.vertices.size())},
        {"index_count", static_cast<int>(r.porcelain_model.indices.size())},
        {"has_texture", r.porcelain_model.has_texture}
    };

    json cracks_j = json::array();
    for (const auto& c : r.cracks) {
        cracks_j.push_back({
            {"crack_id", c.crack_id},
            {"original_depth_um", c.original_depth_um},
            {"repaired_depth_um", c.repaired_depth_um},
            {"repair_quality", c.repair_quality},
            {"vertex_count", static_cast<int>(c.geometry.vertices.size())},
            {"triangle_count", static_cast<int>(c.geometry.indices.size() / 3)}
        });
    }
    j["cracks"] = cracks_j;
    return j;
}

bool VRPreviewModule::cancel() {
    cancelled_ = true;
    status_ = ModuleStatus::IDLE;
    return true;
}

bool VRPreviewModule::shutdown() {
    cancel();
    if (worker_pool_) worker_pool_->shutdown();
    worker_pool_.reset();
    status_ = ModuleStatus::UNINITIALIZED;
    return true;
}

REGISTER_MODULE(VRPreviewModule, "vr_preview")

}
}
