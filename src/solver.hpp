#pragma once
#include <memory>
#include <string> 
#include <vector>
#include "global_types.hpp"
#include "mfem.hpp"
#include "coeffs.hpp"

// Classes like solver, exporter, and optimizer should be stateless

namespace App {
    class Solver {

    public:

        Solver( LevelSet& lset,
                SolverResult& result,
                const PhysicsProblem& problem,
                int nx, int ny = 0, int nz = 0,
                std::string solverAlgo = "newmark"
            );

        ~Solver();

        bool setup();
        bool setMesh(int nx, int ny = 0, int nz = 0, mfem::real_t sx = 1.0, mfem::real_t sy = 1.0, mfem::real_t sz = 1.0);
        bool assembleSolutionSpace();
        bool solve();
        bool bindToGlvis(std::string host, int port);
        void setSimDuration(float val);
        float getSimDuration();
        void setProblem(std::string prob);
        std::string getProblem();



    private:
        // TODO: Come up with better return types for these


        // these are settings derived from the UI, and lset is the global data structure key to the app
        double duration;
        double dt;
        int nx;
        int ny;
        int nz;
        LevelSet& lset;
        SolverResult& result;
        PhysicsProblem& problem;

        std::unique_ptr<mfem::Mesh> mesh;
        std::unique_ptr<mfem::H1_FECollection> fec;
        std::unique_ptr<mfem::FiniteElementSpace> pressure_fes;
        std::unique_ptr<mfem::FiniteElementSpace> displacement_fes;
        std::unique_ptr<mfem::FiniteElementSpace> level_set_fes;

        int fe_order;
        int level_set_order;
        int cut_integration_order;
        mfem::real_t sx;
        mfem::real_t sy;
        mfem::real_t sz;

        // Newmark discrete matrices. What else should be classwide? the state vectors?
        std::unique_ptr<mfem::SparseMatrix> M;
        std::unique_ptr<mfem::SparseMatrix> C;
        std::unique_ptr<mfem::SparseMatrix> K;




    };
};
