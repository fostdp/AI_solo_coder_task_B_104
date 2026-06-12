#pragma once

#include "four_point_bending.h"
#include <string>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>

#if defined(PM_HAS_DEAL_II)
#  include <deal.II/base/quadrature_lib.h>
#  include <deal.II/base/function.h>
#  include <deal.II/lac/vector.h>
#  include <deal.II/lac/full_matrix.h>
#  include <deal.II/lac/sparse_matrix.h>
#  include <deal.II/lac/dynamic_sparsity_pattern.h>
#  include <deal.II/lac/solver_cg.h>
#  include <deal.II/lac/precondition.h>
#  include <deal.II/lac/affine_constraints.h>
#  include <deal.II/grid/tria.h>
#  include <deal.II/grid/grid_generator.h>
#  include <deal.II/dofs/dof_handler.h>
#  include <deal.II/dofs/dof_tools.h>
#  include <deal.II/fe/fe_q.h>
#  include <deal.II/fe/fe_values.h>
#  include <deal.II/numerics/vector_tools.h>
#  include <deal.II/numerics/matrix_tools.h>
#  include <deal.II/numerics/data_out.h>
#endif

namespace porcelain_monitor {
namespace modules {

enum class StrengthSolverBackend {
    BUILTIN,
    DEAL_II
};

class StrengthDealIIAdapter {
public:
    StrengthDealIIAdapter();
    ~StrengthDealIIAdapter();

    bool initialize(const algorithms::BendingTestConfig& config);

    BendingTestResult solve(
        const CrackInfo& crack,
        const RepairMaterial& material,
        bool repaired = true);

    bool is_dealii_available() const;

    void set_backend(StrengthSolverBackend backend) { backend_ = backend; }
    StrengthSolverBackend get_backend() const { return backend_; }

    void set_external_process(bool external) { use_external_process_ = external; }

    nlohmann::json get_solver_info() const;

private:
    BendingTestResult solve_builtin(
        const CrackInfo& crack,
        const RepairMaterial& material,
        bool repaired);

    BendingTestResult solve_dealii(
        const CrackInfo& crack,
        const RepairMaterial& material,
        bool repaired);

    double effective_elastic_modulus(double depth_ratio,
                                     double base_E,
                                     double crack_depth,
                                     double specimen_thickness,
                                     bool repaired,
                                     double repair_strength_ratio) const;

    algorithms::BendingTestConfig config_;
    StrengthSolverBackend backend_ = StrengthSolverBackend::BUILTIN;
    bool use_external_process_ = false;
    std::unique_ptr<algorithms::FourPointBendingTest> builtin_solver_;

#if defined(PM_HAS_DEAL_II)
    static constexpr int dim = 2;
    dealii::Triangulation<dim> triangulation;
    dealii::FE_Q<dim>          fe{2};
    dealii::DoFHandler<dim>    dof_handler{triangulation};
#endif
};

}
}
