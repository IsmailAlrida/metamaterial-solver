#include "exporter.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "export_templates.hpp"
#include "zip.h"

namespace App {

namespace {

void write_number(std::ostream& output, double value)
{
    if (std::isfinite(value)) {
        output << std::setprecision(17) << value;
    }
    else {
        output << "null";
    }
}

void write_string(std::ostream& output, const std::string& value)
{
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) {
                    output << "\\u"
                           << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(character)
                           << std::dec << std::setfill(' ');
                }
                else {
                    output << static_cast<char>(character);
                }
        }
    }
    output << '"';
}

template <typename Container>
void write_numbers(std::ostream& output, const Container& values)
{
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        write_number(output, static_cast<double>(values[index]));
    }
    output << ']';
}

void write_vector(std::ostream& output, const mfem::Vector& values)
{
    output << '[';
    const double* data = values.HostRead();
    for (int index = 0; index < values.Size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        write_number(output, data[index]);
    }
    output << ']';
}

void write_signal(std::ostream& output,
                  const std::shared_ptr<const SignalTD>& signal)
{
    if (!signal) {
        output << "null";
        return;
    }
    output << "{\"size\":" << signal->size << ",\"time_s\":";
    write_numbers(output, signal->time);
    output << ",\"amplitude\":";
    write_numbers(output, signal->amplitude);
    output << '}';
}

const char* status_name(OptimizerStatus status)
{
    switch (status) {
        case OptimizerStatus::Idle: return "idle";
        case OptimizerStatus::Working: return "working";
        case OptimizerStatus::Converged: return "converged";
        case OptimizerStatus::MaximumIterations: return "maximum_iterations";
        case OptimizerStatus::Diverged: return "diverged";
        case OptimizerStatus::Cancelled: return "cancelled";
        case OptimizerStatus::Error: return "error";
    }
    return "unknown";
}

const char* device_name(SolverDevice device)
{
    switch (device) {
        case SolverDevice::serial: return "serial";
        case SolverDevice::parallel: return "parallel";
        case SolverDevice::serialCuda: return "serial_cuda";
        case SolverDevice::parallelCuda: return "parallel_cuda";
    }
    return "unknown";
}

const char* linear_solver_name(LinearSolveMethod method)
{
    return method == LinearSolveMethod::mumps ? "mumps" : "fgmres";
}

std::string make_json(const AppSettings& settings,
                      const SolverResult& result,
                      const LevelSet& geometry,
                      OptimizerStatus status,
                      int iteration,
                      double pass,
                      double stop,
                      double mma_bound)
{
    if (geometry.design.Size() == 0
        || geometry.phi.Size() != geometry.design.Size()) {
        throw std::runtime_error(
            "The level-set vectors are not ready for export.");
    }

    const SolverSettings& solver = settings.solverSettings;
    const OptimizerSettings& optimizer = settings.optSettings;
    const double length = solver.inletLength
        + solver.designLength + solver.outletLength;
    std::unique_ptr<mfem::Mesh> export_mesh;
    if (solver.nz > 0) {
        export_mesh = std::make_unique<mfem::Mesh>(
            mfem::Mesh::MakeCartesian3D(
                solver.nx, solver.ny, solver.nz,
                mfem::Element::HEXAHEDRON,
                length, solver.sy, solver.sz));
    }
    else {
        export_mesh = std::make_unique<mfem::Mesh>(
            mfem::Mesh::MakeCartesian2D(
                solver.nx, solver.ny,
                mfem::Element::QUADRILATERAL,
                true, length, solver.sy));
    }
    const double design_end = solver.inletLength + solver.designLength;
    mfem::Vector center(export_mesh->Dimension());
    for (int element = 0; element < export_mesh->GetNE(); ++element) {
        export_mesh->GetElementCenter(element, center);
        export_mesh->SetAttribute(
            element,
            center[0] < solver.inletLength ? 1 : center[0] < design_end ? 2 : 3);
    }
    export_mesh->SetAttributes();
    mfem::H1_FECollection export_collection(1, export_mesh->Dimension());
    mfem::FiniteElementSpace export_space(
        export_mesh.get(), &export_collection);
    if (geometry.design.Size() != export_space.GetTrueVSize()) {
        throw std::runtime_error(
            "The level-set vectors do not match the configured export mesh.");
    }
    const mfem::Mesh& mesh = *export_mesh;
    const auto inlet = std::atomic_load(&result.inletPressure);
    const auto outlet = std::atomic_load(&result.outletPressure);
    const auto reference = std::atomic_load(&result.referenceOutletPressure);
    const auto response = std::atomic_load(&result.materialImpulseResponse);
    const double solid_infill_fraction = result.solidInfillFraction.load(
        std::memory_order_acquire);

    std::ostringstream output;
    output << "{\n  \"schema_version\":1,\n"
           << "  \"optimizer\":{\"status\":";
    write_string(output, status_name(status));
    output << ",\"completed_iteration\":" << iteration
           << ",\"pass_objective\":";
    write_number(output, pass);
    output << ",\"stop_objective\":";
    write_number(output, stop);
    output << ",\"mma_bound\":";
    write_number(output, mma_bound);
    output << "},\n  \"settings\":{\n    \"solver\":{";

    output << "\"nx\":" << solver.nx
           << ",\"ny\":" << solver.ny
           << ",\"nz\":" << solver.nz
           << ",\"inlet_length_m\":" << solver.inletLength
           << ",\"design_length_m\":" << solver.designLength
           << ",\"outlet_length_m\":" << solver.outletLength
           << ",\"height_m\":" << solver.sy
           << ",\"depth_m\":" << solver.sz
           << ",\"duration_s\":" << solver.duration
           << ",\"dt_s\":" << solver.dt
           << ",\"newmark_beta\":" << solver.newmarkBeta
           << ",\"newmark_gamma\":" << solver.newmarkGamma
           << ",\"source_amplitude_pa\":" << solver.sourceAmplitude
           << ",\"source_seed\":" << solver.sourceSeed
           << ",\"initial_pattern_lx_m\":" << solver.initialPatternLx
           << ",\"initial_pattern_ly_m\":" << solver.initialPatternLy
           << ",\"initial_pattern_x\":" << solver.initialPatternX
           << ",\"initial_pattern_y\":" << solver.initialPatternY
           << ",\"initial_pattern_bias\":" << solver.initialPatternBias
           << ",\"initial_pattern_threshold\":" << solver.initialPatternThreshold
           << ",\"filter_radius_m\":" << solver.filterRadius
           << ",\"cut_derivative_relative_step\":"
           << solver.cutDerivativeRelativeStep
           << ",\"algorithm\":";
    write_string(output, solver.algo);
    output << ",\"isotropic_grid\":"
           << (solver.isotropicGrid ? "true" : "false")
           << ",\"hann_window\":"
           << (solver.useHannWindow ? "true" : "false")
           << ",\"device\":";
    write_string(output, device_name(solver.device));
    output << ",\"linear_solver\":";
    write_string(output, linear_solver_name(solver.linearSolveMethod));

    if (const auto* physics =
            std::get_if<VibroacousticSettings>(&solver.physics)) {
        output << ",\"physics\":{\"type\":\"vibroacoustic\""
               << ",\"solid_density_kg_m3\":" << physics->rho_s
               << ",\"air_density_kg_m3\":" << physics->rho_a
               << ",\"speed_of_sound_m_s\":" << physics->c_a
               << ",\"youngs_modulus_pa\":" << physics->youngs_modulus
               << ",\"poisson_ratio\":" << physics->poisson_ratio
               << ",\"damping_ratio\":" << physics->zeta
               << ",\"damping_frequency_1_hz\":" << physics->f1
               << ",\"damping_frequency_2_hz\":" << physics->f2
               << ",\"fictitious_domain_epsilon\":" << physics->epsilon
               << '}';
    }
    else {
        output << ",\"physics\":{\"type\":\"electromagnetic\"}";
    }
    output << "},\n    \"optimizer\":{"
           << "\"frequency_min_hz\":" << optimizer.frequencyMin
           << ",\"frequency_max_hz\":" << optimizer.frequencyMax
           << ",\"attenuation_min_db\":" << optimizer.attenuationMinDb
           << ",\"attenuation_max_db\":" << optimizer.attenuationMaxDb
           << ",\"frequency_samples\":" << optimizer.frequencySamples
           << ",\"maximum_iterations\":" << optimizer.maxIterations
           << ",\"mma_initial_asymptote\":" << optimizer.mmaInitialAsymptote
           << ",\"mma_decrease_asymptote\":" << optimizer.mmaDecreaseAsymptote
           << ",\"mma_increase_asymptote\":" << optimizer.mmaIncreaseAsymptote
           << ",\"mma_constraint_penalty\":" << optimizer.mmaConstraintPenalty
           << ",\"display_target_in_db\":"
           << (optimizer.displayTargetInDb ? "true" : "false")
           << ",\"frequency_bands\":[";
    for (std::size_t index = 0; index < optimizer.frequencyBands.size(); ++index) {
        const FrequencyBand& band = optimizer.frequencyBands[index];
        if (index != 0) {
            output << ',';
        }
        output << "{\"type\":\""
               << (band.type == FrequencyBandType::pass ? "pass" : "stop")
               << "\",\"start_hz\":" << band.startHz
               << ",\"end_hz\":" << band.endHz
               << ",\"target_transmission\":" << band.targetTransmission
               << '}';
    }
    output << "]}\n  },\n";

    output << "  \"solver\":{\"success\":" << result.success
           << ",\"state_size\":" << result.stateSize
           << ",\"displacement_size\":" << result.displacementSize
           << ",\"pressure_size\":" << result.pressureSize
           << ",\"pressure_offset\":" << result.pressureOffset
           << ",\"time_steps\":" << result.timeSteps
           << ",\"dt_s\":" << result.dt
           << ",\"newmark_residual_norms\":[";
    for (std::size_t index = 0; index < result.residualNorms.size(); ++index) {
        const NewmarkResidualNorms& residual = result.residualNorms[index];
        if (index != 0) {
            output << ',';
        }
        output << "{\"equilibrium\":";
        write_number(output, residual.equilibrium);
        output << ",\"velocity\":";
        write_number(output, residual.velocity);
        output << ",\"acceleration\":";
        write_number(output, residual.acceleration);
        output << '}';
    }
    output << "]},\n  \"time_domain\":{\"inlet\":";
    write_signal(output, inlet);
    output << ",\"outlet\":";
    write_signal(output, outlet);
    output << ",\"reference_outlet\":";
    write_signal(output, reference);
    output << "},\n  \"frequency_response\":[";

    const std::size_t frequency_count = response
        ? response->frequency.size() : 0;
    for (std::size_t bin = 0; bin < frequency_count; ++bin) {
        if (bin != 0) {
            output << ',';
        }
        const bool valid = bin < response->valid.size()
            && response->valid[bin] != 0;
        output << "{\"frequency_hz\":";
        write_number(output, response->frequency[bin]);
        output << ",\"valid\":" << (valid ? "true" : "false")
               << ",\"reference_amplitude\":";
        write_number(output, bin < response->referenceAmplitude.size()
            ? response->referenceAmplitude[bin]
            : std::numeric_limits<double>::quiet_NaN());
        output << ",\"outlet_amplitude\":";
        write_number(output, bin < response->amplitude.size()
            ? response->amplitude[bin]
            : std::numeric_limits<double>::quiet_NaN());
        output << ",\"transmission\":";
        write_number(output, bin < response->transmission.size()
            ? response->transmission[bin]
            : std::numeric_limits<double>::quiet_NaN());
        output << ",\"attenuation_db\":";
        write_number(output, bin < response->attenuationDB.size()
            ? response->attenuationDB[bin]
            : std::numeric_limits<double>::quiet_NaN());
        output << ",\"phase_rad\":";
        write_number(output, bin < response->phase.size()
            ? response->phase[bin]
            : std::numeric_limits<double>::quiet_NaN());
        output << '}';
    }
    output << "],\n  \"geometry\":{\"solid_infill_fraction\":";
    write_number(output, solid_infill_fraction);
    output << ",\"dimension\":" << mesh.Dimension()
           << ",\"space_dimension\":" << mesh.SpaceDimension()
           << ",\"vertices\":[";
    for (int vertex = 0; vertex < mesh.GetNV(); ++vertex) {
        if (vertex != 0) {
            output << ',';
        }
        output << '[';
        const double* position = mesh.GetVertex(vertex);
        for (int coordinate = 0; coordinate < mesh.SpaceDimension(); ++coordinate) {
            if (coordinate != 0) {
                output << ',';
            }
            write_number(output, position[coordinate]);
        }
        output << ']';
    }
    output << "],\"elements\":[";
    mfem::Array<int> vertices;
    for (int element = 0; element < mesh.GetNE(); ++element) {
        if (element != 0) {
            output << ',';
        }
        mesh.GetElementVertices(element, vertices);
        output << "{\"attribute\":" << mesh.GetAttribute(element)
               << ",\"geometry\":"
               << static_cast<int>(mesh.GetElementBaseGeometry(element))
               << ",\"vertices\":[";
        for (int vertex = 0; vertex < vertices.Size(); ++vertex) {
            if (vertex != 0) {
                output << ',';
            }
            output << vertices[vertex];
        }
        output << "]}";
    }
    output << "],\"design_values\":";
    write_vector(output, geometry.design);
    output << ",\"filtered_phi\":";
    write_vector(output, geometry.phi);

    std::vector<unsigned char> active(
        static_cast<std::size_t>(geometry.design.Size()), 0);
    for (int index = 0; index < geometry.activeDesignDofs.Size(); ++index) {
        const int dof = geometry.activeDesignDofs[index];
        if (dof >= 0 && dof < geometry.design.Size()) {
            active[static_cast<std::size_t>(dof)] = 1;
        }
    }
    output << ",\"active_design_mask\":";
    write_numbers(output, active);
    output << "}\n}\n";
    return output.str();
}

bool add_entry(zip_t* archive,
               const char* name,
               std::string_view contents)
{
    if (zip_entry_open(archive, name) != 0) {
        return false;
    }
    const bool written = zip_entry_write(
        archive, contents.data(), contents.size()) == 0;
    const bool closed = zip_entry_close(archive) == 0;
    return written && closed;
}

std::string timestamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream value;
    value << std::put_time(&local, "%Y%m%d-%H%M%S");
    return value.str();
}

} // namespace

Exporter::Exporter(const AppSettings& settings,
                   const SolverResult& result,
                   const LevelSet& geometry,
                   const LogFunction& log)
    : settings(settings),
      result(result),
      geometry(geometry),
      log(log)
{
}

bool Exporter::exportRunData(const std::filesystem::path& directory,
                             OptimizerStatus status,
                             int iteration,
                             double pass,
                             double stop,
                             double mmaBound)
{
    std::filesystem::path temporary_path;
    try {
        if (!std::filesystem::is_directory(directory)) {
            throw std::runtime_error("The selected export path is not a directory.");
        }

        const std::string stem = "metamaterial-run-" + timestamp();
        std::filesystem::path archive_path;
        for (int suffix = 0;; ++suffix) {
            const std::string filename = stem
                + (suffix == 0 ? "" : "-" + std::to_string(suffix))
                + ".zip";
            archive_path = directory / filename;
            temporary_path = archive_path;
            temporary_path += ".tmp";
            if (!std::filesystem::exists(archive_path)
                && !std::filesystem::exists(temporary_path)) {
                break;
            }
        }

        const std::string json = make_json(
            settings, result, geometry, status, iteration,
            pass, stop, mmaBound);
        int archive_error = 0;
        zip_t* archive = zip_openwitherror(
            temporary_path.string().c_str(),
            ZIP_DEFAULT_COMPRESSION_LEVEL,
            'w',
            &archive_error);
        if (archive == nullptr) {
            const char* description = zip_strerror(archive_error);
            throw std::runtime_error(
                "Could not create the temporary ZIP archive: "
                + std::string(description != nullptr
                    ? description : "unknown ZIP error"));
        }

        const bool json_written = add_entry(archive, "response.json", json);
        const bool python_written = add_entry(
            archive, "metamaterial_response.py", ExportTemplates::Python);
        const bool matlab_written = add_entry(
            archive, "Metamaterial.m", ExportTemplates::Matlab);
        zip_close(archive);
        if (!json_written) {
            throw std::runtime_error(
                "Could not write response.json to the ZIP archive.");
        }
        if (!python_written) {
            throw std::runtime_error(
                "Could not write metamaterial_response.py to the ZIP archive.");
        }
        if (!matlab_written) {
            throw std::runtime_error(
                "Could not write Metamaterial.m to the ZIP archive.");
        }
        if (!std::filesystem::exists(temporary_path)
            || std::filesystem::file_size(temporary_path) == 0) {
            throw std::runtime_error("Could not finish the ZIP archive.");
        }

        archive_error = 0;
        zip_t* completed_archive = zip_openwitherror(
            temporary_path.string().c_str(), 0, 'r', &archive_error);
        if (completed_archive == nullptr) {
            const char* description = zip_strerror(archive_error);
            throw std::runtime_error(
                "Could not verify the closed ZIP archive: "
                + std::string(description != nullptr
                    ? description : "unknown ZIP error"));
        }
        const bool complete = zip_entries_total(completed_archive) == 3;
        zip_close(completed_archive);
        if (!complete) {
            throw std::runtime_error("The ZIP archive is missing an entry.");
        }

        std::error_code rename_error;
        std::filesystem::rename(
            temporary_path, archive_path, rename_error);
        if (rename_error) {
            throw std::runtime_error(
                "Could not finalize the ZIP archive: "
                + rename_error.message());
        }

        log(LogLevel::Message,
            "Run-data bundle exported to " + archive_path.string());
        return true;
    }
    catch (const std::exception& error) {
        if (!temporary_path.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary_path, ignored);
        }
        log(LogLevel::Error,
            "Run-data export failed: " + std::string(error.what()));
        return false;
    }
}

bool Exporter::exportMesh()
{
    // TODO: Export the extruded solid mesh and its JSON manifest.

    // Exporter implementation roadmap:
    //
    // 1. Read geometry.phi and reconstruct its background mesh from the settings.
    //    Only visit elements classified as the design domain. The inlet and outlet
    //    are simulation space, not part of the manufactured object.
    //
    // 2. Support a native 2D export path:
    //    - extract closed phi == 0 contours from the 2D design elements;
    //    - save a planar surface for tools that accept 2D geometry; or
    //    - give it a very small configurable thickness when a closed printable
    //      triangle shell is required.
    //
    // 3. Support an extruded-2D path:
    //    - copy the 2D level-set field through a chosen z thickness;
    //    - treat the result as a 3D implicit field;
    //    - send it through the same surface-meshing path as a native 3D result.
    //
    // 4. Support a direct-3D path:
    //    - extract the phi == 0 surface only inside the 3D design region;
    //    - marching cubes/tetrahedra is one possible extractor, not a requirement;
    //    - the extruded-2D and direct-3D paths should both produce the same small
    //      intermediate surface mesh: vertices plus indexed triangles.
    //
    // 5. Finish every surface through one shared cleanup/export path:
    //    - weld shared vertices and remove degenerate triangles;
    //    - use consistent outward winding and close/cap design-space boundaries;
    //    - verify that printable output is watertight and manifold;
    //    - convert the surface to an Assimp scene and write the selected format;
    //    - write a JSON manifest containing units, dimensions, settings, and enough
    //      metadata to reproduce which run/design produced the mesh.
    //
    // 6. Run-data export remains separate from geometry export and is implemented
    //    by exportRunData().

    log(LogLevel::Warning, "Mesh export is not implemented yet.");
    return false;
}

} // namespace App
