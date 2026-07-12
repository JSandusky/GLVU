# Renderables

The classes contained in the `Renderables.h` and `Renderables.cpp` files are not intended to be **final** for end-use. They are canonical only as bases used for Git.

The only thing that matters is the **`IQueriableScene`**, **`Camera`** and **`Light`** interfaces which are used extensively by the `Renderer` and `RenderScript` classes. Everything else is fluff.

In practice an end-user will likely want to rewrite the `Scene` class (or create their own) to use an acceleration structure such as an Octree or Quadtree.

The purpose of what's present is to ensure via test-cases that the core features (instancing, skinning, offscreen-lighting, etc) all function correctly.

## Renderable Helpers

In the helpers folder are some additional assistance classes for test usage.

- Octree Scene
  - IQueriableScene implementation that uses an Octree
- Heightfield Terrains
- Particle Effects
- Static Model
- Animated Model
- OBJ and GLTF loaders

These helpers are kept independent to avoid creating additional dependent clutter, they may find themselves too out-of-date to function with the `head` branch at any given time though they are used for samples.