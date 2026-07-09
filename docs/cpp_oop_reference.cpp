// C++ OOP reference for C programmers.
//
// This file is meant to be read top-to-bottom. It is valid C++17 and can be
// compiled by itself:
//
//   cl /EHsc /std:c++17 docs\cpp_oop_reference.cpp
//   g++ -std=c++17 docs/cpp_oop_reference.cpp -o oop_reference
//
// It intentionally uses the standard library because modern C++ OOP is usually
// RAII + value types + small interfaces, not raw malloc/free object systems.

#include "cpp_oop_reference.h" // Quotes mean local/project header first.

#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace oop_reference_header
{

std::string to_string(SolverBackend backend)
{
    switch (backend)
    {
    case SolverBackend::Cpu:
        return "CPU";
    case SolverBackend::Cuda:
        return "CUDA";
    }
    return "Unknown";
}

HeaderMaterial::HeaderMaterial(std::string name, double density, double sound_speed)
    : name_(std::move(name)), density_(density), sound_speed_(sound_speed)
{
}

const std::string& HeaderMaterial::name() const
{
    return name_;
}

double HeaderMaterial::density() const
{
    return density_;
}

double HeaderMaterial::sound_speed() const
{
    return sound_speed_;
}

double HeaderMaterial::impedance() const
{
    return density_ * sound_speed_;
}

void HeaderMaterial::set_density(double density)
{
    if (density > 0.0)
    {
        density_ = density;
    }
}

namespace
{

// Anonymous namespace:
//   This concrete class is visible only inside this .cpp file.
//   The header exposes HeaderSolver, not this implementation detail.
class DemoHeaderSolver final : public HeaderSolver
{
    SolverBackend backend_ = SolverBackend::Cpu;

public:
    explicit DemoHeaderSolver(SolverBackend backend)
        : backend_(backend)
    {
    }

    SolverBackend backend() const override
    {
        return backend_;
    }

    void run() override
    {
        std::cout << "Header-declared solver running on " << to_string(backend_) << "\n";
    }
};

} // namespace

std::unique_ptr<HeaderSolver> make_demo_solver(bool use_cuda)
{
    return std::make_unique<DemoHeaderSolver>(use_cuda ? SolverBackend::Cuda : SolverBackend::Cpu);
}

} // namespace oop_reference_header

namespace oop_reference
{

// -----------------------------------------------------------------------------
// 1. struct vs class
// -----------------------------------------------------------------------------
//
// In C++, struct and class are almost the same.
//
// Difference:
//   struct members default to public.
//   class members default to private.
//
// Use struct for simple data aggregates.
// Use class when invariants/behavior matter.

struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

class Material
{
    // Private by default in class.
    // Outside code cannot directly touch these fields.
    double density_ = 1.0;
    double sound_speed_ = 343.0;

public:
    // Public constructor. Constructors initialize objects.
    Material(double density, double sound_speed)
        : density_(density), sound_speed_(sound_speed)
    {
        // Constructor body runs after member initialization.
        // Prefer initializer lists for fields.
    }

    // const member function:
    //   promises not to modify *this.
    double impedance() const
    {
        return density_ * sound_speed_;
    }

    double density() const
    {
        return density_;
    }

    void set_density(double density)
    {
        // Public setter can enforce invariants.
        if (density > 0.0)
        {
            density_ = density;
        }
    }
};

// -----------------------------------------------------------------------------
// 2. public, private, protected
// -----------------------------------------------------------------------------
//
// public:
//   Part of the type's external API.
//
// private:
//   Implementation detail. Only this class and friends can access it.
//
// protected:
//   Accessible to derived classes. Use sparingly; it couples subclasses to base
//   internals. Prefer private fields + protected/public functions.

class SolverSettings
{
public:
    int max_iterations = 1000;

protected:
    double internal_relaxation_ = 0.8;

private:
    bool debug_enabled_ = false;

public:
    void enable_debug(bool enabled)
    {
        debug_enabled_ = enabled;
    }

    bool debug_enabled() const
    {
        return debug_enabled_;
    }
};

// -----------------------------------------------------------------------------
// 3. Constructors, destructors, and RAII
// -----------------------------------------------------------------------------
//
// RAII = Resource Acquisition Is Initialization.
//
// Put resource ownership in an object. Constructor acquires. Destructor releases.
// Then cleanup happens automatically on every return path, including exceptions.

class TemporaryBuffer
{
    std::vector<double> data_;

public:
    explicit TemporaryBuffer(std::size_t count)
        : data_(count)
    {
        std::cout << "TemporaryBuffer acquired " << data_.size() << " doubles\n";
    }

    ~TemporaryBuffer()
    {
        std::cout << "TemporaryBuffer released\n";
    }

    double* data()
    {
        return data_.data();
    }

    std::size_t size() const
    {
        return data_.size();
    }
};

// C habit:
//   init(&thing);
//   use(&thing);
//   destroy(&thing);
//
// C++ habit:
//   Thing thing;
//   use(thing);
//   cleanup is automatic.

// -----------------------------------------------------------------------------
// 4. The rule of zero/five
// -----------------------------------------------------------------------------
//
// Rule of zero:
//   If your class owns no raw resource directly, define no destructor/copy/move.
//   Let std::vector/std::string/std::unique_ptr do it.
//
// Rule of five:
//   If you manually define one of destructor, copy constructor, copy assignment,
//   move constructor, move assignment, you probably need to think about all five.

class RuleOfZeroExample
{
    std::string name_;
    std::vector<double> values_;

public:
    RuleOfZeroExample(std::string name, std::vector<double> values)
        : name_(std::move(name)), values_(std::move(values))
    {
    }

    // No destructor.
    // No copy constructor.
    // No copy assignment.
    // No move constructor.
    // No move assignment.
    //
    // The compiler-generated ones are correct because std::string and
    // std::vector already manage their own memory.
};

class NonCopyableGpuHandle
{
    int fake_handle_ = -1;

public:
    explicit NonCopyableGpuHandle(int fake_handle)
        : fake_handle_(fake_handle)
    {
    }

    ~NonCopyableGpuHandle()
    {
        if (fake_handle_ != -1)
        {
            std::cout << "release fake handle " << fake_handle_ << "\n";
        }
    }

    // Disable copying. Two objects must not own the same handle.
    NonCopyableGpuHandle(const NonCopyableGpuHandle&) = delete;
    NonCopyableGpuHandle& operator=(const NonCopyableGpuHandle&) = delete;

    // Enable moving. Ownership transfers from other to this.
    NonCopyableGpuHandle(NonCopyableGpuHandle&& other) noexcept
        : fake_handle_(other.fake_handle_)
    {
        other.fake_handle_ = -1;
    }

    NonCopyableGpuHandle& operator=(NonCopyableGpuHandle&& other) noexcept
    {
        if (this != &other)
        {
            fake_handle_ = other.fake_handle_;
            other.fake_handle_ = -1;
        }
        return *this;
    }
};

// -----------------------------------------------------------------------------
// 5. Inheritance
// -----------------------------------------------------------------------------
//
// Inheritance means "is-a".
//
// A GmresSolver is-a LinearSolver.
// A Material is not a Solver; do not inherit just to share code.
//
// Prefer composition unless runtime polymorphism is actually useful.

class LinearSolver
{
public:
    // Virtual destructor:
    //   If a class has virtual functions and may be deleted via base pointer,
    //   give it a virtual destructor.
    virtual ~LinearSolver() = default;

    // Pure virtual function:
    //   "= 0" means derived classes must implement this.
    //   This makes LinearSolver an abstract base class/interface.
    virtual void solve() = 0;

    // Virtual non-pure function:
    //   Derived classes may override this, but do not have to.
    virtual std::string name() const
    {
        return "LinearSolver";
    }

    // Non-virtual function:
    //   Calls are statically bound. Derived classes should not try to change
    //   this behavior polymorphically.
    void print_name() const
    {
        std::cout << name() << "\n";
    }
};

class CpuGmresSolver final : public LinearSolver
{
    int restart_ = 30;

public:
    explicit CpuGmresSolver(int restart)
        : restart_(restart)
    {
    }

    // override:
    //   Asks the compiler to verify this really overrides a base virtual.
    //   Always use override when overriding virtual functions.
    void solve() override
    {
        std::cout << "CPU GMRES solve, restart = " << restart_ << "\n";
    }

    std::string name() const override
    {
        return "CpuGmresSolver";
    }
};

class CudaGmresSolver final : public LinearSolver
{
public:
    void solve() override
    {
        std::cout << "CUDA GMRES solve\n";
    }

    std::string name() const override
    {
        return "CudaGmresSolver";
    }
};

// final:
//   On a class: cannot be inherited from.
//   On a virtual function: cannot be overridden further.
//
// Use final when you intentionally close extension.

// -----------------------------------------------------------------------------
// 6. Virtual dispatch
// -----------------------------------------------------------------------------
//
// Virtual dispatch happens through pointer/reference to base.
//
// Important:
//   Base object by value causes slicing.
//   Base pointer/reference preserves dynamic type.

void run_solver(LinearSolver& solver)
{
    // Calls the derived implementation at runtime.
    solver.print_name();
    solver.solve();
}

// Bad pattern:
//
//   void run_solver_by_value(LinearSolver solver);
//
// This cannot work for abstract base classes, and with concrete bases it slices
// derived fields away.

// -----------------------------------------------------------------------------
// 7. Owning polymorphic objects
// -----------------------------------------------------------------------------
//
// In C, you may store void* plus function pointers.
// In C++, use a base class plus std::unique_ptr when one owner exists.

std::unique_ptr<LinearSolver> make_solver(bool use_cuda)
{
    if (use_cuda)
    {
        return std::make_unique<CudaGmresSolver>();
    }
    return std::make_unique<CpuGmresSolver>(30);
}

// unique_ptr:
//   Exclusive owner. Automatically deletes the object.
//
// shared_ptr:
//   Shared ownership. Use only when ownership is truly shared.
//
// raw pointer:
//   Non-owning view, or interop with C APIs.

// -----------------------------------------------------------------------------
// 8. Composition
// -----------------------------------------------------------------------------
//
// Composition means "has-a".
//
// A Simulation has-a solver.
// A Simulation has-a material.
//
// This is usually simpler than inheritance.

class Simulation
{
    Material material_;
    std::unique_ptr<LinearSolver> solver_;

public:
    Simulation(Material material, std::unique_ptr<LinearSolver> solver)
        : material_(std::move(material)), solver_(std::move(solver))
    {
    }

    void run()
    {
        std::cout << "Material impedance = " << material_.impedance() << "\n";
        solver_->solve();
    }
};

// -----------------------------------------------------------------------------
// 9. static members
// -----------------------------------------------------------------------------
//
// static data/function belongs to the class, not to one object.

class InstanceCounter
{
    inline static int count_ = 0; // inline static is C++17.

public:
    InstanceCounter()
    {
        ++count_;
    }

    ~InstanceCounter()
    {
        --count_;
    }

    static int count()
    {
        return count_;
    }
};

// -----------------------------------------------------------------------------
// 10. const correctness
// -----------------------------------------------------------------------------
//
// const is part of C++ API design.
//
//   const Thing&  means read-only borrowed object.
//   Thing&        means mutable borrowed object.
//   Thing         means copy/move by value.

void print_material(const Material& material)
{
    // Can call only const member functions.
    std::cout << "density = " << material.density() << "\n";
}

void mutate_material(Material& material)
{
    material.set_density(2.0);
}

// -----------------------------------------------------------------------------
// 11. references
// -----------------------------------------------------------------------------
//
// References are aliases.
//
// C:
//   void f(Material* m);
//
// C++:
//   void f(Material& m);        // required, mutable, non-null by convention
//   void f(const Material& m);  // required, read-only, non-null by convention
//   void f(Material* m);        // optional/null allowed or C interop

// -----------------------------------------------------------------------------
// 12. overloads and default arguments
// -----------------------------------------------------------------------------
//
// C++ allows multiple functions with the same name if parameter types differ.

void log_value(int value)
{
    std::cout << "int: " << value << "\n";
}

void log_value(double value)
{
    std::cout << "double: " << value << "\n";
}

void run_iterations(int count, bool verbose = false)
{
    if (verbose)
    {
        std::cout << "running " << count << " iterations\n";
    }
}

// -----------------------------------------------------------------------------
// 13. explicit
// -----------------------------------------------------------------------------
//
// explicit prevents accidental implicit construction.

class Frequency
{
    double hz_ = 0.0;

public:
    explicit Frequency(double hz)
        : hz_(hz)
    {
    }

    double hz() const
    {
        return hz_;
    }
};

void set_frequency(Frequency frequency)
{
    std::cout << "frequency = " << frequency.hz() << " Hz\n";
}

// Without explicit, this would compile:
//
//   set_frequency(1000.0);
//
// With explicit, caller must be clear:
//
//   set_frequency(Frequency(1000.0));

// -----------------------------------------------------------------------------
// 14. friend
// -----------------------------------------------------------------------------
//
// friend grants access to private members.
// Use rarely. It is useful for operators and tightly-coupled helpers.

class SecretValue
{
    int value_ = 42;

    friend std::ostream& operator<<(std::ostream& os, const SecretValue& secret);
};

std::ostream& operator<<(std::ostream& os, const SecretValue& secret)
{
    return os << secret.value_;
}

// -----------------------------------------------------------------------------
// 15. Operator overloads
// -----------------------------------------------------------------------------
//
// Use only when the operator is obvious.
// Good: Vec3 + Vec3.
// Bad: solver << mesh means run simulation? Too clever.

Vec3 operator+(const Vec3& a, const Vec3& b)
{
    return Vec3{a.x + b.x, a.y + b.y, a.z + b.z};
}

// -----------------------------------------------------------------------------
// 16. Templates
// -----------------------------------------------------------------------------
//
// Templates are compile-time generic code.
//
// They are not OOP by themselves, but modern C++ uses templates heavily.
// Use templates when the type is known at compile time.
// Use virtual interfaces when the type changes at runtime.

template <typename T>
T clamp_value(T value, T low, T high)
{
    if (value < low)
    {
        return low;
    }
    if (high < value)
    {
        return high;
    }
    return value;
}

// -----------------------------------------------------------------------------
// 17. enum class
// -----------------------------------------------------------------------------
//
// C enum leaks names and converts to int easily.
// enum class is scoped and type-safe.

enum class Backend
{
    Cpu,
    Cuda
};

Backend choose_backend(bool use_cuda)
{
    return use_cuda ? Backend::Cuda : Backend::Cpu;
}

// -----------------------------------------------------------------------------
// 18. Namespaces
// -----------------------------------------------------------------------------
//
// namespace prevents name collisions.
//
// Avoid "using namespace std;" in headers.
// In .cpp files it is less dangerous, but explicit std:: is still clear.

// -----------------------------------------------------------------------------
// 19. Header/source split
// -----------------------------------------------------------------------------
//
// Typical C++ project layout:
//
//   solver.h
//     class declarations, public API.
//
//   solver.cpp
//     function definitions, implementation details.
//
// Include guards or #pragma once prevent double inclusion:
//
//   #pragma once
//
// In headers, include the minimum required headers and forward declare when sane.

// -----------------------------------------------------------------------------
// 20. What matters most
// -----------------------------------------------------------------------------
//
// For this project, the useful OOP pattern is probably:
//
//   class Solver {
//   public:
//       virtual ~Solver() = default;
//       virtual void solve() = 0;
//   };
//
//   class CpuSolver final : public Solver { ... };
//   class CudaSolver final : public Solver { ... };
//
//   std::unique_ptr<Solver> make_solver(bool use_cuda);
//
// Do not build a giant inheritance tree. Use plain structs for data, classes for
// invariants/resource ownership, and one small virtual interface where runtime
// backend switching actually matters.

void demo()
{
    oop_reference_header::HeaderMaterial header_air("air", 1.225, 343.0);
    std::cout << "Header material " << header_air.name()
              << " impedance = " << header_air.impedance() << "\n";
    auto header_solver = oop_reference_header::make_demo_solver(true);
    header_solver->run();

    Material air(1.225, 343.0);
    print_material(air);
    mutate_material(air);
    print_material(air);

    TemporaryBuffer buffer(8);
    buffer.data()[0] = 1.0;

    auto solver = make_solver(false);
    run_solver(*solver);

    Simulation sim(Material(1.225, 343.0), make_solver(true));
    sim.run();

    InstanceCounter a;
    InstanceCounter b;
    std::cout << "instances = " << InstanceCounter::count() << "\n";

    log_value(3);
    log_value(3.14);
    run_iterations(10, true);

    set_frequency(Frequency(1000.0));

    SecretValue secret;
    std::cout << "secret = " << secret << "\n";

    Vec3 p{1.0, 2.0, 3.0};
    Vec3 q{4.0, 5.0, 6.0};
    Vec3 r = p + q;
    std::cout << "r = (" << r.x << ", " << r.y << ", " << r.z << ")\n";

    std::cout << "clamp = " << clamp_value(12, 0, 10) << "\n";

    Backend backend = choose_backend(true);
    if (backend == Backend::Cuda)
    {
        std::cout << "selected CUDA backend\n";
    }
}

} // namespace oop_reference

int main()
{
    oop_reference::demo();
    return 0;
}
