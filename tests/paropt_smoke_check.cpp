#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cmath>
#include <iostream>

#include "ParOptOptimizer.h"

namespace {

class QuadraticProblem final : public ParOptProblem {
public:
    QuadraticProblem()
        : ParOptProblem(MPI_COMM_SELF)
    {
        setProblemSizes(1, 1, 0);
        setNumInequalities(1, 0);
    }

    ParOptQuasiDefMat* createQuasiDefMat() override
    {
        return new ParOptQuasiDefBlockMat(this, 0);
    }

    void getVarsAndBounds(
        ParOptVec* variables,
        ParOptVec* lower_bounds,
        ParOptVec* upper_bounds) override
    {
        ParOptScalar* x;
        ParOptScalar* lower;
        ParOptScalar* upper;
        variables->getArray(&x);
        lower_bounds->getArray(&lower);
        upper_bounds->getArray(&upper);
        x[0] = 0.8;
        lower[0] = 0.0;
        upper[0] = 1.0;
    }

    int evalObjCon(
        ParOptVec* variables,
        ParOptScalar* objective,
        ParOptScalar* constraints) override
    {
        ++objective_evaluations;
        ParOptScalar* x;
        variables->getArray(&x);
        *objective = (x[0] - 0.25) * (x[0] - 0.25);
        constraints[0] = x[0] - 0.1;
        return 0;
    }

    int evalObjConGradient(
        ParOptVec* variables,
        ParOptVec* objective_gradient,
        ParOptVec** constraint_gradients) override
    {
        ++gradient_evaluations;
        ParOptScalar* x;
        ParOptScalar* gradient;
        variables->getArray(&x);
        objective_gradient->getArray(&gradient);
        gradient[0] = 2.0 * (x[0] - 0.25);
        constraint_gradients[0]->getArray(&gradient);
        gradient[0] = 1.0;
        return 0;
    }

    int objective_evaluations = 0;
    int gradient_evaluations = 0;
};

} // namespace

int main(int argc, char** argv)
{
    int provided = 0;
    if (MPI_Init_thread(&argc, &argv, MPI_THREAD_SERIALIZED, &provided)
            != MPI_SUCCESS) {
        std::cerr << "ParOpt smoke check could not initialize MPI.\n";
        return 1;
    }
    if (provided < MPI_THREAD_SERIALIZED) {
        std::cerr << "ParOpt smoke check needs MPI_THREAD_SERIALIZED.\n";
        MPI_Finalize();
        return 1;
    }

    auto* problem = new QuadraticProblem();
    problem->incref();
    auto* options = new ParOptOptions(MPI_COMM_SELF);
    options->incref();
    ParOptOptimizer::addDefaultOptions(options);
    const int option_error =
        options->setOption("algorithm", "mma")
        | options->setOption("mma_output_file", "")
        | options->setOption("mma_max_iterations", 5)
        | options->setOption("mma_l1_tol", 0.0)
        | options->setOption("mma_linfty_tol", 0.0)
        | options->setOption("mma_infeas_tol", 0.0);
    if (option_error != 0) {
        options->decref();
        problem->decref();
        MPI_Finalize();
        std::cerr << "ParOpt rejected the smoke-check options.\n";
        return 1;
    }

    auto* optimizer = new ParOptOptimizer(problem, options);
    optimizer->incref();
    optimizer->optimize();

    ParOptVec* optimized = nullptr;
    optimizer->getOptimizedPoint(
        &optimized, nullptr, nullptr, nullptr, nullptr);
    ParOptScalar* x;
    optimized->getArray(&x);
    const double value = x[0];
    const bool passed = std::isfinite(value)
        && value >= 0.1 && value <= 1.0
        && problem->objective_evaluations == 6
        && problem->gradient_evaluations == 6
        && (value - 0.25) * (value - 0.25) < (0.8 - 0.25) * (0.8 - 0.25);

    optimizer->decref();
    options->decref();
    problem->decref();
    MPI_Finalize();

    if (!passed) {
        std::cerr << "ParOpt MMA did not improve the smoke problem.\n";
        return 1;
    }
    std::cout << "ParOpt MMA smoke check passed.\n";
    return 0;
}
