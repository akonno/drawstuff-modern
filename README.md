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

While the internal implementation has been completely redesigned using
modern OpenGL techniques, the API compatibility is intentionally preserved
to allow existing drawstuff-based simulation code to be reused with minimal changes.

## License

drawstuff-modern is released under the BSD 3-Clause License.

See the LICENSE file for details.

## Acknowledgements

Texture files included in this repository are derived from the drawstuff
library distributed with the Open Dynamics Engine (ODE).

Copyright (c) 2001–2007 Russell L. Smith.
Redistributed under the terms of the BSD 3-Clause License.
