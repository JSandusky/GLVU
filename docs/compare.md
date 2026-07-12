# Comparison To Others

None of these are really all that comparable to each other, they have very different usages. BGFX for instance is really just a common-API for graphics whereas GLVU also deals with lighting, frame-management, and shader-combinations ... those aren't directly comparable and have different values depending on needs.

Some fairly impartial notes (only the order is partial):

| Lib / Engine   | APIs                   | More Than GFX                                                           | Bloat                                                             | Sample Quality                             | Built-in VR Considerations                 |
| -------------- | ---------------------- | ----------------------------------------------------------------------- | ----------------------------------------------------------------- | ------------------------------------------ | ------------------------------------------ |
| GLVU (goals)   | GL, Vulkan             | little, basic scene base-types and non-trivial lighting/rendering logic | Several 2-file 3rd party libs, optional extras, uses STL          | Poor                                       | Shared view-management, clustered shading  |
| BGFX           | basically everything   | no                                                                      | BLIB, BIMG                                                        | Good                                       | None                                       |
| Urho3D         | GL, DX9, DX11          | yes, complete engine                                                    | Many 3rd party dependencies                                       | Good                                       | Shared view management (culling)           |
| Magnum         | GL, Vulkan             | technically no, but modules that are                                    | Is the very definition of the word                                | Poor                                       | None                                       |
| DiligentEngine | GL, DX11, DX12, Vulkan | no, Lua bindings and mini-framework on the side                         | Little to none, though poor project layout makes it look enormous | Minimal (problematic given DX12 style API) | None                                       |
| RayLib         | GL                     | yes, lots of academic dumpster fire                                     | debatable                                                         | Good                                       | Basic                                      |
| BSF            | GL, DX11, DX12, Vulkan | yes, complete engine                                                    | extensive to say the least                                        | Poor                                       | None                                       |
| Godot          | GL                     | yes, complete engine                                                    | Many 3rd party dependencies, lots of "NIH"                        | Good                                       | Extensive in the guts but poor in practice |
