# Design Notes

This document summarizes the design decisions behind drawstuff-modern.
It is intended for users who are familiar with drawstuff / ODE, as well as
for future reference by the author.

The goal is not to provide a complete graphics tutorial, but to explain
*why* certain technical choices were made and what assumptions the
implementation is based on.

---

## Target Users

The primary users of drawstuff-modern are expected to be:

- Users of drawstuff and the Open Dynamics Engine (ODE) who need better
  visualization performance without adopting a full game engine
- Researchers and engineers working on rigid-body physics simulations
- The future version of the author, revisiting this code after some time

The design therefore prioritizes clarity, predictability, and minimal
dependencies over generality or feature completeness.

---

## Why OpenGL 3.3 Core Profile

drawstuff-modern is implemented using the OpenGL 3.3 core profile.

This choice was made deliberately, based on the following considerations:

- **Availability and stability**  
  OpenGL 3.3 core is widely supported on Linux systems and in WSL environments,
  including relatively old GPUs and drivers. This makes it suitable for
  research and educational environments where hardware constraints vary.

- **Removal of legacy fixed-function features**  
  The core profile enforces a modern rendering model (VAOs, VBOs, shaders)
  and prevents accidental reliance on deprecated fixed-function behavior.
  This makes the rendering pipeline explicit and easier to reason about.

- **Educational and debugging value**  
  OpenGL 3.3 provides a good balance between low-level control and practical
  usability. The rendering flow remains visible and understandable, which
  is important for debugging simulation results.

More modern APIs such as Vulkan offer greater control and scalability,
but they also significantly increase implementation complexity.
For the intended scope of drawstuff-modern, OpenGL 3.3 core provides
sufficient expressiveness without unnecessary overhead.

---

## Instanced Rendering as a Core Design Assumption

A key design assumption of drawstuff-modern is that many objects of the
same shape are rendered simultaneously.

In traditional drawstuff, primitives are often rendered one by one,
resulting in a large number of draw calls and significant CPU overhead.
This becomes a bottleneck when visualizing large-scale simulations
with thousands or more rigid bodies.

drawstuff-modern addresses this by relying on **instanced rendering**:

- Geometry data for a given primitive (e.g., sphere, box, cylinder)
  is stored once on the GPU.
- Per-object transforms and colors are provided as per-instance data.
- Multiple objects are rendered in a single draw call.

This approach shifts the rendering workload from the CPU to the GPU and
scales much better with increasing object counts.

The API remains drawstuff-compatible, but internally the rendering model
is fundamentally different from the original immediate-mode approach.

---

## Design Philosophy

Several guiding principles influenced the overall design:

- **Minimalism over abstraction**  
  The library avoids complex scene graphs or hidden state.
  Rendering behavior should be easy to trace and reason about.

- **Correctness before optimization**  
  Performance optimizations are applied where they do not obscure
  correctness or debugging. Known performance limitations are documented
  explicitly.

- **Compatibility with existing code**  
  Preserving drawstuff-style APIs allows existing ODE-based simulation
  code to be reused with minimal modification.

- **Research-oriented trade-offs**  
  The library is optimized for research workflows, not for game development
  or visual effects production.

---

## Notes on Current Limitations

Some shape types are not yet fully optimized (e.g., convex and capsule shapes),
and platform support is currently limited to Linux and WSL environments
with an X11-based window system.

These limitations are documented in the README.md and primarily affect
performance characteristics rather than correctness or API behavior.

---

## Closing Remarks

drawstuff-modern is not intended to be a general-purpose graphics library.
It is a focused visualization layer designed to support physics simulation
workflows with predictable behavior and reasonable performance.

This document records the rationale behind the current design and serves
as a reference point for future extensions or refactoring.
