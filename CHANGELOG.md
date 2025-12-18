# Changelog

## [v0.1.0] - 2025-12-18

Initial public release of **drawstuff-modern**, a drawstuff-compatible
3D visualization library based on modern OpenGL (3.3 core profile).

### Added
- Core rendering pipeline implemented using OpenGL 3.3 core profile.
- drawstuff-compatible API allowing existing ODE-based code to be reused
  with minimal modification.
- Instanced rendering support for boxes, spheres, and cylinders,
  enabling efficient visualization of large numbers of rigid bodies.
- Registered mesh interface for efficient drawing of indexed triangle meshes.
- Minimal demonstration programs, including large-scale instanced sphere rendering.

### Known Limitations
- Platform support is currently limited to Linux or WSL environments
  using an X11-based display server.
- Convex shapes are rendered using a legacy per-triangle approach
  and are not yet optimized with batched or instanced rendering.
- Capsule shapes are not yet instanced and are rendered individually.

