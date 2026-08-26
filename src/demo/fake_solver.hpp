#pragma once

#include <algorithm>
#include <cmath>

#include "global_types.hpp"
#include "logging.hpp"

namespace App::Demo {

class FakeSolver {
    public:
        FakeSolver(const SolverSettings& settings,
                   LevelSet& lset,
                   SolverResult& result,
                   const LogFunction& log)
            : settings(settings),
              lset(lset),
              result(result),
              log(log)
        {
            populateResponse();
        }

        bool setup()
        {
            if (result.materialImpulseResponse.frequency.empty()) {
                populateResponse();
            }
            return true;
        }

        bool setMesh()
        {
            log(LogLevel::Message,
                settings.nz > 0
                    ? "Demo solver prepared a synthetic 3D mesh."
                    : "Demo solver prepared a synthetic 2D mesh.");
            return true;
        }

        bool assembleSolutionSpace()
        {
            log(LogLevel::Message,
                "Demo solver assembled the synthetic vibroacoustic system.");
            return true;
        }

        bool solve()
        {
            ++solveCount;
            populateResponse();
            result.success = 1;
            log(LogLevel::Message,
                "Demo forward solve updated the shared FFT response.");
            return true;
        }

        bool bindToGlvis()
        {
            log(LogLevel::Warning,
                "GLVis remains on the network-stream integration track.");
            return false;
        }

    private:
        void populateResponse()
        {
            constexpr int sampleCount = 256;
            constexpr float maximumFrequency = 5000.0f;
            SignalFFT& response = result.materialImpulseResponse;

            response.size = sampleCount;
            response.frequency.resize(sampleCount);
            response.attenuationDB.resize(sampleCount);
            response.amplitude.resize(sampleCount);
            response.phase.resize(sampleCount);

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

                response.frequency[i] = frequency;
                response.attenuationDB[i] = attenuation;
                response.amplitude[i] = std::pow(10.0f, attenuation / 20.0f);
                response.phase[i] = 0.55f * std::sin(t * 13.0f + improvement * 0.1f);
            }
        }

        const SolverSettings& settings;
        LevelSet& lset;
        SolverResult& result;
        const LogFunction& log;
        int solveCount = 0;
};

} // namespace App::Demo
