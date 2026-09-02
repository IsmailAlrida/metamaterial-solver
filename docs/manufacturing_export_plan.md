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

The default product should be a two-part, support-free tray assembly:

- **Body:** rear/base plate, top wall, bottom wall, and every metamaterial
  feature extruded through the selected flow depth and unioned into the base.
- **Lid:** a separate flat front plate.
- **Inlet and outlet:** no end caps; both x ends remain open.
- **Internal cavity:** exactly the simulated x-y duct rectangle. Housing walls
  grow outward so they do not shrink the simulated acoustic domain.

Printing the body with its base on the build plate turns every disconnected 2D
blob into a vertical rib rooted in the base. This removes literal floating
parts and avoids support material inside the acoustic path without inventing
in-plane support struts that would alter the simulated response.

The first assembly deliberately omits snap fits, screws, gaskets, flanges, and
printer profiles. Add those only after the first body/lid prototype establishes
the needed fit and seal.

## Manufacturing settings

Add the minimum physical controls to `ExporterSettings`, stored in metres and
displayed in millimetres:

- flow/extrusion depth;
- wall and lid thickness;
- lid fit clearance (a calibration knob is required for real printers);
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

## Tests before UI

Add one focused manufacturing check executable covering:

- half-plane and diagonal Q1 cuts;
- an analytic circle with measured contour/area error below tolerance;
- an ambiguous saddle cell;
- a hole;
- material touching a design boundary;
- multiple disconnected 2D islands becoming one connected tray body;
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
3. **Solid assembly:** pin Manifold, create the tray body/lid, and validate the
   resulting indexed meshes.
4. **File output:** enable Assimp STL, write binary STL parts plus generic 3MF
   and the manufacturing report atomically.
5. **Dispatcher/UI:** add the manufacturing action, controls, background rebuild,
   progress/error states, and preview of the exact export mesh.
6. **Manufacturing-aware optimization:** add minimum solid/void length scales
   and, for product variants without a backing plate, a real connectivity
   constraint. Re-run the physics after any geometry-changing cleanup.
7. **Native 3D later:** use adaptive marching cubes/tetrahedra only when `phi`
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
