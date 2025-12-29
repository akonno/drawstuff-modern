# drawstuff-modern

## Overview

drawstuff-modern is a lightweight, drawstuff-compatible 3D visualization library
built on modern OpenGL (3.3 core profile).

It is designed for efficient, GPU-driven rendering of large numbers of rigid bodies,
primarily for physics simulations and research applications where traditional
drawstuff becomes a performance bottleneck.

## Key Features

- OpenGL 3.3 core profile (no fixed-function pipeline)
- drawstuff-compatible API for easy migration from ODE-based code
- Instanced rendering for efficient visualization of large numbers of objects
- Minimal and explicit API design, suitable for debugging and research use
- Designed for physics simulations, not for general-purpose game development

## Intended Use

This library is intended for:

- Physics simulation research (rigid-body dynamics, contact, friction, etc.)
- Visualization of large-scale simulations (e.g., thousands to hundreds of thousands of objects)
- Educational use in computational mechanics and physics
- Users who want simple, controllable visualization without relying on full game engines

## Current Limitations

The following limitations are known in the current implementation:

- **Platform support** is currently limited to Linux systems or Windows environments
  using WSL with an X11-based display server.
  Native Windows and macOS platforms are not supported at this time.

- **Convex shapes**  are supported in the core-based rendering pipeline.
  A convex polyhedron is tessellated on the CPU into a temporary list of triangles
  (e.g., via triangle fans per face) and rendered in a single batched draw call.
  Unlike **TriMesh**, convex shapes are not yet cached or registered as persistent GPU meshes.


These limitations are primarily related to performance optimization and do not
affect correctness or API compatibility.

### Rendering order and overlays (drawstuff-modern note)

drawstuff-modern uses deferred and batched rendering internally to achieve
high performance, especially for instanced primitives and registered meshes.

As a result, draw calls issued from the user `step()` callback are not always
rendered immediately to the framebuffer. Scene geometry may be flushed later
in the frame, after `step()` returns.

For this reason, drawing HUDs, text, or other 2D overlays directly from
`step()` may lead to them being partially or fully overwritten by subsequent
3D rendering.

To address this, drawstuff-modern provides an optional `postStep()` callback.
If defined, `postStep()` is called after all internal 3D rendering has been
completed and just before the frame is presented.

User-defined HUDs and overlay rendering should be performed in `postStep()`
rather than in `step()`.

Several demo programs under `demo/demo_*.cpp` include simple HUD examples,
such as FPS counters, implemented using `postStep()`, which can be used as
reference code.

### Rendering quality and performance notes

To balance performance and visual clarity, drawstuff-modern applies several
simplifications in its current rendering pipeline:

- **Shadow rendering** uses reduced geometric detail compared to the main
  object geometry. This lower-detail representation is applied intentionally
  to reduce rendering cost for shadows.

- **No distance-based level-of-detail (LOD)** is currently implemented for
  object geometry. All objects are rendered using uniform quality settings
  (e.g., sphere or capsule tessellation levels), regardless of distance.

- **No explicit view-frustum or object culling** is currently performed by
  the library. Aside from any automatic culling handled internally by the
  OpenGL driver, all issued draw calls are submitted as-is.

### Fast rendering of TriMesh objects (drawstuff-modern extension)

The original drawstuff library did not provide a mechanism to pre-register
triangle meshes for reuse. TriMesh objects were rendered by issuing draw calls
per triangle, which became a significant bottleneck for large meshes.

drawstuff-modern introduces an additional API for efficient rendering of
triangle meshes. TriMesh geometry can be registered once as an indexed mesh
and stored in persistent GPU buffers (VAO/VBO/EBO). Subsequent draw calls
reference the registered mesh by handle, avoiding repeated CPU-side
tessellation and per-triangle draw calls.

This functionality is provided by the following non-original drawstuff APIs:

- `dsRegisterIndexedMesh(...)`
- `dsDrawRegisteredMesh(...)`

These functions are extensions specific to drawstuff-modern and are not part
of the original drawstuff API.

## Non-Goals

drawstuff-modern is **not** intended to be:

- A general-purpose game engine
- A replacement for Unity, Unreal Engine, or similar frameworks
- A scene-graph–based rendering engine
- A fully featured graphics abstraction layer

The focus is on simplicity, performance, and transparency rather than completeness.

## Relationship to ODE / drawstuff

This project is a modern reimplementation inspired by the drawstuff library
distributed with the Open Dynamics Engine (ODE), originally developed by
Russell L. Smith.

Open Dynamics Engine (ODE): https://www.ode.org/

While the internal implementation has been completely redesigned using
modern OpenGL techniques, the API compatibility is intentionally preserved
to allow existing drawstuff-based simulation code to be reused with minimal changes.

## Design Notes

For implementation details and design decisions, including the rationale
for using OpenGL 3.3 core and instanced rendering, see
[docs/design-notes.md](docs/design-notes.md).

## Build and Run

### Build

This library currently targets Linux or WSL environments with an X11 display server
and OpenGL 3.3 support.

```
mkdir build
cd build
cmake --fresh ..
make
```

### Run demos
```
./demo/demo_minimal
./demo/demo_100k_objects
./demo/demo_show_obj ../demo/sample_torus.obj
```

## License

drawstuff-modern is released under the BSD 3-Clause License.

See the [LICENSE](LICENSE) file for details.

## Acknowledgements

Texture files included in this repository are derived from the drawstuff
library distributed with the Open Dynamics Engine (ODE).

Copyright (c) 2001–2007 Russell L. Smith.
Redistributed under the terms of the BSD 3-Clause License.

This project includes OpenGL loader source code generated by glad
(https://glad.dav1d.de/).

## Support

drawstuff-modern is provided freely as an open research and visualization tool.

If you find it useful in your research, teaching, or learning, optional support
via Ko-fi is welcome:

https://ko-fi.com/akonno

Support is entirely optional and does not affect access to the library,
available features, or its use in any research or educational context.
