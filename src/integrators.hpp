#pragma once

#include <memory>
#include <utility>

#include "coeffs.hpp"
#include "mfem.hpp"

namespace App {

/** @brief Apply an existing MFEM bilinear integrator on one side of an
    implicit interface.

    The assembled element operator is

    @f[
      a_e = \varepsilon a_e^{\Omega_e}
          + (1-\varepsilon)a_e^{\Omega_e^{\pm}},
    @f]

    where Algoim supplies the cut-volume rule and the wrapped MFEM integrator
    supplies the actual mass, diffusion, or elasticity operator. A fresh
    Algoim rule is constructed for every element assembly so a changed level
    set can never reuse stale cut quadrature.
 */
class ImplicitDomainIntegrator final : public mfem::BilinearFormIntegrator
{
public:
    ImplicitDomainIntegrator(
        std::unique_ptr<mfem::BilinearFormIntegrator> integrator_,
        mfem::GridFunction& phi_,
        int integration_order_,
        int level_set_order_,
        mfem::real_t epsilon_,
        bool positive_)
        : integrator(std::move(integrator_)),
          phi(phi_),
          integration_order(integration_order_),
          level_set_order(level_set_order_),
          epsilon(epsilon_),
          positive(positive_)
    {
    }

    void AssembleElementMatrix(
        const mfem::FiniteElement& element,
        mfem::ElementTransformation& transformation,
        mfem::DenseMatrix& element_matrix) override
    {
        mfem::DenseMatrix full_matrix;
        integrator->SetIntRule(nullptr);
        integrator->AssembleElementMatrix(
            element, transformation, full_matrix);

        mfem::GridFunctionCoefficient phi_coefficient(&phi);
        NegatedCoefficient negative_phi(phi_coefficient);
        mfem::Coefficient& cut_coefficient = positive
            ? static_cast<mfem::Coefficient&>(phi_coefficient)
            : static_cast<mfem::Coefficient&>(negative_phi);
        mfem::AlgoimIntegrationRules integration_rules(
            integration_order, cut_coefficient, level_set_order);
        mfem::IntegrationRule cut_rule;
        integration_rules.GetVolumeIntegrationRule(
            transformation, cut_rule);

        mfem::DenseMatrix cut_matrix;
        integrator->SetIntegrationRule(cut_rule);
        integrator->AssembleElementMatrix(
            element, transformation, cut_matrix);
        integrator->SetIntRule(nullptr);

        element_matrix = full_matrix;
        element_matrix *= epsilon;
        element_matrix.Add(1.0 - epsilon, cut_matrix);
    }

private:
    std::unique_ptr<mfem::BilinearFormIntegrator> integrator;
    mfem::GridFunction& phi;
    int integration_order;
    int level_set_order;
    mfem::real_t epsilon;
    bool positive;
};

/** @brief Assemble a scalar-normal-vector product on an implicit surface.

    This integrator evaluates

    @f[
      a(s, \mathbf{v}) = c \int_{\{\phi = 0\}}
      s\,(\mathbf{v} \cdot \mathbf{n}_{\phi})\,d\Gamma,
      \qquad
      \mathbf{n}_{\phi} = \frac{\nabla\phi}{\lVert\nabla\phi\rVert},
    @f]

    where Algoim supplies quadrature points and weights on the zero level set.
    With @a transpose set to false, the scalar space is the trial space and
    the vector space is the test space. With @a transpose set to true, the
    vector space is the trial space and the scalar space is the test space.

    @note The supplied GridFunction is borrowed and must outlive this
    integrator and every form that owns it.
 */
class ImplicitSurfaceNormalIntegrator
    : public mfem::BilinearFormIntegrator
{
private:
    mfem::GridFunction& phi;
    int integration_order;
    int level_set_order;
    mfem::real_t scale;
    bool transpose;

public:
    /** @brief Construct an implicit-surface normal coupling integrator.

        @param[in] phi_ Level-set GridFunction whose zero contour defines the
                       integration surface. Its gradient points toward the
                       positive side of the level set.
        @param[in] integrationOrder Quadrature order used by Algoim.
        @param[in] levelSetOrder Polynomial order used by Algoim when
                                 projecting the level set.
        @param[in] scale_ Constant multiplier @f$c@f$, including the desired
                          physical sign.
        @param[in] transpose_ If false, assemble scalar trial to vector test.
                              If true, assemble vector trial to scalar test.
     */
    ImplicitSurfaceNormalIntegrator(
        mfem::GridFunction& phi_,
        int integrationOrder,
        int levelSetOrder,
        mfem::real_t scale_,
        bool transpose_
    )
        : phi(phi_),
          integration_order(integrationOrder),
          level_set_order(levelSetOrder),
          scale(scale_),
          transpose(transpose_)
    {
    }

    /** @brief Assemble the local mixed matrix on @f$\{\phi=0\}@f$.

        The rows correspond to test-space DOFs and the columns to trial-space
        DOFs, following MFEM's MixedBilinearForm contract.
        Elements not intersected by the zero level set return a zero matrix.

        @param[in] trial_fe Trial finite element for the current mesh element.
        @param[in] test_fe Test finite element for the current mesh element.
        @param[in,out] transformation Transformation for the current element.
        @param[out] element_matrix Assembled local mixed matrix.
     */
    void AssembleElementMatrix2(
        const mfem::FiniteElement& trial_fe,
        const mfem::FiniteElement& test_fe,
        mfem::ElementTransformation& transformation,
        mfem::DenseMatrix& element_matrix
    ) override
    {
        const int dim = transformation.GetSpaceDim();
        const int trial_dofs = trial_fe.GetDof();
        const int test_dofs = test_fe.GetDof();

        if (!transpose) {
            element_matrix.SetSize(dim * test_dofs, trial_dofs);
        }
        else {
            element_matrix.SetSize(test_dofs, dim * trial_dofs);
        }
        element_matrix = 0.0;

        // Constructing this locally is intentional: Algoim caches the last
        // element rule, while optimization changes phi between assemblies.
        mfem::GridFunctionCoefficient phi_coefficient(&phi);
        mfem::AlgoimIntegrationRules integration_rules(
            integration_order, phi_coefficient, level_set_order);
        mfem::IntegrationRule interface_rule;
        integration_rules.GetSurfaceIntegrationRule(
            transformation,
            interface_rule
        );

        if (interface_rule.GetNPoints() == 0) {
            return;
        }

        mfem::Vector surface_weights;
        integration_rules.GetSurfaceWeights(
            transformation,
            interface_rule,
            surface_weights
        );

        mfem::Vector trial_shape(trial_dofs);
        mfem::Vector test_shape(test_dofs);
        mfem::Vector normal(dim);

        for (int point = 0; point < interface_rule.GetNPoints(); point++) {
            const mfem::IntegrationPoint& integration_point =
                interface_rule.IntPoint(point);

            transformation.SetIntPoint(&integration_point);
            trial_fe.CalcShape(integration_point, trial_shape);
            test_fe.CalcShape(integration_point, test_shape);

            // The interface normal points toward the positive side of phi.
            phi.GetGradient(transformation, normal);
            const mfem::real_t normal_length = normal.Norml2();
            if (normal_length <= 1.0e-12) {
                continue;
            }
            normal /= normal_length;

            const mfem::real_t weight = integration_point.weight
                * surface_weights[point]
                * transformation.Weight();

            if (!transpose) {
                for (int component = 0; component < dim; component++) {
                    for (int row_dof = 0; row_dof < test_dofs; row_dof++) {
                        const int row = component * test_dofs + row_dof;
                        for (int column = 0; column < trial_dofs; column++) {
                            element_matrix(row, column) +=
                                scale * weight
                                * test_shape[row_dof]
                                * normal[component]
                                * trial_shape[column];
                        }
                    }
                }
            }
            else {
                for (int row = 0; row < test_dofs; row++) {
                    for (int component = 0; component < dim; component++) {
                        for (int column_dof = 0; column_dof < trial_dofs; column_dof++) {
                            const int column = component * trial_dofs + column_dof;
                            element_matrix(row, column) +=
                                scale * weight
                                * test_shape[row]
                                * normal[component]
                                * trial_shape[column_dof];
                        }
                    }
                }
            }
        }
    }
};

} // namespace App
