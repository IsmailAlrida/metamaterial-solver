#pragma once
#include <memory>
#include "global_types.hpp"
#include "mfem.hpp"

// Business objects retain references to top-level app data instead of owning snapshots.

namespace App {

    class Solver {

    public:

        Solver(const SolverSettings& settings,
               LevelSet& lset,
               SolverResult& result);

        ~Solver();

        bool setup();
        bool setMesh();
        bool assembleSolutionSpace();
        bool solve();
        bool bindToGlvis();



    private:
        // TODO: Come up with better return types for these


        // Settings are edited by the UI and read at the start of each sequential solver step.
        const SolverSettings& settings;
        LevelSet& lset;
        SolverResult& result;

        std::unique_ptr<mfem::Mesh> mesh;
        std::unique_ptr<mfem::H1_FECollection> fec;
        std::unique_ptr<mfem::FiniteElementSpace> pressure_fes;
        std::unique_ptr<mfem::FiniteElementSpace> displacement_fes;
        std::unique_ptr<mfem::FiniteElementSpace> level_set_fes;

        int fe_order;
        int level_set_order;
        int cut_integration_order;

        // Newmark discrete matrices. What else should be classwide? the state vectors?
        std::unique_ptr<mfem::SparseMatrix> M;
        std::unique_ptr<mfem::SparseMatrix> C;
        std::unique_ptr<mfem::SparseMatrix> K;




    };

} // namespace App
