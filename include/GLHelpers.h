//****************************************************************************
//
//  File:       GLHelpers.h
//  Authors:    Jonathan Sandusky
//  License:    MIT
//  Project:    GLVU
//  Contents:   Utilities for working with OpenGL state.
//
//****************************************************************************

#pragma once

#if defined(GLVU_GL) || defined(GLVU_GLES3)

#include <GLEW/GL/glew.h>

#include <glvu.h>
#include <Geometry.h>

#include <array>

namespace GLVU
{

/// State that is tied to a specific GLContext.
struct GLContextState
{
	bool vao_ActiveStates[16] = {
		false, false, false, false,
		false, false, false, false,
		false, false, false, false,
		false, false, false, false
	};
	int vao_Instanced[16] = {
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0
	};
};

/// GLState aids in keeping GL state reasonably clean, in particular this is so 3rd party libraries can be used with less headache
/// and multiple/shared contexts can be used within reason.
struct GLState
{
    GLboolean blendOn;
    GLint blendRGBDestFunc;
    GLint blendRGBSrcFunc;
    GLint blendADestFunc;
    GLint blendASrcFunc;
    GLint blendEquation;
    GLint viewport[4] = { 0 };

    GLint alphaTest;
    GLint alphaToCoverage;
    GLint depthCompare;
    GLboolean depthTest;
    GLboolean depthWrite;

    GLint stencilFunc;
    GLint stencilRef;
    GLint stencilCompareMask;
    GLint stencilWriteMask;
    GLint stencilFail, stencilZFail, stencilZPass;
    GLboolean stencilTest;
    float depthBias;
    float slopeDepthBias;
    float depthRange[2];

    struct TexBind {
        GLint tex;
        GLenum target;
    };
    std::array<TexBind,16> textures = { 0 };
    std::array<GLint, 16> samplers = { 0 };
    std::array<GLint, 16> uniformBuffers = { 0 };
    std::array<GLint, 16> computeImages = { 0 };
    GLint framebuffer = 0;
    GLint shaderProgram = 0;

    GLint cullFace = GL_BACK;
    bool cullingOn;
    bool scissorOn;

    bool vao_ActiveStates[16] = {
        false, false, false, false,
        false, false, false, false,
        false, false, false, false,
        false, false, false, false
    };
    uint8_t vao_Instanced[16] = {
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0
    };

    void InitDefault()
    {
    }

    /// Captures the live state form the GL api.
    void CaptureCurrent()
    {
        blendOn = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blendRGBSrcFunc);
        glGetIntegerv(GL_BLEND_DST_RGB, &blendRGBDestFunc);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendASrcFunc);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blendADestFunc);
        glGetIntegerv(GL_BLEND_EQUATION, &blendEquation);

        glGetFloatv(GL_DEPTH_RANGE, depthRange);

        depthTest = glIsEnabled(GL_DEPTH_TEST);
        depthWrite = glIsEnabled(GL_DEPTH_WRITEMASK);

        stencilTest = glIsEnabled(GL_STENCIL_TEST);
        glGetIntegerv(GL_STENCIL_REF, &stencilRef);
        glGetIntegerv(GL_STENCIL_WRITEMASK, &stencilWriteMask);
        glGetIntegerv(GL_STENCIL_VALUE_MASK, &stencilCompareMask);
        glGetIntegerv(GL_STENCIL_FUNC, &stencilFunc);
        glGetIntegerv(GL_STENCIL_FAIL, &stencilFail);
        glGetIntegerv(GL_STENCIL_PASS_DEPTH_FAIL, &stencilZFail);
        glGetIntegerv(GL_STENCIL_PASS_DEPTH_PASS, &stencilZPass);

        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &slopeDepthBias);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &depthBias);

        glGetIntegerv(GL_SAMPLE_ALPHA_TO_COVERAGE, &alphaToCoverage);
        alphaTest = glIsEnabled(GL_ALPHA_TEST);

        glGetIntegerv(GL_CULL_FACE_MODE, &cullFace);
        cullingOn = glIsEnabled(GL_CULL_FACE);
        scissorOn = glIsEnabled(GL_SCISSOR_TEST);
    }

    /// Applies this state.
    void Apply(const GLState* lastState)
    {
        samplers = { };
        textures = { };

#define BOOL_ENABLE(name, KEY) if (lastState == nullptr || lastState-> name != name) { if (name) glEnable(KEY); else glDisable(KEY); }

        BOOL_ENABLE(blendOn, GL_BLEND);
        if (lastState == nullptr ||
            lastState->blendRGBDestFunc != blendRGBDestFunc || lastState->blendRGBSrcFunc != blendRGBSrcFunc ||
            lastState->blendADestFunc != blendADestFunc || lastState->blendASrcFunc != blendASrcFunc)
        {
            glBlendFuncSeparate(blendRGBSrcFunc, blendRGBDestFunc, blendASrcFunc, blendADestFunc);
        }

        if (lastState == nullptr || lastState->blendEquation != blendEquation)
            glBlendEquation(blendEquation);

        BOOL_ENABLE(depthTest, GL_DEPTH_TEST);
        BOOL_ENABLE(scissorOn, GL_SCISSOR_TEST);
        BOOL_ENABLE(stencilTest, GL_STENCIL_TEST);
        BOOL_ENABLE(cullingOn, GL_CULL_FACE);
        BOOL_ENABLE(alphaTest, GL_ALPHA_TEST);
        BOOL_ENABLE(alphaToCoverage, GL_SAMPLE_ALPHA_TO_COVERAGE);

        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glDepthRange(depthRange[0], depthRange[1]);

        if (lastState == nullptr || lastState->depthWrite != depthWrite)
            glDepthMask(depthWrite);
        else
            glDepthMask(GL_FALSE);

        if (lastState == nullptr || lastState->slopeDepthBias != slopeDepthBias || lastState->depthBias != depthBias)
        {
            glPolygonOffset(slopeDepthBias, depthBias);
            if (depthBias != 0 || slopeDepthBias != 0)
                glEnable(GL_POLYGON_OFFSET_FILL);
            else
                glDisable(GL_POLYGON_OFFSET_FILL);
        }

        if (lastState == nullptr || ((lastState->stencilRef != stencilRef ||
            lastState->stencilCompareMask != stencilCompareMask ||
            lastState->stencilWriteMask != stencilWriteMask ||
            lastState->stencilFunc != stencilFunc ||
            lastState->stencilFail != stencilFail ||
            lastState->stencilZFail != stencilZFail ||
            lastState->stencilZPass != stencilZPass) &&
            stencilTest))
        {
            glStencilFunc(stencilFunc, stencilRef, stencilCompareMask);
            glStencilMask(stencilWriteMask);
            glStencilOp(stencilFail, stencilZFail, stencilZPass);
        }

        if (lastState == nullptr || lastState->depthBias != depthBias)
            glPolygonOffset(depthBias, 0.0f);

#undef BOOL_ENABLE

        // just set these to 0, that's good enough for us
        // do the same with images?
        for (auto& b : uniformBuffers)
            b = 0;
        for (auto& c : computeImages)
            c = 0;

		// Oh, dear.
		// Just flip their values so we're forced to touch everything???
		for (auto& b : vao_ActiveStates)
			b = !b;
		for (auto& b : vao_Instanced)
			b = !b;
    }

    void SetViewport(int x, int y, int w, int h)
    {
        if (viewport[0] != x || viewport[1] != y || viewport[2] != w || viewport[3] != h)
        {
            viewport[0] = x;
            viewport[1] = y;
            viewport[2] = w;
            viewport[3] = h;
            glViewport(x, y, w, h);
        }
    }

    void SetDepthRange(float minR, float maxR)
    {
        if (minR != depthRange[0] && maxR != depthRange[1])
        {
            depthRange[0] = minR;
            depthRange[1] = maxR;
            glDepthRange(depthRange[0], depthRange[1]);
        }
    }

    bool TextureIsActive(GLint handle)
    {
        for (int i = 0; i < 16; ++i)
            if (textures[i].tex == handle)
                return true;
        return false;
    }

    bool SetTexture(unsigned idx, GLint handle, GLenum target)
    {
        if (textures[idx].tex != handle)// || textures[idx].target != target)
        {
            textures[idx] = { handle, target };
            return true;
        }
        return false;
    }

    bool SetSampler(unsigned idx, GLint samp)
    {
        if (samplers[idx] != samp)
        {
            samplers[idx] = samp;
            return true;
        }
        return false;
    }

    bool SetFBO(GLint handle)
    {
        if (handle != framebuffer)
        {
            framebuffer = handle;
            return true;
        }
        return false;
    }

    void SetUBO(int slot, GLint buffer)
    {
        if (uniformBuffers[slot] == buffer)
            return;
        uniformBuffers[slot] = buffer;
        glBindBufferBase(GL_UNIFORM_BUFFER, slot, buffer);
    }

    void SetBlendMode(BlendMode blendMode)
    {
        static const GLuint glBlendSrcTable[] = { 0, GL_SRC_ALPHA,           GL_ONE,             GL_ONE,                     GL_DST_COLOR,       GL_ONE };
        static const GLuint glBlendDestTable[] = { 0, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,             GL_ONE,                     GL_ZERO,            GL_ONE_MINUS_SRC_ALPHA };
        static const GLuint glBlendFuncTable[] = { 0, GL_FUNC_ADD,            GL_FUNC_ADD,        GL_FUNC_REVERSE_SUBTRACT,   GL_FUNC_ADD,        GL_FUNC_ADD };
        
        GLboolean wantBlend = blendMode != Blend_None;
        if (wantBlend != this->blendOn)
        {
            if (blendMode != Blend_None)
                glEnable(GL_BLEND);
            else
                glDisable(GL_BLEND);
            this->blendOn = wantBlend;
        }

        // leave the rest of state alone if we're just turning things off
        if (blendOn == GL_FALSE)
            return;

        if (blendADestFunc != glBlendDestTable[blendMode] ||
            blendRGBDestFunc != glBlendDestTable[blendMode] ||
            blendASrcFunc != glBlendSrcTable[blendMode] ||
            blendRGBSrcFunc != glBlendSrcTable[blendMode])
        {
            glBlendFuncSeparate(glBlendSrcTable[blendMode], glBlendDestTable[blendMode], glBlendSrcTable[blendMode], glBlendDestTable[blendMode]);
            blendADestFunc = glBlendDestTable[blendMode];
            blendRGBDestFunc = glBlendDestTable[blendMode];
            blendASrcFunc = glBlendSrcTable[blendMode];
            blendRGBSrcFunc = glBlendSrcTable[blendMode];
        }

        if (blendEquation != glBlendFuncTable[blendMode])
        {
            glBlendEquation(glBlendFuncTable[blendMode]);
            blendEquation = glBlendFuncTable[blendMode];
        }
    }

#define GL_BOOL_SETTER(TITLE, ENUM, VARIABLE) void Set ## TITLE (bool state) { \
    if (state != VARIABLE) { \
        VARIABLE = state; \
        if (state) \
            glEnable(ENUM); \
        else \
            glDisable(ENUM); \
    } \
}

#define GL_BOOL_CALLER(TITLE, FUNC, VARIABLE) void Set ## TITLE (bool state) { \
    if (state != VARIABLE) { \
        VARIABLE = state; \
        FUNC(state ? GL_TRUE : GL_FALSE); \
    } \
}

#define GL_ENUM_CALLER(TITLE, FUNC, VARIABLE) void Set ## TITLE (GLenum state) { \
    if (state != VARIABLE) { \
        VARIABLE = state; \
        FUNC(state); \
    } \
}

#define GL_MASK_CALLER(TITLE, FUNC, VARIABLE, REFERENCE) void Set ## TITLE (uint32_t state) { \
    if (state != VARIABLE && REFERENCE) { \
        VARIABLE = state; \
        FUNC(state); \
    } \
}

    GL_BOOL_SETTER(ScissorOn, GL_SCISSOR_TEST, scissorOn);
    GL_BOOL_SETTER(AlphaTest, GL_ALPHA_TEST, alphaTest);
    GL_BOOL_SETTER(AlphaToCoverage, GL_SAMPLE_ALPHA_TO_COVERAGE, alphaToCoverage);
    GL_BOOL_SETTER(Culling, GL_CULL_FACE, cullingOn);
    GL_BOOL_SETTER(DepthTest, GL_DEPTH_TEST, depthTest);
    GL_BOOL_CALLER(DepthMask, glDepthMask, depthWrite);
    GL_ENUM_CALLER(CullingFace, glCullFace, cullFace);
    GL_ENUM_CALLER(DepthFunc, glDepthFunc, depthCompare);
    GL_BOOL_SETTER(StencilTest, GL_STENCIL_TEST, stencilTest);
    GL_MASK_CALLER(StencilMask, glStencilMask, stencilWriteMask, stencilTest);

    void SetStencilFunc(GLint func, uint32_t ref, uint32_t mask)
    {
        if (func != stencilFunc || ref != stencilRef || mask != stencilCompareMask)
        {
            glStencilFunc(func, ref, mask);
            stencilFunc = func;
            stencilRef = ref;
            stencilCompareMask = mask;
        }
    }
};

struct GLBufferStates {
    struct BuffState {
        GLuint target;
        GLuint buff;
        size_t start;
        size_t end;
    } states_[16];

    struct TexState {
        GLuint target;
        GLuint tex;
    } tex_[16];

    GLBufferStates() {
        memset(states_, 0, sizeof(states_));
        memset(tex_, 0, sizeof(tex_));
    }

    void BindTex(GLuint texTarget, GLuint tex, int slot)
    {
        if (tex_[slot].target != texTarget || tex_[slot].tex != tex)
        {
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(texTarget, tex);
        }
    }



    void Bind(GLuint bufferTarget, GLuint buffer, int slot)
    {
        if (states_[slot].target != bufferTarget || states_[slot].buff != buffer || states_[slot].start != 0 && states_[slot].end != SIZE_MAX)
        {
            states_[slot] = { bufferTarget, buffer, 0, SIZE_MAX };
            glBindBufferBase(bufferTarget, slot, buffer);
        }
    }

    void Bind(GLuint bufferTarget, GLuint buffer, int slot, size_t start, size_t end)
    {
        if (states_[slot].target != bufferTarget || states_[slot].buff != buffer || states_[slot].start != start && states_[slot].end != end)
        {
            states_[slot] = { bufferTarget, buffer, start, end };
            glBindBufferRange(bufferTarget, slot, buffer, start, end);
        }
    }
};

}

#endif
