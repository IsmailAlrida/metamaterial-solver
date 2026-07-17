# Shared level-set data

This document answers one question: **which data owns the geometry, and how is
that data translated for optimization, CutFEM, and export?**

The answer is one renderer-owned `LevelSet`. The optimizer changes it, while
the solver and exporter read it. MFEM, Assimp, and Gmsh objects are temporary
adapter data, not alternative owners of the geometry.

```text
Renderer edits design
        |
Optimizer: design s -> signed map -> PDE filter -> physical phi
        |
Solver adapter: phi -> mfem::GridFunction
        |
MFEM: GridFunctionCoefficient -> AlgoimIntegrationRules
        |
Geometry extractor: phi = 0 -> SurfaceMesh
        |
Assimp or Gmsh -> STL and mesh formats
OpenCASCADE      -> faceted STEP/CAD
```

## 1. The authoritative application type

The target replacement for the current placeholder `LevelSet::grid` is:

```cpp
struct LevelSet
{
    int dim;
    int nx, ny, nz;                // Number of grid nodes, not elements
    std::array<double, 3> origin;  // Physical position of node (0, 0, 0)
    std::array<double, 3> spacing; // Physical distance between adjacent nodes

    std::vector<double> design;    // Optimizer variable s in [0, 1]
    std::vector<double> phi;       // Filtered physical level-set value
};
```

The invariants are deliberately small:

- `design.size() == phi.size() == nx * ny * nz`;
- values use x-fastest storage: `i + nx * (j + ny * k)`;
- node `(i,j,k)` is at `origin + spacing * (i,j,k)`;
- `phi > 0` is structural material and `phi < 0` is acoustic/void;
- `phi == 0` is the implicit material boundary;
- dimensions and vector sizes remain fixed during an optimization or solve.

Use `double`, not an MFEM class, because the renderer and exporter do not need
to depend on MFEM's ownership or degree-of-freedom layout. Conversion to
`mfem::real_t` happens in the solver adapter if the MFEM build uses a different
scalar type.

The intended application contract is reference based:

```cpp
void Optimizer::update(LevelSet& levelSet);
void Solver::solve(const LevelSet& levelSet,
                   const PhysicsProblem& problem,
                   SolverResult& result);
bool Exporter::write(const LevelSet& levelSet, const std::string& path);
```

Renderer owns `LevelSet` and `PhysicsProblem`. These functions borrow them for
one call; they do not store references in constructors. Most application code
is still TODO, so these signatures describe the intended ownership rather than
the current partial class definitions.

## 2. The paper and optimizer representation

The paper distinguishes the **mathematical design** from the **physical level
set**. They must not be collapsed into one vector.

| Paper quantity | Shared field | Meaning |
|---|---|---|
| Mathematical design `s` | `LevelSet::design` | Nodal MMA variables bounded by 0 and 1 |
| Mapped design | Optimizer-local scratch | Signed values scaled to about half an element size |
| Cell-centred mapped/filtered values | Optimizer-local scratch | Intermediate values used by the paper's finite-volume PDE filter |
| Physical design / level set | `LevelSet::phi` | Nodal signed field consumed by CutFEM and export |

The forward geometry update is:

$$
s\in[0,1]
\longrightarrow
\widetilde{s}=h_e(s-0.5)
\longrightarrow
\bar{s}=\phi,
$$

where the final arrow applies the smoothing PDE with homogeneous Neumann
boundary conditions. The paper performs node-to-cell interpolation, solves the
filter at cell centres, and interpolates back to nodes. Only the input `design`
and final `phi` belong in the shared object; matrices and cell-centred arrays
are optimizer-local scratch.

An MFEM implementation may express the analogous nodal Helmholtz filter as

$$
(M_\phi+r^2K_\phi)\phi=M_\phi\widetilde{s},
$$

using `MassIntegrator` and `DiffusionIntegrator`. This is an implementation
choice inside the optimizer, not a change to the shared data contract.

## 3. The MFEM solver representation

Inside the solver, the level set is a scalar, continuous, order-one
`mfem::GridFunction` associated with the same fixed Cartesian background mesh
used by the physics.

```cpp
mfem::Mesh backgroundMesh;
mfem::H1_FECollection levelSetElements(1, levelSet.dim);
mfem::FiniteElementSpace levelSetSpace(
    &backgroundMesh, &levelSetElements);
mfem::GridFunction mfemPhi(&levelSetSpace);
```

These types have separate jobs:

| MFEM type | Job |
|---|---|
| `mfem::Mesh` | Background element connectivity and physical coordinates |
| `mfem::H1_FECollection` | Continuous Q1 finite-element definition |
| `mfem::FiniteElementSpace` | Maps mesh entities to MFEM degrees of freedom |
| `mfem::GridFunction` | Stores nodal `phi` in MFEM degree-of-freedom order |
| `mfem::GridFunctionCoefficient` | Evaluates `phi(x)` inside an element |
| `mfem::AlgoimIntegrationRules` | Produces quadrature on `phi > 0`, `phi < 0`, or `phi == 0` |

The application vector cannot be blindly assigned to a `GridFunction`: MFEM's
degree-of-freedom order is owned by `FiniteElementSpace`. Build a node-to-DOF
map once, then copy each updated field:

```cpp
for (std::size_t node = 0; node < levelSet.phi.size(); ++node)
{
    mfemPhi[nodeToDof[node]] = levelSet.phi[node];
}

mfem::GridFunctionCoefficient phiCoefficient(&mfemPhi);
mfem::AlgoimIntegrationRules cutRules(
    integrationOrder, phiCoefficient, 1);
```

Algoim evaluates the Q1 interpolation inside each background element. A cut is
therefore detected when nodal values change sign even if no node stores exactly
zero. The background mesh remains fixed; only quadrature changes as `phi`
changes. Solver matrices, state vectors, `GridFunction`s, and integration rules
remain solver-owned.

## 4. The export representation

Assimp and Gmsh consume explicit vertices and elements, not an implicit scalar
grid. Export therefore needs one neutral intermediate mesh:

```cpp
struct SurfaceMesh
{
    std::vector<std::array<double, 3>> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};
```

`SurfaceMesh` is derived output, never the authoritative optimized geometry.
Extract it from `LevelSet::phi == 0` using marching squares for a 2D contour or
marching tetrahedra/cubes for a 3D surface. A 2D contour must be closed and
extruded by a chosen thickness before it can become a solid STL.

### Assimp

The adapter creates one `aiScene` containing one triangular `aiMesh`:

| Application data | Assimp data |
|---|---|
| `SurfaceMesh::vertices` | `aiMesh::mVertices` as `aiVector3D[]` |
| `SurfaceMesh::triangles` | `aiMesh::mFaces` as three-index `aiFace`s |
| Mesh index | `aiNode::mMeshes` in the scene root |

`Assimp::Exporter` can then write a supported polygon format such as STL.
Assimp is a mesh/asset interchange library; its `aiScene` is not a CAD B-Rep
and does not recover analytic STEP surfaces.

### Gmsh

The adapter creates a two-dimensional discrete model entity and supplies:

- node tags plus flattened xyz coordinates to `gmsh::model::mesh::addNodes`;
- triangle tags/connectivity to `gmsh::model::mesh::addElementsByType`.

Gmsh can save or remesh that discrete surface. `classifySurfaces` and
`createGeometry` can create piecewise parametrizations for suitable discrete
surfaces, but this remains mesh-derived geometry rather than recovered smooth
CAD.

### STEP/CAD

STL is a direct match for `SurfaceMesh`. STEP requires a boundary
representation. For faceted STEP, convert each triangle into an OpenCASCADE
face, sew the faces into a closed shell, validate a solid, and pass it to
`STEPControl_Writer`. Smooth CAD requires an additional surface-fitting and
healing process; neither Assimp nor a Gmsh discrete mesh supplies that
automatically.

## 5. Final type map

| Boundary | Source | Destination | Copy? |
|---|---|---|---|
| Renderer to optimizer | `LevelSet&` | Same `design` and `phi` vectors | No |
| Optimizer to solver | `const LevelSet&` | Q1 `mfem::GridFunction` | Yes, into MFEM DOF order |
| Grid function to cut integration | `mfem::GridFunction` | `GridFunctionCoefficient` | No, coefficient refers to the grid function |
| Level set to extraction | `const LevelSet&` | `SurfaceMesh` | Yes, zero-isosurface extraction |
| Surface mesh to Assimp | `SurfaceMesh` | `aiScene` / `aiMesh` | Yes |
| Surface mesh to Gmsh | `SurfaceMesh` | Discrete entity nodes/elements | Yes |
| Surface mesh to STEP | `SurfaceMesh` | OpenCASCADE shell/solid | Yes |

The top-level shared object is therefore `LevelSet`. `mfem::GridFunction` is
the solver view of `phi`, and `SurfaceMesh` is the exporter view of `phi == 0`.
Neither view should be stored back into the shared object.

## References

- [Paper notes used by this repository](golden-paper.md)
- [Detailed MFEM CutFEM guide](mfem_cutfem_guide.md)
- [MFEM 4.9 documentation](https://docs.mfem.org/4.9/)
- [MFEM `AlgoimIntegrationRules`](https://docs.mfem.org/4.9/classmfem_1_1AlgoimIntegrationRules.html)
- [Gmsh geometry and mesh data model](https://gmsh.info/doc/texinfo/)
- [Assimp `aiScene` and `aiMesh` usage](https://the-asset-importer-lib-documentation.readthedocs.io/en/latest/usage/use_the_lib.html)
- [OpenCASCADE `STEPControl_Writer`](https://dev.opencascade.org/doc/refman/html/class_s_t_e_p_control___writer.html)
