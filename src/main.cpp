#include <string>
#include <vector>
#include <map> 
#include <utility>
#include <stdexcept>
#include <memory>
#include "global_types.hpp"
#include "renderer.hpp"
#include "optimizer.hpp"
#include "solver.hpp"
#include "exporter.hpp"


int main() {
    std::string glvisHost = "localhost";
    int glvisPort = 19916;

    // Construct with default settings.
    // TODO: Make App settings construct defaults
    // TODO: Also instantiate these objects as unique pointers
    auto settings = std::make_unique<AppSettings>(); 
    auto result   = std::make_unique<SolverResult>();

    // TODO: This needs an initial guess constructor to construct the first lset guess
    // Probably an equally spaced square grid of cylinders/circles with some radius each; basically a sonic crystal whose shape we can weakly try to guess from the bandgap. Or just hardocde a single crystal structure. how about that?
    auto lset     = std::make_unique<LevelSet>();


    Renderer renderer = Renderer(settings);
    renderer.setup();

    // in opt, result is const, not edited.
    Optimizer optimizer = Optimizer(lset&, result&, settings.optSettings.);
    // I would like to change the phy
    App::Solver solver = App::Solver(settings.solverSettings, result&, lset&);
    Exporter exporter = Exporter(settings.exporterSettings);

    // For now, like this. At least until we somehow integrate Glvis natively in the IMGUI plane
    renderer.GlvisPanel();
    while (!renderer.shouldClose) {
        renderer.displayFrame();
    };

    executor.thread.join()
};
