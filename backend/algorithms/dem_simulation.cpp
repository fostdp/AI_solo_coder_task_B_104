#include "dem_simulation.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace porcelain_monitor {
namespace algorithms {

DEMSimulation::DEMSimulation() {
    auto& cfg = config::get_config();
    params_.youngs_modulus = cfg.algorithms.dem.youngs_modulus;
    params_.poissons_ratio = cfg.algorithms.dem.poissons_ratio;
    params_.density = cfg.algorithms.dem.density;
    params_.time_step = cfg.algorithms.dem.time_step;
    params_.min_particle_radius = cfg.algorithms.dem.particle_radius_nm * 0.8;
    params_.max_particle_radius = cfg.algorithms.dem.particle_radius_nm * 1.2;
}

double DEMSimulation::vector_norm(const std::array<double, 3>& v) const {
    return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

std::array<double, 3> DEMSimulation::vector_normalize(const std::array<double, 3>& v) const {
    double n = vector_norm(v);
    if (n < 1e-15) return {0, 0, 0};
    return {v[0]/n, v[1]/n, v[2]/n};
}

std::array<double, 3> DEMSimulation::vector_sub(const std::array<double, 3>& a,
                                                 const std::array<double, 3>& b) const {
    return {a[0]-b[0], a[1]-b[1], a[2]-b[2]};
}

std::array<double, 3> DEMSimulation::vector_add(const std::array<double, 3>& a,
                                                 const std::array<double, 3>& b) const {
    return {a[0]+b[0], a[1]+b[1], a[2]+b[2]};
}

std::array<double, 3> DEMSimulation::vector_mul(const std::array<double, 3>& v, double s) const {
    return {v[0]*s, v[1]*s, v[2]*s};
}

double DEMSimulation::vector_dot(const std::array<double, 3>& a,
                                  const std::array<double, 3>& b) const {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

std::array<double, 3> DEMSimulation::vector_cross(const std::array<double, 3>& a,
                                                    const std::array<double, 3>& b) const {
    return {
        a[1]*b[2] - a[2]*b[1],
        a[2]*b[0] - a[0]*b[2],
        a[0]*b[1] - a[1]*b[0]
    };
}

double DEMSimulation::random_double(double min, double max) {
    std::uniform_real_distribution<double> dist(min, max);
    return dist(rng_);
}

double DEMSimulation::effective_radius(double r1, double r2) const {
    return (r1 * r2) / (r1 + r2);
}

double DEMSimulation::effective_modulus() const {
    return params_.youngs_modulus / (2.0 * (1.0 - params_.poissons_ratio * params_.poissons_ratio));
}

double DEMSimulation::hertz_contact_force(double overlap, double radius_a, double radius_b) const {
    if (overlap <= 0) return 0;
    double r_eff = effective_radius(radius_a, radius_b);
    double e_eff = effective_modulus();
    double k_h = (4.0/3.0) * e_eff * std::sqrt(r_eff);
    return k_h * std::pow(overlap, 1.5);
}

double DEMSimulation::damping_force(double relative_velocity, double overlap) const {
    if (overlap <= 0) return 0;
    double gamma = 2.0 * params_.damping_coeff * std::sqrt(params_.youngs_modulus * params_.density);
    return -gamma * relative_velocity * std::sqrt(overlap);
}

double DEMSimulation::van_der_waals_force(double distance, double radius) const {
    const double hamaker = 1.0e-19;
    if (distance < 1e-10) distance = 1e-10;
    return -hamaker * radius / (6.0 * distance * distance);
}

void DEMSimulation::set_crack_geometry(const CrackInfo& crack) {
    geometry_.crack_length = crack.total_length;
    geometry_.crack_depth = crack.max_depth;
    geometry_.crack_width = crack.max_width;

    if (!crack.points.empty()) {
        geometry_.crack_origin = {
            crack.points[0].x,
            crack.points[0].y,
            crack.points[0].z
        };
    }

    for (const auto& pt : crack.points) {
        geometry_.boundary_points.push_back({pt.x, pt.y, pt.z});
    }

    geometry_.crack_angle = std::atan2(crack.max_depth, crack.max_width);
}

void DEMSimulation::set_material_properties(const RepairMaterial& material) {
    material_ = material;
    params_.youngs_modulus = 70e9;
    params_.density = material.type == RepairMaterialType::ZIRCONIA ? 5890.0 :
                       material.type == RepairMaterialType::SILICA ? 2200.0 : 3950.0;
    params_.min_particle_radius = material.particle_size_nm * 0.6 * 1e-9;
    params_.max_particle_radius = material.particle_size_nm * 1.4 * 1e-9;
}

std::array<double, 3> DEMSimulation::random_point_in_crack() {
    double half_len = geometry_.crack_length * 0.5;
    double half_depth = geometry_.crack_depth * 0.5 * 1e-6;
    double half_width = geometry_.crack_width * 0.5 * 1e-6;

    return {
        geometry_.crack_origin[0] + random_double(-half_len, half_len),
        geometry_.crack_origin[1] + random_double(-half_width, half_width),
        geometry_.crack_origin[2] + random_double(-half_depth, half_depth)
    };
}

bool DEMSimulation::is_inside_crack(const std::array<double, 3>& point) const {
    auto rel = vector_sub(point, geometry_.crack_origin);
    double half_len = geometry_.crack_length * 0.5;
    double half_depth = geometry_.crack_depth * 0.5 * 1e-6;
    double half_width = geometry_.crack_width * 0.5 * 1e-6;

    return std::abs(rel[0]) <= half_len &&
           std::abs(rel[1]) <= half_width &&
           std::abs(rel[2]) <= half_depth;
}

void DEMSimulation::generate_particles(int count) {
    particles_.clear();

    for (int i = 0; i < count; ++i) {
        double radius = random_double(params_.min_particle_radius, params_.max_particle_radius);
        auto pos = random_point_in_crack();

        bool overlapping = false;
        for (const auto& p : particles_) {
            auto diff = vector_sub(pos, p.position);
            double dist = vector_norm(diff);
            if (dist < radius + p.radius) {
                overlapping = true;
                break;
            }
        }

        if (!overlapping) {
            DEMParticle particle;
            particle.id = i;
            particle.position = pos;
            particle.velocity = {0, 0, 0};
            particle.force = {0, 0, -9.81 * params_.density * (4.0/3.0) * M_PI * std::pow(radius, 3)};
            particle.radius = radius;
            particle.mass = params_.density * (4.0/3.0) * M_PI * std::pow(radius, 3);
            particle.material_id = material_.id;
            particle.is_boundary = false;
            particle.is_fixed = false;
            particles_.push_back(particle);
        }
    }
}

void DEMSimulation::generate_boundary() {
    int boundary_count = static_cast<int>(geometry_.boundary_points.size());
    for (int i = 0; i < boundary_count; ++i) {
        DEMParticle particle;
        particle.id = particles_.size() + i;
        particle.position = geometry_.boundary_points[i];
        particle.velocity = {0, 0, 0};
        particle.force = {0, 0, 0};
        particle.radius = 0.5e-6;
        particle.mass = 1e6;
        particle.material_id = 0;
        particle.is_boundary = true;
        particle.is_fixed = true;
        particles_.push_back(particle);
    }
}

void DEMSimulation::compute_contact_forces(DEMParticle& a, DEMParticle& b) {
    auto diff = vector_sub(b.position, a.position);
    double dist = vector_norm(diff);
    double min_dist = a.radius + b.radius;

    if (dist < min_dist && dist > 1e-15) {
        auto normal = vector_normalize(diff);
        double overlap = min_dist - dist;

        double rel_vel = vector_dot(vector_sub(b.velocity, a.velocity), normal);

        double f_hertz = hertz_contact_force(overlap, a.radius, b.radius);
        double f_damp = damping_force(rel_vel, overlap);
        double f_vdw = van_der_waals_force(dist, (a.radius + b.radius) / 2.0);

        double f_normal = f_hertz + f_damp + f_vdw;

        auto force_vec = vector_mul(normal, -f_normal);
        std::array<double, 3> tangent = {0.0, 0.0, 0.0};

        a.force = vector_add(a.force, force_vec);
        b.force = vector_sub(b.force, force_vec);

        DEMContact contact;
        contact.particle_a = a.id;
        contact.particle_b = b.id;
        contact.normal = normal;
        contact.overlap = overlap;
        contact.normal_force = f_normal;
        contact.tangent_force = tangent;
        contacts_.push_back(contact);
    }
}

void DEMSimulation::detect_collisions() {
    contacts_.clear();
    contacts_.reserve(particles_.size() * 2);

    for (size_t i = 0; i < particles_.size(); ++i) {
        for (size_t j = i + 1; j < particles_.size(); ++j) {
            if (contacts_.size() >= MAX_CONTACTS) break;
            compute_contact_forces(particles_[i], particles_[j]);
        }
        if (contacts_.size() >= MAX_CONTACTS) break;
    }
}

void DEMSimulation::apply_boundary_conditions() {
    for (auto& p : particles_) {
        if (p.is_fixed) {
            p.velocity = {0, 0, 0};
            p.force = {0, 0, 0};
            continue;
        }

        double max_dist = std::max({geometry_.crack_length,
                                    geometry_.crack_width * 1e-6,
                                    geometry_.crack_depth * 1e-6});

        auto rel = vector_sub(p.position, geometry_.crack_origin);
        if (std::abs(rel[0]) > max_dist ||
            std::abs(rel[1]) > max_dist ||
            std::abs(rel[2]) > max_dist) {
            p.velocity = {0, 0, 0};
            p.position = random_point_in_crack();
        }
    }
}

void DEMSimulation::integrate_velocities() {
    for (auto& p : particles_) {
        if (!p.is_fixed && p.mass > 0) {
            auto acc = vector_mul(p.force, 1.0 / p.mass);
            p.velocity = vector_add(p.velocity, vector_mul(acc, params_.time_step * 0.5));
        }
    }
}

void DEMSimulation::integrate_positions() {
    for (auto& p : particles_) {
        if (!p.is_fixed) {
            p.position = vector_add(p.position, vector_mul(p.velocity, params_.time_step));
            p.force = {0, 0, -9.81 * p.mass};
        }
    }
}

void DEMSimulation::step() {
    integrate_velocities();
    detect_collisions();
    integrate_positions();
    apply_boundary_conditions();
}

double DEMSimulation::calculate_filling_rate() const {
    if (geometry_.crack_length <= 0 || geometry_.crack_width <= 0 || geometry_.crack_depth <= 0) {
        return 0;
    }

    double crack_volume = geometry_.crack_length *
                          geometry_.crack_width * 1e-6 *
                          geometry_.crack_depth * 1e-6;

    double particle_volume = 0;
    for (const auto& p : particles_) {
        if (!p.is_boundary && is_inside_crack(p.position)) {
            particle_volume += (4.0/3.0) * M_PI * std::pow(p.radius, 3);
        }
    }

    return crack_volume > 0 ? std::min(1.0, particle_volume / crack_volume) : 0;
}

double DEMSimulation::calculate_packing_density() const {
    double occupied_volume = 0;
    double total_volume = 0;

    for (const auto& p : particles_) {
        if (!p.is_boundary) {
            occupied_volume += (4.0/3.0) * M_PI * std::pow(p.radius, 3);
        }
    }

    double half_len = geometry_.crack_length * 0.5;
    double half_depth = geometry_.crack_depth * 0.5 * 1e-6;
    double half_width = geometry_.crack_width * 0.5 * 1e-6;
    total_volume = 8 * half_len * half_width * half_depth;

    return total_volume > 0 ? occupied_volume / total_volume : 0;
}

double DEMSimulation::calculate_surface_smoothness() const {
    if (particles_.size() < 2) return 1.0;

    double variance = 0;
    double mean_z = 0;
    int count = 0;

    for (const auto& p : particles_) {
        if (!p.is_boundary && is_inside_crack(p.position)) {
            mean_z += p.position[2];
            count++;
        }
    }

    if (count > 0) {
        mean_z /= count;
        for (const auto& p : particles_) {
            if (!p.is_boundary && is_inside_crack(p.position)) {
                variance += std::pow(p.position[2] - mean_z, 2);
            }
        }
        variance /= count;
    }

    double roughness = std::sqrt(variance);
    double smoothness = 1.0 - std::min(1.0, roughness / (params_.max_particle_radius * 2.0));
    return std::max(0.0, smoothness);
}

double DEMSimulation::calculate_bonding_strength() const {
    double total_bond = 0;
    int contact_count = 0;

    for (const auto& c : contacts_) {
        if (c.normal_force > 0) {
            total_bond += c.normal_force / std::max(1e-9, c.overlap);
            contact_count++;
        }
    }

    if (contact_count == 0) return 0.5;

    double avg_bond = total_bond / contact_count;
    double normalized = avg_bond / (params_.bond_strength * 1e-6);
    return std::min(1.0, normalized);
}

double DEMSimulation::calculate_durability() const {
    double filling = calculate_filling_rate();
    double bonding = calculate_bonding_strength();
    double smoothness = calculate_surface_smoothness();
    double packing = calculate_packing_density();

    return 0.35 * filling + 0.3 * bonding + 0.2 * smoothness + 0.15 * packing;
}

double DEMSimulation::calculate_porosity() const {
    double packing = calculate_packing_density();
    return 1.0 - packing;
}

DEMResult DEMSimulation::run(int max_steps, bool) {
    DEMResult result;
    result.parameters = params_;
    result.particle_count = static_cast<int>(particles_.size() -
        std::count_if(particles_.begin(), particles_.end(),
                      [](const DEMParticle& p) { return p.is_boundary; }));

    result.energy_history.reserve(max_steps / 10 + 1);
    result.force_history.reserve(max_steps / 10 + 1);

    double total_energy = 0;
    double max_force = 0;
    double sum_force = 0;

    for (int step = 0; step < max_steps; ++step) {
        this->step();

        double ke = 0, pe = 0;
        double step_max_force = 0;
        double step_sum_force = 0;

        for (const auto& p : particles_) {
            if (!p.is_fixed) {
                ke += 0.5 * p.mass * vector_dot(p.velocity, p.velocity);
                pe += p.mass * 9.81 * p.position[2];
            }
            double f = vector_norm(p.force);
            step_max_force = std::max(step_max_force, f);
            step_sum_force += f;
        }

        total_energy = ke + pe;
        max_force = std::max(max_force, step_max_force);
        sum_force += step_sum_force;

        if (step % 10 == 0) {
            result.energy_history.push_back(total_energy);
            result.force_history.push_back(step_max_force);
        }

        if (step % 100 == 0 && step > 0) {
            double avg_vel = 0;
            int count = 0;
            for (const auto& p : particles_) {
                if (!p.is_fixed) {
                    avg_vel += vector_norm(p.velocity);
                    count++;
                }
            }
            if (count > 0) avg_vel /= count;
            if (avg_vel < 1e-12) break;
        }

        if (step % 500 == 0 && step > 0) {
            contacts_.shrink_to_fit();
        }
    }

    result.total_steps = max_steps;
    result.simulation_time = max_steps * params_.time_step;

    for (const auto& p : particles_) {
        if (!p.is_boundary) {
            result.particles.push_back(p);
        }
    }

    result.contact_count = static_cast<int>(contacts_.size());
    result.filling_rate = calculate_filling_rate();
    result.average_packing_density = calculate_packing_density();
    result.bonding_strength = calculate_bonding_strength();
    result.surface_smoothness = calculate_surface_smoothness();
    result.durability_score = calculate_durability();
    result.porosity = calculate_porosity();
    result.max_force = max_force;
    result.avg_force = max_steps > 0 ? sum_force / max_steps : 0;

    contacts_.shrink_to_fit();

    return result;
}

nlohmann::json DEMSimulation::result_to_json(const DEMResult& result) const {
    nlohmann::json j;

    j["total_steps"] = result.total_steps;
    j["simulation_time"] = result.simulation_time;
    j["particle_count"] = result.particle_count;
    j["contact_count"] = result.contact_count;
    j["filling_rate"] = result.filling_rate;
    j["packing_density"] = result.average_packing_density;
    j["bonding_strength"] = result.bonding_strength;
    j["surface_smoothness"] = result.surface_smoothness;
    j["durability_score"] = result.durability_score;
    j["porosity"] = result.porosity;
    j["max_force"] = result.max_force;
    j["avg_force"] = result.avg_force;
    j["energy_history"] = result.energy_history;
    j["force_history"] = result.force_history;

    nlohmann::json particles_json = nlohmann::json::array();
    for (const auto& p : result.particles) {
        if (!p.is_boundary) {
            particles_json.push_back({
                {"id", p.id},
                {"position", {p.position[0], p.position[1], p.position[2]}},
                {"radius", p.radius}
            });
        }
    }
    j["particles"] = particles_json;

    j["parameters"] = {
        {"youngs_modulus", result.parameters.youngs_modulus},
        {"poissons_ratio", result.parameters.poissons_ratio},
        {"density", result.parameters.density},
        {"particle_radius_range", {result.parameters.min_particle_radius,
                                   result.parameters.max_particle_radius}}
    };

    return j;
}

}
}
