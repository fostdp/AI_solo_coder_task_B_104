#include "test_framework.h"
#include "modules/common/imodule.h"
#include "modules/common/module_factory.h"
#include "modules/common/thread_pool.h"
#include "modules/common/ipc_channel.h"
#include "modules/fem_stress/fem_stress_module.h"
#include "modules/washburn_penetration/washburn_module.h"
#include "modules/strength_fem/strength_module.h"
#include "modules/vr_preview/vr_module.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <vector>

using namespace porcelain_monitor;
using namespace porcelain_monitor::modules;
using json = nlohmann::json;

namespace {

TEST(ModuleRegressionTests, ModuleFactory_Registered) {
    auto& factory = ModuleFactory::instance();

    ASSERT_TRUE(factory.is_registered("fem_stress"));
    ASSERT_TRUE(factory.is_registered("washburn_penetration"));
    ASSERT_TRUE(factory.is_registered("strength_fem"));
    ASSERT_TRUE(factory.is_registered("vr_preview"));

    auto names = factory.get_registered_modules();
    ASSERT_GE(names.size(), 4);
}

TEST(ModuleRegressionTests, FemStressModule_CreateAndDestroy) {
    auto module = ModuleFactory::instance().create("fem_stress");
    ASSERT_NE(module, nullptr);
    ASSERT_EQ(module->get_name(), "fem_stress");
    ASSERT_EQ(module->get_version(), "2.0.0");
    ASSERT_TRUE(module->supports_async());
    ASSERT_TRUE(module->supports_parallel());
}

TEST(ModuleRegressionTests, FemStressModule_Initialize) {
    FemStressModule module;

    json config;
    config["fem_config"]["grid_resolution"] = 30;
    config["fem_config"]["use_adaptive_mesh"] = true;
    config["fem_config"]["max_refinement_level"] = 2;

    ASSERT_TRUE(module.initialize(config));
    ASSERT_EQ(module.get_status(), ModuleStatus::IDLE);

    module.shutdown();
    ASSERT_EQ(module.get_status(), ModuleStatus::UNINITIALIZED);
}

TEST(ModuleRegressionTests, FemStressModule_ExecuteSync) {
    FemStressModule module;

    json config;
    config["fem_config"]["grid_resolution"] = 20;
    config["fem_config"]["use_adaptive_mesh"] = false;
    ASSERT_TRUE(module.initialize(config));

    json input;
    input["porcelain_id"] = 1;
    json cracks = json::array();
    json crack;
    crack["id"] = 1;
    crack["start_x"] = 10.0;
    crack["start_y"] = 10.0;
    crack["end_x"] = 20.0;
    crack["end_y"] = 10.0;
    crack["depth_um"] = 50.0;
    crack["width_um"] = 5.0;
    cracks.push_back(crack);
    input["cracks"] = cracks;

    json dims;
    dims["thickness_mm"] = 3.0;
    dims["diameter_mm"] = 50.0;
    input["dimensions"] = dims;

    ModuleResult result;
    ASSERT_TRUE(module.execute_sync(input, result));
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.data.contains("max_von_mises_mpa"));
    ASSERT_TRUE(result.data.contains("total_nodes"));
    ASSERT_GT(result.data["max_von_mises_mpa"].get<double>(), 0.0);

    module.shutdown();
}

TEST(ModuleRegressionTests, FemStressModule_ExecuteAsync) {
    FemStressModule module;

    json config;
    config["fem_config"]["grid_resolution"] = 20;
    config["fem_config"]["use_adaptive_mesh"] = false;
    ASSERT_TRUE(module.initialize(config));

    json input;
    json cracks = json::array();
    json crack;
    crack["id"] = 1;
    crack["start_x"] = 10.0;
    crack["start_y"] = 10.0;
    crack["end_x"] = 20.0;
    crack["end_y"] = 10.0;
    crack["depth_um"] = 50.0;
    crack["width_um"] = 5.0;
    cracks.push_back(crack);
    input["cracks"] = cracks;

    std::atomic<bool> callback_called{false};
    std::atomic<bool> result_success{false};

    ASSERT_TRUE(module.execute(input, [&](const ModuleResult& r) {
        result_success = r.success;
        callback_called = true;
    }));

    int timeout = 0;
    while (!callback_called && timeout < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        timeout++;
    }

    ASSERT_TRUE(callback_called);
    ASSERT_TRUE(result_success);

    module.shutdown();
}

TEST(ModuleRegressionTests, WashburnModule_CreateAndInitialize) {
    auto module = ModuleFactory::instance().create("washburn_penetration");
    ASSERT_NE(module, nullptr);
    ASSERT_EQ(module->get_name(), "washburn_penetration");

    json config;
    config["washburn_config"]["wall_roughness_ra_um"] = 1.0;
    config["washburn_config"]["wenzel_roughness_correction"] = true;
    ASSERT_TRUE(module->initialize(config));
    ASSERT_EQ(module->get_status(), ModuleStatus::IDLE);

    module->shutdown();
}

TEST(ModuleRegressionTests, WashburnModule_ThreadPool) {
    WashburnPenetrationModule module(4);
    ASSERT_EQ(module.num_threads(), 4);

    json config;
    config["washburn_config"]["default_viscosity_pa_s"] = 0.5;
    config["washburn_config"]["wall_roughness_ra_um"] = 1.0;
    config["washburn_config"]["wenzel_roughness_correction"] = true;
    ASSERT_TRUE(module.initialize(config));

    std::vector<CrackInfo> cracks;
    for (int i = 0; i < 10; ++i) {
        CrackInfo c;
        c.id = i;
        c.max_width = 20.0 + i * 2.0;
        c.max_depth = 100.0;
        cracks.push_back(c);
    }

    RepairMaterial material;
    material.id = 1;
    material.viscosity = 0.5;

    auto results = module.predict_batch(cracks, material, 80.0);
    ASSERT_EQ(results.size(), 10);

    for (const auto& r : results) {
        ASSERT_GT(r.predicted_time_s, 0.0);
        ASSERT_TRUE(r.roughness_correction_applied);
    }

    module.shutdown();
}

TEST(ModuleRegressionTests, WashburnModule_ExecuteSyncBatch) {
    WashburnPenetrationModule module(2);

    json config;
    config["washburn_config"]["wall_roughness_ra_um"] = 0.5;
    ASSERT_TRUE(module.initialize(config));

    json input;
    json cracks = json::array();
    for (int i = 0; i < 5; ++i) {
        json c;
        c["id"] = i;
        c["max_width"] = 15.0 + i;
        c["max_depth"] = 80.0;
        cracks.push_back(c);
    }
    input["cracks"] = cracks;

    json material;
    material["id"] = 1;
    material["name"] = "test_material";
    material["viscosity"] = 1.0;
    input["material"] = material;
    input["target_depth_um"] = 60.0;

    ModuleResult result;
    ASSERT_TRUE(module.execute_sync(input, result));
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.data["count"].get<int>(), 5);
    ASSERT_EQ(result.data["predictions"].size(), 5);

    module.shutdown();
}

TEST(ModuleRegressionTests, StrengthModule_CreateAndInitialize) {
    auto module = ModuleFactory::instance().create("strength_fem");
    ASSERT_NE(module, nullptr);
    ASSERT_EQ(module->get_name(), "strength_fem");
    ASSERT_TRUE(module->supports_async());

    json config;
    config["bending_config"]["specimen_thickness_mm"] = 4.0;
    config["bending_config"]["enable_bayesian_calibration"] = false;
    ASSERT_TRUE(module->initialize(config));
    ASSERT_EQ(module->get_status(), ModuleStatus::IDLE);

    module->shutdown();
}

TEST(ModuleRegressionTests, StrengthModule_ExecuteSync) {
    StrengthFemModule module;

    json config;
    config["bending_config"]["support_span_mm"] = 40.0;
    config["bending_config"]["specimen_thickness_mm"] = 5.0;
    ASSERT_TRUE(module.initialize(config));

    json input;
    input["porcelain_id"] = 1;
    json crack;
    crack["id"] = 1;
    crack["max_depth"] = 0.1;
    crack["max_width"] = 0.01;
    input["crack"] = crack;

    json material;
    material["id"] = 1;
    material["name"] = "silica";
    material["viscosity"] = 1.0;
    input["material"] = material;
    input["repaired"] = true;

    ModuleResult result;
    ASSERT_TRUE(module.execute_sync(input, result));
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.data.contains("repaired_strength_mpa"));
    ASSERT_TRUE(result.data.contains("strength_recovery_ratio"));
    ASSERT_GT(result.data["repaired_strength_mpa"].get<double>(), 0.0);
    ASSERT_GT(result.data["strength_recovery_ratio"].get<double>(), 0.0);

    module.shutdown();
}

TEST(ModuleRegressionTests, StrengthModule_RepairedVsUnrepaired) {
    StrengthFemModule module;

    json config;
    ASSERT_TRUE(module.initialize(config));

    CrackInfo cd;
    cd.max_depth = 2.0;
    cd.max_width = 0.05;
    cd.total_length = 10.0;

    RepairMaterial material;
    material.id = 1;
    material.viscosity = 1.0;
    material.properties["bonding_strength"] = 80.0;

    auto result_repaired = module.simulate_bending_sync(cd, material, true);
    auto result_unrepaired = module.simulate_bending_sync(cd, material, false);

    ASSERT_GT(result_repaired.repaired_strength_mpa, 0.0);
    ASSERT_GT(result_unrepaired.unrepaired_strength_mpa, 0.0);
    ASSERT_LT(result_unrepaired.unrepaired_strength_mpa, result_unrepaired.original_strength_mpa);
    ASSERT_GE(result_repaired.repaired_strength_mpa, result_unrepaired.unrepaired_strength_mpa);
    ASSERT_GT(result_repaired.strength_recovery_ratio, 0.3);

    module.shutdown();
}

TEST(ModuleRegressionTests, VrModule_CreateAndInitialize) {
    auto module = ModuleFactory::instance().create("vr_preview");
    ASSERT_NE(module, nullptr);
    ASSERT_EQ(module->get_name(), "vr_preview");
    ASSERT_TRUE(module->supports_async());

    json config;
    config["vr_config"]["default_repair_radius_mm"] = 3.0;
    config["vr_config"]["animation_frames"] = 30;
    ASSERT_TRUE(module->initialize(config));
    ASSERT_EQ(module->get_status(), ModuleStatus::IDLE);

    module->shutdown();
}

TEST(ModuleRegressionTests, VrModule_ExecuteSync) {
    VRPreviewModule module;

    json config;
    config["preview_config"]["enable_repair_visualization"] = true;
    config["preview_config"]["num_preview_angles"] = 4;
    ASSERT_TRUE(module.initialize(config));

    json input;
    input["porcelain_id"] = 1;

    json porcelain_geometry;
    porcelain_geometry["thickness_mm"] = 3.0;
    porcelain_geometry["diameter_mm"] = 50.0;
    input["porcelain_geometry"] = porcelain_geometry;

    json cracks = json::array();
    json crack;
    crack["id"] = 1;
    crack["max_width"] = 10.0;
    crack["max_depth"] = 50.0;
    json points = json::array();
    for (int i = 0; i < 5; ++i) {
        json p;
        p["x"] = i * 2.0;
        p["y"] = 10.0;
        p["z"] = 0.0;
        p["depth"] = 50.0;
        p["width"] = 10.0;
        points.push_back(p);
    }
    crack["points"] = points;
    cracks.push_back(crack);
    input["cracks"] = cracks;

    json repairs = json::array();
    json repair;
    repair["crack_id"] = 1;
    repair["estimated_closure_ratio"] = 0.8;
    repairs.push_back(repair);
    input["repairs"] = repairs;

    ModuleResult result;
    ASSERT_TRUE(module.execute_sync(input, result));
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.data.contains("total_triangles"));
    ASSERT_GT(result.data["total_triangles"].get<int>(), 0);
    ASSERT_TRUE(result.data.contains("porcelain_model"));
    ASSERT_TRUE(result.data.contains("cracks"));

    module.shutdown();
}

TEST(ModuleRegressionTests, VrModule_BuildPreview) {
    VRPreviewModule module;

    json config;
    config["vr_config"]["animation_frames"] = 20;
    config["vr_config"]["enable_particle_effect"] = true;
    ASSERT_TRUE(module.initialize(config));

    json geometry;
    geometry["thickness_mm"] = 3.0;
    geometry["diameter_mm"] = 50.0;

    json cracks = json::array();
    json crack;
    crack["id"] = 1;
    crack["max_width"] = 10.0;
    json points = json::array();
    for (int i = 0; i < 3; ++i) {
        json p;
        p["x"] = i * 5.0;
        p["y"] = 10.0;
        p["z"] = 0.0;
        p["depth"] = 30.0;
        p["width"] = 10.0;
        points.push_back(p);
    }
    crack["points"] = points;
    cracks.push_back(crack);

    json repairs = json::array();
    json repair;
    repair["crack_id"] = 1;
    repair["estimated_closure_ratio"] = 0.8;
    repairs.push_back(repair);

    auto result = module.build_preview_sync(1, geometry, cracks, repairs);
    ASSERT_TRUE(result.success);
    ASSERT_GT(result.total_triangles, 0);

    module.shutdown();
}

TEST(ModuleRegressionTests, ThreadPool_Basic) {
    ThreadPool pool(4);
    ASSERT_EQ(pool.num_threads(), 4);

    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 20; ++i) {
        futures.push_back(pool.submit([&counter]() {
            counter++;
        }));
    }

    for (auto& f : futures) f.get();

    ASSERT_EQ(counter, 20);
    pool.shutdown();
}

TEST(ModuleRegressionTests, ThreadPool_ReturnValues) {
    ThreadPool pool(2);

    auto f1 = pool.submit([](int a, int b) { return a + b; }, 3, 4);
    auto f2 = pool.submit([](double x) { return x * x; }, 5.0);

    ASSERT_EQ(f1.get(), 7);
    ASSERT_NEAR(f2.get(), 25.0, 0.001);

    pool.shutdown();
}

TEST(ModuleRegressionTests, ThreadPool_WaitAll) {
    ThreadPool pool(3);

    std::atomic<int> counter{0};
    for (int i = 0; i < 30; ++i) {
        pool.submit([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            counter++;
        });
    }

    pool.wait_all();
    ASSERT_EQ(counter, 30);

    pool.shutdown();
}

TEST(ModuleRegressionTests, IpcLocalChannel_Basic) {
    auto server = std::make_unique<LocalIpcChannel>();
    auto client = std::make_unique<LocalIpcChannel>();

    server->set_peer(client.get());
    client->set_peer(server.get());

    server->initialize("test_server");
    client->connect("test_client");

    ASSERT_TRUE(server->is_connected());
    ASSERT_TRUE(client->is_connected());

    IpcMessage msg;
    msg.module_name = "test";
    msg.command = "hello";
    msg.payload = {{"value", 42}};
    msg.request_id = 1;

    ASSERT_TRUE(client->send(msg));

    IpcMessage received;
    ASSERT_TRUE(server->receive(received, 1000));
    ASSERT_EQ(received.module_name, "test");
    ASSERT_EQ(received.command, "hello");
    ASSERT_EQ(received.payload["value"].get<int>(), 42);
    ASSERT_EQ(received.request_id, 1);

    server->close();
    client->close();
}

TEST(ModuleRegressionTests, IpcLocalChannel_Callback) {
    auto server = std::make_unique<LocalIpcChannel>();
    auto client = std::make_unique<LocalIpcChannel>();

    server->set_peer(client.get());
    client->set_peer(server.get());

    server->initialize("test2");
    client->connect("test2");

    std::atomic<bool> callback_called{false};
    std::string received_cmd;

    server->set_message_callback([&](const IpcMessage& msg) {
        received_cmd = msg.command;
        callback_called = true;
    });

    IpcMessage msg;
    msg.command = "callback_test";
    client->send(msg);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_TRUE(callback_called);
    ASSERT_EQ(received_cmd, "callback_test");

    server->close();
    client->close();
}

TEST(ModuleRegressionTests, Acceptance_ModulePipeline) {
    FemStressModule stress_module;
    WashburnPenetrationModule washburn_module(2);
    StrengthFemModule strength_module;
    VRPreviewModule vr_module;

    json stress_config;
    stress_config["fem_config"]["grid_resolution"] = 25;
    stress_config["fem_config"]["use_adaptive_mesh"] = true;
    ASSERT_TRUE(stress_module.initialize(stress_config));

    json washburn_config;
    washburn_config["washburn_config"]["wall_roughness_ra_um"] = 0.8;
    ASSERT_TRUE(washburn_module.initialize(washburn_config));

    json strength_config;
    ASSERT_TRUE(strength_module.initialize(strength_config));

    json vr_config;
    ASSERT_TRUE(vr_module.initialize(vr_config));

    ASSERT_EQ(stress_module.get_status(), ModuleStatus::IDLE);
    ASSERT_EQ(washburn_module.get_status(), ModuleStatus::IDLE);
    ASSERT_EQ(strength_module.get_status(), ModuleStatus::IDLE);
    ASSERT_EQ(vr_module.get_status(), ModuleStatus::IDLE);

    json input;
    json cracks = json::array();
    json crack;
    crack["id"] = 1;
    crack["start_x"] = 15.0;
    crack["start_y"] = 15.0;
    crack["end_x"] = 35.0;
    crack["end_y"] = 15.0;
    crack["depth_um"] = 30.0;
    crack["width_um"] = 3.0;
    cracks.push_back(crack);
    input["cracks"] = cracks;

    json dims;
    dims["thickness_mm"] = 3.0;
    dims["diameter_mm"] = 60.0;
    input["dimensions"] = dims;

    ModuleResult stress_result;
    ASSERT_TRUE(stress_module.execute_sync(input, stress_result));
    ASSERT_TRUE(stress_result.success);
    ASSERT_GT(stress_result.data["max_von_mises_mpa"].get<double>(), 0);

    json washburn_input;
    washburn_input["cracks"] = cracks;
    json material;
    material["id"] = 1;
    material["name"] = "silica_nano";
    material["viscosity"] = 0.8;
    washburn_input["material"] = material;
    washburn_input["target_depth_um"] = 50.0;

    ModuleResult washburn_result;
    ASSERT_TRUE(washburn_module.execute_sync(washburn_input, washburn_result));
    ASSERT_TRUE(washburn_result.success);
    ASSERT_GE(washburn_result.data["count"].get<int>(), 0);

    json strength_input;
    strength_input["porcelain_id"] = 1;
    strength_input["crack"] = crack;
    strength_input["material"] = material;
    strength_input["repaired"] = true;

    ModuleResult strength_result;
    ASSERT_TRUE(strength_module.execute_sync(strength_input, strength_result));
    ASSERT_TRUE(strength_result.success);
    ASSERT_GT(strength_result.data["strength_recovery_ratio"].get<double>(), 0.7);

    json vr_input;
    vr_input["cracks"] = cracks;
    json repair_points = json::array();
    json rp; rp["x"] = 25.0; rp["y"] = 15.0; rp["z"] = 0.0;
    repair_points.push_back(rp);
    vr_input["repair_points"] = repair_points;
    vr_input["repair_radius_mm"] = 10.0;

    ModuleResult vr_result;
    ASSERT_TRUE(vr_module.execute_sync(vr_input, vr_result));
    ASSERT_TRUE(vr_result.success);
    ASSERT_GT(vr_result.data["estimated_closure_ratio"].get<double>(), 0.0);

    stress_module.shutdown();
    washburn_module.shutdown();
    strength_module.shutdown();
    vr_module.shutdown();
}

}
