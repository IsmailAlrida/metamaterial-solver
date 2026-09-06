# Manufacturing export and preview plan

Created: 2026-09-02 17:47:41 +04:00 (Asia/Dubai)

Status: architecture review; no implementation started.

## Objective

Turn a completed 2D Q1 level-set solution into a geometrically faithful,
validated, printable duct assembly. The same generated triangle data must drive
both the in-app 3D preview and file export so the preview cannot disagree with
the exported artifact.

The first deliverable is the manufacturing core and exporter. UI work follows
after the geometry path passes deterministic tests.

## Current findings

1. `LevelSet` currently contains only `design`, `phi`, and active DOFs. It does
   not retain the grid extents, resolution, design bounds, units, or a geometry
   revision.
2. `Exporter::make_json()` reconstructs a Cartesian MFEM mesh from the live
   `AppSettings`. This is unsafe after a run: the UI can change a physical
   length without changing the DOF count, causing a valid-size but incorrectly
   scaled export.
3. `Exporter::exportMesh()` is only a roadmap and has no destination argument.
   The dispatcher currently exports only the run-data ZIP.
4. The renderer receives settings and solver results, but not the `LevelSet`.
   Its visualization is the solver field streamed into embedded GLVis, not the
   final manufactured assembly.
5. Assimp is configured with all exporters disabled by default and only the 3MF
   exporter explicitly enabled. Binary STL must be explicitly enabled before
   relying on Assimp's `stlb` writer.
6. The paper deliberately imposes no minimum length scale and explicitly lists
   free structures/connectivity as unfinished manufacturing work. The exporter
   cannot honestly guarantee printability by silently changing those features.

## Architecture decision

Do not use marching cubes for the current 2D path. Do not add OpenCASCADE just
to create STL.

Use this pipeline:

```text
immutable LevelSet snapshot
    -> Q1 zero-contour extraction in the 2D design region
    -> normalized 2D cross-section (outer loops and holes)
    -> printable tray body and separate lid
    -> robust extrusion and Boolean union
    -> validated indexed TriangleMesh
       -> 3D preview
       -> binary STL part files
       -> generic 3MF assembly
       -> manufacturing report/manifest
```

Add the Manifold geometry library as the one new geometry dependency. Its
`CrossSection`, `Extrude`, primitive, Boolean, decomposition, and status APIs
cover the required polygon cleanup, triangulation, wall construction, union,
and manifold checks. Keep Assimp as the file-format adapter.

OpenCASCADE becomes justified only when editable STEP/B-Rep is a committed
output. A B-Rep-to-STL detour does not improve the sampled `phi == 0` contour;
STL still ends as triangles. The neutral contour and assembly pipeline below
will allow an OCCT writer to be added later without changing the solver.

## Geometry contract

Extend `LevelSet` with the immutable provenance required to interpret `phi`:

- spatial dimension;
- Cartesian element counts;
- physical origin and extents in metres;
- design-region x bounds;
- FE order/basis identifier (Q1 for the current implementation);
- monotonically increasing geometry revision.

The solver sets this metadata when it builds the mesh and updates the revision
only when a complete filtered `phi` is published. The exporter must never infer
geometry from mutable current UI settings.

MFEM remains responsible for DOF ordering. Reconstruct the matching MFEM Q1
space from the saved grid descriptor and load `phi` with
`GridFunction::SetFromTrueDofs`; do not assume x-fastest vector ordering.

## Faithful 2D contour extraction

The simulated boundary is the zero set of the continuous Q1 field, not a binary
pixel mask. Thresholding nodes or cells would lose the CutFEM geometry.

For every quadrilateral in the saved design region:

1. Read the four Q1 nodal values through the MFEM finite-element space.
2. Find edge crossings by linear interpolation; these crossings are exact on
   Q1 element edges.
3. Resolve four-crossing saddle cases with the asymptotic decider.
4. Adaptively sample each bilinear zero-contour branch until its chord error is
   below the requested contour tolerance.
5. Stitch segments into closed, oriented loops and classify holes.
6. Include solid portions that meet the design-domain boundary, rather than
   discarding open contours.

All geometry calculations remain in metres. Convert once to millimetres in the
manufacturing model because STL has no units and slicers conventionally treat
coordinates as millimetres; 3MF will explicitly declare millimetres.

## Printable assembly

The default product should be a wall-connected, constant-depth insert:

- **Structural insert:** two lateral duct walls and every metamaterial feature
  extruded through the selected depth. Every feature is joined to at least one
  lateral wall by the optimized 2D geometry.
- **Base/lid:** optional separate enclosure or visualization parts. They may
  close the acoustic duct but are not Boolean-unioned or treated as structural
  supports for the optimized material.
- **Inlet and outlet:** no end caps; both x ends remain open.
- **Internal cavity:** exactly the simulated x-y duct rectangle. Housing walls
  grow outward so they do not shrink the simulated acoustic domain.

Printing along the extrusion direction puts every constant cross-section on the
build plate. After removal, the wall connections retained from the simulation
support the material; no exporter-invented base plate or support strut changes
the optimized structure.

The first assembly deliberately omits snap fits, screws, gaskets, flanges, and
printer profiles. Add those only after the first body/lid prototype establishes
the needed fit and seal.

## Manufacturing settings

Add the minimum physical controls to `ExporterSettings`, stored in metres and
displayed in millimetres:

- flow/extrusion depth;
- lateral wall thickness;
- optional base/lid thickness and non-structural clearance (calibration knobs
  are required for real printers);
- contour chord tolerance;
- minimum printable feature used for validation and warnings.

Do not silently thicken, delete, bridge, or offset the optimized material.
Offer an exact export and report violations. Any geometry-changing cleanup must
be an explicit operation followed by re-analysis before it is called a verified
design.

## Shared manufacturing types

Keep one small neutral representation shared by the renderer and exporter:

```cpp
struct TriangleMesh {
    std::vector<std::array<double, 3>> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};

struct PrintablePart {
    std::string name;
    TriangleMesh mesh;
};

struct PrintableAssembly {
    std::vector<PrintablePart> parts;
    ManufacturingReport report;
};
```

A pure `buildPrintableAssembly(levelSetSnapshot, exporterSettings)` function
creates this result. `Exporter` writes it; `Renderer` previews it. Neither side
reconstructs or modifies geometry independently.

## Export behavior

The manufacturing export action should write atomically:

- one generic `.assembly.3mf` containing named body and lid objects, explicit
  millimetre units, and run metadata;
- one binary STL per physical part for the explicitly requested STL workflow;
- one JSON manufacturing report containing source revision, dimensions,
  tolerance, triangle counts, checks, warnings, and hashes;
- the existing run-data bundle, preserved as a separate artifact or linked by
  identifier.

3MF is the preferred handoff because it preserves units and assembly objects.
STL remains supported because it is universally accepted, but it cannot encode
units or an assembly hierarchy.

## Validation gates

Export fails on geometry errors and warns on manufacturing limits.

Hard failures:

- non-finite coordinates;
- empty body or lid;
- degenerate or duplicate triangles after cleanup;
- inconsistent winding or non-positive signed volume;
- any undirected triangle edge not incident to exactly two faces;
- Manifold status other than `NoError`;
- body not a single connected solid after union;
- a cap closing the inlet or outlet;
- output bounds inconsistent with configured dimensions/tolerance.

Warnings:

- features or gaps below the configured printable minimum;
- contour approximation tolerance larger than the printable minimum;
- triangle count unusually high;
- physical model caveat: the rear plate constrains the extrusion in a way the
  present 2D structural model does not explicitly resolve.

## Manufacturing-aware optimization

Recorded: 2026-09-03 09:51:46 +04:00 (Asia/Dubai)

Treat four requirements separately. They are not interchangeable:

1. minimum solid member thickness;
2. minimum void/channel width;
3. connectivity of the final printed assembly;
4. build-direction overhang/support rules.

Do not constrain connected-component bounding boxes or component diameters.
Those quantities are discontinuous when topology changes, awkward for Ipopt,
and allow a long but unprintably thin member to pass. A component-size rule
also says nothing about whether that component is attached.

The existing Helmholtz/PDE filter remains the regularizer, but increasing its
radius alone is not a length-scale guarantee. A smooth filtered field can still
cross zero twice over a small distance. Add differentiable geometric
constraints on top of the existing filter, following Zhou et al. rather than
replacing the CutFEM material model.

For the current mapping, let

```text
q_i = 1/2 + filtered_center_i / level_set_scale
rho_i = H_beta(q_i; eta_i),   eta_i = 1/2
```

where `q` is the normalized cell-centred field already computed by
`Solver::smooth_level_set` and `H_beta` is used only as a smooth phase indicator
for manufacturing constraints. The simulated geometry remains the sharp Q1
zero contour `phi == 0`.

Use the two geometric constraints from the three-field formulation:

```text
I_s = rho       exp(-c |grad q|^2)
I_v = (1 - rho) exp(-c |grad q|^2)

g_s = mean(I_s [min(q - eta_e, 0)]^2) <= epsilon_s
g_v = mean(I_v [min(eta_d - q, 0)]^2) <= epsilon_v
```

`g_s` suppresses undersized solid members and `g_v` suppresses undersized voids.
The effective physical sizes depend jointly on filter radius and the threshold
interval `(eta_d, eta_e)`. The published calibration is for a linear hat
filter, while this repository uses a Helmholtz finite-volume filter. Therefore
the UI must not label `filterRadius` as a guaranteed wall thickness. Calibrate
the mapping in metres with deterministic bar, ligament, hole, and slot cases,
and have the final contour validator measure it independently.

For the chosen top-down product architecture, require every 2D solid component
to join at least one of the two lateral duct walls. It need not touch both. The
manufactured solid is the Cartesian extrusion of that wall-connected footprint
through the duct height. Optional base/lid parts are not structural anchors for
the optimized material.

The exporter verifies the 2D wall-contact rule before extrusion and the 3D
component/contact rule afterwards. It must also verify wall thicknesses and the
open inlet/outlet. The actual mounting assumption is quantified by the mandatory
extruded-3D forward solve below; it must not be hidden by the exporter.

Keep the robust eroded/nominal/dilated formulation as an optional later mode for
performance under manufacturing variation. It requires multiple full
vibroacoustic forward/adjoint analyses per iteration and is not needed merely
to enforce member/gap size. Also, `phi` is not maintained as a signed-distance
field, so `phi +/- delta` must not be presented as an exact offset in metres.

Repository integration sequence:

1. retain/expose the normalized filtered cell field and its spatial gradient;
2. add `g_s` and `g_v` to the existing Ipopt constraint vector, mapping their
   derivatives through the existing transpose node/cell and filter chain;
3. add finite-difference gradient checks plus canonical minimum-thickness and
   minimum-gap fixtures;
4. enable the geometric constraints after a short unconstrained topology
   warm-up and continue the projection sharpness, since imposing them on the
   initial grey field can trap a poor local minimum;
5. add the wall-anchored virtual-temperature inequality and exact component-to-
   either-wall certificate described below;
6. expose requested solid thickness, gap width, extrusion height, and measured
   export values in millimetres; keep numerical threshold/tolerance parameters
   in an advanced panel;
7. for the current constant 2D extrusion printed flat, defer overhang
   optimization: every vertical extrusion is self-supporting and the lid is a
   separate part;
8. run exactly one broadband extruded-3D forward solve for every completed 2D
   candidate before it becomes export-ready. Do not add 3D optimization or 3D
   adjoints at this stage.

Primary references: [geometric minimum-length constraints](https://doi.org/10.1016/j.cma.2015.05.003),
[robust projection formulation](https://doi.org/10.1007/s00158-010-0602-y), and
[virtual-temperature connectivity](https://doi.org/10.1007/s00158-016-1459-5).

## Wall-connected solid mode

Recorded: 2026-09-03 12:40:30 +04:00 (Asia/Dubai)

The geometric contract is: every connected component of
`Omega_s = {phi >= 0}` must intersect an allowed wall set `Gamma_w`. This is
stronger than making the final base/column/lid union printable. It is a selected
product constraint when in-plane wall attachment is desired.

Use a fixed-solid anchor rail of at least the requested minimum thickness beside
each allowed wall. On the smooth phase indicator `rho = H_beta(phi)`, solve one
virtual heat problem:

```text
-div(k(rho) grad T) = q0 rho                 in the design region
T = 0                                        on Gamma_w
n.grad(T) = 0                                on every other boundary
k(rho) = k_void + (k_solid-k_void) rho^p
g_conn = KS(T / T_limit - 1) <= 0
```

Heat is generated only in solid. A component with a solid path to an anchor
rail conducts to the zero-temperature wall; a floating component can escape
only through the deliberately tiny `k_void` and becomes hot. The KS smooth
maximum is preferred over an integral compliance because a small disconnected
component must not disappear inside a domain average. Pair this constraint with
minimum solid thickness so a one-cell hairline connection is not accepted.

For `A(rho) T = b(rho)`, obtain the design gradient with one adjoint:

```text
A(rho)^T lambda = partial(g_conn) / partial(T)
d g_conn / d rho = partial(g_conn) / partial(rho)
                  + lambda^T (partial(b)/partial(rho)
                              - partial(A)/partial(rho) T)
```

Then apply the smooth-Heaviside derivative and the existing transpose PDE-filter
chain to reach the bounded level-set design variables. Add this value as one
more Ipopt inequality; no changes to the vibroacoustic state equations are
required.

A sink on the union of allowed walls means "attached to at least one wall." If
the requirement is instead one solid network spanning two specified walls,
force solid anchor rails at both walls, use one as the sink, and require the
other to belong to the same final component.

The PDE remains a finite-contrast differentiable surrogate. The hard export
certificate operates on the extracted 2D solid polygons before base/lid union:

1. compute their connected components;
2. intersect each component with the allowed wall/anchor geometry;
3. reject export if any component has no allowed intersection;
4. if wall-to-wall spanning is required, reject unless one component intersects
   every required anchor rail.

Do not place component labels, bounding boxes, or flood-fill results inside
Ipopt: they jump discontinuously when a neck appears. Do not start with a
structural-eigenfrequency constraint either; repeated/mode-switching
eigenvalues and fictitious-domain modes make it more expensive and fragile.
A weighted graph-Laplacian eigenvalue is a valid later alternative if the heat
surrogate proves inadequate, but it is not the first implementation.

## Top-down extrusion and 2D-to-3D fidelity

Recorded: 2026-09-03 12:07:57 +04:00 (Asia/Dubai)

Product coordinates are `x` along the duct, `y` across the top-down footprint,
and `z` through the exported height `H`. The export geometry is

```text
phi_3D(x, y, z) = phi_2D(x, y),  0 <= z <= H,
Omega_s,3D = Omega_s,2D x [0, H].
```

The operational assembly is a hollow rectangular duct with open inlet/outlet.
The optimized insert is joined only to the two lateral walls. Base/lid pieces
may be generated as separate enclosure or visualization parts, but they do not
join or clamp the resonator structure. In physical operation they still need to
provide the assumed sound-hard acoustic boundary without creating an unintended
structural contact or leakage path.

### Acoustic reduction

For perfectly rigid base/lid walls, a geometry and material distribution that
are independent of `z`, and an inlet field that is uniform in `z`,

```text
p_3D(x, y, z, t) = p_2D(x, y, t)
```

is an exact invariant solution of the 3D acoustic wave equation because
`d p / d z = 0` also satisfies the sound-hard conditions at `z = 0,H`.
The first unmodelled vertical duct mode has cutoff

```text
f_z,1 = c_air / (2 H).
```

Keeping the highest design frequency below this cutoff makes small
manufacturing/source asymmetries evanescent rather than propagating. At
`c_air = 343 m/s` and `f_max = 4 kHz`, this requires `H < 42.9 mm`; practical
design should leave margin and confirm the response with a 3D solve. Above
cutoff, a mathematically perfect uniform model can remain in the constant mode,
but the printed article can excite vertical modes through tolerances, leakage,
wall motion, or a nonuniform source.

### Structural reduction

The current 2D formulation uses plane-stress elasticity, which represents free
out-of-plane stress. Joining every solid to one of the two lateral walls removes
the free-body mismatch, and leaving its `z` faces unbonded avoids artificial
base/lid end clamps. This is substantially closer to the simulated structure.
It is still an approximation for a finite-depth extrusion: through-depth modes,
Poisson deformation, lateral-wall joint detail, and accidental base/lid contact
can make the displacement vary with `z`.

Consequently:

- a design working mainly as a rigid acoustic labyrinth is a strong candidate
  for faithful 2D extrusion;
- a wall-connected moving resonator with free `z` faces is more credible than a
  floating 2D island or a column bonded to both enclosure plates;
- changing extrusion height is not generally a physics-neutral UI operation,
  even though acoustic mass and force both scale with height in the ideal
  invariant model.

The remaining fidelity risks are finite through-depth structural response,
finite plate motion instead of perfect sound-hard walls, FDM anisotropy and
uncertain polymer modulus/damping, seal leakage, surface roughness, and
thermoviscous loss in narrow air gaps.

### Minimum validation path

Do not begin with 3D optimization. Run one broadband transient forward solve for
every completed 2D candidate, then obtain the entire approximate response with
one FFT:

1. use the same Q1 contour and exact extrusion as the exporter;
2. repeat the saved 2D level-set field through `z` on the existing hexahedral
   forward mesh;
3. clamp the two lateral wall anchors used by the 2D problem, leave the
   resonator `z` faces structurally free, and apply sound-hard conditions to the
   acoustic enclosure faces;
4. run the coupled extruded-3D case once at the selected `H`;
5. compare the full transmission curve, pass/stop objective values, resonance
   shifts, and displacement variation through `z` against the 2D result;
6. expose the result as a `2D approximation: verified/warning/failed` report for
   the chosen height and frequency range.

The source already has an unvalidated `nz > 0` forward branch with 3D Lame
coefficients. Reuse and validate that branch, but change its product boundary
mapping: the present hard-coded `z` bottom/top structural clamps do not describe
the lateral-wall-only mount. Do not create another solver, add 3D adjoints, or
sweep multiple 3D realizations at this stage.

### Gravity scope

Recorded: 2026-09-03 13:37:46 +04:00 (Asia/Dubai)

Do not add gravity to the optimizer now. The mandatory minimum member thickness,
connection to at least one lateral wall, and final extruded-3D broadband solve
are the first manufacturable slice. In the current small-deformation linear
model, gravity primarily creates a static offset and does not change the FFT
transfer function unless sag changes geometry, contact, or prestress enough to
make nonlinear effects important.

Revisit gravity only if the chosen material is very soft, a one-wall cantilever
has a long span, creep is relevant, or a prototype visibly sags. The smallest
upgrade is then a post-optimization static solve `K u_g = f_gravity` on the same
extruded 3D model and a clearance limit. If that fails, require two-wall
spanning or add a stiffness/compliance constraint; do not start with a gravity
constraint in every 2D optimization iteration.

References: [the paper's actual publisher-hosted PDF](https://doi.org/10.1016/j.finel.2024.104123),
[plane-stress/plane-strain applicability](https://doc.comsol.com/6.3/doc/com.comsol.help.sme/sme_ug_solid.07.002.html),
and [rectangular-duct cutoff behavior](https://doc.comsol.com/6.4/doc/com.comsol.help.aco/aco_ug_pipe.12.18.html).

## Tests before UI

Add one focused manufacturing check executable covering:

- half-plane and diagonal Q1 cuts;
- an analytic circle with measured contour/area error below tolerance;
- an ambiguous saddle cell;
- a hole;
- material touching a design boundary;
- several 2D components, each touching at least one of the two lateral walls,
  pass while one floating component fails;
- optional base/lid meshes remain separate from the resonator insert;
- open inlet/outlet and exact cavity dimensions;
- binary STL and 3MF written, reopened, and non-empty;
- deterministic mesh hashes for identical inputs;
- stale live settings cannot rescale a saved level-set snapshot.

The implementation is not complete until an exported sample imports cleanly in
Bambu Studio and slices without automatic repair. Automate that smoke test only
when a stable Bambu Studio CLI is available in CI; do not make it a build
dependency.

## UI plan after the exporter passes

Add a **Manufacture** mode/window available only for a completed exportable
geometry revision:

- reuse the existing dockspace;
- show the generated body/lid mesh, not a second approximation;
- pair each ImGui slider with `InputDouble` bound to the same value;
- debounce rebuilds and run them off the frame thread;
- show body/lid visibility toggles, fit view, dimensions, triangle count, and a
  green/warning/error validation summary;
- provide `Export 3MF + STL` as the primary action and keep run-data export as a
  secondary action.

Initially reuse/extend the existing embedded GLVis path if it can accept the
indexed surface cleanly. Add a small dedicated OpenGL mesh viewport only if
GLVis blocks part coloring, transparency, or dependable camera behavior.

## Implementation order

1. **Geometry truth:** add `LevelSet` grid provenance/revision and make JSON
   export use it. Add the stale-settings regression check.
2. **Cross-section:** implement and test Q1/asymptotic/adaptive zero-contour
   extraction.
3. **Solid assembly:** pin Manifold, create the wall-connected insert and
   optional separate base/lid, and validate the resulting indexed meshes.
4. **File output:** enable Assimp STL, write binary STL parts plus generic 3MF
   and the manufacturing report atomically.
5. **Dispatcher/UI:** add the manufacturing action, controls, background rebuild,
   progress/error states, and preview of the exact export mesh.
6. **Manufacturing-aware optimization:** add minimum solid/void length scales
   and the mandatory virtual-temperature connection to either lateral wall.
   Re-run the physics after any geometry-changing cleanup.
7. **Extruded-3D verification:** after every completed 2D optimization, run one
   broadband 3D forward solve and FFT on the exact export geometry before
   enabling export. No 3D adjoint or optimization.
8. **Native 3D later:** use adaptive marching cubes/tetrahedra only when `phi`
   is genuinely 3D, then feed that surface into the same Manifold validation,
   preview, and writer stages.

## Explicit non-goals for the first slice

- no OpenCASCADE/STEP;
- no native 3D level-set extractor;
- no automatic support-strut invention;
- no snap-fit or fastener system;
- no custom slicer or G-code generation;
- no second renderer-specific geometry pipeline.

## Decision log

- 2026-09-02 17:47:41 +04:00: Initial repository, paper, dependency, exporter,
  solver, and renderer review recorded. Selected adaptive Q1 contouring plus
  Manifold extrusion/Boolean and a two-part tray assembly. Deferred B-Rep until
  STEP is a real output requirement.
- 2026-09-03 09:51:46 +04:00: Separated minimum member, minimum gap,
  attachment, and overhang requirements. Selected filter-plus-geometric
  constraints for solid/void length scale and a virtual-temperature constraint
  for wall attachment. Rejected component bounding boxes and filter radius
  alone. Deferred robust multi-realization physics and 3D overhang filtering.
- 2026-09-03 12:07:57 +04:00: Corrected the product orientation to a top-down
  2D footprint extruded through the duct height. Superseded mandatory in-plane
  wall connectivity: the base/lid provide printable assembly connectivity.
  Identified plane-stress structural restraint, rather than acoustic extrusion,
  as the dominant fidelity risk and selected post-optimization extruded-3D
  forward verification before any 3D optimization work.
- 2026-09-03 12:40:30 +04:00: Added an explicit wall-connected product mode.
  Selected a wall-anchored virtual-temperature inequality plus minimum member
  thickness for gradient optimization, followed by an exact pre-assembly
  connected-component export certificate. Distinguished attachment to any
  allowed wall from a single network required to span multiple walls.
- 2026-09-03 13:37:46 +04:00: Made wall connectivity part of the first product:
  each component may attach to either lateral wall and need not span both.
  Base/lid are optional separate acoustic enclosure parts and do not support the
  resonators. Made one broadband extruded-3D forward solve plus FFT a mandatory
  export gate, with lateral rather than base/lid structural anchors. Deferred
  gravity until observed sag, soft material, or long cantilevers justify one
  post-optimization static check.
