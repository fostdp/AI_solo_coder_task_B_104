#include "modules/fem_stress/dealii_adapter.h"
#include <cmath>
#include <chrono>

namespace porcelain_monitor {
namespace modules {

using json = nlohmann::json;

DealIIAdapter::DealIIAdapter() = default;
DealIIAdapter::~DealIIAdapter() = default;

bool DealIIAdapter::is_dealii_available() const {
#if defined(PM_HAS_DEAL_II)
    return true;
#else
    return false;
#endif
}

json DealIIAdapter::get_solver_info() const {
    json j;
    j["dealii_available"] = is_dealii_available();
    j["active_backend"] = (backend_ == FemSolverBackend::BUILTIN) ? "builtin" : "dealii";
    j["external_process"] = use_external_process_;
    j["grid_resolution"] = config_.grid_resolution;
    j["adaptive_mesh"] = config_.use_adaptive_mesh;
    j["youngs_modulus_gpa"] = config_.youngs_modulus_gpa;
    j["poissons_ratio"] = config_.poissons_ratio;
    return j;
}

bool DealIIAdapter::initialize(const config::StressFEMConfig& config) {
    config_ = config;

    builtin_solver_ = std::make_unique<algorithms::StressAnalysisFEM>();
    algorithms::FEMConfig algo_config;
    algo_config.grid_resolution = config.grid_resolution;
    algo_config.youngs_modulus_gpa = config.youngs_modulus_gpa;
    algo_config.poissons_ratio = config.poissons_ratio;
    algo_config.use_adaptive_mesh = config.use_adaptive_mesh;
    algo_config.max_refinement_level = config.max_refinement_level;
    algo_config.crack_density_sensitivity = config.crack_density_sensitivity;
    algo_config.max_stress_mpa = config.max_stress_mpa;
    algo_config.refinement_stress_gradient_threshold = config.refinement_stress_gradient_threshold;
    algo_config.refinement_crack_density_threshold = config.refinement_crack_density_threshold;
    algo_config.coarsening_stress_gradient_threshold = config.coarsening_stress_gradient_threshold;
    algo_config.min_grid_resolution = config.min_grid_resolution;
    algo_config.max_total_nodes = config.max_total_nodes;
    builtin_solver_->set_config(algo_config);

#if defined(PM_HAS_DEAL_II)
    dealii_initialized_ = true;
#endif

    if (is_dealii_available()) {
        backend_ = FemSolverBackend::DEAL_II;
    } else {
        backend_ = FemSolverBackend::BUILTIN;
    }

    return true;
}

double DealIIAdapter::compute_crack_influence(
    double x, double y, double z,
    const std::vector<CrackData>& cracks) const
{
    double influence = 0.0;
    const double sigma = config_.crack_density_sensitivity;

    for (const auto& c : cracks) {
        double cx = 0.5 * (c.start_x + c.end_x);
        double cy = 0.5 * (c.start_y + c.end_y);
        double cz = 0.0;

        double dx = x - cx;
        double dy = y - cy;
        double dz = z - cz;
        double dist_sq = dx*dx + dy*dy + dz*dz;

        double width_eff = c.width_um / 1000.0 + 0.01;
        double gaussian = std::exp(-dist_sq / (2.0 * sigma * sigma));
        double depth_factor = std::tanh(c.depth_um / 50.0);
        influence += gaussian * depth_factor * width_eff;
    }

    return influence;
}

StressAnalysisResultData DealIIAdapter::solve(
    const std::vector<CrackData>& cracks,
    const GlazeDimensions& dims)
{
    if (backend_ == FemSolverBackend::DEAL_II && is_dealii_available()) {
        return solve_dealii(cracks, dims);
    }
    return solve_builtin(cracks, dims);
}

StressAnalysisResultData DealIIAdapter::solve_builtin(
    const std::vector<CrackData>& cracks,
    const GlazeDimensions& dims)
{
    (void)dims;
    StressAnalysisResultData result;
    result.success = false;

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<CrackInfo> crack_infos;
    for (const auto& c : cracks) {
        CrackInfo ci{};
        ci.id = c.id;
        ci.porcelain_id = 0;
        ci.max_depth = c.depth_um;
        ci.max_width = c.width_um;
        ci.total_length = c.length_mm;

        Point3D p1{}, p2{};
        p1.x = c.start_x; p1.y = c.start_y; p1.z = 0.0;
        p1.depth = c.depth_um; p1.width = c.width_um;
        p2.x = c.end_x;   p2.y = c.end_y;   p2.z = 0.0;
        p2.depth = c.depth_um; p2.width = c.width_um;
        ci.points.push_back(p1);
        ci.points.push_back(p2);
        crack_infos.push_back(ci);
    }

    StressAnalysisResult fem_result = builtin_solver_->analyze(0, crack_infos);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    result.solve_time_ms = static_cast<double>(duration.count());

    result.success = true;
    result.total_nodes = fem_result.grid_points.size();
    result.total_elements = fem_result.grid_points.size();
    result.max_von_mises_mpa = fem_result.max_von_mises;
    result.average_stress_mpa = fem_result.avg_von_mises;

    if (!fem_result.grid_points.empty()) {
        result.min_von_mises_mpa = fem_result.grid_points[0].stress.von_mises;
        for (const auto& gp : fem_result.grid_points) {
            result.min_von_mises_mpa = std::min(result.min_von_mises_mpa, gp.stress.von_mises);
            StressGridPointData p{};
            p.x_mm = gp.x;
            p.y_mm = gp.y;
            p.z_mm = gp.z;
            p.von_mises_mpa = gp.stress.von_mises;
            p.refinement_level = gp.refinement_level;
            result.stress_grid.push_back(p);
        }
    }

    if (result.average_stress_mpa > 0) {
        result.stress_concentration_factor =
            result.max_von_mises_mpa / result.average_stress_mpa;
    }
    return result;
}

StressAnalysisResultData DealIIAdapter::solve_dealii(
    const std::vector<CrackData>& cracks,
    const GlazeDimensions& dims)
{
#if defined(PM_HAS_DEAL_II)
    StressAnalysisResultData result;
    result.success = false;

    auto start = std::chrono::high_resolution_clock::now();

    const double radius = dims.diameter_mm / 2.0;
    const double thickness = dims.thickness_mm;

    dealii::GridGenerator::subdivided_hyper_rectangle(
        triangulation,
        {config_.grid_resolution, config_.grid_resolution,
         std::max(2, static_cast<int>(config_.grid_resolution * thickness / (2.0 * radius)))},
        dealii::Point<dim>(-radius, -radius, 0.0),
        dealii::Point<dim>(radius, radius, thickness),
        true);

    if (config_.use_adaptive_mesh) {
        triangulation.refine_global(1);
        for (int lvl = 0; lvl < std::min(2, config_.max_refinement_level); ++lvl) {
            for (auto& cell : triangulation.active_cell_iterators()) {
                dealii::Point<dim> center = cell->center();
                double influence = compute_crack_influence(
                    center[0], center[1], center[2], cracks);
                if (influence > 0.05) {
                    cell->set_refine_flag();
                }
            }
            triangulation.execute_coarsening_and_refinement();
        }
    }

    dof_handler.distribute_dofs(fe);

    constraints.clear();
    dealii::VectorTools::interpolate_boundary_values(
        dof_handler,
        0,
        dealii::ZeroFunction<dim>(dim),
        constraints);
    constraints.close();

    dealii::DynamicSparsityPattern dsp(dof_handler.n_dofs());
    dealii::DoFTools::make_sparsity_pattern(dof_handler, dsp, constraints);
    sparsity_pattern.copy_from(dsp);

    system_matrix.reinit(sparsity_pattern);
    solution.reinit(dof_handler.n_dofs());
    system_rhs.reinit(dof_handler.n_dofs());

    const double E = config_.youngs_modulus_gpa * 1e3;
    const double nu = config_.poissons_ratio;
    const double lambda = E * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
    const double mu = E / (2.0 * (1.0 + nu));

    dealii::QGauss<dim> quadrature_formula(fe.degree + 1);
    dealii::FEValues<dim> fe_values(fe, quadrature_formula,
        dealii::update_values | dealii::update_gradients |
        dealii::update_quadrature_points | dealii::update_JxW_values);

    const unsigned int dofs_per_cell = fe.n_dofs_per_cell();
    dealii::FullMatrix<double> cell_matrix(dofs_per_cell, dofs_per_cell);
    dealii::Vector<double> cell_rhs(dofs_per_cell);
    std::vector<dealii::types::global_dof_index> local_dof_indices(dofs_per_cell);

    for (const auto& cell : dof_handler.active_cell_iterators()) {
        fe_values.reinit(cell);
        cell_matrix = 0;
        cell_rhs = 0;

        dealii::Point<dim> cell_center = cell->center();
        double weakening = 1.0 - 0.5 * std::tanh(
            5.0 * compute_crack_influence(cell_center[0], cell_center[1], cell_center[2], cracks));

        for (unsigned int q = 0; q < quadrature_formula.size(); ++q) {
            const auto& JxW = fe_values.JxW(q);
            for (unsigned int i = 0; i < dofs_per_cell; ++i) {
                const auto& grad_i = fe_values.shape_grad(i, q);
                for (unsigned int j = 0; j < dofs_per_cell; ++j) {
                    const auto& grad_j = fe_values.shape_grad(j, q);
                    double val = 0.0;
                    for (unsigned int a = 0; a < dim; ++a) {
                        for (unsigned int b = 0; b < dim; ++b) {
                            val += lambda * grad_i[a] * grad_j[b] * (a == b ? 1.0 : 0.0);
                            val += mu * (grad_i[a] * grad_j[b] + (a == b ? grad_i[b] * grad_j[a] : 0.0));
                        }
                    }
                    cell_matrix(i, j) += weakening * val * JxW;
                }
            }
            for (unsigned int i = 0; i < dofs_per_cell; ++i) {
                cell_rhs(i) += 1e-6 * fe_values.shape_value(i, q) * JxW;
            }
        }

        cell->get_dof_indices(local_dof_indices);
        constraints.distribute_local_to_global(
            cell_matrix, cell_rhs, local_dof_indices,
            system_matrix, system_rhs);
    }

    dealii::SolverControl solver_control(1000, 1e-10 * system_rhs.l2_norm());
    dealii::SolverCG<dealii::Vector<double>> solver(solver_control);
    dealii::PreconditionSSOR<dealii::SparseMatrix<double>> preconditioner;
    preconditioner.initialize(system_matrix, 1.2);
    solver.solve(system_matrix, solution, system_rhs, preconditioner);
    constraints.distribute(solution);

    result.total_nodes = dof_handler.n_dofs() / dim;
    result.total_elements = triangulation.n_active_cells();

    result.stress_grid.reserve(result.total_elements);
    double max_stress = 0.0;
    double min_stress = 1e18;
    double sum_stress = 0.0;
    size_t count = 0;

    dealii::QGauss<dim> post_quadrature(2);
    dealii::FEValues<dim> post_fe(fe, post_quadrature,
        dealii::update_gradients | dealii::update_quadrature_points |
        dealii::update_JxW_values);

    std::vector<dealii::Tensor<1, dim>> grad_solution(post_quadrature.size());

    for (const auto& cell : dof_handler.active_cell_iterators()) {
        post_fe.reinit(cell);
        post_fe.get_function_gradients(solution, grad_solution);
        dealii::Point<dim> c = cell->center();

        double avg_vm = 0.0;
        for (unsigned int q = 0; q < post_quadrature.size(); ++q) {
            double eps_xx = grad_solution[q][0][0];
            double eps_yy = grad_solution[q][1][1];
            double eps_zz = grad_solution[q][2][2];
            double eps_xy = 0.5 * (grad_solution[q][0][1] + grad_solution[q][1][0]);
            double eps_yz = 0.5 * (grad_solution[q][1][2] + grad_solution[q][2][1]);
            double eps_zx = 0.5 * (grad_solution[q][2][0] + grad_solution[q][0][2]);

            double trace = eps_xx + eps_yy + eps_zz;
            double s_xx = lambda * trace + 2.0 * mu * eps_xx;
            double s_yy = lambda * trace + 2.0 * mu * eps_yy;
            double s_zz = lambda * trace + 2.0 * mu * eps_zz;
            double s_xy = 2.0 * mu * eps_xy;
            double s_yz = 2.0 * mu * eps_yz;
            double s_zx = 2.0 * mu * eps_zx;

            double vm = std::sqrt(0.5 * (
                (s_xx - s_yy) * (s_xx - s_yy) +
                (s_yy - s_zz) * (s_yy - s_zz) +
                (s_zz - s_xx) * (s_zz - s_xx) +
                6.0 * (s_xy * s_xy + s_yz * s_yz + s_zx * s_zx)));
            avg_vm += vm;
        }
        avg_vm /= post_quadrature.size();
        avg_vm /= 1e3;

        StressGridPointData p{};
        p.x_mm = c[0]; p.y_mm = c[1]; p.z_mm = c[2];
        p.von_mises_mpa = avg_vm;
        p.refinement_level = cell->level();
        result.stress_grid.push_back(p);

        max_stress = std::max(max_stress, avg_vm);
        min_stress = std::min(min_stress, avg_vm);
        sum_stress += avg_vm;
        count++;
    }

    result.max_von_mises_mpa = max_stress;
    result.min_von_mises_mpa = (count > 0) ? min_stress : 0.0;
    result.average_stress_mpa = (count > 0) ? sum_stress / count : 0.0;
    if (result.average_stress_mpa > 0) {
        result.stress_concentration_factor = max_stress / result.average_stress_mpa;
    }
    result.success = true;

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    result.solve_time_ms = static_cast<double>(duration.count());

    return result;
#else
    return solve_builtin(cracks, dims);
#endif
}

}
}
