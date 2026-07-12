# Render Loop

- The `Renderer` class maintains a collection of views into a scene which maps a render script to a camera , scene, render-target, and viewport region.
- `Renderer::Execute` is called:
  - For each unique scene that there is a view for:
    - collect all shadow casting lights that are within view
    - render shadowmaps for those lights
      - spot lights use single image shadowmaps
      - point lights use dual-parabaloid shadowmaps
  - For each view
    - collect all visible batches
    - if the render-script for that view contains any references to the light tile data
      - generate tiled/clustered lighting buffers for the view
    - select batches with an `OFFSCREEN_LIGHT` pass in their `Material`'s `Effect`
      - render light-maps for those batches
    - Execute the render-script for that view

