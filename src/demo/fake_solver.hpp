#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

#include "glvis_adapter.hpp"
#include "global_types.hpp"
#include "logging.hpp"
#include "mfem.hpp"

namespace App::Demo {

class FakeSolver {
    public:
        FakeSolver(SolverSettings& settings,
                   OptimizerSettings& optimizerSettings,
                   LevelSet& lset,
                   SolverResult& result,
                   const LogFunction& log,
                   bool = false)
            : settings(settings),
              optimizerSettings(optimizerSettings),
              lset(lset),
              result(result),
              log(log),
              mesh(mfem::Mesh::MakeCartesian2D(
                  32,
                  16,
                  mfem::Element::QUADRILATERAL,
                  true,
                  2.0,
                  1.0)),
              fec(1, mesh.Dimension()),
              fespace(&mesh, &fec)
        {
            lset.setSpace(fespace);
            populateResponse();
        }

        ~FakeSolver()
        {
            lset.detach();
        }

        bool setMesh()
        {
            log(LogLevel::Message, "MFEM Example 1 prepared its Cartesian mesh.");
            return true;
        }

        bool setMesh(bool)
        {
            return setMesh();
        }

        bool assembleSolutionSpace()
        {
            log(LogLevel::Message, "MFEM Example 1 prepared the Poisson problem.");
            return true;
        }

        bool assembleSolutionSpace(bool)
        {
            return assembleSolutionSpace();
        }

        bool solve()
        {
            status.store(SolverStatus::Working);
            // MFEM Example 1: solve -Delta u = 1 with homogeneous Dirichlet data.
            mfem::Array<int> essentialTrueDofs;
            if (mesh.bdr_attributes.Size()) {
                mfem::Array<int> essentialBoundary(mesh.bdr_attributes.Max());
                essentialBoundary = 0;
                mesh.MarkExternalBoundaries(essentialBoundary);
                fespace.GetEssentialTrueDofs(essentialBoundary, essentialTrueDofs);
            }

            mfem::ConstantCoefficient one(1.0);
            mfem::LinearForm rightHandSide(&fespace);
            rightHandSide.AddDomainIntegrator(new mfem::DomainLFIntegrator(one));
            rightHandSide.Assemble();

            mfem::GridFunction solution(&fespace);
            solution = 0.0;

            mfem::BilinearForm poisson(&fespace);
            poisson.AddDomainIntegrator(new mfem::DiffusionIntegrator(one));
            poisson.Assemble();

            mfem::OperatorPtr system;
            mfem::Vector systemRightHandSide;
            mfem::Vector systemSolution;
            poisson.FormLinearSystem(
                essentialTrueDofs,
                solution,
                rightHandSide,
                system,
                systemSolution,
                systemRightHandSide);

#if METAMATERIAL_USE_CUDA
            mfem::DSmoother smoother(static_cast<mfem::SparseMatrix&>(*system));
#else
            mfem::GSSmoother smoother(static_cast<mfem::SparseMatrix&>(*system));
#endif
            mfem::PCG(
                *system,
                smoother,
                systemRightHandSide,
                systemSolution,
                0,
                200,
                1e-12,
                0.0);
            poisson.RecoverFEMSolution(
                systemSolution,
                rightHandSide,
                solution);

            if (glvisStream || bindToGlvis()) {
                *glvisStream << "solution\n" << mesh << solution << std::flush;
                if (!glvisStream->good()) {
                    glvisStream.reset();
                    ++glvisConnectionFailures;
                    log(LogLevel::Warning,
                        glvisConnectionFailures >= 3
                            ? "MFEM Example 1 lost GLVis three times; disabling streaming."
                            : "MFEM Example 1 lost GLVis; a later solve will reconnect.");
                }
            }

            ++solveCount;
            populateResponse();
            result.success = 1;
            status.store(SolverStatus::Converged);
            log(LogLevel::Message, "MFEM Example 1 solved and published its demo response.");
            return true;
        }

        bool solve(bool)
        {
            return solve();
        }

        void parallelWorkerLoop()
        {
        }

        void shutdownParallelWorkers()
        {
        }

        SolverStatus get_status() const
        {
            return status.load();
        }

    private:
        bool bindToGlvis()
        {
            if (glvisStream && glvisStream->good()) {
                return true;
            }
            glvisStream.reset();
            if (glvisConnectionFailures >= 3) {
                return false;
            }
            char host[] = "127.0.0.1";
            auto stream = std::make_unique<mfem::socketstream>(
                host, GlvisAdapter::Port);
            if (!stream->good()) {
                ++glvisConnectionFailures;
                log(LogLevel::Warning,
                    glvisConnectionFailures == 3
                        ? "MFEM Example 1 could not connect to GLVis three times; disabling streaming."
                        : "MFEM Example 1 could not connect to GLVis; continuing headless.");
                return false;
            }
            stream->precision(8);
            glvisStream = std::move(stream);
            glvisConnectionFailures = 0;
            return true;
        }

        void populateResponse()
        {
            constexpr int sampleCount = 256;
            constexpr float maximumFrequency = 5000.0f;
            auto response = std::make_shared<SignalFFT>();

            response->size = sampleCount;
            response->frequency.resize(sampleCount);
            response->referenceAmplitude.resize(sampleCount);
            response->attenuationDB.resize(sampleCount);
            response->amplitude.resize(sampleCount);
            response->transmission.resize(sampleCount);
            response->phase.resize(sampleCount);
            response->valid.assign(sampleCount, 1);

            const float improvement = static_cast<float>(std::min(solveCount, 8));
            for (int i = 0; i < sampleCount; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
                const float frequency = maximumFrequency * t;
                const float firstBand = std::exp(-std::pow((frequency - 1450.0f) / 330.0f, 2.0f));
                const float secondBand = std::exp(-std::pow((frequency - 2850.0f) / 460.0f, 2.0f));
                const float ripple = 2.2f * std::sin(t * 28.0f + improvement * 0.22f);
                const float attenuation =
                    -4.0f + ripple
                    - (12.0f + improvement * 2.0f) * firstBand
                    - (17.0f + improvement * 2.4f) * secondBand;

                response->frequency[i] = frequency;
                response->attenuationDB[i] = attenuation;
                response->referenceAmplitude[i] = 1.0f;
                response->transmission[i] = std::pow(10.0f, attenuation / 20.0f);
                response->amplitude[i] = response->transmission[i];
                response->phase[i] = 0.55f * std::sin(t * 13.0f + improvement * 0.1f);
            }

            std::shared_ptr<const SignalFFT> publishedResponse = std::move(response);
            std::atomic_store(
                &result.materialImpulseResponse,
                std::move(publishedResponse));
        }

        const SolverSettings& settings;
        const OptimizerSettings& optimizerSettings;
        LevelSet& lset;
        SolverResult& result;
        const LogFunction& log;
        mfem::Mesh mesh;
        mfem::H1_FECollection fec;
        mfem::FiniteElementSpace fespace;
        std::unique_ptr<mfem::socketstream> glvisStream;
        int glvisConnectionFailures = 0;
        int solveCount = 0;
        std::atomic<SolverStatus> status{SolverStatus::Idle};
};

} // namespace App::Demo
