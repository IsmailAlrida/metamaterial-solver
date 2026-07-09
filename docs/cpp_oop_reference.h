#pragma once

// Header file example.
//
// A header is the public contract other .cpp files include.
//
// Put in headers:
//   - class/struct declarations
//   - function declarations
//   - enum declarations
//   - small inline/template definitions
//
// Usually avoid in headers:
//   - large function bodies
//   - private helper functions only used by one .cpp
//   - "using namespace std;"
//   - heavy includes when a forward declaration is enough
//
// #pragma once prevents this file from being included twice in one translation
// unit. Traditional include guards are also valid:
//
//   #ifndef CPP_OOP_REFERENCE_H
//   #define CPP_OOP_REFERENCE_H
//   ...
//   #endif

#include <memory>
#include <string>

namespace oop_reference_header
{

// enum class is safe to expose in a header: it is small, scoped, and typed.
enum class SolverBackend
{
    Cpu,
    Cuda
};

// Function declaration.
//
// This tells other .cpp files the function exists.
// The function body is in cpp_oop_reference.cpp.
std::string to_string(SolverBackend backend);

// Class declaration.
//
// The class layout must be visible to code that creates it by value, so private
// fields appear in the header. They are visible, but not accessible.
class HeaderMaterial
{
public:
    // Constructor declaration.
    HeaderMaterial(std::string name, double density, double sound_speed);

    // Public API declarations.
    const std::string& name() const;
    double density() const;
    double sound_speed() const;
    double impedance() const;
    void set_density(double density);

private:
    // Private implementation state.
    //
    // Naming convention: trailing underscore marks member variables.
    std::string name_;
    double density_ = 1.0;
    double sound_speed_ = 343.0;
};

// Abstract interface declaration.
//
// This header lets app code depend on "something that can run" without knowing
// the implementation class.
class HeaderSolver
{
public:
    virtual ~HeaderSolver() = default;
    virtual SolverBackend backend() const = 0;
    virtual void run() = 0;
};

// Factory declaration.
//
// The caller sees HeaderSolver, not the concrete class hidden in the .cpp.
//
// std::unique_ptr is part of the API, so <memory> is included in this header.
std::unique_ptr<HeaderSolver> make_demo_solver(bool use_cuda);

} // namespace oop_reference_header
