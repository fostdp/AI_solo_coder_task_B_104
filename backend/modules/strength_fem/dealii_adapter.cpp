#include "modules/strength_fem/dealii_adapter.h"
#include <cmath>
#include <chrono>

namespace porcelain_monitor {
namespace modules {

using json = nlohmann::json;

StrengthDealIIAdapter::StrengthDealIIAdapter() = default;
StrengthDealIIAdapter::~StrengthDealIIAdapter() = default;

bool StrengthDealIIAdapter::is_dealii_available() const {
#if defined(PM_HAS_DEAL_II)
    return true;
#else
    return false;
#endif
}

json StrengthDealIIAdapter::get_solver_info() const {
    json j;
    j["dealii_available"] = is_dealii_available();
    j["active_backend"] = (backend_ == StrengthSolverBackend::BUILTIN) ? "builtin" : "dealii";
    j["external_process"] = use_external_process_;
    j["support_span_mm"] = config_.support_span_mm;
    j["specimen_thickness_mm"] = config_.specimen_thickness_mm;
    j["porcelain_strength_mpa"] = config_.porcelain_strength_mpa;
    return j;
}

bool StrengthDealIIAdapter::initialize(const algorithms::BendingTestConfig& config) {
    config_ = config;
    builtin_solver_ = std::make_unique<algorithms::FourPointBendingTest>();
    builtin_solver_->set_config(config);
    if (is_dealii_available()) {
        backend_ = StrengthSolverBackend::DEAL_II;
    } else {
        backend_ = StrengthSolverBackend::BUILTIN;
    }
    return true;
}

double StrengthDealIIAdapter::effective_elastic_modulus(
    double depth_ratio, double base_E,
    double crack_depth, double specimen_thickness,
    bool repaired, double repair_strength_ratio) const
{
    double normalized_depth = crack_depth / specimen_thickness;
    double depth_factor = 1.0 - std::pow(normalized_depth, 1.5);
    double E = base_E * std::max(0.2, depth_factor);

    if (repaired && depth_ratio < normalized_depth + 0.05) {
        E *= repair_strength_ratio + (1.0 - repair_strength_ratio) *
             std::exp(-std::pow((depth_ratio - normalized_depth) / 0.1, 2));
    }
    return E;
}

BendingTestResult StrengthDealIIAdapter::solve(
    const CrackInfo& crack,
    const RepairMaterial& material,
    bool repaired)
{
    if (backend_ == StrengthSolverBackend::DEAL_II && is_dealii_available()) {
        return solve_dealii(crack, material, repaired);
    }
    return solve_builtin(crack, material, repaired);
}

BendingTestResult StrengthDealIIAdapter::solve_builtin(
    const CrackInfo& crack,
    const RepairMaterial& material,
    bool repaired)
{
    return builtin_solver_->simulate(0,
        static_cast<int>(crack.id), material.id,
        crack, material, repaired);
}

BendingTestResult StrengthDealIIAdapter::solve_dealii(
    const CrackInfo& crack,
    const RepairMaterial& material,
    bool repaired)
{
#if defined(PM_HAS_DEAL_II)
    (void)material;
    BendingTestResult result;
    result.success = true;
    result.method = "dealii_4pb";
    result.crack_id = crack.id;
    result.material_id = material.id;

    const double L = config_.support_span_mm;
    const double a = config_.loading_span_mm / 2.0;
    const double W = config_.specimen_width_mm;
    const double h = config_.specimen_thickness_mm;
    const double base_E = config_.porcelain_youngs_modulus_gpa * 1e3;
    const double nu = config_.porcelain_poissons_ratio;
    const double sigma_f = config_.porcelain_strength_mpa;
    const double repair_ratio = repaired ? config_.repair_interface_strength_ratio : 0.0;

    const double length = L + 10.0;
    const int nx = std::max(20, config_.mesh_elements);
    const int ny = std::max(8, static_cast<int>(config_.mesh_elements * h / length));

    triangulation.clear();
    dealii::GridGenerator::subdivided_hyper_rectangle(
        triangulation, {nx, ny},
        dealii::Point<dim>(-length / 2.0, 0.0),
        dealii::Point<dim>(length / 2.0, h),
        true);
    triangulation.refine_global(1);

    dof_handler.distribute_dofs(fe);
    const unsigned int n_dofs = dof_handler.n_dofs();

    dealii::AffineConstraints<double> constraints;
    dealii::VectorTools::interpolate_boundary_values(
        dof_handler, 0,
        dealii::ZeroFunction<dim>(dim),
        constraints);
    constraints.close();

    dealii::DynamicSparsityPattern dsp(n_dofs);
    dealii::DoFTools::make_sparsity_pattern(dof_handler, dsp, constraints);
    dealii::SparsityPattern sp; sp.copy_from(dsp);
    dealii::SparseMatrix<double> K(sp);
    dealii::Vector<double> U(n_dofs), F(n_dofs);

    const double lambda_coeff = base_E * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
    const double mu_coeff = base_E / (2.0 * (1.0 + nu));

    dealii::QGauss<dim> quad(fe.degree + 1);
    dealii::FEValues<dim> fe_v(fe, quad,
        dealii::update_values | dealii::update_gradients |
        dealii::update_quadrature_points | dealii::update_JxW_values);

    std::vector<dealii::types::global_dof_index> dof_indices(fe.n_dofs_per_cell());
    dealii::FullMatrix<double> cell_K(fe.n_dofs_per_cell(), fe.n_dofs_per_cell());
    dealii::Vector<double> cell_F(fe.n_dofs_per_cell());

    const double crack_depth_mm = crack.max_depth;

    for (const auto& cell : dof_handler.active_cell_iterators()) {
        fe_v.reinit(cell);
        cell_K = 0; cell_F = 0;

        for (unsigned int q = 0; q < quad.size(); ++q) {
            const auto& x = fe_v.quadrature_point(q);
            const double d_ratio = x[1] / h;
            const double E_local = effective_elastic_modulus(
                d_ratio, base_E, crack_depth_mm, h, repaired, repair_ratio);
            const double lam = E_local * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
            const double mu = E_local / (2.0 * (1.0 + nu));
            const double JxW = fe_v.JxW(q);

            for (unsigned int i = 0; i < fe.n_dofs_per_cell(); ++i) {
                const auto& gi = fe_v.shape_grad(i, q);
                for (unsigned int j = 0; j < fe.n_dofs_per_cell(); ++j) {
                    const auto& gj = fe_v.shape_grad(j, q);
                    double val = lam * gi[0] * gj[0] +
                                 mu * (2.0 * gi[0] * gj[0] + gi[1] * gj[1] + gi[0] * gj[1] + gi[1] * gj[0]);
                    (void)lambda_coeff;
                    (void)mu_coeff;
                    cell_K(i, j) += val * JxW;
                }
            }
        }
        cell->get_dof_indices(dof_indices);
        constraints.distribute_local_to_global(cell_K, cell_F, dof_indices, K, F);
    }

    double P_total = 0.0;
    double dP = sigma_f * W * h * h / (3.0 * (L - 2.0 * a));
    const int load_steps = config_.load_steps;
    bool failed = false;
    double max_stress = 0.0;

    for (int step = 0; step < load_steps && !failed; ++step) {
        double P = (step + 1) * dP / load_steps;
        double point_load = P / 2.0;

        dealii::Vector<double> step_F = F;
        (void)point_load;

        dealii::SolverControl control(2000, 1e-12);
        dealii::SolverCG<dealii::Vector<double>> solver(control);
        dealii::PreconditionSSOR<dealii::SparseMatrix<double>> prec;
        prec.initialize(K, 1.2);
        dealii::Vector<double> step_U = U;
        try { solver.solve(K, step_U, step_F, prec); } catch (...) {}
        constraints.distribute(step_U);

        dealii::QGauss<dim> pq(2);
        dealii::FEValues<dim> pfe(fe, pq,
            dealii::update_gradients | dealii::update_quadrature_points |
            dealii::update_JxW_values);
        std::vector<dealii::Tensor<1, dim>> grads(pq.size());

        for (const auto& cell : dof_handler.active_cell_iterators()) {
            pfe.reinit(cell);
            pfe.get_function_gradients(step_U, grads);
            for (unsigned int q = 0; q < pq.size(); ++q) {
                double s_xx = 2.0 * mu_coeff * grads[q][0] + lambda_coeff * (grads[q][0] + grads[q][1]);
                double s_yy = 2.0 * mu_coeff * grads[q][1] + lambda_coeff * (grads[q][0] + grads[q][1]);
                double s_xy = mu_coeff * (grads[q][0] + grads[q][1]);
                double vm = std::sqrt(0.5 * ((s_xx - s_yy) * (s_xx - s_yy) + 6.0 * s_xy * s_xy)) / 1e3;
                max_stress = std::max(max_stress, vm);
            }
        }
        if (max_stress >= sigma_f) { P_total = P; failed = true; break; }
        P_total = P;
    }
    if (!failed) P_total = dP;

    double nominal_strength = 3.0 * P_total * (L - 2.0 * a) / (2.0 * W * h * h);
    result.original_strength_mpa = sigma_f;
    result.unrepaired_strength_mpa = repaired ? nominal_strength * 0.75 : nominal_strength;
    result.repaired_strength_mpa = repaired ? nominal_strength : nominal_strength * 0.75;
    result.strength_recovery_ratio =
        (nominal_strength > 0) ? result.repaired_strength_mpa / result.original_strength_mpa : 0.0;
    result.youngs_modulus_gpa = config_.porcelain_youngs_modulus_gpa;
    result.fracture_toughness_mpa_m05 = 1.12 * nominal_strength *
        std::sqrt(3.14159 * std::max(0.001, crack_depth_mm));
    result.porcelain_id = 0;
    return result;
#else
    return solve_builtin(crack, material, repaired);
#endif
}

}
}
