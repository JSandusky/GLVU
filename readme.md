Abandoned as I've embraced NVRHI and NRI in different cases rather than maintaining this.

# GLVU-3

[TOC]

Minimal but still reasonable rendering or OpenGL, or Vulkan. Using GLEW and V-EZ, GLFW for examples. (V-EZ is broken ATM)

A rendering library that's mostly raw, but provides the necessary guts to get moving quickly and intended for direct inclusion - not yet another sacrosanct git dependency to manage. It is not designed to be an out-of-the-box state-of-the-art renderer.

**Core Features of Note:**

- Automatic instancing
- Skinning (256 max bones default)
- Point / Spot / Directional lights
- Low-resolution off-screen lighting (ie. DOOM 2016 particle lighting)
- Full pipe support from VS -> PS
  - No raytracing support, let's wait and see if that ever becomes common-place

You might want to use GLVU if you're after something that you take and hack into what you need.

---

**Libraries:**

- STL for templates
  - conversion to EASTL should be trivial
- GLEW for GL extensions
- AMD's V-EZ for simplifying Vulkan
- GLFW for Examples
- MathGeoLib for math
  - vector, matrix, AABB, Frustum
  - Relatively mindless to replace with GLM etc
- stb_c_lexer for loading file formats
- stb_image for loading basic images (png, jpg, gif, etc)
- ddsktx for loading DDS and KTX files
- Examples:
  - jobxx
  - dearimgui

**Experimental / WIP:**

- GLES 3.0 support (should mostly function)
- Nintendo 3DS (not in GIT)

**Influenced by**:

- Urho3D, batching and automatic instancing mechanisms are very similar
- Horde3D
- AMD Leo demo, discarding normal forward rendering
- Ashes of the Singularity / Doom (2016), offscreen lighting
- Just Cause 2 (Avalanche clustered slides), for deferred + ZX clustering focus

**More and more**:

[Todo](docs/todo.md)

[Doubtable Notes](docs/doubts.md)

[Comparison to other libraries](docs/compare.md)

[Renderable objects base code](docs/renderables.md)

## Rendering

### Task-centric

Rendering operations are compartmentalized to per-API handling of the execution of a *render-script* rather than abstracting a minimum common API this means writing code that's more appropriate for a given task instead of code that fits some lowest-common-denominator.

This is actually a common paradigm in "get it fucking done" **real** development but pretty much unseen in OSS where most everything (except *get it fucking done* projects try for god-abstractions).

It does however present some pains in fitting things into where the ostensibly belong and some code duplication, that's the trade-off. Since this boils down to "*draw this pile of geometries with these shaders*", crying about near code-duplication isn't terribly meaningful.

### Reasonably Modern

Modern constructs like `glBindBufferRange()` are used where possible and data uploads coalesced. There are more opportunities with Vulkan but some usage patterns have to be set into stone (reusing command-buffers for post-processing, etc).

# Formats

GLVU uses custom text-based formats (parsed via stb_c_lexer) rather than an established format like XML or JSON because:

- Comments aren't present in JSON, requiring the use of *junk* fields for them
  - Comments are incredibly valuable when tweaking files and trying things out
- Inheritance as a natural syntax element
- Legibility, XML legibility shouldn't even be on the table and JSON is polluted with quotes
  - HJSON would be reasonable

Of course that comes with the con that error report is nowhere near as robust as can be found in other libraries.

As that won't be satisfactory to everyone (or all cases) all data-file loading is handled in `FormatLoaders.cpp` so only those static functions need to be rewritten to use a different format.

### Render Scripts

Anything prefixed with `$` is a user specified value that might otherwise be misread as part of the syntax.

```C
render_script {
    target $TargetName {
        format = TEXTURE_FORMAT; // required
        width_fraction = FLOAT;  // optional, proportional to `backbuffer`
        height_fraction = FLOAT; // optional, proportional to `backbuffer`
        width = INTEGER;  // optional, for fixed size
        height = INTEGER; // optional, for fixed size
        ping_pong = BOOL; // optional for ping-ponging
        global = BOOL; // store this target for global access, not local script
        mips = BOOL; // optional, default = false
    }

    stage $StageName {
        enabled = BOOL; // optional, default is true
        ban = $StageName; // optional, disable this stage if another is enabled
        require = $StageName; // optional, only run if another stage is enabled

        targets { // targets to render into
            $TargetName,
            $TargetName,
            $TargetName
        }

        bind_targets { // targets to bind to samplers
            TargetName = INTEGER; // specify texture slot
            PONG_TargetName = INTEGER; // uses the PONG data
        }

        pass $Name {
            enabled = true; // optional
            context = CONTEXT_NAME; // context name to find a pass for
            sort = ContextSwitch|FrontToBack|BackToFront; // sorting method, default ContextSwitch
        }

        lighting_pass $Name {
            method = forward|deferred|forward_tiled|light_volumes; // lighting technique
            ... same members as pass ...
        }

        quad $Name { // it's actually a full-screen triangle
            shader = "ShaderFile.fs"; // uses an internal vertex-shader
            input_size_src = RenderTargetName; // fills uniform with size info
            output_size_src = RenderTargetName; // fills uniform with size info
            params { // UBO parameters
                color = 1.0 0.0 0.5 0.5 // anything not a number is ignored
            }
        }

        clear_targets {
            color = <0, 0, 0, 1.0>;
            stencil = INTEGER;
            depth = FLOAT;
            discard_depth = BOOL; // optional, set to clear depth
            discard_color = BOOL; // optional, set to clear color
            discard_stencil = BOOL; // optional, set to clear stencil
        }

        blit SrcTarget DestTarget; // handy for ping-pongs and the like
        gen_mips RenderTargetName; // must have been specified to use mips
        buffer_copy SrcBuffer DestBuffer; // rare utility

        callback $CallID { // see GLVUHelpers `DrawGUI` usage
            1.0 0.0 0.5 0.5 // anything not a number is ignored
        }
        callback $CallID; // short, no parameter version
    }
}
```

### Effects

```c
effect EffectName {
    alias Name Alias; // use alias to hide gobbly-gook shader sampler names, materials can use the Alias instead of the name
    alias Name as Alias; // alternative syntax for non-programmers that don't get typedef

    skin = true; // compile variation with SKINNED and MAX_BONES defined
    instance = true; // compile variation with INSTANCED and MAX_INSTANCES defined

    sampler $Name {
        filter = POINT|LINEAR|TRILINEAR|ANISOTROPIC; // filtering method
        slot = 0; // texture-unit index
        wrap = BOOL; // whether to wrap or clamp
        default = "DefaultTextureImage.dds"; // optional
    }

    context $Name {
        // note the usage of DX style names
        vs = "VertexShader.vs";
        gs = "GeometryShader.gs";
        ps = "PixelShader.ps";
        hs = "HullShader.hs";
        ds = "DomainShader.ds";
        defines = "NO_TEX"; // added to all shaders
        vs_defines = "SAMPLES 4, CUBE_MIPS 6"; // and so on for gs/hs/ds/ps

        blend = NONE|ALPHA|PREMULTIPLIED|ADD|SUBTRACT|MULTIPLY;
        depth_test = BOOL;
        depth_write = BOOL;
        depth_bias = FLOAT;
        slope_bias = FLOAT;
        alpha_test = BOOL;
        alpha_to_coverage = BOOL;
        uses_discard = BOOL;
    }

    context $SecondPass:points { // passes can have a geometry-type specific overload
        ...
    }

    context $SecondPass:triangles { // overloaded version of above
        ...
    }

    ... however many more passes or samplers are desired ...
}
```

### Materials

```c
material Name {
    effect = "EffectName.fx";
    effect { // an effect can be defined inline, this is not recommended
        ... effect defintion ...
    }
    texture Name = "texture_file.dds"; // specify texture by name in the shader or Effect alias
    texture 0 = "texture_file2.dds"; // textures can be specified by unit

    lit = BOOL;             // runs through lighting passes
    cast_shadow = BOOL;     // runs through shadowing passes
    receive_shadows = BOOL;     // uses shadow-testing variations of lighting functions
    shadowed = BOOL; // shorthand, for cast_shadow and receive_shadows

    // you can configure custom UBOs this way per material.
    ubo Name {
       pbr_ctrl = 0.0 0.5 0.1 5.0 // only floats are read, use notation to ease pain
    }
}
```

### Particle Systems

```c
particle_effect {
    paths PathSetName {
        { /*this is a path*/
            "0.0 0.2 0.3" // tuples
            "0.0 0.3 0.8" // tuples
        }
    }

    emitter Name {
        kind = point;
        trajectory = PathSetName;    
        offset = "0.0 0.0 0.0";

        phase {
            start = 0.3;
            end = 0.8;

        }
    }

    emission Module {

    }

    timeline {
        { 0.1, 0.5 0.3 0.1 1.0 }
        { 0.2, 0.5 0.3 0.1 1.0 }
    }
}
```

# Rendering

GLVU takes a view construct along with a collection of "*batches*" and "*lights*" which it will then render. The scene structure is as minimal as possible and only basic trivial types are provided.

Rendering a view proceeds as:

- Coalesce instance capable batches
- Collect sets of batches for lit and shadowed batches
- Render shadowmaps into shadowmap atlas
- Iterate over each stage in the render script
  - bind render targets as outputs|inputs for the stage
  - execute each command in the stage

## Render Script Commands

### Stage

Stages form logical groupings related to a set of render-targets for the render-script. In Vulkan these map directly to render-passes.

There are several special names that can be used for `targets { }` and `bind_targets { }` blocks:

- `BACKBUFFER` binds the main targets color buffer (presumably the window surface's)
- `DEPTHBUFFER` binds the main targets depth buffer
- `SHADOWMAP` binds the shadowmap atlas

#### Quad (fs_quad, quad)

Internally implemented as a full-screen triangle. A vertex shader is provided automatically.

Input parameters: 

- `in vec2 screenCoord`
- ```c
    layout(binding = 1) uniform ViewData {
        vec2 inputSize; vec2 invInputSize;
        vec2 outputSize; vec2 invOutputSize;
    };
  ```

### Geometry Pass (pass)

The most important part is specifying the `context` to use. This will select which `context` should be sought from an effect when running the pass. If the `Effect` does not have the given context then the geometry in question will not be rendered.

### Lighting Pass (lighting_pass, light_pass)

Lighting behaves the same as a geometry pass except with some additional data fed along for rendering.

What happens is based on the method chosen.

#### Forward Lights

For each light passing the filter criteria the scene will be queried for drawables within it's effective area. Each drawable's material will be invoked based on the light.

- Where the queried context is "MYCONTEXT"
  - Point lights will try to find a context called:
    - `MYCONTEXT_LITPOINT`, `MYCONTEXT_LITPOINT_INST`, and `MYCONTEXT_LITPOINT_SKINNED`
  - Spot lights will try to find a context called:
    - `MYCONTEXT_LITSPOT`, `MYCONTEXT_LITSPOT_INST`, and `MYCONTEXT_LITSPOT_SKINNED`
  - Directional lights will try to find a context called:
    - `MYCONTEXT_LITDIR`, `MYCONTEXT_LITDIR_INST`, and `MYCONTEXT_LITDIR_SKINNED`

Forward lights results in a substantial number of batches being drawn so it's recommended to avoid using it. The only reasonable scenario to use it is for transparent objects.

#### Light Volumes

Hull shapes are rendered for the lights instead. This method is intended for use with deferred/light-prepass shading approaches in which a GBuffer has been written. Though it can be used for other purposes.

This method does not use Effect contexts and instead uses a single pixel shader (the vertex shader is inferred).

#### Deferred Tiled

Light information is collected into two monolithic uniform buffers. One for the light data, and another for the light grid.

A fullscreen pass is then run using the specified pixel shader.

#### Forward Tiled

Light information is collected into two monolithic uniform buffers. One for the light data, and another for the light grid.

All lit geometries are rendered again (if they have the correct context), this time with the two light tile uniform buffers bound to special *system binding points*.

It's the responsibility the pixel shader in the Effect's context to perform the lighting loop, ideally using the helper header.

When either tiled is used then the buffers will be built and shared between them. Multiple tiled lighting passes should only be used when there are different contents being drawn, such as first opaque geometry followed by sorted transparent geometry.

### Compute Pass

### Blit

The blit command allows

## Shadowmaps

**Point Lights:** use dual-paraboloid maps.

**Spot Lights:** use a single classic shadow map.

**Directional Lights:** do not have shadow-casting capability at present.

## Offscreen Lighting Pass

Batches may elect (via flag) to use an offscreen-lighting pass. An attempt to allocate a cell within an RGBA8 render-target atlas will be made and if successful the batch will be enqueued.

It is the Effect's responsibility to implement an `OFFSCREEN_LIGHT` pass that will write into the designated region and also its' responsibility to somewhere refer to the given region.

**WARNING:** Offscreen lighting cannot be used for instanced batches (whether via auto-instancing or explicit transform lists).

## Limitations

`std::vector<T>::push_back` is an actual bottle-neck in batch through-put. 65,202 cubes as individual batches takes ~20ms (GTX-950 Vulkan).

Naturally, that's a **stupidly bad** case, it is more appropriate to emit a handful of batches each containing many transforms than a unique batch+transform pair per cube - but a stress-test is a stress-test. When handled more appropriately 65,202 cubes can be pushed out in ~3 ms on the same hardware as above.

There's like 30% or more to squeeze out, though the problem is that the squeeze points are different for Vulkan and OpenGL. Vulkan can likely gain from restructuring command buffer construction for better threaded building and identifying *fixed* command-buffers (ie. post-processing passes), while OpenGL could likely gain from better internal state management.

For Vulkan the necessary information is largely there, rendering is coalesced into stages before discrete passes so entire stages could be assessed as a fixed command-buffer or parallel command-buffer region instead of the current naïve threaded-command buffer handling.
