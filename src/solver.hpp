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

    struct SolverPerformance {
        double meshSetupSeconds = 0.0;
        double levelSetSmoothingSeconds = 0.0;
        double assemblySeconds = 0.0;
        double referenceTransientSeconds = 0.0;
        double designedTransientSeconds = 0.0;
        double fourierSeconds = 0.0;
        double passAdjointSeconds = 0.0;
        double stopAdjointSeconds = 0.0;
        double cutDifferentiationSeconds = 0.0;
        double filterAdjointSeconds = 0.0;
        double mumpsInitialFactorizationSeconds = 0.0;
        double mumpsEffectiveFactorizationSeconds = 0.0;
        double mumpsSolveSeconds = 0.0;
        double maximumForwardResidual = 0.0;
        int mpiRanks = 1;
        long long forwardFgmresIterations = 0;
        int forwardFgmresSolves = 0;
        int maximumForwardFgmresIterations = 0;
        long long adjointFgmresIterations = 0;
        int adjointFgmresSolves = 0;
        int maximumAdjointFgmresIterations = 0;
        int cutElements = 0;
        int differentiatedDofs = 0;
    };

    // The optimizer needs only the forward/adjoint contract. Keeping this
    // seam here lets its CTest suite use deterministic curated physics while
    // the application continues to pass the real Solver by reference.
    class ForwardSolver {
    public:
        virtual ~ForwardSolver() = default;
        virtual bool setMesh() = 0;
        virtual bool assembleSolutionSpace() = 0;
        virtual bool solve() = 0;
        virtual SolverStatus get_status() const = 0;
        virtual const FrequencyResponse& frequencyResponse() const = 0;
        virtual bool differentiateFrequencyResponses(
            const std::vector<std::complex<double>>& pass_spectrum_derivative,
            const std::vector<std::complex<double>>& stop_spectrum_derivative,
            mfem::Vector& pass_design_gradient,
            mfem::Vector& stop_design_gradient) = 0;
    };

    class Solver : public ForwardSolver {

    public:

        Solver(SolverSettings& settings,
               LevelSet& lset,
               SolverResult& result,
               const LogFunction& log);

        ~Solver() override;

        bool setMesh() override;
        bool assembleSolutionSpace() override;
        bool solve() override;
        void parallelWorkerLoop();
        void shutdownParallelWorkers();
        SolverStatus get_status() const override;
        const FrequencyResponse& frequencyResponse() const override;
        const SolverPerformance& performance() const;
        bool differentiateFrequencyResponses(
            const std::vector<std::complex<double>>& pass_spectrum_derivative,
            const std::vector<std::complex<double>>& stop_spectrum_derivative,
            mfem::Vector& pass_design_gradient,
            mfem::Vector& stop_design_gradient) override;



    private:
        // TODO: Come up with better return types for these


        // Settings are edited by the UI and read at the start of each sequential solver step.
        SolverSettings& settings;
        LevelSet& lset;
        SolverResult& result;
        const LogFunction& log;

        std::unique_ptr<mfem::Mesh> mesh;
        std::unique_ptr<mfem::H1_FECollection> fec;
        std::unique_ptr<mfem::FiniteElementSpace> level_set_fes;
        std::unique_ptr<mfem::FiniteElementSpace> scalar_fes;
        std::unique_ptr<mfem::FiniteElementSpace> displacement_fes;
        std::unique_ptr<mfem::GridFunction> phi_field;

        int fe_order;
        int level_set_order;
        int cut_integration_order;

        // Newmark discrete matrices. What else should be classwide? the state vectors?
#if !METAMATERIAL_USE_MPI
        std::unique_ptr<mfem::SparseMatrix> M;
        std::unique_ptr<mfem::SparseMatrix> C;
        std::unique_ptr<mfem::SparseMatrix> K;
        std::unique_ptr<mfem::SparseMatrix> reference_M;
        std::unique_ptr<mfem::SparseMatrix> reference_C;
        std::unique_ptr<mfem::SparseMatrix> reference_K;
        std::unique_ptr<mfem::SparseMatrix> Muu_block;
        std::unique_ptr<mfem::SparseMatrix> Cuu_block;
        std::unique_ptr<mfem::SparseMatrix> Kuu_block;
        std::unique_ptr<mfem::SparseMatrix> Mpp_block;
        std::unique_ptr<mfem::SparseMatrix> Cpp_block;
        std::unique_ptr<mfem::SparseMatrix> Kpp_block;
        std::unique_ptr<mfem::SparseMatrix> reference_Muu_block;
        std::unique_ptr<mfem::SparseMatrix> reference_Cuu_block;
        std::unique_ptr<mfem::SparseMatrix> reference_Kuu_block;
        std::unique_ptr<mfem::SparseMatrix> reference_Mpp_block;
        std::unique_ptr<mfem::SparseMatrix> reference_Cpp_block;
        std::unique_ptr<mfem::SparseMatrix> reference_Kpp_block;
        std::unique_ptr<mfem::SparseMatrix> effective_displacement_block;
        std::unique_ptr<mfem::SparseMatrix> effective_pressure_block;
        std::unique_ptr<mfem::SparseMatrix> initial_displacement_block;
        std::unique_ptr<mfem::SparseMatrix> initial_pressure_block;
        std::unique_ptr<mfem::SparseMatrix> effective_matrix;
        std::unique_ptr<mfem::SparseMatrix> effective_matrix_transpose;
        std::unique_ptr<mfem::SparseMatrix> initial_matrix;
        std::unique_ptr<mfem::SparseMatrix> initial_matrix_transpose;
#endif
        std::unique_ptr<mfem::SparseMatrix> design_to_cell;
        std::unique_ptr<mfem::SparseMatrix> cell_to_level_set;
        std::unique_ptr<mfem::SparseMatrix> filter_matrix;
        mfem::Vector inlet_load;
        mfem::Vector outlet_functional;
        mfem::Vector system_inlet_load;
        mfem::Vector system_outlet_functional;
        mfem::DenseMatrix element_centers;
        mfem::Vector cell_volumes;
        mfem::Array<int> displacement_essential_tdofs;
        std::vector<double> source_pressure;
        std::vector<double> source_pressure_derivative;
        std::vector<double> inlet_pressure;
        std::vector<double> reference_inlet_pressure;
        std::vector<double> outlet_pressure;
        std::vector<double> reference_outlet_pressure;
        std::vector<double> fft_window;
        std::vector<std::complex<double>> reference_spectrum;
        std::vector<mfem::Vector> pass_adjoint_history;
        std::vector<mfem::Vector> stop_adjoint_history;
        FrequencyResponse frequency_response;
        SolverPerformance performance_data;
        std::unique_ptr<mfem::socketstream> glvis_stream;
        double level_set_scale = 0.0;
        double design_region_measure = 0.0;
        int pressure_offset = 0;
        int glvis_connection_failures = 0;
        int mesh_nx = -1;
        int mesh_ny = -1;
        int mesh_nz = -1;
        bool initialize_design_on_next_mesh = true;
        bool mesh_is_ready = false;
        bool assembly_is_ready = false;
        bool forward_is_ready = false;
        bool reference_ready = false;
        SolverSettings cached_settings{};
        bool settings_cache_ready = false;
        std::atomic<SolverStatus> status{SolverStatus::Idle};

#if METAMATERIAL_USE_MPI
        struct ParallelState;
        std::unique_ptr<ParallelState> parallel_state;
        bool parallel_workers_shutdown = false;
#endif

        bool smooth_level_set(
            const mfem::GridFunction& level_set,
            mfem::GridFunction& smoothed_level_set);
        bool buildDesignMesh(bool prepare_design_data = true);
        bool meshSettingsMatch() const;
        bool matrixSettingsMatch() const;
        bool sourceSettingsMatch() const;
#if METAMATERIAL_USE_MPI
        bool setMeshParallelLocal();
        bool assembleSolutionSpaceParallelLocal();
        bool solveParallelLocal();
        bool differentiateFrequencyResponsesParallelLocal(
            const std::vector<std::complex<double>>& pass_spectrum_derivative,
            const std::vector<std::complex<double>>& stop_spectrum_derivative,
            mfem::Vector& pass_design_gradient,
            mfem::Vector& stop_design_gradient);
#endif
        bool prepareLevelSetAndSource();
        bool bindToGlvis();
        void streamToGlvis();
        bool postprocessFourierResponse();



    };

} // namespace App
