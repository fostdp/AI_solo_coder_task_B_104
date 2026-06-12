#pragma once

#include "config.h"
#include "stress_analysis.h"
#include "common.h"
#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

#if defined(PM_HAS_DEAL_II)
#  include <deal.II/base/quadrature_lib.h>
#  include <deal.II/base/function.h>
#  include <deal.II/base/logstream.h>
#  include <deal.II/lac/vector.h>
#  include <deal.II/lac/full_matrix.h>
#  include <deal.II/lac/sparse_matrix.h>
#  include <deal.II/lac/dynamic_sparsity_pattern.h>
#  include <deal.II/lac/solver_cg.h>
#  include <deal.II/lac/precondition.h>
#  include <deal.II/lac/affine_constraints.h>
#  include <deal.II/grid/tria.h>
#  include <deal.II/grid/grid_generator.h>
#  include <deal.II/grid/grid_refinement.h>
#  include <deal.II/grid/grid_out.h>
#  include <deal.II/dofs/dof_handler.h>
#  include <deal.II/dofs/dof_tools.h>
#  include <deal.II/fe/fe_q.h>
#  include <deal.II/fe/fe_values.h>
#  include <deal.II/numerics/vector_tools.h>
#  include <deal.II/numerics/matrix_tools.h>
#  include <deal.II/numerics/data_out.h>
#  include <deal.II/numerics/error_estimator.h>
#endif

namespace porcelain_monitor {
namespace modules {

enum class FemSolverBackend {
    BUILTIN,
    DEAL_II
};

struct CrackData {
    int id = 0;
    double start_x = 0.0;
    double start_y = 0.0;
    double end_x = 0.0;
    double end_y = 0.0;
    double depth_um = 10.0;
    double width_um = 1.0;
    double length_mm = 0.0;
};

struct GlazeDimensions {
    double thickness_mm = 3.0;
    double diameter_mm = 100.0;
};

struct StressGridPointData {
    double x_mm = 0.0;
    double y_mm = 0.0;
    double z_mm = 0.0;
    double von_mises_mpa = 0.0;
    int refinement_level = 0;
};

struct StressAnalysisResultData {
    bool success = false;
    size_t total_nodes = 0;
    size_t total_elements = 0;
    double max_von_mises_mpa = 0.0;
    double min_von_mises_mpa = 0.0;
    double average_stress_mpa = 0.0;
    double stress_concentration_factor = 0.0;
    double solve_time_ms = 0.0;
    std::string solver_backend = "builtin";
    std::vector<StressGridPointData> stress_grid;
};

class DealIIAdapter {
public:
    DealIIAdapter();
    ~DealIIAdapter();

    bool initialize(const config::StressFEMConfig& config);

    StressAnalysisResultData solve(
        const std::vector<CrackData>& cracks,
        const GlazeDimensions& dims);

    bool is_dealii_available() const;

    void set_backend(FemSolverBackend backend) { backend_ = backend; }
    FemSolverBackend get_backend() const { return backend_; }

    void set_external_process(bool external) { use_external_process_ = external; }

    nlohmann::json get_solver_info() const;

private:
    StressAnalysisResultData solve_builtin(
        const std::vector<CrackData>& cracks,
        const GlazeDimensions& dims);

    StressAnalysisResultData solve_dealii(
        const std::vector<CrackData>& cracks,
        const GlazeDimensions& dims);

    double compute_crack_influence(double x, double y, double z,
                                   const std::vector<CrackData>& cracks) const;

    config::StressFEMConfig config_;
    FemSolverBackend backend_ = FemSolverBackend::BUILTIN;
    bool use_external_process_ = false;
    std::unique_ptr<algorithms::StressAnalysisFEM> builtin_solver_;
    bool dealii_initialized_ = false;

#if defined(PM_HAS_DEAL_II)
    static constexpr int dim = 3;
    dealii::Triangulation<dim> triangulation;
    dealii::FE_Q<dim>          fe{1};
    dealii::DoFHandler<dim>    dof_handler{triangulation};
    dealii::AffineConstraints<double> constraints;
    dealii::SparsityPattern      sparsity_pattern;
    dealii::SparseMatrix<double> system_matrix;
    dealii::Vector<double>       solution;
    dealii::Vector<double>       system_rhs;
#endif
};

}
}
