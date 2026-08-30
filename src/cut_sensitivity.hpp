#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "integrators.hpp"
#include "solver.hpp"

namespace App::detail {

/** Differentiate the assembled cut-domain operators with respect to active
    level-set DOFs.

    MFEM integrators and Algoim rules assemble every perturbed element matrix.
    This routine only owns the central perturbation and the discrete-adjoint
    contractions because neither library differentiates a moving cut rule.
 */
inline bool differentiateCutElements(
    const SolverSettings& settings,
    const OptimizerSettings& optimizer_settings,
    mfem::Mesh& mesh,
    mfem::FiniteElementSpace& level_set_fes,
    mfem::FiniteElementSpace& displacement_fes,
    mfem::FiniteElementSpace& scalar_fes,
    mfem::GridFunction& phi,
    const mfem::Array<int>& active,
    const std::vector<mfem::Vector>& forward_history,
    const std::vector<mfem::Vector>* pass_adjoint,
    const std::vector<mfem::Vector>* stop_adjoint,
    const mfem::Vector* pass_initial_adjoint,
    const mfem::Vector* stop_initial_adjoint,
    int pressure_offset,
    int time_steps,
    int design_attribute,
    int cut_integration_order,
    int level_set_order,
    double level_set_scale,
    mfem::Vector& pass_physical_gradient,
    mfem::Vector& stop_physical_gradient,
    int& cut_element_count,
    int& differentiated_dof_count)
{
    const auto* physics =
        std::get_if<VibroacousticSettings>(&settings.physics);
    if (physics == nullptr || mesh.Dimension() != 2
        || level_set_scale <= 0.0
        || optimizer_settings.cutDerivativeRelativeStep <= 0.0) {
        return false;
    }
    const bool has_pass = pass_adjoint != nullptr;
    const bool has_stop = stop_adjoint != nullptr;
    const int state_size = pressure_offset + scalar_fes.GetVSize();
    auto valid_history = [state_size](
                             const std::vector<mfem::Vector>& history,
                             int blocks) {
        return std::all_of(
            history.begin(), history.end(),
            [state_size, blocks](const mfem::Vector& state) {
                return state.Size() == blocks * state_size;
            });
    };
    if (forward_history.size() != static_cast<std::size_t>(time_steps + 1)
        || !valid_history(forward_history, 3)
        || active.Size() != level_set_fes.GetVSize()
        || phi.Size() != level_set_fes.GetVSize()
        || (has_pass
            && (pass_initial_adjoint == nullptr
                || pass_initial_adjoint->Size() != state_size
                || pass_adjoint->size()
                    != static_cast<std::size_t>(time_steps + 1)
                || !valid_history(*pass_adjoint, 1)))
        || (has_stop
            && (stop_initial_adjoint == nullptr
                || stop_initial_adjoint->Size() != state_size
                || stop_adjoint->size()
                    != static_cast<std::size_t>(time_steps + 1)
                || !valid_history(*stop_adjoint, 1)))) {
        return false;
    }

    const double beta = settings.newmarkBeta;
    const double gamma = settings.newmarkGamma;
    const double a_1 = 1.0 - gamma / beta;
    const double a_2 =
        (1.0 - gamma / (2.0 * beta)) * settings.dt;
    const double a_3 = gamma / (beta * settings.dt);
    const double a_4 = 1.0 / (beta * settings.dt);
    const double a_5 = 1.0 / (2.0 * beta) - 1.0;
    const double a_6 = 1.0 / (beta * settings.dt * settings.dt);
    const double lambda = physics->youngs_modulus * physics->poisson_ratio
        / (1.0 - physics->poisson_ratio * physics->poisson_ratio);
    const double mu = physics->youngs_modulus
        / (2.0 * (1.0 + physics->poisson_ratio));
    const double omega_1 = 2.0 * std::acos(-1.0) * physics->f1;
    const double omega_2 = 2.0 * std::acos(-1.0) * physics->f2;
    const double alpha_d = 2.0 * physics->zeta * omega_1 * omega_2
        / (omega_1 + omega_2);
    const double beta_d = 2.0 * physics->zeta / (omega_1 + omega_2);
    const double perturbation =
        optimizer_settings.cutDerivativeRelativeStep * level_set_scale;
    const double inverse_perturbation = 0.5 / perturbation;

    pass_physical_gradient.SetSize(level_set_fes.GetVSize());
    stop_physical_gradient.SetSize(level_set_fes.GetVSize());
    pass_physical_gradient.UseDevice(false);
    stop_physical_gradient.UseDevice(false);
    pass_physical_gradient = 0.0;
    stop_physical_gradient = 0.0;
    cut_element_count = 0;
    differentiated_dof_count = 0;
    bool normals_are_valid = true;

    std::vector<const mfem::real_t*> forward_data(forward_history.size());
    for (std::size_t state = 0; state < forward_history.size(); ++state) {
        forward_data[state] = forward_history[state].HostRead();
    }
    std::vector<const mfem::real_t*> pass_data;
    std::vector<const mfem::real_t*> stop_data;
    if (has_pass) {
        pass_data.resize(pass_adjoint->size());
        for (std::size_t state = 0; state < pass_adjoint->size(); ++state) {
            pass_data[state] = (*pass_adjoint)[state].HostRead();
        }
    }
    if (has_stop) {
        stop_data.resize(stop_adjoint->size());
        for (std::size_t state = 0; state < stop_adjoint->size(); ++state) {
            stop_data[state] = (*stop_adjoint)[state].HostRead();
        }
    }
    const mfem::real_t* pass_initial_data = has_pass
        ? pass_initial_adjoint->HostRead() : nullptr;
    const mfem::real_t* stop_initial_data = has_stop
        ? stop_initial_adjoint->HostRead() : nullptr;
    const mfem::real_t* phi_data = phi.HostRead();

#ifdef _OPENMP
#pragma omp parallel
#endif
    {
        mfem::GridFunction thread_phi(&level_set_fes);
        thread_phi.UseDevice(false);
        std::copy(phi_data, phi_data + phi.Size(), thread_phi.HostWrite());
        mfem::ConstantCoefficient solid_density(physics->rho_s);
        mfem::ConstantCoefficient solid_lambda(lambda);
        mfem::ConstantCoefficient solid_mu(mu);
        mfem::ConstantCoefficient acoustic_mass(
            1.0 / (physics->rho_a * physics->c_a * physics->c_a));
        mfem::ConstantCoefficient acoustic_stiffness(1.0 / physics->rho_a);
        ImplicitDomainIntegrator Muu_integrator(
            std::make_unique<mfem::VectorMassIntegrator>(solid_density),
            thread_phi, cut_integration_order, level_set_order,
            physics->epsilon, true);
        ImplicitDomainIntegrator Kuu_integrator(
            std::make_unique<mfem::ElasticityIntegrator>(solid_lambda, solid_mu),
            thread_phi, cut_integration_order, level_set_order,
            physics->epsilon, true);
        ImplicitDomainIntegrator Mpp_integrator(
            std::make_unique<mfem::MassIntegrator>(acoustic_mass),
            thread_phi, cut_integration_order, level_set_order,
            physics->epsilon, false);
        ImplicitDomainIntegrator Kpp_integrator(
            std::make_unique<mfem::DiffusionIntegrator>(acoustic_stiffness),
            thread_phi, cut_integration_order, level_set_order,
            physics->epsilon, false);
        ImplicitSurfaceNormalIntegrator Kup_integrator(
            thread_phi, cut_integration_order, level_set_order, -1.0, false);
        ImplicitSurfaceNormalIntegrator Mpu_integrator(
            thread_phi, cut_integration_order, level_set_order, 1.0, true);

        mfem::Vector thread_pass_gradient(level_set_fes.GetVSize());
        mfem::Vector thread_stop_gradient(level_set_fes.GetVSize());
        thread_pass_gradient.UseDevice(false);
        thread_stop_gradient.UseDevice(false);
        thread_pass_gradient = 0.0;
        thread_stop_gradient = 0.0;
        int thread_cut_elements = 0;
        int thread_differentiated_dofs = 0;
        mfem::Array<int> phi_dofs;
        mfem::Array<int> displacement_dofs;
        mfem::Array<int> pressure_dofs;
        mfem::Vector matrix_product;
        matrix_product.UseDevice(false);
        mfem::IsoparametricTransformation transformation;
        auto contract = [&matrix_product](
                            const mfem::Vector& left,
                            const mfem::DenseMatrix& matrix,
                            const mfem::Vector& right) {
            matrix_product.SetSize(matrix.Height());
            matrix.Mult(right, matrix_product);
            return left * matrix_product;
        };
        auto difference = [inverse_perturbation](
                              mfem::DenseMatrix& plus,
                              const mfem::DenseMatrix& minus) {
            plus -= minus;
            plus *= inverse_perturbation;
        };
        auto gather = [](const mfem::real_t* source,
                         const mfem::Array<int>& dofs,
                         mfem::Vector& values,
                         int offset = 0) {
            values.SetSize(dofs.Size());
            for (int i = 0; i < dofs.Size(); ++i) {
                const int encoded = dofs[i];
                const int dof = encoded >= 0 ? encoded : -1 - encoded;
                values[i] = encoded >= 0
                    ? source[offset + dof] : -source[offset + dof];
            }
        };

#ifdef _OPENMP
#pragma omp for schedule(dynamic)
#endif
        for (int element = 0; element < mesh.GetNE(); ++element) {
            if (mesh.GetAttribute(element) != design_attribute) {
                continue;
            }
            level_set_fes.GetElementDofs(element, phi_dofs);
            double minimum_phi = std::numeric_limits<double>::max();
            double maximum_phi = std::numeric_limits<double>::lowest();
            for (int dof = 0; dof < phi_dofs.Size(); ++dof) {
                const int encoded = phi_dofs[dof];
                const int index = encoded >= 0 ? encoded : -1 - encoded;
                const double value = encoded >= 0
                    ? thread_phi[index] : -thread_phi[index];
                minimum_phi = std::min(minimum_phi, value);
                maximum_phi = std::max(maximum_phi, value);
            }
            if (minimum_phi > perturbation || maximum_phi < -perturbation) {
                continue;
            }
            ++thread_cut_elements;
            displacement_fes.GetElementVDofs(element, displacement_dofs);
            scalar_fes.GetElementDofs(element, pressure_dofs);
            mesh.GetElementTransformation(element, &transformation);
            const mfem::FiniteElement& scalar_element =
                *scalar_fes.GetFE(element);
            const mfem::FiniteElement& displacement_element =
                *displacement_fes.GetFE(element);

            mfem::Array<int> pressure_state_dofs(pressure_dofs.Size());
            for (int i = 0; i < pressure_dofs.Size(); ++i) {
                const int encoded = pressure_dofs[i];
                const int dof = encoded >= 0 ? encoded : -1 - encoded;
                const int state_dof = pressure_offset + dof;
                pressure_state_dofs[i] = encoded >= 0
                    ? state_dof : -1 - state_dof;
            }

            for (int local_dof = 0; local_dof < phi_dofs.Size(); ++local_dof) {
                const int encoded = phi_dofs[local_dof];
                const int phi_dof = encoded >= 0 ? encoded : -1 - encoded;
                if (active[phi_dof] == 0) {
                    continue;
                }
                ++thread_differentiated_dofs;
                const double original_phi = thread_phi[phi_dof];

                mfem::DenseMatrix Muu_plus, Kuu_plus, Mpp_plus, Kpp_plus;
                mfem::DenseMatrix Kup_plus, Mpu_plus;
                thread_phi[phi_dof] = original_phi + perturbation;
                Muu_integrator.AssembleElementMatrix(
                    displacement_element, transformation, Muu_plus);
                Kuu_integrator.AssembleElementMatrix(
                    displacement_element, transformation, Kuu_plus);
                Mpp_integrator.AssembleElementMatrix(
                    scalar_element, transformation, Mpp_plus);
                Kpp_integrator.AssembleElementMatrix(
                    scalar_element, transformation, Kpp_plus);
                Kup_integrator.AssembleElementMatrix2(
                    scalar_element, displacement_element,
                    transformation, Kup_plus);
                Mpu_integrator.AssembleElementMatrix2(
                    displacement_element, scalar_element,
                    transformation, Mpu_plus);

                mfem::DenseMatrix Muu_minus, Kuu_minus, Mpp_minus, Kpp_minus;
                mfem::DenseMatrix Kup_minus, Mpu_minus;
                thread_phi[phi_dof] = original_phi - perturbation;
                Muu_integrator.AssembleElementMatrix(
                    displacement_element, transformation, Muu_minus);
                Kuu_integrator.AssembleElementMatrix(
                    displacement_element, transformation, Kuu_minus);
                Mpp_integrator.AssembleElementMatrix(
                    scalar_element, transformation, Mpp_minus);
                Kpp_integrator.AssembleElementMatrix(
                    scalar_element, transformation, Kpp_minus);
                Kup_integrator.AssembleElementMatrix2(
                    scalar_element, displacement_element,
                    transformation, Kup_minus);
                Mpu_integrator.AssembleElementMatrix2(
                    displacement_element, scalar_element,
                    transformation, Mpu_minus);
                thread_phi[phi_dof] = original_phi;

                difference(Muu_plus, Muu_minus);
                difference(Kuu_plus, Kuu_minus);
                difference(Mpp_plus, Mpp_minus);
                difference(Kpp_plus, Kpp_minus);
                difference(Kup_plus, Kup_minus);
                difference(Mpu_plus, Mpu_minus);

                mfem::Vector initial_u, initial_p;
                mfem::Vector acceleration_u, acceleration_p;
                gather(forward_data[0], displacement_dofs,
                    acceleration_u, 2 * state_size);
                gather(forward_data[0], pressure_state_dofs,
                    acceleration_p, 2 * state_size);
                auto initial_sensitivity = [&](const mfem::real_t* initial_data) {
                    gather(initial_data, displacement_dofs, initial_u);
                    gather(initial_data, pressure_state_dofs, initial_p);
                    double sensitivity = -contract(
                        initial_u, Muu_plus, acceleration_u);
                    sensitivity -= contract(
                        initial_p, Mpu_plus, acceleration_u);
                    sensitivity -= contract(
                        initial_p, Mpp_plus, acceleration_p);
                    return sensitivity;
                };
                double pass_sensitivity = has_pass
                    ? initial_sensitivity(pass_initial_data) : 0.0;
                double stop_sensitivity = has_stop
                    ? initial_sensitivity(stop_initial_data) : 0.0;

                mfem::Vector lambda_u, lambda_p;
                mfem::Vector v_u, v_p, previous_v_u, previous_v_p;
                mfem::Vector previous_dot_u, previous_dot_p;
                mfem::Vector previous_ddot_u, previous_ddot_p;
                mfem::Vector mass_u, mass_p, damping_u;
                for (int n = 1; n <= time_steps; ++n) {
                    gather(forward_data[n], displacement_dofs, v_u);
                    gather(forward_data[n], pressure_state_dofs, v_p);
                    gather(forward_data[n - 1], displacement_dofs, previous_v_u);
                    gather(forward_data[n - 1], pressure_state_dofs, previous_v_p);
                    gather(forward_data[n - 1], displacement_dofs,
                        previous_dot_u, state_size);
                    gather(forward_data[n - 1], pressure_state_dofs,
                        previous_dot_p, state_size);
                    gather(forward_data[n - 1], displacement_dofs,
                        previous_ddot_u, 2 * state_size);
                    gather(forward_data[n - 1], pressure_state_dofs,
                        previous_ddot_p, 2 * state_size);

                    mass_u.SetSize(previous_v_u.Size());
                    mass_u = 0.0;
                    mass_u.Add(a_4, previous_dot_u);
                    mass_u.Add(a_5, previous_ddot_u);
                    mass_u.Add(a_6, previous_v_u);
                    mass_u.Add(-a_6, v_u);
                    mass_p.SetSize(previous_v_p.Size());
                    mass_p = 0.0;
                    mass_p.Add(a_4, previous_dot_p);
                    mass_p.Add(a_5, previous_ddot_p);
                    mass_p.Add(a_6, previous_v_p);
                    mass_p.Add(-a_6, v_p);
                    damping_u.SetSize(previous_v_u.Size());
                    damping_u = 0.0;
                    damping_u.Add(-a_1, previous_dot_u);
                    damping_u.Add(-a_2, previous_ddot_u);
                    damping_u.Add(a_3, previous_v_u);
                    damping_u.Add(-a_3, v_u);

                    auto accumulate = [&](const mfem::real_t* adjoint_data,
                                          double& sensitivity) {
                        gather(adjoint_data, displacement_dofs, lambda_u);
                        gather(adjoint_data, pressure_state_dofs, lambda_p);
                        sensitivity += contract(lambda_u, Muu_plus, mass_u);
                        sensitivity += contract(lambda_p, Mpu_plus, mass_u);
                        sensitivity += contract(lambda_p, Mpp_plus, mass_p);
                        sensitivity += alpha_d
                            * contract(lambda_u, Muu_plus, damping_u);
                        sensitivity += beta_d
                            * contract(lambda_u, Kuu_plus, damping_u);
                        sensitivity -= contract(lambda_u, Kuu_plus, v_u);
                        sensitivity -= contract(lambda_u, Kup_plus, v_p);
                        sensitivity -= contract(lambda_p, Kpp_plus, v_p);
                    };
                    if (has_pass) {
                        accumulate(pass_data[n], pass_sensitivity);
                    }
                    if (has_stop) {
                        accumulate(stop_data[n], stop_sensitivity);
                    }
                }
                thread_pass_gradient[phi_dof] += pass_sensitivity;
                thread_stop_gradient[phi_dof] += stop_sensitivity;
            }
        }

#ifdef _OPENMP
#pragma omp critical
#endif
        {
            pass_physical_gradient += thread_pass_gradient;
            stop_physical_gradient += thread_stop_gradient;
            cut_element_count += thread_cut_elements;
            differentiated_dof_count += thread_differentiated_dofs;
            normals_are_valid = normals_are_valid
                && Kup_integrator.GetDegenerateNormalCount() == 0
                && Mpu_integrator.GetDegenerateNormalCount() == 0;
        }
    }
    return normals_are_valid;
}

/** Apply the transpose node/cell maps and the adjoint finite-volume filter. */
inline bool reverseFilterGradients(
    const mfem::SparseMatrix& filter_matrix,
    const mfem::SparseMatrix& design_to_cell,
    const mfem::SparseMatrix& cell_to_level_set,
    const mfem::Vector& cell_volumes,
    const mfem::Array<int>& active_design_dofs,
    double level_set_scale,
    bool has_pass,
    bool has_stop,
    const mfem::Vector& pass_physical_gradient,
    const mfem::Vector& stop_physical_gradient,
    mfem::Vector& pass_design_gradient,
    mfem::Vector& stop_design_gradient,
    SolverPerformance& performance,
    const LogFunction& log)
{
#if METAMATERIAL_USE_CUDA
    mfem::DSmoother preconditioner(filter_matrix);
#else
    mfem::GSSmoother preconditioner(filter_matrix);
#endif
    mfem::CGSolver solver;
    solver.SetPreconditioner(preconditioner);
    solver.SetOperator(filter_matrix);
    solver.SetRelTol(1.0e-10);
    solver.SetAbsTol(1.0e-12);
    solver.SetMaxIter(1500);
    solver.SetPrintLevel(-1);
    const auto started_at = std::chrono::steady_clock::now();
    auto reverse = [&](const char* name,
                       bool active,
                       const mfem::Vector& physical_gradient,
                       mfem::Vector& design_gradient) {
        design_gradient.SetSize(design_to_cell.Width());
        design_gradient = 0.0;
        if (!active) {
            return true;
        }
        mfem::Vector filtered_gradient(cell_to_level_set.Width());
        cell_to_level_set.MultTranspose(
            physical_gradient, filtered_gradient);
        mfem::Vector filter_adjoint(filtered_gradient.Size());
        filter_adjoint = 0.0;
        solver.Mult(filtered_gradient, filter_adjoint);
        if (!solver.GetConverged()) {
            std::ostringstream message;
            message << "The " << name
                    << " adjoint PDE filter solve did not converge: "
                    << solver.GetNumIterations()
                    << " CG iterations, relative residual " << std::scientific
                    << solver.GetFinalRelNorm() << ".";
            log(LogLevel::Error, message.str());
            return false;
        }
        mfem::Vector center_gradient(filter_adjoint.Size());
        for (int cell = 0; cell < center_gradient.Size(); ++cell) {
            center_gradient[cell] = cell_volumes[cell] * filter_adjoint[cell];
        }
        design_to_cell.MultTranspose(center_gradient, design_gradient);
        design_gradient *= level_set_scale;
        design_gradient.SetSubVectorComplement(active_design_dofs, 0.0);
        if (design_gradient.CheckFinite() != 0) {
            log(LogLevel::Error,
                std::string("The ") + name
                    + " discrete-adjoint design gradient is non-finite.");
            return false;
        }
        return true;
    };
    const bool success = reverse(
            "pass-band", has_pass,
            pass_physical_gradient, pass_design_gradient)
        && reverse(
            "stop-band", has_stop,
            stop_physical_gradient, stop_design_gradient);
    performance.filterAdjointSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started_at).count();
    return success;
}

} // namespace App::detail
