#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

#include "mfem.hpp"

int main(int argc, char** argv)
{
    mfem::Mpi::Init(argc, argv);
    mfem::Hypre::Init();

    int rank = 0;
    int ranks = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &ranks);

    int failed = 0;
    try {
        mfem::Mesh serial = mfem::Mesh::MakeCartesian2D(
            8, 4, mfem::Element::QUADRILATERAL, true, 2.0, 1.0);
        std::vector<int> partition(serial.GetNE());
        for (int element = 0; element < serial.GetNE(); ++element) {
            partition[element] = std::min(
                ranks - 1, element * ranks / serial.GetNE());
        }
        mfem::ParMesh mesh(MPI_COMM_WORLD, serial, partition.data());

        mfem::H1_FECollection collection(1, mesh.Dimension());
        mfem::ParFiniteElementSpace space(&mesh, &collection);
        mfem::ConstantCoefficient mass(2.0);
        mfem::Vector velocity({0.25, -0.1});
        mfem::VectorConstantCoefficient convection(velocity);

        mfem::ParBilinearForm form(&space);
        form.AddDomainIntegrator(new mfem::MassIntegrator(mass));
        form.AddDomainIntegrator(
            new mfem::ConvectionIntegrator(convection, -1.0));
        form.Assemble();
        form.Finalize();
        std::unique_ptr<mfem::HypreParMatrix> matrix(
            form.ParallelAssemble());

        mfem::Vector expected(space.GetTrueVSize());
        for (int i = 0; i < expected.Size(); ++i) {
            expected[i] = 1.0 + 0.01 * (space.GetMyTDofOffset() + i);
        }

        mfem::Vector right_hand_side(expected.Size());
        mfem::Vector solution(expected.Size());
        matrix->Mult(expected, right_hand_side);
        solution = 0.0;

        mfem::MUMPSSolver solver(matrix->GetComm());
        solver.SetPrintLevel(0);
        solver.SetMatrixSymType(mfem::MUMPSSolver::UNSYMMETRIC);
        solver.SetOperator(*matrix);
        solver.Mult(right_hand_side, solution);

        solution -= expected;
        double local_error = solution * solution;
        double forward_error = 0.0;
        MPI_Allreduce(
            &local_error, &forward_error, 1, MPI_DOUBLE, MPI_SUM,
            MPI_COMM_WORLD);

        matrix->MultTranspose(expected, right_hand_side);
        solution = 0.0;
        solver.MultTranspose(right_hand_side, solution);
        solution -= expected;
        local_error = solution * solution;
        double transpose_error = 0.0;
        MPI_Allreduce(
            &local_error, &transpose_error, 1, MPI_DOUBLE, MPI_SUM,
            MPI_COMM_WORLD);

        failed = !std::isfinite(forward_error)
            || !std::isfinite(transpose_error)
            || std::sqrt(forward_error) > 1.0e-10
            || std::sqrt(transpose_error) > 1.0e-10;
        if (rank == 0) {
            std::cout << "MUMPS forward error: " << std::sqrt(forward_error)
                      << "\nMUMPS transpose error: "
                      << std::sqrt(transpose_error) << '\n';
        }
    }
    catch (const std::exception& error) {
        if (rank == 0) {
            std::cerr << "MUMPS smoke check failed: " << error.what() << '\n';
        }
        failed = 1;
    }

    int any_failed = 0;
    MPI_Allreduce(
        &failed, &any_failed, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
    return any_failed;
}
