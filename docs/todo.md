# GLVU-3 To Do List

## General

- [ ] Revisit storage and allocation in RenderScript
  - [ ] Drop raw pointers completely
  - [ ] Custom `PerFrameData` (void* + size)
- [ ] Use a single per-scene execution for offscreen lighting, per view offscreen lighting has VR issues with hysteresis in eye results (most visible with billboards/particles)
- [ ] Make file loading more robust and with better error reporting
  - [ ] Consider moving to HJSON or an alternative to the bespoke stb_c_lexer format
- [ ] MSAA
  - The focus is on deferred / tiled-deferred and I just walked right by it for no particular reason other than knowing how much it sucks to do in Vulkan and DX11.
- [ ] Revisit the internal shader-permutations
  - `_LITDIR_SKINNED` etc suffixes
  - Hard-coded
  - What happens when `_LITDIR_SKINNED_INST` becomes a thing? Yet another set of branches in many subtle locations?

## Effects and Materials

- [ ] Level-of-detail
- [ ] Texture LOD in mip-level loading

## Rendering and Passes

- [ ] Software occlusion culling tie-ins
- [ ] Deferred decals
  - [ ] Can be done with the existing passes functionality
- [ ] Environment / Reflection rendering considerations
- [ ] Dependent rendering
- [ ] Indirect draw

## Lighting and Shadows

- [x] Cubemap filtering
- [ ] Support cubes for point-light shadows
- [ ] CSM for directional shadow-maps
- [ ] Add optional blur pass to shadows (VSM/ESM)
- [ ] Forward Rendering as First-class citizen
- [x] Better shadow and offscreen low-res lighting atlasing

## Examples

- [ ] IM3D immediate mode renderer as alternative to basic `DebugRenderer`