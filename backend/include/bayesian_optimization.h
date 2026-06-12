#pragma once

#include <vector>
#include <string>
#include <functional>
#include <random>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <optional>
#include <nlohmann/json.hpp>
#include "common.h"

namespace porcelain_monitor {
namespace algorithms {

using json = nlohmann::json;

class Matrix {
public:
    Matrix() : rows_(0), cols_(0) {}
    Matrix(int rows, int cols, double value = 0.0)
        : rows_(rows), cols_(cols), data_(rows, std::vector<double>(cols, value)) {}

    static Matrix Zero(int rows, int cols) { return Matrix(rows, cols, 0.0); }
    static Matrix Identity(int n) {
        Matrix mat(n, n, 0.0);
        for (int i = 0; i < n; ++i) mat(i, i) = 1.0;
        return mat;
    }

    int rows() const { return rows_; }
    int cols() const { return cols_; }

    double& operator()(int i, int j) { return data_[i][j]; }
    const double& operator()(int i, int j) const { return data_[i][j]; }

    std::vector<double>& operator[](int i) { return data_[i]; }
    const std::vector<double>& operator[](int i) const { return data_[i]; }

    Matrix operator+(const Matrix& other) const {
        Matrix result(rows_, cols_);
        for (int i = 0; i < rows_; ++i)
            for (int j = 0; j < cols_; ++j)
                result(i, j) = data_[i][j] + other(i, j);
        return result;
    }

    Matrix operator-(const Matrix& other) const {
        Matrix result(rows_, cols_);
        for (int i = 0; i < rows_; ++i)
            for (int j = 0; j < cols_; ++j)
                result(i, j) = data_[i][j] - other(i, j);
        return result;
    }

    Matrix operator*(double scalar) const {
        Matrix result(rows_, cols_);
        for (int i = 0; i < rows_; ++i)
            for (int j = 0; j < cols_; ++j)
                result(i, j) = data_[i][j] * scalar;
        return result;
    }

    Matrix operator*(const Matrix& other) const {
        Matrix result(rows_, other.cols_);
        for (int i = 0; i < rows_; ++i)
            for (int j = 0; j < other.cols_; ++j)
                for (int k = 0; k < cols_; ++k)
                    result(i, j) += data_[i][k] * other(k, j);
        return result;
    }

    Matrix transpose() const {
        Matrix result(cols_, rows_);
        for (int i = 0; i < rows_; ++i)
            for (int j = 0; j < cols_; ++j)
                result(j, i) = data_[i][j];
        return result;
    }

    Matrix inverse() const {
        int n = rows_;
        Matrix aug(n, 2 * n, 0.0);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j)
                aug(i, j) = data_[i][j];
            aug(i, i + n) = 1.0;
        }

        for (int col = 0; col < n; ++col) {
            int pivot_row = col;
            double max_val = std::abs(aug(col, col));
            for (int row = col + 1; row < n; ++row) {
                if (std::abs(aug(row, col)) > max_val) {
                    max_val = std::abs(aug(row, col));
                    pivot_row = row;
                }
            }
            if (pivot_row != col)
                std::swap(aug.data_[col], aug.data_[pivot_row]);

            double pivot = aug(col, col);
            if (std::abs(pivot) < 1e-12)
                return Matrix::Zero(n, n);
            for (int j = col; j < 2 * n; ++j)
                aug(col, j) /= pivot;

            for (int row = 0; row < n; ++row) {
                if (row != col && std::abs(aug(row, col)) > 1e-12) {
                    double factor = aug(row, col);
                    for (int j = col; j < 2 * n; ++j)
                        aug(row, j) -= factor * aug(col, j);
                }
            }
        }

        Matrix inv(n, n);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                inv(i, j) = aug(i, j + n);
        return inv;
    }

    bool choleskyDecompose(Matrix& L) const {
        int n = rows_;
        L = Matrix(n, n, 0.0);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j <= i; ++j) {
                double sum = data_[i][j];
                for (int k = 0; k < j; ++k)
                    sum -= L(i, k) * L(j, k);
                if (i == j) {
                    if (sum <= 0.0) return false;
                    L(i, j) = std::sqrt(sum);
                } else {
                    L(i, j) = sum / L(j, j);
                }
            }
        }
        return true;
    }

    std::vector<double> solve(const std::vector<double>& b) const {
        int n = rows_;
        Matrix L;
        if (!choleskyDecompose(L))
            return inverse() * b;

        std::vector<double> y(n, 0.0);
        for (int i = 0; i < n; ++i) {
            y[i] = b[i];
            for (int j = 0; j < i; ++j)
                y[i] -= L(i, j) * y[j];
            y[i] /= L(i, i);
        }

        std::vector<double> x(n, 0.0);
        for (int i = n - 1; i >= 0; --i) {
            x[i] = y[i];
            for (int j = i + 1; j < n; ++j)
                x[i] -= L(j, i) * x[j];
            x[i] /= L(i, i);
        }
        return x;
    }

    std::vector<double> solveLowerTriangular(const std::vector<double>& b) const {
        int n = rows_;
        std::vector<double> y(n, 0.0);
        for (int i = 0; i < n; ++i) {
            y[i] = b[i];
            for (int j = 0; j < i; ++j)
                y[i] -= data_[i][j] * y[j];
            y[i] /= data_[i][i];
        }
        return y;
    }

    std::vector<double> operator*(const std::vector<double>& v) const {
        std::vector<double> result(rows_, 0.0);
        for (int i = 0; i < rows_; ++i)
            for (int j = 0; j < cols_; ++j)
                result[i] += data_[i][j] * v[j];
        return result;
    }

private:
    int rows_;
    int cols_;
    std::vector<std::vector<double>> data_;
};

struct ParameterBounds {
    std::string name;
    double lower;
    double upper;
    double default_value;
};

struct CalibrationDataset {
    int id;
    std::vector<double> input_features;
    double measured_value;
    std::optional<double> measurement_std;
    int porcelain_id;
    int crack_id;
    int material_id;
};

struct GPHyperparameters {
    double sigma_f = 1.0;
    double length_scale = 1.0;
    double sigma_n = 0.01;
};

class GaussianProcess {
public:
    GaussianProcess() = default;
    explicit GaussianProcess(const GPHyperparameters& params) : params_(params) {}

    void setHyperparameters(const GPHyperparameters& params) {
        params_ = params;
    }

    double kernel(const std::vector<double>& x1, const std::vector<double>& x2) const {
        double dist_sq = 0.0;
        for (size_t i = 0; i < x1.size(); ++i) {
            double diff = x1[i] - x2[i];
            dist_sq += diff * diff;
        }
        return params_.sigma_f * params_.sigma_f *
               std::exp(-dist_sq / (2.0 * params_.length_scale * params_.length_scale));
    }

    void train(const Matrix& X, const std::vector<double>& y) {
        int n = X.rows();
        X_train_ = X;
        y_train_ = y;

        K_ = Matrix(n, n);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                std::vector<double> xi(X.cols());
                std::vector<double> xj(X.cols());
                for (int k = 0; k < X.cols(); ++k) {
                    xi[k] = X(i, k);
                    xj[k] = X(j, k);
                }
                K_(i, j) = kernel(xi, xj);
                if (i == j)
                    K_(i, j) += params_.sigma_n * params_.sigma_n;
            }
        }

        L_ = Matrix(n, n);
        K_.choleskyDecompose(L_);
        alpha_ = solveLowerTriangular(y);
        alpha_ = solveUpperTriangular(alpha_);
    }

    void predict(const std::vector<double>& x_star, double& mu, double& sigma_sq) const {
        int n = X_train_.rows();
        int d = X_train_.cols();

        std::vector<double> k_star(n);
        for (int i = 0; i < n; ++i) {
            std::vector<double> xi(d);
            for (int j = 0; j < d; ++j)
                xi[j] = X_train_(i, j);
            k_star[i] = kernel(xi, x_star);
        }

        mu = 0.0;
        for (int i = 0; i < n; ++i)
            mu += k_star[i] * alpha_[i];

        std::vector<double> v = solveLowerTriangular(k_star);
        double v_dot_v = 0.0;
        for (double vi : v) v_dot_v += vi * vi;

        double k_star_star = kernel(x_star, x_star) + params_.sigma_n * params_.sigma_n;
        sigma_sq = k_star_star - v_dot_v;
        sigma_sq = std::max(sigma_sq, 1e-12);
    }

    std::pair<double, double> predict(const std::vector<double>& x_star) const {
        double mu, sigma_sq;
        predict(x_star, mu, sigma_sq);
        return {mu, sigma_sq};
    }

private:
    std::vector<double> solveLowerTriangular(const std::vector<double>& b) const {
        int n = L_.rows();
        std::vector<double> y(n, 0.0);
        for (int i = 0; i < n; ++i) {
            y[i] = b[i];
            for (int j = 0; j < i; ++j)
                y[i] -= L_(i, j) * y[j];
            y[i] /= L_(i, i);
        }
        return y;
    }

    std::vector<double> solveUpperTriangular(const std::vector<double>& b) const {
        int n = L_.rows();
        std::vector<double> x(n, 0.0);
        for (int i = n - 1; i >= 0; --i) {
            x[i] = b[i];
            for (int j = i + 1; j < n; ++j)
                x[i] -= L_(j, i) * x[j];
            x[i] /= L_(i, i);
        }
        return x;
    }

    GPHyperparameters params_;
    Matrix X_train_;
    std::vector<double> y_train_;
    Matrix K_;
    Matrix L_;
    std::vector<double> alpha_;
};

struct BayesianOptimizerConfig {
    int max_iter = 50;
    int n_init = 10;
    int grid_search_points = 1000;
    double xi = 0.01;
    int random_seed = 42;
};

struct OptimizationResult {
    std::vector<double> best_params;
    double best_objective;
    std::vector<std::vector<double>> sample_points;
    std::vector<double> sample_values;
    int iterations;
};

class BayesianOptimizer {
public:
    using ObjectiveFunction = std::function<double(const std::vector<double>&)>;

    BayesianOptimizer() = default;

    BayesianOptimizer(const std::vector<ParameterBounds>& bounds,
                      ObjectiveFunction objective,
                      const BayesianOptimizerConfig& config = BayesianOptimizerConfig())
        : bounds_(bounds), objective_(objective), config_(config) {
        rng_.seed(config_.random_seed);
    }

    void setBounds(const std::vector<ParameterBounds>& bounds) { bounds_ = bounds; }
    void setObjective(ObjectiveFunction objective) { objective_ = objective; }
    void setConfig(const BayesianOptimizerConfig& config) { config_ = config; rng_.seed(config_.random_seed); }

    OptimizationResult optimize() {
        int d = static_cast<int>(bounds_.size());

        std::vector<std::vector<double>> X_samples;
        std::vector<double> y_samples;

        latinHypercubeSampling(config_.n_init, d, X_samples);

        for (auto& x : X_samples) {
            for (int i = 0; i < d; ++i) {
                x[i] = bounds_[i].lower + x[i] * (bounds_[i].upper - bounds_[i].lower);
            }
            double val = objective_(x);
            y_samples.push_back(val);
        }

        int best_idx = 0;
        double f_best = y_samples[0];
        for (size_t i = 1; i < y_samples.size(); ++i) {
            if (y_samples[i] < f_best) {
                f_best = y_samples[i];
                best_idx = static_cast<int>(i);
            }
        }
        std::vector<double> best_x = X_samples[best_idx];

        for (int iter = 0; iter < config_.max_iter; ++iter) {
            Matrix X_mat(static_cast<int>(X_samples.size()), d);
            for (size_t i = 0; i < X_samples.size(); ++i)
                for (int j = 0; j < d; ++j)
                    X_mat(static_cast<int>(i), j) = X_samples[i][j];

            gp_.train(X_mat, y_samples);

            std::vector<double> next_x;
            double next_y;

            maximizeAcquisition(X_samples, y_samples, f_best, next_x, next_y);

            double true_y = objective_(next_x);

            X_samples.push_back(next_x);
            y_samples.push_back(true_y);

            if (true_y < f_best) {
                f_best = true_y;
                best_x = next_x;
            }
        }

        OptimizationResult result;
        result.best_params = best_x;
        result.best_objective = f_best;
        result.sample_points = X_samples;
        result.sample_values = y_samples;
        result.iterations = config_.max_iter + config_.n_init;
        return result;
    }

private:
    double normalCDF(double x) const {
        return 0.5 * (1.0 + std::erf(x / std::sqrt(2.0)));
    }

    double normalPDF(double x) const {
        return std::exp(-0.5 * x * x) / std::sqrt(2.0 * M_PI);
    }

    double expectedImprovement(const std::vector<double>& x, double f_best) const {
        double mu, sigma_sq;
        gp_.predict(x, mu, sigma_sq);
        double sigma = std::sqrt(sigma_sq);

        if (sigma < 1e-9) return 0.0;

        double Z = (mu - f_best - config_.xi) / sigma;
        double ei = (mu - f_best - config_.xi) * normalCDF(Z) + sigma * normalPDF(Z);
        return ei;
    }

    void latinHypercubeSampling(int n, int d, std::vector<std::vector<double>>& samples) const {
        samples.clear();
        samples.resize(n, std::vector<double>(d));

        for (int j = 0; j < d; ++j) {
            std::vector<double> perm(n);
            for (int i = 0; i < n; ++i) perm[i] = i;
            std::shuffle(perm.begin(), perm.end(), rng_);

            for (int i = 0; i < n; ++i) {
                std::uniform_real_distribution<double> dist(0.0, 1.0);
                samples[i][j] = (perm[i] + dist(rng_)) / n;
            }
        }
    }

    void maximizeAcquisition(const std::vector<std::vector<double>>& X_samples,
                             const std::vector<double>& y_samples,
                             double f_best,
                             std::vector<double>& best_x,
                             double& best_ei) {
        int d = static_cast<int>(bounds_.size());
        best_ei = -1e18;

        std::uniform_real_distribution<double> dist(0.0, 1.0);

        for (int i = 0; i < config_.grid_search_points; ++i) {
            std::vector<double> x(d);
            for (int j = 0; j < d; ++j) {
                x[j] = bounds_[j].lower + dist(rng_) * (bounds_[j].upper - bounds_[j].lower);
            }

            double ei = expectedImprovement(x, f_best);

            if (ei > best_ei) {
                best_ei = ei;
                best_x = x;
            }
        }

        for (const auto& x_saved : X_samples) {
            std::vector<double> x_near = x_saved;
            for (int j = 0; j < d; ++j) {
                double range = bounds_[j].upper - bounds_[j].lower;
                std::normal_distribution<double> noise(0.0, range * 0.05);
                x_near[j] += noise(rng_);
                x_near[j] = std::max(bounds_[j].lower, std::min(bounds_[j].upper, x_near[j]));
            }
            double ei = expectedImprovement(x_near, f_best);
            if (ei > best_ei) {
                best_ei = ei;
                best_x = x_near;
            }
        }

        lbfgsLocalSearch(best_x, f_best, best_ei);
    }

    void lbfgsLocalSearch(std::vector<double>& x, double f_best, double& best_ei) {
        int d = static_cast<int>(bounds_.size());
        int max_iter = 50;
        double step_size = 0.01;
        double tol = 1e-6;

        std::vector<double> grad(d);
        for (int iter = 0; iter < max_iter; ++iter) {
            computeNumericalGradient(x, f_best, grad);

            double grad_norm = 0.0;
            for (double g : grad) grad_norm += g * g;
            grad_norm = std::sqrt(grad_norm);

            if (grad_norm < tol) break;

            std::vector<double> x_new(d);
            for (int j = 0; j < d; ++j) {
                x_new[j] = x[j] + step_size * grad[j] / std::max(grad_norm, 1e-12);
                x_new[j] = std::max(bounds_[j].lower, std::min(bounds_[j].upper, x_new[j]));
            }

            double ei_new = expectedImprovement(x_new, f_best);

            if (ei_new > best_ei + 1e-12) {
                x = x_new;
                best_ei = ei_new;
                step_size *= 1.5;
            } else {
                step_size *= 0.5;
                if (step_size < 1e-8) break;
            }
        }
    }

    void computeNumericalGradient(const std::vector<double>& x, double f_best,
                                  std::vector<double>& grad) {
        int d = static_cast<int>(x.size());
        double h = 1e-6;

        for (int j = 0; j < d; ++j) {
            std::vector<double> x_plus = x;
            std::vector<double> x_minus = x;
            x_plus[j] += h;
            x_minus[j] -= h;

            x_plus[j] = std::max(bounds_[j].lower, std::min(bounds_[j].upper, x_plus[j]));
            x_minus[j] = std::max(bounds_[j].lower, std::min(bounds_[j].upper, x_minus[j]));

            double ei_plus = expectedImprovement(x_plus, f_best);
            double ei_minus = expectedImprovement(x_minus, f_best);

            grad[j] = (ei_plus - ei_minus) / (2.0 * h);
        }
    }

    std::vector<ParameterBounds> bounds_;
    ObjectiveFunction objective_;
    BayesianOptimizerConfig config_;
    GaussianProcess gp_;
    mutable std::mt19937 rng_;
};

struct CalibrationResult {
    std::vector<ParameterBounds> calibrated_params;
    std::vector<double> optimal_values;
    double mse_before;
    double mse_after;
    double r_squared;
    int iterations;
    json to_json() const {
        json j;
        j["optimal_values"] = optimal_values;
        j["mse_before"] = mse_before;
        j["mse_after"] = mse_after;
        j["r_squared"] = r_squared;
        j["iterations"] = iterations;
        json params_json = json::array();
        for (size_t i = 0; i < calibrated_params.size(); ++i) {
            params_json.push_back({
                {"name", calibrated_params[i].name},
                {"value", optimal_values[i]},
                {"lower", calibrated_params[i].lower},
                {"upper", calibrated_params[i].upper}
            });
        }
        j["parameters"] = params_json;
        return j;
    }
};

}
}
