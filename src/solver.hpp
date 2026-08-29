#pragma once
#include <atomic>
#include <complex>
#include <memory>
#include <vector>
#include "global_types.hpp"
#include "logging.hpp"
#include "mfem.hpp"

// Business objects retain references to top-level app data instead of owning snapshots.

namespace App {

    struct FrequencyResponse {
        std::vector<double> frequency;
        std::vector<std::complex<double>> outlet;
        std::vector<std::complex<double>> reference;
        std::vector<unsigned char> valid;
    };

    class Solver {

    public:

        Solver(const SolverSettings& settings,
               const OptimizerSettings& optimizer_settings,
               LevelSet& lset,
               SolverResult& result,
               const LogFunction& log);

        ~Solver();

        bool setMesh();
        bool assembleSolutionSpace();
        bool solve();
        SolverStatus get_status() const;
        const FrequencyResponse& frequencyResponse() const;
        bool differentiateFrequencyResponse(
            const std::vector<std::complex<double>>& spectrum_derivative,
            mfem::Vector& design_gradient);



    private:
        // TODO: Come up with better return types for these


        // Settings are edited by the UI and read at the start of each sequential solver step.
        const SolverSettings& settings;
        const OptimizerSettings& optimizer_settings;
        LevelSet& lset;
        SolverResult& result;
        const LogFunction& log;

        std::unique_ptr<mfem::Mesh> mesh;
        std::unique_ptr<mfem::H1_FECollection> fec;
        std::unique_ptr<mfem::FiniteElementSpace> level_set_fes;
        std::unique_ptr<mfem::FiniteElementSpace> scalar_fes;
        std::unique_ptr<mfem::FiniteElementSpace> displacement_fes;

        int fe_order;
        int level_set_order;
        int cut_integration_order;

        // Newmark discrete matrices. What else should be classwide? the state vectors?
        std::unique_ptr<mfem::SparseMatrix> M;
        std::unique_ptr<mfem::SparseMatrix> C;
        std::unique_ptr<mfem::SparseMatrix> K;
        std::unique_ptr<mfem::SparseMatrix> effective_matrix_transpose;
        std::unique_ptr<mfem::SparseMatrix> initial_matrix_transpose;
        std::unique_ptr<mfem::SparseMatrix> design_to_cell;
        std::unique_ptr<mfem::SparseMatrix> cell_to_level_set;
        std::unique_ptr<mfem::SparseMatrix> filter_matrix;
        mfem::Vector inlet_load;
        mfem::Vector outlet_functional;
        mfem::DenseMatrix element_centers;
        mfem::Vector cell_volumes;
        mfem::Array<int> displacement_essential_tdofs;
        std::vector<double> source_pressure;
        std::vector<double> source_pressure_derivative;
        std::vector<double> outlet_pressure;
        std::vector<double> reference_outlet_pressure;
        std::vector<double> fft_window;
        std::vector<mfem::Vector> adjoint_history;
        FrequencyResponse frequency_response;
        double level_set_scale = 0.0;
        int pressure_offset = 0;
        bool design_initialized = false;
        bool reference_ready = false;
        std::atomic<SolverStatus> status{SolverStatus::Idle};

        bool smooth_level_set(
            const mfem::GridFunction& level_set,
            mfem::GridFunction& smoothed_level_set);
        bool postprocessFourierResponse();



    };

} // namespace App
