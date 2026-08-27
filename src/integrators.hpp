#pragma once

#include "mfem.hpp"

namespace App {

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
    mfem::GridFunctionCoefficient phi_coefficient;
    mfem::AlgoimIntegrationRules integration_rules;
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
          phi_coefficient(&phi_),
          integration_rules(
              integrationOrder,
              phi_coefficient,
              levelSetOrder
          ),
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
