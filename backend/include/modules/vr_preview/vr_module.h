#pragma once

#include "modules/common/imodule.h"
#include "modules/common/thread_pool.h"
#include <memory>
#include <atomic>
#include <vector>
#include <future>
#include <nlohmann/json.hpp>

namespace porcelain_monitor {
namespace modules {

enum class VRRenderMode {
    WIREFRAME = 0,
    SOLID = 1,
    TEXTURED = 2
};

enum class VRPlatform {
    OPENVR = 0,
    OPENXR = 1,
    WEBXR = 2
};

struct VRVertexData {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float nx = 0.0f, ny = 0.0f, nz = 1.0f;
    float u = 0.0f, v = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

struct VRModelData {
    std::vector<VRVertexData> vertices;
    std::vector<uint32_t> indices;
    bool has_texture = false;
    std::string texture_path;
};

struct VRCrackRepresentation {
    int crack_id = 0;
    VRModelData geometry;
    double original_depth_um = 0.0;
    double repaired_depth_um = 0.0;
    double repair_quality = 0.0;
};

struct VRPreviewConfig {
    VRPlatform platform = VRPlatform::OPENXR;
    VRRenderMode render_mode = VRRenderMode::SOLID;
    float ipd_mm = 63.5f;
    float fov_degrees = 110.0f;
    float pixel_density = 1.5f;
    int render_resolution_width = 2160;
    int render_resolution_height = 2160;
    int max_polycount_per_view = 500000;
    bool enable_lod = true;
    bool enable_repair_visualization = true;
    bool export_glb = true;
    int num_preview_angles = 8;
};

struct VRPreviewResult {
    bool success = false;
    VRModelData porcelain_model;
    std::vector<VRCrackRepresentation> cracks;
    std::vector<std::string> preview_image_paths;
    std::string glb_export_path;
    double build_ms = 0.0;
    double render_ms = 0.0;
    double export_ms = 0.0;
    int total_triangles = 0;
};

class VRPreviewModule : public IModule {
public:
    explicit VRPreviewModule(size_t num_workers = 0);
    ~VRPreviewModule() override;

    bool initialize(const json& config) override;
    bool execute(const json& input, ResultCallback callback = nullptr) override;
    bool execute_sync(const json& input, ModuleResult& result) override;
    ModuleStatus get_status() const override { return status_; }
    std::string get_name() const override { return "vr_preview"; }
    std::string get_version() const override { return "2.0.0"; }
    bool cancel() override;
    bool shutdown() override;
    bool supports_async() const override { return true; }
    bool supports_parallel() const override { return true; }

    VRPreviewResult build_preview_sync(int porcelain_id,
                                       const json& porcelain_geometry,
                                       const json& cracks,
                                       const json& repairs,
                                       const json& material_overrides = {});

    std::future<VRPreviewResult> build_preview_async(
        int porcelain_id,
        const json& porcelain_geometry,
        const json& cracks,
        const json& repairs,
        const json& material_overrides = {});

    std::vector<std::future<VRPreviewResult>> build_batch_preview(
        const std::vector<std::tuple<int, json, json, json>>& batch);

    bool supports_platform(VRPlatform p) const;

    void set_render_mode(VRRenderMode m) { preview_config_.render_mode = m; }
    VRRenderMode get_render_mode() const { return preview_config_.render_mode; }
    const VRPreviewConfig& get_config() const { return preview_config_; }

private:
    void run_async_impl(int porcelain_id,
                        const json porcelain_geometry,
                        const json cracks,
                        const json repairs,
                        const json material_overrides,
                        ResultCallback callback);

    json result_to_json(const VRPreviewResult& result) const;
    VRModelData generate_porcelain_mesh(double thickness_mm, double diameter_mm, int resolution);
    VRCrackRepresentation generate_crack_mesh(const json& crack,
                                               const json& repair,
                                               double porcelain_diameter_mm);
    std::string generate_stub_image(float r, float g, float b, const std::string& label);

    std::unique_ptr<ThreadPool> worker_pool_;
    std::atomic<ModuleStatus> status_{ModuleStatus::UNINITIALIZED};
    std::atomic<bool> cancelled_{false};
    VRPreviewConfig preview_config_;
};

}
}
