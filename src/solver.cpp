#include <mfem.hpp>
#include <iostream>
#include <vector>
#include <string> 
#include "solver.hpp"



using namespace std;
using namespace mfem;


App::Solver::Solver( LevelSet& lset, SolverResult& result,
                PhysicsProblem& problem,
                int nx, int ny, int nz,
                string solverAlgo
            )
            : 
            duration(0.2),
            nx(nx),
            ny(ny),
            nz(nz),
            lset(lset),
            result(result),
            problem(problem),
            mesh(nullptr),
            fec(nullptr),
            scalar_fes(nullptr),
            displacement_fes(nullptr),
            fe_order(0),
            level_set_order(0),
            cut_integration_order(0),
            sx(1.0),
            sy(1.0),
            sz(1.0)
            {

                setMesh(nx, ny, nz);
                (void)solverAlgo;
}

App::Solver::~Solver() = default;

//todo: do something about the mixed camelCase and snake_case. Choose one.
bool App::Solver::setMesh(int nx, int ny, int nz, mfem::real_t sx, mfem::real_t sy, mfem::real_t sz) {

    if (nx <= 0 || ny < 0 || nz < 0 || sx <= 0.0 || sy <= 0.0 || sz <= 0.0) {
        return false;
    }

    if (ny == 0 && nz == 0) {
        cout << "1D not yet lol." << endl;
        return false;
    }

    if (nz > 0 && ny == 0) {
        return false;
    }

    scalar_fes.reset();
    displacement_fes.reset();
    fec.reset();

    if (nz > 0) {
        mesh = make_unique<Mesh>(Mesh::MakeCartesian3D(nx, ny, nz, Element::HEXAHEDRON, sx, sy, sz));
    }
    else
    {
        mesh = make_unique<Mesh>(Mesh::MakeCartesian2D(nx, ny, Element::QUADRILATERAL, true, sx, sy));
    }

    this->nx = nx;
    this->ny = ny;
    this->nz = nz;
    this->sx = sx;
    this->sy = sy;
    this->sz = sz;
    fe_order = 1;
    level_set_order = 1;
    cut_integration_order = 4;

    return true;
}

bool App::Solver::assembleSolutionSpace(){

    if (!mesh || fe_order <= 0) {
        return false;
    }

    const int dim = mesh->Dimension();

    fec = make_unique<H1_FECollection>(fe_order, dim);
    scalar_fes = make_unique<FiniteElementSpace>(mesh.get(), fec.get());
    displacement_fes = make_unique<FiniteElementSpace>(mesh.get(), fec.get(), dim, Ordering::byVDIM);

    return true;
}


bool App::Solver::solve(){
    return false;
}

bool App::Solver::bindToGlvis(string host, int port){
    return false;
}

void App::Solver::setSimDuration(float val){
}

void App::Solver::getSimDuration(float val){
    (void)val;
}

void App::Solver::setProblem(string prob){
    (void)prob;
}

void App::Solver::getProblem(string prob){
    (void)prob;
}

bool App::Solver::setup(){

    //todo: think about getting rid of this
    return true;
}