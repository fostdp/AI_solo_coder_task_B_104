#include "stress_analysis.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <limits>
#include <cstdint>
#include <cstdlib>

namespace porcelain_monitor {
namespace algorithms {

StressAnalysisFEM::StressAnalysisFEM()
    : lambda_(0.0), mu_(0.0) {
}

void StressAnalysisFEM::set_config(const FEMConfig& config) {
    config_ = config;

    if (config_.max_refinement_level < 0) {
        config_.max_refinement_level = 0;
    }

    if (config_.max_total_nodes < 1) {
        config_.max_total_nodes = 1;
    }

    if (config_.grid_resolution < 1) {
        config_.grid_resolution = 1;
    }

    double E = config_.youngs_modulus_gpa * 1e9;
    double nu = config_.poissons_ratio;
    lambda_ = E * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
    mu_ = E / (2.0 * (1.0 + nu));
}

void StressAnalysisFEM::build_mesh(FEAMesh& mesh, const std::vector<CrackInfo>& cracks) {
    if (cracks.empty()) {
        mesh.nx = mesh.ny = mesh.nz = config_.grid_resolution;
        mesh.dx = mesh.dy = mesh.dz = 1.0;
        mesh.bbox_min[0] = mesh.bbox_min[1] = mesh.bbox_min[2] = 0.0;
        mesh.bbox_max[0] = mesh.bbox_max[1] = mesh.bbox_max[2] = 1.0;
        mesh.nodes.clear();
        mesh.refinement_levels.clear();
        mesh.max_level_used = 0;
        return;
    }

    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double min_z = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();
    double max_z = std::numeric_limits<double>::lowest();

    for (const auto& crack : cracks) {
        for (const auto& p : crack.points) {
            min_x = std::min(min_x, p.x);
            min_y = std::min(min_y, p.y);
            min_z = std::min(min_z, p.z);
            max_x = std::max(max_x, p.x);
            max_y = std::max(max_y, p.y);
            max_z = std::max(max_z, p.z);
        }
    }

    double padding = 0.1 * std::max({max_x - min_x, max_y - min_y, max_z - min_z});
    if (padding < 1e-6) padding = 1e-3;

    mesh.bbox_min[0] = min_x - padding;
    mesh.bbox_min[1] = min_y - padding;
    mesh.bbox_min[2] = min_z - padding;
    mesh.bbox_max[0] = max_x + padding;
    mesh.bbox_max[1] = max_y + padding;
    mesh.bbox_max[2] = max_z + padding;

    mesh.nx = config_.grid_resolution;
    mesh.ny = config_.grid_resolution;
    mesh.nz = config_.grid_resolution;

    mesh.dx = (mesh.bbox_max[0] - mesh.bbox_min[0]) / (mesh.nx - 1);
    mesh.dy = (mesh.bbox_max[1] - mesh.bbox_min[1]) / (mesh.ny - 1);
    mesh.dz = (mesh.bbox_max[2] - mesh.bbox_min[2]) / (mesh.nz - 1);

    mesh.nodes.clear();
    mesh.nodes.reserve(mesh.nx * mesh.ny * mesh.nz);
    mesh.refinement_levels.clear();
    mesh.refinement_levels.reserve(mesh.nx * mesh.ny * mesh.nz);
    mesh.max_level_used = 0;

    for (int k = 0; k < mesh.nz; ++k) {
        for (int j = 0; j < mesh.ny; ++j) {
            for (int i = 0; i < mesh.nx; ++i) {
                FEMNode node;
                node.x = mesh.bbox_min[0] + i * mesh.dx;
                node.y = mesh.bbox_min[1] + j * mesh.dy;
                node.z = mesh.bbox_min[2] + k * mesh.dz;
                node.is_boundary = (i == 0 || i == mesh.nx - 1 ||
                                   j == 0 || j == mesh.ny - 1 ||
                                   k == 0 || k == mesh.nz - 1);
                node.refinement_level = 0;
                node.needs_refinement = false;
                node.stress_gradient_magnitude = 0.0;
                node.parent_node_idx = -1;
                mesh.nodes.push_back(node);
                mesh.refinement_levels.push_back(0);
            }
        }
    }
}

double StressAnalysisFEM::gaussian_weight(double dist, double sigma) const {
    if (sigma < 1e-12) return 0.0;
    return exp(-dist * dist / (2.0 * sigma * sigma));
}

void StressAnalysisFEM::compute_crack_density_field(FEAMesh& mesh, const std::vector<CrackInfo>& cracks) {
    if (cracks.empty() || mesh.nodes.empty()) return;

    double avg_spacing = std::cbrt(mesh.dx * mesh.dy * mesh.dz);
    double sigma = config_.crack_density_sensitivity * avg_spacing;

    for (auto& node : mesh.nodes) {
        double total_weight = 0.0;
        double weighted_sum = 0.0;
        double dir_x = 0.0, dir_y = 0.0, dir_z = 0.0;

        for (const auto& crack : cracks) {
            double crack_contribution = crack.max_depth * crack.max_width;
            for (const auto& p : crack.points) {
                double dx = node.x - p.x;
                double dy = node.y - p.y;
                double dz = node.z - p.z;
                double dist = sqrt(dx * dx + dy * dy + dz * dz);
                double w = gaussian_weight(dist, sigma);
                weighted_sum += w * crack_contribution;
                total_weight += w;

                if (p.normal) {
                    dir_x += w * (*p.normal)[0];
                    dir_y += w * (*p.normal)[1];
                    dir_z += w * (*p.normal)[2];
                } else {
                    dir_x += w * dx / (dist + 1e-12);
                    dir_y += w * dy / (dist + 1e-12);
                    dir_z += w * dz / (dist + 1e-12);
                }
            }
        }

        if (total_weight > 1e-12) {
            node.crack_density = weighted_sum / total_weight;
            double norm = sqrt(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
            if (norm > 1e-12) {
                node.crack_direction[0] = dir_x / norm;
                node.crack_direction[1] = dir_y / norm;
                node.crack_direction[2] = dir_z / norm;
            }
        } else {
            node.crack_density = 0.0;
        }
    }

    double max_density = 0.0;
    for (const auto& node : mesh.nodes) {
        max_density = std::max(max_density, node.crack_density);
    }
    if (max_density > 1e-12) {
        for (auto& node : mesh.nodes) {
            node.crack_density /= max_density;
        }
    }
}

void StressAnalysisFEM::principal_stress_direction(FEMNode& node) {
    double nx = node.crack_direction[0];
    double ny = node.crack_direction[1];
    double nz = node.crack_direction[2];

    double len = sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12) {
        node.crack_direction[0] = 1.0;
        node.crack_direction[1] = 0.0;
        node.crack_direction[2] = 0.0;
        return;
    }

    node.crack_direction[0] = nx / len;
    node.crack_direction[1] = ny / len;
    node.crack_direction[2] = nz / len;
}

int8_t StressAnalysisFEM::get_octant_sign(int octant, int axis) {
    if (octant < 0 || octant > 7) return 1;
    int sign = (octant & (1 << axis)) ? 1 : -1;
    return static_cast<int8_t>(sign);
}

void StressAnalysisFEM::compute_stress_gradients(FEAMesh& mesh) {
    if (mesh.nodes.empty()) return;

    int total_nodes = static_cast<int>(mesh.nodes.size());
    auto idx = [&](int i, int j, int k) -> int {
        return k * mesh.nx * mesh.ny + j * mesh.nx + i;
    };

    for (int k = 1; k < mesh.nz - 1; ++k) {
        for (int j = 1; j < mesh.ny - 1; ++j) {
            for (int i = 1; i < mesh.nx - 1; ++i) {
                int n = idx(i, j, k);
                if (n < 0 || n >= total_nodes) continue;

                int n_ip = idx(i + 1, j, k);
                int n_im = idx(i - 1, j, k);
                int n_jp = idx(i, j + 1, k);
                int n_jm = idx(i, j - 1, k);
                int n_kp = idx(i, j, k + 1);
                int n_km = idx(i, j, k - 1);

                if (n_ip < 0 || n_ip >= total_nodes ||
                    n_im < 0 || n_im >= total_nodes ||
                    n_jp < 0 || n_jp >= total_nodes ||
                    n_jm < 0 || n_jm >= total_nodes ||
                    n_kp < 0 || n_kp >= total_nodes ||
                    n_km < 0 || n_km >= total_nodes) {
                    continue;
                }

                double dx = mesh.dx;
                double dy = mesh.dy;
                double dz = mesh.dz;

                if (dx < 1e-12 || dy < 1e-12 || dz < 1e-12) {
                    continue;
                }

                double vm_p_x = mesh.nodes[n_ip].stress.von_mises;
                double vm_m_x = mesh.nodes[n_im].stress.von_mises;
                double vm_p_y = mesh.nodes[n_jp].stress.von_mises;
                double vm_m_y = mesh.nodes[n_jm].stress.von_mises;
                double vm_p_z = mesh.nodes[n_kp].stress.von_mises;
                double vm_m_z = mesh.nodes[n_km].stress.von_mises;

                double dvm_dx = (vm_p_x - vm_m_x) / (2.0 * dx);
                double dvm_dy = (vm_p_y - vm_m_y) / (2.0 * dy);
                double dvm_dz = (vm_p_z - vm_m_z) / (2.0 * dz);

                double gradient_mag_pa_per_m = sqrt(dvm_dx * dvm_dx + dvm_dy * dvm_dy + dvm_dz * dvm_dz);
                mesh.nodes[n].stress_gradient_magnitude = gradient_mag_pa_per_m / 1e9;
            }
        }
    }
}

void StressAnalysisFEM::mark_nodes_for_refinement(FEAMesh& mesh) {
    if (mesh.nodes.empty()) return;

    double avg_spacing = std::cbrt(mesh.dx * mesh.dy * mesh.dz);
    double crack_tip_distance = 3.0 * avg_spacing;

    for (auto& node : mesh.nodes) {
        node.needs_refinement = false;

        double gradient_threshold = config_.refinement_stress_gradient_threshold;
        double density_threshold = config_.refinement_crack_density_threshold;
        double coarsening_threshold = config_.coarsening_stress_gradient_threshold;

        bool should_refine = false;
        bool should_coarsen = false;

        if (node.stress_gradient_magnitude > gradient_threshold) {
            should_refine = true;
        }

        if (node.crack_density > density_threshold) {
            should_refine = true;
        }

        double min_dist_to_crack_tip = std::numeric_limits<double>::max();
        for (const auto& neighbor : mesh.nodes) {
            if (neighbor.crack_density > 0.8) {
                double dx = node.x - neighbor.x;
                double dy = node.y - neighbor.y;
                double dz = node.z - neighbor.z;
                double dist = sqrt(dx * dx + dy * dy + dz * dz);
                min_dist_to_crack_tip = std::min(min_dist_to_crack_tip, dist);
            }
        }
        if (min_dist_to_crack_tip < crack_tip_distance) {
            should_refine = true;
        }

        if (node.stress_gradient_magnitude < coarsening_threshold &&
            node.crack_density < 0.1 * density_threshold &&
            node.refinement_level > 0) {
            should_coarsen = true;
        }

        if (node.refinement_level >= config_.max_refinement_level) {
            should_refine = false;
        }

        if (should_refine && !should_coarsen) {
            node.needs_refinement = true;
        }
    }
}

void StressAnalysisFEM::refine_marked_nodes(FEAMesh& mesh) {
    if (mesh.nodes.empty()) return;

    if (static_cast<int>(mesh.nodes.size()) >= config_.max_total_nodes) return;

    std::vector<FEMNode> new_nodes;
    new_nodes.reserve(mesh.nodes.size() * 2);

    std::vector<bool> parent_removed(mesh.nodes.size(), false);

    for (int i = 0; i < static_cast<int>(mesh.nodes.size()); ++i) {
        const auto& node = mesh.nodes[i];

        if (node.needs_refinement &&
            node.refinement_level < config_.max_refinement_level &&
            static_cast<int>(new_nodes.size()) + 8 < config_.max_total_nodes) {

            double dx = mesh.dx;
            double dy = mesh.dy;
            double dz = mesh.dz;

            for (int octant = 0; octant < 8; ++octant) {
                FEMNode child;
                child.x = node.x + get_octant_sign(octant, 0) * dx / 4.0;
                child.y = node.y + get_octant_sign(octant, 1) * dy / 4.0;
                child.z = node.z + get_octant_sign(octant, 2) * dz / 4.0;

                child.refinement_level = node.refinement_level + 1;
                child.parent_node_idx = i;
                child.needs_refinement = false;
                child.stress_gradient_magnitude = 0.0;

                child.crack_density = node.crack_density * (0.9 + 0.2 * (rand() / static_cast<double>(RAND_MAX)));
                child.crack_direction[0] = node.crack_direction[0];
                child.crack_direction[1] = node.crack_direction[1];
                child.crack_direction[2] = node.crack_direction[2];

                child.displacement[0] = node.displacement[0];
                child.displacement[1] = node.displacement[1];
                child.displacement[2] = node.displacement[2];

                for (int s = 0; s < 6; ++s) {
                    child.strain[s] = node.strain[s];
                }
                child.stress = node.stress;

                child.is_boundary = node.is_boundary;

                new_nodes.push_back(child);
            }

            parent_removed[i] = true;
        }
    }

    for (int i = 0; i < static_cast<int>(mesh.nodes.size()); ++i) {
        if (!parent_removed[i]) {
            new_nodes.push_back(mesh.nodes[i]);
        }
    }

    mesh.nodes.swap(new_nodes);

    mesh.refinement_levels.clear();
    mesh.refinement_levels.reserve(mesh.nodes.size());
    for (const auto& node : mesh.nodes) {
        mesh.refinement_levels.push_back(node.refinement_level);
        if (node.refinement_level > mesh.max_level_used) {
            mesh.max_level_used = node.refinement_level;
        }
    }
}

void StressAnalysisFEM::rebuild_node_connectivity(FEAMesh& mesh) {
    if (mesh.nodes.empty()) return;

    int num_nodes = static_cast<int>(mesh.nodes.size());
    int new_nx = static_cast<int>(std::round(std::cbrt(num_nodes)));
    int new_ny = new_nx;
    int new_nz = new_nx;

    while (new_nx * new_ny * new_nz < num_nodes) {
        if (new_nx <= new_ny && new_nx <= new_nz) new_nx++;
        else if (new_ny <= new_nx && new_ny <= new_nz) new_ny++;
        else new_nz++;
    }

    mesh.nx = std::max(new_nx, 2);
    mesh.ny = std::max(new_ny, 2);
    mesh.nz = std::max(new_nz, 2);

    if (mesh.nx > 1) mesh.dx = (mesh.bbox_max[0] - mesh.bbox_min[0]) / (mesh.nx - 1);
    if (mesh.ny > 1) mesh.dy = (mesh.bbox_max[1] - mesh.bbox_min[1]) / (mesh.ny - 1);
    if (mesh.nz > 1) mesh.dz = (mesh.bbox_max[2] - mesh.bbox_min[2]) / (mesh.nz - 1);

    mesh.dx = std::max(mesh.dx, 1e-9);
    mesh.dy = std::max(mesh.dy, 1e-9);
    mesh.dz = std::max(mesh.dz, 1e-9);
}

void StressAnalysisFEM::adaptive_mesh_refinement(FEAMesh& mesh) {
    if (mesh.nodes.empty()) return;

    for (int iteration = 0; iteration < config_.max_refinement_level; ++iteration) {
        solve_elasticity(mesh);
        apply_crack_stress_coupling(mesh);
        compute_von_mises(mesh);

        compute_stress_gradients(mesh);

        mark_nodes_for_refinement(mesh);

        bool has_nodes_to_refine = false;
        for (const auto& node : mesh.nodes) {
            if (node.needs_refinement) {
                has_nodes_to_refine = true;
                break;
            }
        }

        if (!has_nodes_to_refine) {
            break;
        }

        if (static_cast<int>(mesh.nodes.size()) >= config_.max_total_nodes) {
            break;
        }

        refine_marked_nodes(mesh);
        rebuild_node_connectivity(mesh);
    }
}

void StressAnalysisFEM::solve_elasticity(FEAMesh& mesh) {
    if (mesh.nodes.empty()) return;

    int total_nodes = mesh.nx * mesh.ny * mesh.nz;
    int max_iterations = 50;
    double relaxation = 0.5;

    auto idx = [&](int i, int j, int k) -> int {
        return k * mesh.nx * mesh.ny + j * mesh.nx + i;
    };

    for (int iter = 0; iter < max_iterations; ++iter) {
        for (int k = 1; k < mesh.nz - 1; ++k) {
            for (int j = 1; j < mesh.ny - 1; ++j) {
                for (int i = 1; i < mesh.nx - 1; ++i) {
                    int n = idx(i, j, k);
                    if (mesh.nodes[n].is_boundary) continue;

                    int n_ip = idx(i + 1, j, k);
                    int n_im = idx(i - 1, j, k);
                    int n_jp = idx(i, j + 1, k);
                    int n_jm = idx(i, j - 1, k);
                    int n_kp = idx(i, j, k + 1);
                    int n_km = idx(i, j, k - 1);

                    double inv_dx2 = 1.0 / (mesh.dx * mesh.dx);
                    double inv_dy2 = 1.0 / (mesh.dy * mesh.dy);
                    double inv_dz2 = 1.0 / (mesh.dz * mesh.dz);
                    double diag = 2.0 * (inv_dx2 + inv_dy2 + inv_dz2);

                    double rho = mesh.nodes[n].crack_density * config_.max_stress_mpa * 1e6;
                    double coupling = config_.directional_coupling;

                    for (int d = 0; d < 3; ++d) {
                        double laplacian =
                            (mesh.nodes[n_ip].displacement[d] + mesh.nodes[n_im].displacement[d]) * inv_dx2 +
                            (mesh.nodes[n_jp].displacement[d] + mesh.nodes[n_jm].displacement[d]) * inv_dy2 +
                            (mesh.nodes[n_kp].displacement[d] + mesh.nodes[n_km].displacement[d]) * inv_dz2;

                        double source = rho * mesh.nodes[n].crack_direction[d] * coupling / (lambda_ + 2.0 * mu_ + 1e-12);
                        double new_u = (laplacian + source) / diag;
                        mesh.nodes[n].displacement[d] = (1.0 - relaxation) * mesh.nodes[n].displacement[d] + relaxation * new_u;
                    }
                }
            }
        }
    }

    for (int k = 1; k < mesh.nz - 1; ++k) {
        for (int j = 1; j < mesh.ny - 1; ++j) {
            for (int i = 1; i < mesh.nx - 1; ++i) {
                int n = idx(i, j, k);
                if (mesh.nodes[n].is_boundary) continue;

                int n_ip = idx(i + 1, j, k);
                int n_im = idx(i - 1, j, k);
                int n_jp = idx(i, j + 1, k);
                int n_jm = idx(i, j - 1, k);
                int n_kp = idx(i, j, k + 1);
                int n_km = idx(i, j, k - 1);

                double u_x = (mesh.nodes[n_ip].displacement[0] - mesh.nodes[n_im].displacement[0]) / (2.0 * mesh.dx);
                double v_y = (mesh.nodes[n_jp].displacement[1] - mesh.nodes[n_jm].displacement[1]) / (2.0 * mesh.dy);
                double w_z = (mesh.nodes[n_kp].displacement[2] - mesh.nodes[n_km].displacement[2]) / (2.0 * mesh.dz);

                double u_y = (mesh.nodes[n_jp].displacement[0] - mesh.nodes[n_jm].displacement[0]) / (2.0 * mesh.dy);
                double v_x = (mesh.nodes[n_ip].displacement[1] - mesh.nodes[n_im].displacement[1]) / (2.0 * mesh.dx);

                double v_z = (mesh.nodes[n_kp].displacement[1] - mesh.nodes[n_km].displacement[1]) / (2.0 * mesh.dz);
                double w_y = (mesh.nodes[n_jp].displacement[2] - mesh.nodes[n_jm].displacement[2]) / (2.0 * mesh.dy);

                double w_x = (mesh.nodes[n_ip].displacement[2] - mesh.nodes[n_im].displacement[2]) / (2.0 * mesh.dx);
                double u_z = (mesh.nodes[n_kp].displacement[0] - mesh.nodes[n_km].displacement[0]) / (2.0 * mesh.dz);

                mesh.nodes[n].strain[0] = u_x;
                mesh.nodes[n].strain[1] = v_y;
                mesh.nodes[n].strain[2] = w_z;
                mesh.nodes[n].strain[3] = 0.5 * (u_y + v_x);
                mesh.nodes[n].strain[4] = 0.5 * (v_z + w_y);
                mesh.nodes[n].strain[5] = 0.5 * (w_x + u_z);
            }
        }
    }
}

double StressAnalysisFEM::hookes_law_3d(double strain_xx, double strain_yy, double strain_zz,
                                         double strain_xy, double strain_yz, double strain_zx,
                                         int component) const {
    double trace = strain_xx + strain_yy + strain_zz;
    switch (component) {
        case 0: return lambda_ * trace + 2.0 * mu_ * strain_xx;
        case 1: return lambda_ * trace + 2.0 * mu_ * strain_yy;
        case 2: return lambda_ * trace + 2.0 * mu_ * strain_zz;
        case 3: return 2.0 * mu_ * strain_xy;
        case 4: return 2.0 * mu_ * strain_yz;
        case 5: return 2.0 * mu_ * strain_zx;
        default: return 0.0;
    }
}

void StressAnalysisFEM::apply_crack_stress_coupling(FEAMesh& mesh) {
    for (auto& node : mesh.nodes) {
        if (node.is_boundary) continue;

        node.stress.sigma_xx = hookes_law_3d(
            node.strain[0], node.strain[1], node.strain[2],
            node.strain[3], node.strain[4], node.strain[5], 0);
        node.stress.sigma_yy = hookes_law_3d(
            node.strain[0], node.strain[1], node.strain[2],
            node.strain[3], node.strain[4], node.strain[5], 1);
        node.stress.sigma_zz = hookes_law_3d(
            node.strain[0], node.strain[1], node.strain[2],
            node.strain[3], node.strain[4], node.strain[5], 2);
        node.stress.tau_xy = hookes_law_3d(
            node.strain[0], node.strain[1], node.strain[2],
            node.strain[3], node.strain[4], node.strain[5], 3);
        node.stress.tau_yz = hookes_law_3d(
            node.strain[0], node.strain[1], node.strain[2],
            node.strain[3], node.strain[4], node.strain[5], 4);
        node.stress.tau_zx = hookes_law_3d(
            node.strain[0], node.strain[1], node.strain[2],
            node.strain[3], node.strain[4], node.strain[5], 5);

        double coupling = config_.directional_coupling;
        double alpha = node.crack_density * coupling;

        double nx = node.crack_direction[0];
        double ny = node.crack_direction[1];
        double nz = node.crack_direction[2];

        double stress_release = alpha * config_.max_stress_mpa * 1e6;
        node.stress.sigma_xx -= stress_release * nx * nx;
        node.stress.sigma_yy -= stress_release * ny * ny;
        node.stress.sigma_zz -= stress_release * nz * nz;
        node.stress.tau_xy -= stress_release * nx * ny;
        node.stress.tau_yz -= stress_release * ny * nz;
        node.stress.tau_zx -= stress_release * nz * nx;
    }
}

void StressAnalysisFEM::compute_von_mises(FEAMesh& mesh) {
    for (auto& node : mesh.nodes) {
        double sxx = node.stress.sigma_xx;
        double syy = node.stress.sigma_yy;
        double szz = node.stress.sigma_zz;
        double txy = node.stress.tau_xy;
        double tyz = node.stress.tau_yz;
        double tzx = node.stress.tau_zx;

        double vm_sq = 0.5 * (
            (sxx - syy) * (sxx - syy) +
            (syy - szz) * (syy - szz) +
            (szz - sxx) * (szz - sxx) +
            6.0 * (txy * txy + tyz * tyz + tzx * tzx)
        );

        node.stress.von_mises = sqrt(std::max(0.0, vm_sq));
    }
}

void StressAnalysisFEM::smooth_stress_field(FEAMesh& mesh) {
    if (mesh.nodes.empty()) return;

    FEAMesh temp_mesh = mesh;
    auto idx = [&](int i, int j, int k) -> int {
        return k * mesh.nx * mesh.ny + j * mesh.nx + i;
    };

    double sigma = 1.5;
    std::vector<double> weights = {
        gaussian_weight(sqrt(2.0), sigma),
        gaussian_weight(1.0, sigma),
        gaussian_weight(sqrt(3.0), sigma)
    };

    for (int k = 1; k < mesh.nz - 1; ++k) {
        for (int j = 1; j < mesh.ny - 1; ++j) {
            for (int i = 1; i < mesh.nx - 1; ++i) {
                int n = idx(i, j, k);
                if (mesh.nodes[n].is_boundary) continue;

                double total_w = gaussian_weight(0.0, sigma);
                StressTensor sum_stress = temp_mesh.nodes[n].stress;
                sum_stress.sigma_xx *= total_w;
                sum_stress.sigma_yy *= total_w;
                sum_stress.sigma_zz *= total_w;
                sum_stress.tau_xy *= total_w;
                sum_stress.tau_yz *= total_w;
                sum_stress.tau_zx *= total_w;
                sum_stress.von_mises *= total_w;

                for (int dk = -1; dk <= 1; ++dk) {
                    for (int dj = -1; dj <= 1; ++dj) {
                        for (int di = -1; di <= 1; ++di) {
                            if (di == 0 && dj == 0 && dk == 0) continue;
                            int ni = idx(i + di, j + dj, k + dk);
                            double dist = sqrt(di * di + dj * dj + dk * dk);
                            double w = gaussian_weight(dist, sigma);

                            sum_stress.sigma_xx += w * temp_mesh.nodes[ni].stress.sigma_xx;
                            sum_stress.sigma_yy += w * temp_mesh.nodes[ni].stress.sigma_yy;
                            sum_stress.sigma_zz += w * temp_mesh.nodes[ni].stress.sigma_zz;
                            sum_stress.tau_xy += w * temp_mesh.nodes[ni].stress.tau_xy;
                            sum_stress.tau_yz += w * temp_mesh.nodes[ni].stress.tau_yz;
                            sum_stress.tau_zx += w * temp_mesh.nodes[ni].stress.tau_zx;
                            sum_stress.von_mises += w * temp_mesh.nodes[ni].stress.von_mises;

                            total_w += w;
                        }
                    }
                }

                if (total_w > 1e-12) {
                    mesh.nodes[n].stress.sigma_xx = sum_stress.sigma_xx / total_w;
                    mesh.nodes[n].stress.sigma_yy = sum_stress.sigma_yy / total_w;
                    mesh.nodes[n].stress.sigma_zz = sum_stress.sigma_zz / total_w;
                    mesh.nodes[n].stress.tau_xy = sum_stress.tau_xy / total_w;
                    mesh.nodes[n].stress.tau_yz = sum_stress.tau_yz / total_w;
                    mesh.nodes[n].stress.tau_zx = sum_stress.tau_zx / total_w;
                    mesh.nodes[n].stress.von_mises = sum_stress.von_mises / total_w;
                }
            }
        }
    }
}

void StressAnalysisFEM::assemble_result(const FEAMesh& mesh, StressAnalysisResult& result, int porcelain_id) {
    result.id = 0;
    result.porcelain_id = porcelain_id;
    result.method = "FEM_FINITE_DIFFERENCE";
    result.created_at = std::chrono::system_clock::now();
    result.grid_points.clear();

    double max_vm = 0.0;
    double sum_vm = 0.0;
    int count = 0;
    int high_stress_count = 0;
    double threshold = config_.max_stress_mpa * 1e6 * 0.7;

    for (const auto& node : mesh.nodes) {
        StressGridPoint gp;
        gp.x = node.x;
        gp.y = node.y;
        gp.z = node.z;
        gp.stress = node.stress;
        gp.crack_density = node.crack_density;
        gp.refinement_level = node.refinement_level;

        double nx = node.crack_direction[0];
        double ny = node.crack_direction[1];
        double nz = node.crack_direction[2];
        gp.principal_direction = atan2(ny, nx);

        result.grid_points.push_back(gp);

        max_vm = std::max(max_vm, node.stress.von_mises);
        sum_vm += node.stress.von_mises;
        count++;
        if (node.stress.von_mises > threshold) {
            high_stress_count++;
        }
    }

    result.max_von_mises = max_vm / 1e6;
    result.avg_von_mises = (count > 0) ? (sum_vm / count) / 1e6 : 0.0;
    result.high_stress_area_ratio = (count > 0) ? static_cast<double>(high_stress_count) / count : 0.0;

    result.parameters = {
        {"grid_resolution", config_.grid_resolution},
        {"youngs_modulus_gpa", config_.youngs_modulus_gpa},
        {"poissons_ratio", config_.poissons_ratio},
        {"lambda_pa", lambda_},
        {"mu_pa", mu_},
        {"crack_density_sensitivity", config_.crack_density_sensitivity},
        {"directional_coupling", config_.directional_coupling},
        {"max_stress_mpa", config_.max_stress_mpa}
    };

    json summary = {
        {"max_von_mises_mpa", result.max_von_mises},
        {"avg_von_mises_mpa", result.avg_von_mises},
        {"high_stress_area_ratio", result.high_stress_area_ratio},
        {"grid_point_count", static_cast<int>(result.grid_points.size())}
    };
    result.result = summary;
}

StressAnalysisResult StressAnalysisFEM::analyze(int porcelain_id,
                                                 const std::vector<CrackInfo>& cracks) {
    StressAnalysisResult result;

    FEAMesh mesh;
    build_mesh(mesh, cracks);

    if (mesh.nodes.empty()) {
        assemble_result(mesh, result, porcelain_id);
        return result;
    }

    compute_crack_density_field(mesh, cracks);

    for (auto& node : mesh.nodes) {
        principal_stress_direction(node);
    }

    solve_elasticity(mesh);

    if (config_.use_adaptive_mesh) {
        adaptive_mesh_refinement(mesh);
        compute_crack_density_field(mesh, cracks);
        for (auto& node : mesh.nodes) {
            principal_stress_direction(node);
        }
        solve_elasticity(mesh);
    }

    apply_crack_stress_coupling(mesh);
    compute_von_mises(mesh);
    smooth_stress_field(mesh);

    assemble_result(mesh, result, porcelain_id);

    return result;
}

json StressAnalysisFEM::result_to_json(const StressAnalysisResult& result) const {
    json j;
    j["id"] = result.id;
    j["porcelain_id"] = result.porcelain_id;
    j["method"] = result.method;
    j["parameters"] = result.parameters;
    j["max_von_mises"] = result.max_von_mises;
    j["avg_von_mises"] = result.avg_von_mises;
    j["high_stress_area_ratio"] = result.high_stress_area_ratio;
    j["result"] = result.result;

    json grid_array = json::array();
    for (const auto& gp : result.grid_points) {
        json gp_json = {
            {"x", gp.x},
            {"y", gp.y},
            {"z", gp.z},
            {"stress", gp.stress},
            {"crack_density", gp.crack_density},
            {"principal_direction", gp.principal_direction}
        };
        grid_array.push_back(gp_json);
    }
    j["grid_points"] = grid_array;

    return j;
}

}
}
