#include "test_framework.h"
#include "stress_analysis.h"
#include <random>
#include <cmath>
#include <chrono>
#include <limits>
#include <stdexcept>

using namespace porcelain_monitor;
using namespace porcelain_monitor::algorithms;

namespace {

CrackInfo createStraightCrack(double x1, double y1, double x2, double y2, int n_points) {
    CrackInfo crack;
    crack.id = 1;
    crack.porcelain_id = 1;
    crack.crack_code = "CRACK_TEST";
    crack.max_depth = 0.5;
    crack.max_width = 0.1;
    crack.status = "active";
    crack.detected_at = std::chrono::system_clock::now();

    crack.points.clear();
    for (int i = 0; i < n_points; ++i) {
        double t = static_cast<double>(i) / (n_points - 1);
        Point3D p;
        p.x = x1 + t * (x2 - x1);
        p.y = y1 + t * (y2 - y1);
        p.z = 0.0;
        p.depth = 0.5;
        p.width = 0.1;
        crack.points.push_back(p);
    }

    double dx = x2 - x1;
    double dy = y2 - y1;
    crack.total_length = std::sqrt(dx * dx + dy * dy);

    return crack;
}

std::vector<CrackInfo> createRandomCracks(int n_cracks, int points_per_crack) {
    std::vector<CrackInfo> cracks;
    std::mt19937 gen(42);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);

    for (int i = 0; i < n_cracks; ++i) {
        CrackInfo crack;
        crack.id = i + 1;
        crack.porcelain_id = 1;
        crack.crack_code = "CRACK_" + std::to_string(i);
        crack.max_depth = 0.3 + dist(gen) * 0.2;
        crack.max_width = 0.05 + dist(gen) * 0.05;
        crack.status = "active";
        crack.detected_at = std::chrono::system_clock::now();

        double cx = dist(gen);
        double cy = dist(gen);
        double length = 0.2 + std::abs(dist(gen)) * 0.3;
        double angle = dist(gen) * M_PI;

        double x1 = cx - length * 0.5 * std::cos(angle);
        double y1 = cy - length * 0.5 * std::sin(angle);
        double x2 = cx + length * 0.5 * std::cos(angle);
        double y2 = cy + length * 0.5 * std::sin(angle);

        for (int j = 0; j < points_per_crack; ++j) {
            double t = static_cast<double>(j) / (points_per_crack - 1);
            Point3D p;
            p.x = x1 + t * (x2 - x1);
            p.y = y1 + t * (y2 - y1);
            p.z = 0.0;
            p.depth = crack.max_depth;
            p.width = crack.max_width;
            crack.points.push_back(p);
        }

        crack.total_length = length;
        cracks.push_back(crack);
    }

    return cracks;
}

double computeStressError(const StressGridPoint& p, double expected_stress) {
    if (std::abs(expected_stress) < 1e-15) {
        if (std::abs(p.stress.von_mises) < 1e-15) return 0.0;
        return 100.0;
    }
    return std::abs(p.stress.von_mises - expected_stress) / std::abs(expected_stress) * 100.0;
}

CrackInfo createCrackTipScenario() {
    CrackInfo crack;
    crack.id = 1;
    crack.porcelain_id = 1;
    crack.crack_code = "CRACK_TIP";
    crack.max_depth = 0.5;
    crack.max_width = 0.1;
    crack.status = "active";
    crack.detected_at = std::chrono::system_clock::now();

    crack.points.clear();
    for (int i = 0; i < 11; ++i) {
        double t = static_cast<double>(i) / 10.0;
        Point3D p;
        p.x = -0.8 + t * 0.8;
        p.y = 0.0;
        p.z = 0.0;
        p.depth = 0.5;
        p.width = 0.1;
        crack.points.push_back(p);
    }

    crack.total_length = 0.8;
    return crack;
}

int countRefinedNodes(const std::vector<StressGridPoint>& grid_points) {
    int count = 0;
    for (const auto& gp : grid_points) {
        if (gp.refinement_level > 0) {
            count++;
        }
    }
    return count;
}

int maxRefinementLevel(const std::vector<StressGridPoint>& grid_points) {
    int max_level = 0;
    for (const auto& gp : grid_points) {
        if (gp.refinement_level > max_level) {
            max_level = gp.refinement_level;
        }
    }
    return max_level;
}

}

TEST(StressFEM, SingleCrack_AnalysisProducesValidStressField) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 20;
    fem.set_config(config);

    std::vector<CrackInfo> cracks;
    cracks.push_back(createStraightCrack(-0.5, 0.0, 0.5, 0.0, 11));

    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());
    ASSERT_GT(result.grid_points.size(), 0u);

    ASSERT_RANGE(result.max_von_mises, 0.0, 300.0);
    ASSERT_RANGE(result.avg_von_mises, 0.0, 300.0);
    ASSERT_GT(result.max_von_mises, 0.0);

    ASSERT_GT(result.high_stress_area_ratio, 0.0);
    ASSERT_LT(result.high_stress_area_ratio, 1.0);
}

TEST(StressFEM, MultipleCracks_StressHigherNearCracks) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 25;
    fem.set_config(config);

    auto cracks = createRandomCracks(5, 8);
    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());

    auto findClosestPoint = [&](double cx, double cy, double cz) -> const StressGridPoint& {
        double min_dist = std::numeric_limits<double>::max();
        size_t min_idx = 0;
        for (size_t i = 0; i < result.grid_points.size(); ++i) {
            double dx = result.grid_points[i].x - cx;
            double dy = result.grid_points[i].y - cy;
            double dz = result.grid_points[i].z - cz;
            double dist = dx * dx + dy * dy + dz * dz;
            if (dist < min_dist) {
                min_dist = dist;
                min_idx = i;
            }
        }
        return result.grid_points[min_idx];
    };

    auto findFarPoint = [&](double cx, double cy, double cz, double min_dist) -> const StressGridPoint& {
        size_t best_idx = 0;
        double best_vm = std::numeric_limits<double>::max();
        for (size_t i = 0; i < result.grid_points.size(); ++i) {
            double dx = result.grid_points[i].x - cx;
            double dy = result.grid_points[i].y - cy;
            double dz = result.grid_points[i].z - cz;
            double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist > min_dist && result.grid_points[i].stress.von_mises < best_vm) {
                best_vm = result.grid_points[i].stress.von_mises;
                best_idx = i;
            }
        }
        return result.grid_points[best_idx];
    };

    for (int i = 0; i < 5; ++i) {
        const auto& crack = cracks[i % cracks.size()];
        double cx = 0, cy = 0, cz = 0;
        for (const auto& p : crack.points) {
            cx += p.x;
            cy += p.y;
            cz += p.z;
        }
        cx /= crack.points.size();
        cy /= crack.points.size();
        cz /= crack.points.size();

        const auto& near_pt = findClosestPoint(cx, cy, cz);
        const auto& far_pt = findFarPoint(cx, cy, cz, 1.0);

        ASSERT_GT(near_pt.stress.von_mises, far_pt.stress.von_mises);
    }
}

TEST(StressFEM, StressSymmetry_AxisAlignedCrack) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 21;
    fem.set_config(config);

    std::vector<CrackInfo> cracks;
    cracks.push_back(createStraightCrack(-0.8, 0.0, 0.8, 0.0, 17));

    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());

    auto findPoint = [&](double x, double y, double z) -> const StressGridPoint* {
        for (const auto& gp : result.grid_points) {
            if (std::abs(gp.x - x) < 1e-6 && std::abs(gp.y - y) < 1e-6 && std::abs(gp.z - z) < 1e-6) {
                return &gp;
            }
        }
        return nullptr;
    };

    int checked = 0;
    for (const auto& gp : result.grid_points) {
        if (std::abs(gp.y) < 1e-6) continue;

        const StressGridPoint* mirror = findPoint(gp.x, -gp.y, gp.z);
        if (mirror) {
            ASSERT_NEAR(gp.stress.von_mises, mirror->stress.von_mises, 5.0);
            checked++;
        }
    }
    ASSERT_GT(checked, 0);
}

TEST(StressFEM, UniformCrackDensity_ProportionalStress) {
    FEMConfig config1;
    config1.grid_resolution = 20;
    config1.crack_density_sensitivity = 25.0;

    FEMConfig config2;
    config2.grid_resolution = 20;
    config2.crack_density_sensitivity = 50.0;

    auto cracks = createRandomCracks(3, 10);

    StressAnalysisFEM fem1;
    fem1.set_config(config1);
    auto result1 = fem1.analyze(1, cracks);

    StressAnalysisFEM fem2;
    fem2.set_config(config2);
    auto result2 = fem2.analyze(1, cracks);

    ASSERT_GT(result1.max_von_mises, 0.0);
    ASSERT_GT(result2.max_von_mises, 0.0);

    double ratio = result2.max_von_mises / result1.max_von_mises;
    ASSERT_RANGE(ratio, 1.5, 2.5);
}

TEST(StressFEM, VonMisesCalculation_PhysicallyValid) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 20;
    fem.set_config(config);

    auto cracks = createRandomCracks(4, 8);
    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());

    double max_allowed = config.max_stress_mpa * 1e6 * 1.1;

    for (const auto& gp : result.grid_points) {
        ASSERT_GE(gp.stress.von_mises, 0.0);
        ASSERT_LE(gp.stress.von_mises, max_allowed);

        double sxx = gp.stress.sigma_xx;
        double syy = gp.stress.sigma_yy;
        double szz = gp.stress.sigma_zz;
        double txy = gp.stress.tau_xy;
        double tyz = gp.stress.tau_yz;
        double tzx = gp.stress.tau_zx;

        double vm_sq = 0.5 * (
            (sxx - syy) * (sxx - syy) +
            (syy - szz) * (syy - szz) +
            (szz - sxx) * (szz - sxx) +
            6.0 * (txy * txy + tyz * tyz + tzx * tzx)
        );
        double expected_vm = std::sqrt(std::max(0.0, vm_sq));
        ASSERT_NEAR(gp.stress.von_mises, expected_vm, 1.0);
    }
}

TEST(StressFEM, EmptyCracks_ReturnsZeroStress) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 15;
    fem.set_config(config);

    std::vector<CrackInfo> cracks;
    auto result = fem.analyze(1, cracks);

    ASSERT_LT(result.max_von_mises, 1e-6);
    ASSERT_LT(result.avg_von_mises, 1e-6);
}

TEST(StressFEM, SinglePointCrack_StressLocalized) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 25;
    fem.set_config(config);

    CrackInfo crack;
    crack.id = 1;
    crack.porcelain_id = 1;
    crack.crack_code = "POINT_CRACK";
    crack.max_depth = 0.5;
    crack.max_width = 0.1;
    crack.total_length = 0.0;
    crack.status = "active";
    crack.detected_at = std::chrono::system_clock::now();

    Point3D p;
    p.x = 0.0;
    p.y = 0.0;
    p.z = 0.0;
    p.depth = 0.5;
    p.width = 0.1;
    crack.points.push_back(p);

    std::vector<CrackInfo> cracks;
    cracks.push_back(crack);

    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());
    ASSERT_GT(result.max_von_mises, 0.0);

    double max_stress = 0.0;
    size_t max_idx = 0;
    for (size_t i = 0; i < result.grid_points.size(); ++i) {
        if (result.grid_points[i].stress.von_mises > max_stress) {
            max_stress = result.grid_points[i].stress.von_mises;
            max_idx = i;
        }
    }

    const auto& max_pt = result.grid_points[max_idx];
    double dx = max_pt.x - 0.0;
    double dy = max_pt.y - 0.0;
    double dz = max_pt.z - 0.0;
    double dist_to_center = std::sqrt(dx * dx + dy * dy + dz * dz);

    double avg_spacing = 2.0 / (config.grid_resolution - 1);
    double min_far_stress = std::numeric_limits<double>::max();

    for (const auto& gp : result.grid_points) {
        double gdx = gp.x - 0.0;
        double gdy = gp.y - 0.0;
        double gdz = gp.z - 0.0;
        double gdist = std::sqrt(gdx * gdx + gdy * gdy + gdz * gdz);
        if (gdist > 2.0 * avg_spacing && gp.stress.von_mises < min_far_stress) {
            min_far_stress = gp.stress.von_mises;
        }
    }

    ASSERT_LT(min_far_stress, 0.5 * max_stress);
}

TEST(StressFEM, HighGridResolution_RuntimeReasonable) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 40;
    fem.set_config(config);

    auto cracks = createRandomCracks(3, 6);

    auto t_start = std::chrono::high_resolution_clock::now();
    auto result = fem.analyze(1, cracks);
    auto t_end = std::chrono::high_resolution_clock::now();

    double duration_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    ASSERT_LT(duration_ms, 5000.0);
    ASSERT_FALSE(result.grid_points.empty());
}

TEST(StressFEM, MaxStressClipping_ValuesBounded) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 20;
    config.max_stress_mpa = 300.0;
    fem.set_config(config);

    auto cracks = createRandomCracks(20, 15);
    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());

    double upper_bound = config.max_stress_mpa * 1.1;
    ASSERT_LE(result.max_von_mises, upper_bound);

    for (const auto& gp : result.grid_points) {
        ASSERT_LE(gp.stress.von_mises / 1e6, upper_bound);
    }
}

TEST(StressFEM, DuplicateCrackPoints_StableResult) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 20;
    fem.set_config(config);

    CrackInfo crack = createStraightCrack(-0.5, 0.0, 0.5, 0.0, 11);
    auto original_points = crack.points;

    for (int i = 0; i < 5; ++i) {
        crack.points.push_back(original_points[i % original_points.size()]);
    }

    std::vector<CrackInfo> cracks;
    cracks.push_back(crack);

    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());
    ASSERT_GT(result.max_von_mises, 0.0);
    ASSERT_LT(result.max_von_mises, 300.0);
    ASSERT_RANGE(result.avg_von_mises, 0.0, 300.0);
}

TEST(StressFEM, VerySmallCrack_NoDegeneracy) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 20;
    fem.set_config(config);

    double cx = 0.5;
    double cy = 0.5;

    CrackInfo crack;
    crack.id = 1;
    crack.porcelain_id = 1;
    crack.crack_code = "TINY_CRACK";
    crack.max_depth = 1e-7;
    crack.max_width = 1e-7;
    crack.total_length = 1e-7;
    crack.status = "active";
    crack.detected_at = std::chrono::system_clock::now();

    for (int i = 0; i < 5; ++i) {
        Point3D p;
        double t = static_cast<double>(i) / 4.0;
        p.x = cx + t * 1e-7;
        p.y = cy + t * 1e-7;
        p.z = 0.0;
        p.depth = 1e-7;
        p.width = 1e-7;
        crack.points.push_back(p);
    }

    std::vector<CrackInfo> cracks;
    cracks.push_back(crack);

    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());

    for (const auto& gp : result.grid_points) {
        ASSERT_FALSE(std::isnan(gp.stress.von_mises));
        ASSERT_FALSE(std::isinf(gp.stress.von_mises));
        ASSERT_FALSE(std::isnan(gp.stress.sigma_xx));
        ASSERT_FALSE(std::isinf(gp.stress.sigma_xx));
        ASSERT_GE(gp.stress.von_mises, 0.0);
    }
}

TEST(StressFEM, LargeCoordinateRange_NormalizedProperly) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 20;
    fem.set_config(config);

    CrackInfo crack;
    crack.id = 1;
    crack.porcelain_id = 1;
    crack.crack_code = "LARGE_RANGE_CRACK";
    crack.max_depth = 1e5;
    crack.max_width = 1e4;
    crack.total_length = 1e6;
    crack.status = "active";
    crack.detected_at = std::chrono::system_clock::now();

    for (int i = 0; i < 10; ++i) {
        Point3D p;
        double t = static_cast<double>(i) / 9.0;
        p.x = 0.0 + t * 1e6;
        p.y = 5e5 + t * 1e5;
        p.z = 0.0;
        p.depth = 1e5;
        p.width = 1e4;
        crack.points.push_back(p);
    }

    std::vector<CrackInfo> cracks;
    cracks.push_back(crack);

    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());
    ASSERT_GT(result.max_von_mises, 0.0);
    ASSERT_LT(result.max_von_mises, 300.0);

    for (const auto& gp : result.grid_points) {
        ASSERT_FALSE(std::isnan(gp.x));
        ASSERT_FALSE(std::isnan(gp.y));
        ASSERT_FALSE(std::isnan(gp.z));
        ASSERT_GE(gp.stress.von_mises, 0.0);
    }
}

TEST(StressFEM, Acceptance_ErrorVsAnalyticalLessThan10pct) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 25;
    config.crack_density_sensitivity = 100.0;
    config.directional_coupling = 1.0;
    fem.set_config(config);

    std::vector<CrackInfo> cracks;
    double spacing = 0.05;
    for (double x = -0.9; x <= 0.9; x += spacing) {
        for (double y = -0.9; y <= 0.9; y += spacing) {
            CrackInfo crack;
            crack.id = cracks.size() + 1;
            crack.porcelain_id = 1;
            crack.crack_code = "UNIFORM_" + std::to_string(cracks.size());
            crack.max_depth = 0.5;
            crack.max_width = 0.1;
            crack.total_length = 0.0;
            crack.status = "active";
            crack.detected_at = std::chrono::system_clock::now();

            Point3D p;
            p.x = x;
            p.y = y;
            p.z = 0.0;
            p.depth = 0.5;
            p.width = 0.1;
            crack.points.push_back(p);

            cracks.push_back(crack);
        }
    }

    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());
    ASSERT_GT(result.avg_von_mises, 1.0);

    double mean_vm = 0.0;
    int interior_count = 0;
    for (const auto& gp : result.grid_points) {
        if (std::abs(gp.x) < 0.7 && std::abs(gp.y) < 0.7 && std::abs(gp.z) < 0.7) {
            mean_vm += gp.stress.von_mises;
            interior_count++;
        }
    }
    ASSERT_GT(interior_count, 0);
    mean_vm /= interior_count;

    double max_deviation = 0.0;
    for (const auto& gp : result.grid_points) {
        if (std::abs(gp.x) < 0.7 && std::abs(gp.y) < 0.7 && std::abs(gp.z) < 0.7) {
            double err = computeStressError(gp, mean_vm);
            max_deviation = std::max(max_deviation, err);
        }
    }

    ASSERT_LT(max_deviation, 10.0);
}

TEST(StressFEM, AMR_Enabled_AdaptiveMeshCreated) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 15;
    config.use_adaptive_mesh = true;
    config.max_refinement_level = 2;
    config.refinement_stress_gradient_threshold = 10.0;
    fem.set_config(config);

    CrackInfo crack;
    crack.id = 1;
    crack.porcelain_id = 1;
    crack.crack_code = "POINT_CRACK_CENTER";
    crack.max_depth = 0.5;
    crack.max_width = 0.1;
    crack.total_length = 0.0;
    crack.status = "active";
    crack.detected_at = std::chrono::system_clock::now();

    Point3D p;
    p.x = 0.0;
    p.y = 0.0;
    p.z = 0.0;
    p.depth = 0.5;
    p.width = 0.1;
    crack.points.push_back(p);

    std::vector<CrackInfo> cracks;
    cracks.push_back(crack);

    size_t initial_node_count = config.grid_resolution * config.grid_resolution * config.grid_resolution;

    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());
    ASSERT_GT(result.grid_points.size(), 0u);

    int refined_count = countRefinedNodes(result.grid_points);
    ASSERT_GT(refined_count, 0);

    ASSERT_GT(result.grid_points.size(), initial_node_count);
}

TEST(StressFEM, AMR_Disabled_UniformMeshOnly) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 15;
    config.use_adaptive_mesh = false;
    fem.set_config(config);

    CrackInfo crack;
    crack.id = 1;
    crack.porcelain_id = 1;
    crack.crack_code = "POINT_CRACK_CENTER";
    crack.max_depth = 0.5;
    crack.max_width = 0.1;
    crack.total_length = 0.0;
    crack.status = "active";
    crack.detected_at = std::chrono::system_clock::now();

    Point3D p;
    p.x = 0.0;
    p.y = 0.0;
    p.z = 0.0;
    p.depth = 0.5;
    p.width = 0.1;
    crack.points.push_back(p);

    std::vector<CrackInfo> cracks;
    cracks.push_back(crack);

    size_t expected_node_count = config.grid_resolution * config.grid_resolution * config.grid_resolution;

    auto result = fem.analyze(1, cracks);

    ASSERT_FALSE(result.grid_points.empty());

    for (const auto& gp : result.grid_points) {
        ASSERT_EQ(gp.refinement_level, 0);
    }

    ASSERT_EQ(result.grid_points.size(), expected_node_count);
}

TEST(StressFEM, AMR_HighGradientRegion_Refined) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 20;
    config.use_adaptive_mesh = true;
    config.max_refinement_level = 2;
    config.refinement_stress_gradient_threshold = 15.0;
    fem.set_config(config);

    std::vector<CrackInfo> cracks;
    cracks.push_back(createCrackTipScenario());

    auto result_amr = fem.analyze(1, cracks);
    ASSERT_FALSE(result_amr.grid_points.empty());

    size_t max_grad_idx = 0;
    double max_stress = 0.0;
    for (size_t i = 0; i < result_amr.grid_points.size(); ++i) {
        if (result_amr.grid_points[i].stress.von_mises > max_stress) {
            max_stress = result_amr.grid_points[i].stress.von_mises;
            max_grad_idx = i;
        }
    }

    const auto& max_pt = result_amr.grid_points[max_grad_idx];
    bool near_refined = false;
    double avg_refined_stress = 0.0;
    int refined_count = 0;
    double avg_unrefined_stress = 0.0;
    int unrefined_count = 0;

    for (const auto& gp : result_amr.grid_points) {
        double dx = gp.x - max_pt.x;
        double dy = gp.y - max_pt.y;
        double dz = gp.z - max_pt.z;
        double dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (dist < 0.2) {
            if (gp.refinement_level > 0) {
                near_refined = true;
                avg_refined_stress += gp.stress.von_mises;
                refined_count++;
            } else {
                avg_unrefined_stress += gp.stress.von_mises;
                unrefined_count++;
            }
        }
    }

    ASSERT_TRUE(near_refined || max_pt.refinement_level > 0);

    if (refined_count > 0 && unrefined_count > 0) {
        avg_refined_stress /= refined_count;
        avg_unrefined_stress /= unrefined_count;
        ASSERT_GT(avg_refined_stress, avg_unrefined_stress * 1.15);
    }
}

TEST(StressFEM, AMR_MaxLevelRespected) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 12;
    config.use_adaptive_mesh = true;
    config.max_refinement_level = 3;
    config.max_total_nodes = 50000;
    config.refinement_stress_gradient_threshold = 5.0;
    fem.set_config(config);

    std::vector<CrackInfo> cracks;
    std::mt19937 gen(123);
    std::uniform_real_distribution<double> dist(-0.6, 0.6);
    std::uniform_real_distribution<double> angle_dist(0.0, M_PI);

    for (int i = 0; i < 5; ++i) {
        CrackInfo crack;
        crack.id = i + 1;
        crack.porcelain_id = 1;
        crack.crack_code = "CRACK_" + std::to_string(i);
        crack.max_depth = 0.4;
        crack.max_width = 0.08;
        crack.status = "active";
        crack.detected_at = std::chrono::system_clock::now();

        double cx = dist(gen);
        double cy = dist(gen);
        double length = 0.3 + std::abs(dist(gen)) * 0.3;
        double angle = angle_dist(gen);

        for (int j = 0; j < 8; ++j) {
            double t = static_cast<double>(j) / 7.0;
            Point3D p;
            p.x = cx - length * 0.5 * std::cos(angle) + t * length * std::cos(angle);
            p.y = cy - length * 0.5 * std::sin(angle) + t * length * std::sin(angle);
            p.z = 0.0;
            p.depth = crack.max_depth;
            p.width = crack.max_width;
            crack.points.push_back(p);
        }

        crack.total_length = length;
        cracks.push_back(crack);
    }

    auto result = fem.analyze(1, cracks);
    ASSERT_FALSE(result.grid_points.empty());

    int max_level = maxRefinementLevel(result.grid_points);
    ASSERT_LE(max_level, 3);

    ASSERT_LE(static_cast<int>(result.grid_points.size()), config.max_total_nodes);
}

TEST(StressFEM, AMR_StressConcentration_Captured) {
    FEMConfig config_amr;
    config_amr.grid_resolution = 15;
    config_amr.use_adaptive_mesh = true;
    config_amr.max_refinement_level = 3;
    config_amr.refinement_stress_gradient_threshold = 10.0;

    FEMConfig config_uniform;
    config_uniform.grid_resolution = 15;
    config_uniform.use_adaptive_mesh = false;

    std::vector<CrackInfo> cracks;
    cracks.push_back(createCrackTipScenario());

    StressAnalysisFEM fem_amr;
    fem_amr.set_config(config_amr);
    auto result_amr = fem_amr.analyze(1, cracks);

    StressAnalysisFEM fem_uniform;
    fem_uniform.set_config(config_uniform);
    auto result_uniform = fem_uniform.analyze(1, cracks);

    ASSERT_FALSE(result_amr.grid_points.empty());
    ASSERT_FALSE(result_uniform.grid_points.empty());

    ASSERT_GT(result_amr.max_von_mises, result_uniform.max_von_mises * 1.15);
}

TEST(StressFEM, AMR_StressGradient_Threshold) {
    StressAnalysisFEM fem;
    FEMConfig config_high;
    config_high.grid_resolution = 15;
    config_high.use_adaptive_mesh = true;
    config_high.max_refinement_level = 2;
    config_high.refinement_stress_gradient_threshold = 50.0;
    fem.set_config(config_high);

    std::vector<CrackInfo> cracks;
    cracks.push_back(createCrackTipScenario());

    auto result_high = fem.analyze(1, cracks);
    ASSERT_FALSE(result_high.grid_points.empty());

    int refined_high = countRefinedNodes(result_high.grid_points);

    FEMConfig config_low;
    config_low.grid_resolution = 15;
    config_low.use_adaptive_mesh = true;
    config_low.max_refinement_level = 2;
    config_low.refinement_stress_gradient_threshold = 1.0;
    fem.set_config(config_low);

    auto result_low = fem.analyze(1, cracks);
    ASSERT_FALSE(result_low.grid_points.empty());

    int refined_low = countRefinedNodes(result_low.grid_points);

    ASSERT_GT(refined_low, refined_high);
}

TEST(StressFEM, AMR_Coarsening_Works) {
    StressAnalysisFEM fem;
    FEMConfig config;
    config.grid_resolution = 15;
    config.use_adaptive_mesh = true;
    config.max_refinement_level = 2;
    config.refinement_stress_gradient_threshold = 10.0;
    config.coarsening_stress_gradient_threshold = 5.0;
    fem.set_config(config);

    std::vector<CrackInfo> cracks_high;
    cracks_high.push_back(createCrackTipScenario());
    auto result_high = fem.analyze(1, cracks_high);

    int max_level_high = maxRefinementLevel(result_high.grid_points);
    ASSERT_GE(max_level_high, 1);

    int refined_high = countRefinedNodes(result_high.grid_points);
    ASSERT_GT(refined_high, 0);

    std::vector<CrackInfo> cracks_low;
    auto result_low = fem.analyze(1, cracks_low);

    int max_level_low = maxRefinementLevel(result_low.grid_points);
    ASSERT_LE(max_level_low, 0);

    int refined_low = countRefinedNodes(result_low.grid_points);
    ASSERT_EQ(refined_low, 0);

    ASSERT_LT(result_low.grid_points.size(), result_high.grid_points.size());
}

TEST(StressFEM, AMR_InvalidConfig_Handled) {
    StressAnalysisFEM fem;
    FEMConfig config_invalid_level;
    config_invalid_level.grid_resolution = 12;
    config_invalid_level.use_adaptive_mesh = true;
    config_invalid_level.max_refinement_level = -1;
    fem.set_config(config_invalid_level);

    std::vector<CrackInfo> cracks;
    cracks.push_back(createCrackTipScenario());

    auto result_neg = fem.analyze(1, cracks);
    ASSERT_FALSE(result_neg.grid_points.empty());

    int max_level_neg = maxRefinementLevel(result_neg.grid_points);
    ASSERT_GE(max_level_neg, 0);

    FEMConfig config_small_nodes;
    config_small_nodes.grid_resolution = 12;
    config_small_nodes.use_adaptive_mesh = true;
    config_small_nodes.max_refinement_level = 3;
    config_small_nodes.max_total_nodes = 100;
    fem.set_config(config_small_nodes);

    auto result_small = fem.analyze(1, cracks);
    ASSERT_FALSE(result_small.grid_points.empty());

    ASSERT_LE(static_cast<int>(result_small.grid_points.size()), config_small_nodes.max_total_nodes + 8);
}

#ifndef PORCELAIN_TESTS_COMBINED
int main() {
    return RUN_ALL_TESTS();
}
#endif
