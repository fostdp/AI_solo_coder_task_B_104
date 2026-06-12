#pragma once

#include <vector>
#include <array>
#include <cmath>
#include <random>
#include <nlohmann/json.hpp>
#include "common.h"
#include "config.h"

namespace porcelain_monitor {
namespace algorithms {

struct DEMParticle {
    std::array<double, 3> position;
    std::array<double, 3> velocity;
    std::array<double, 3> force;
    double radius;
    double mass;
    int material_id;
    bool is_boundary = false;
    bool is_fixed = false;
    int id;
};

struct DEMContact {
    int particle_a;
    int particle_b;
    std::array<double, 3> normal;
    double overlap;
    double normal_force;
    std::array<double, 3> tangent_force;
};

struct DEMParameters {
    double youngs_modulus = 70e9;
    double poissons_ratio = 0.22;
    double density = 3950.0;
    double restitution_coeff = 0.3;
    double friction_coeff = 0.5;
    double adhesion_coeff = 1.0e-9;
    double time_step = 1e-9;
    double gravity = 9.81;
    double damping_coeff = 0.05;
    double min_particle_radius = 15e-9;
    double max_particle_radius = 35e-9;
    double bond_strength = 100e6;
    double surface_energy = 1.0;
};

struct DEMGeometry {
    std::array<double, 3> crack_origin;
    double crack_length;
    double crack_depth;
    double crack_width;
    double crack_angle;
    std::vector<std::array<double, 3>> boundary_points;
};

struct DEMResult {
    std::vector<DEMParticle> particles;
    std::vector<DEMContact> contacts;
    int total_steps;
    double simulation_time;
    double filling_rate;
    double average_packing_density;
    double bonding_strength;
    double surface_smoothness;
    double durability_score;
    int particle_count;
    int contact_count;
    double max_force;
    double avg_force;
    double porosity;
    std::vector<double> energy_history;
    std::vector<double> force_history;
    DEMParameters parameters;
};

class DEMSimulation {
public:
    DEMSimulation();
    ~DEMSimulation() = default;

    void set_parameters(const DEMParameters& params) { params_ = params; }
    const DEMParameters& get_parameters() const { return params_; }

    void set_crack_geometry(const CrackInfo& crack);
    void set_material_properties(const RepairMaterial& material);

    DEMResult run(int max_steps = 1000, bool visualize = false);

    void generate_particles(int count);
    void generate_boundary();

    void reset() {
        particles_.clear();
        particles_.shrink_to_fit();
        contacts_.clear();
        contacts_.shrink_to_fit();
        geometry_.boundary_points.clear();
        geometry_.boundary_points.shrink_to_fit();
    }

    int particle_count() const { return particles_.size(); }
    const std::vector<DEMParticle>& particles() const { return particles_; }
    const std::vector<DEMContact>& contacts() const { return contacts_; }

    nlohmann::json result_to_json(const DEMResult& result) const;

    double calculate_filling_rate() const;
    double calculate_packing_density() const;
    double calculate_surface_smoothness() const;
    double calculate_bonding_strength() const;
    double calculate_durability() const;
    double calculate_porosity() const;

private:
    static constexpr size_t MAX_CONTACTS = 50000;
    DEMParameters params_;
    DEMGeometry geometry_;
    std::vector<DEMParticle> particles_;
    std::vector<DEMContact> contacts_;
    std::mt19937 rng_{std::random_device{}()};
    RepairMaterial material_;

    void step();
    void compute_forces();
    void integrate_velocities();
    void integrate_positions();
    void detect_collisions();
    void apply_bonding();
    void compute_contact_forces(DEMParticle& a, DEMParticle& b);
    void apply_boundary_conditions();
    void update_contacts();

    double hertz_contact_force(double overlap, double radius_a, double radius_b) const;
    double damping_force(double relative_velocity, double overlap) const;
    double van_der_waals_force(double distance, double radius) const;
    double effective_radius(double r1, double r2) const;
    double effective_modulus() const;

    double random_double(double min, double max);
    std::array<double, 3> random_point_in_crack();
    bool is_inside_crack(const std::array<double, 3>& point) const;

    double vector_norm(const std::array<double, 3>& v) const;
    std::array<double, 3> vector_normalize(const std::array<double, 3>& v) const;
    std::array<double, 3> vector_sub(const std::array<double, 3>& a,
                                      const std::array<double, 3>& b) const;
    std::array<double, 3> vector_add(const std::array<double, 3>& a,
                                      const std::array<double, 3>& b) const;
    std::array<double, 3> vector_mul(const std::array<double, 3>& v, double s) const;
    double vector_dot(const std::array<double, 3>& a,
                       const std::array<double, 3>& b) const;
    std::array<double, 3> vector_cross(const std::array<double, 3>& a,
                                        const std::array<double, 3>& b) const;
};

}
}
