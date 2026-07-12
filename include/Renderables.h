//****************************************************************************
//
//  File:       Renderables.h
//  License:    MIT
//  Project:    GLVU
//  Contents:   Crude objects to assist with batch queries and the needed
//              interfaces to render a View object.
//
//  Usage:      Not intended for serious use. Overhaul as personally needed.
//
//****************************************************************************

#pragma once

#include "glvu.h"

#include "Geometry.h"

#include <array>
#include <vector>
#include <memory>

/// Renderables are used to provide some common utility functionality.
/*
Most of these are not required.
*/

namespace GLVU
{

class Camera;
class Light;
class Texture;

class GLVU_API IQueriableScene abstract
{
public:
    virtual std::vector<Batch> GetBatches(const math::Frustum& frustum) abstract;
    virtual std::vector<std::shared_ptr<Light>> GetLights(const math::Frustum& frustum) abstract;
    virtual std::vector<Batch> GetBatches(const Light* forLight, unsigned shadowMapIndex, math::AABB& totalBnds) abstract;
};

class GLVU_API SceneObject
{
public:
    SceneObject(GraphicsDevice*);
    SceneObject(GraphicsDevice*, const math::float3& pos, const math::Quat& rot, const math::float3& scale);
    virtual ~SceneObject() { }

    inline math::float3 GetPosition() const { return position_; }
    inline math::Quat GetRotation() const { return rotation_; }
    inline math::float3 GetScale() const { return scale_; }
    inline bool IsVisible() const { return isVisible_; }

    math::float4x4 GetTransform() const;
    math::float4x4 GetInvTransform() const;

    math::float3 GetDirection() const;
    math::float3 GetUp() const;
    math::float3 GetRight() const;

    inline void SetPosition(const math::float3& pos) { position_ = pos; MarkDirty(); }
    inline void SetRotation(const math::Quat& rot) { rotation_ = rot; MarkDirty(); }
    inline void SetScale(const math::float3& scl) { scale_ = scl; MarkDirty(); }
    inline void SetScale(float scl) { scale_ = { scl, scl, scl }; MarkDirty(); }
    inline void SetVisible(bool state) { isVisible_ = state; }

    void Translate(const math::float3& offset, bool isLocal = false);
    void Rotate(const math::Quat& offset, bool isLocal = false);
    void Scale(const math::float3& scaleBy, bool isLocal = false);
    void RotateAround(const math::float3& pt, math::Quat& rot);
    void LookAt(const math::float3& pt);
    void LookAtDir(const math::float3& dir);

    math::float3 ToLocalSpace(const math::float3& worldSpaceCoord) const { return (GetInvTransform() * math::float4(worldSpaceCoord, 1.0f)).xyz(); }
    math::float3 ToWorldSpace(const math::float3& localSpaceCoord) const { return (GetTransform() * math::float4(localSpaceCoord, 1.0f)).xyz(); }
    math::float4x4 ToLocalSpace(const math::float4x4& worldSpaceMatrix) const { return GetInvTransform() * worldSpaceMatrix; }
    math::float4x4 ToWorldSpace(const math::float4x4& worldSpaceMatrix) const { return GetTransform() * worldSpaceMatrix; }

    virtual math::AABB GetBounds() const { return math::AABB(); }
    virtual void GetBatches(std::vector<Batch>&) { }

protected:
    virtual void MarkDirty() { transformDirty_ = true; }
    virtual void UpdateTransform() const;

    GraphicsDevice* device_;
    math::float3 position_;
    math::Quat rotation_;
    math::float3 scale_;
    bool isVisible_;

    mutable math::float4x4 transform_;
    mutable math::float4x4 invTransform_;
    mutable bool transformDirty_;
};

class GLVU_API Scene : public IQueriableScene
{
public:
    Scene();
    ~Scene();

    void Insert(std::shared_ptr<SceneObject>);
    void Remove(std::shared_ptr<SceneObject>);

    std::vector< std::shared_ptr<SceneObject> >& GetContents() { return contents_; }
    const std::vector< std::shared_ptr<SceneObject> >& GetContents() const { return contents_; }

    std::vector<Batch> GetBatches(const math::Frustum& frustum) override;
    std::vector<std::shared_ptr<Light>> GetLights(const math::Frustum& frustum) override;
    std::vector<Batch> GetBatches(const Light* forLight, unsigned, math::AABB& totalBnds) override;

protected:
    std::vector< std::shared_ptr<SceneObject> > contents_;
    std::vector< std::shared_ptr<Light> > lights_;
};

class GLVU_API Renderable
{
public:
    virtual void UpdateFrame() = 0;
    virtual void Update(float td) = 0;
};

class GLVU_API Billboards
{
public:
private:
    std::unique_ptr<Buffer> vertexBuffer_;
};

class GLVU_API ParticleSystem
{
public:

private:
    /// Particle simulation data is packed into a blob using an [A A A A A][B B B B B][C C C C C] sequence.
    std::unique_ptr<char[]> particleData_;
    std::array<size_t, 32> dataOffsets_;
    size_t particleDataSize_;
};

class GLVU_API HeightfieldTerrain
{
public:
private:
    std::unique_ptr<Buffer> vertexBuffer_;
};

class GLVU_API StaticModel : public SceneObject
{
public:
    StaticModel(GraphicsDevice*);

    std::vector< std::shared_ptr<Geometry> >& GetGeometries() { return geometries_; }
    const std::vector< std::shared_ptr<Geometry> >& GetGeometries() const { return geometries_; }

    std::vector< std::shared_ptr<Material> >& GetMaterials() { return materials_; }
    const std::vector< std::shared_ptr<Material> >& GetMaterials() const { return materials_; }

    static std::shared_ptr<StaticModel> Load(GraphicsDevice* device, const char* fileName);

    virtual void GetBatches(std::vector<Batch>& batches) override;

    std::shared_ptr<StaticModel> Clone() const;

    virtual math::AABB GetBounds() const { if (transformDirty_) UpdateTransform(); return bounds_; }

protected:
    virtual void UpdateTransform() const override;

    std::vector< std::shared_ptr<Geometry> > geometries_;
    std::vector< std::shared_ptr<Material> > materials_;
    mutable math::AABB bounds_;
    std::vector<Batch> batches_;
};

//****************************************************************************
//
//  Class:      InstanceCluster
//
//  Purpose:    SceneObject that groups it's contents into a single master batch.
//
//****************************************************************************
class GLVU_API InstanceCluster : public SceneObject
{
public:
    InstanceCluster(GraphicsDevice*);

    uint32_t PushInstancePosition(math::float3, math::Quat = Quat::identity, math::float3 = math::float3::one);
    void SetInstancePosition(uint32_t idx, math::float3, math::Quat = Quat::identity, math::float3 = math::float3::one);
    void SetInstancePosition(uint32_t idx, math::float4x4);
    void SetInstanceCount(uint32_t ct);
    inline uint32_t GetInstanceCount() const { return transforms_.size(); }
    virtual void GetBatches(std::vector<Batch>& batches) override;
    virtual math::AABB GetBounds() const override;
    virtual void DrawDebug(class DebugGeometry*) const;

    std::shared_ptr<Geometry> geometry_;
    std::shared_ptr<Material> material_;
    std::vector<math::float4x4> transforms_;
    std::vector<Batch> batches_;
    mutable math::AABB bounds_;
    mutable bool boundsDirty_ = true;
};

class GLVU_API SkeletalAnimation
{
public:
};

class GLVU_API SkeletalModel : public StaticModel
{
public:
    SkeletalModel(GraphicsDevice*);
    ~SkeletalModel();

private:
    struct MorphBuffer
    {
        std::shared_ptr<Buffer> buffer_;
        uint32_t slot;
    };
};

class GLVU_API Light : public SceneObject
{
public:
    Light(GraphicsDevice* device) : SceneObject(device) { }
    virtual ~Light() { }

    enum Kind
    {
        POINT,
        SPOT,
        DIRECTIONAL,
        AREA_SPHERE,
        AREA_QUAD
    };

    inline float GetFOV() const { return fov_; }
    inline float GetRadius() const { return radius_; }
    inline Kind GetKind() const { return kind_; }
    inline math::float4 GetColor() const { return color_; }
    inline bool IsShadowCasting() const { return shadowMask_ != 0; }
    inline unsigned GetShadowMask() const { return shadowMask_; }
    inline unsigned GetShadowMapCount() const { return kind_ == POINT ? 2 : 1; }

	void SetFOV(float fov);
    void SetRadius(float r) { radius_ = r; lightDirty_ = true; }
    void SetKind(Kind kind) { kind_ = kind; lightDirty_ = true; }
    void SetColor(const math::float4& c) { color_ = c; lightDirty_ = true; }
    void SetShadowMask(unsigned mask) { shadowMask_ = mask; lightDirty_ = true; }

    bool Contains(const math::AABB& bounds) const;
    bool Contains(const math::float3& pt) const;
    bool Contains(const Camera*) const;
    bool Contains(const math::Frustum&) const;

    virtual math::AABB GetBounds() const;

    unsigned GetNumberOfShadowMaps() const;
    void SetupShadowCamera(Camera*, uint32_t shadowMapIndex, const math::AABB& casterBounds) const;
    bool NeedsShadowRender(unsigned frame) const { return true; }// shadowFrame_ != frame; }
    void SetShadowDomain(std::shared_ptr<Texture>, math::uint4 viewRect, math::float4 sampleRect, unsigned mapIndex);
    math::float4 GetShadowDomain(unsigned index) const { return shadowSampleRect_[index]; }
    std::shared_ptr<Texture> GetShadowMapTexture() const { return shadowMap_; }

    void SetShadowDim(uint32_t value) { shadowDim_ = value; }
    uint32_t GetShadowDim() const { return shadowDim_; }

    math::float4x4 GetShadowMatrix(uint32_t index) const { return shadowMatrix_[index]; }

    inline bool IsAnalytic() const { return shadowMask_ == 0; }

private:
    virtual void MarkDirty() override { SceneObject::MarkDirty(); lightDirty_ = true; }

    uint32_t shadowDim_ = 256;
    float radius_ = 1.0f;
    float fov_ = 90.0f;                     // only applicable to spot lights
    Kind kind_ = POINT;
    math::float4 color_ = math::float4::one;
    /// This is &'ed with the shadow-mask in a batch, if any bits pass then we render it for shadows.
    unsigned shadowMask_ = 0xFFFFFFFF;
    bool static_ = false;
    mutable bool lightDirty_ = true;

    // State data.
    std::shared_ptr<Texture> shadowMap_;
    math::uint4 shadowViewRect_;
    math::float4 shadowSampleRect_[2];
    mutable math::AABB lastCasterBounds_;
    mutable math::float4x4 shadowMatrix_[2] = { math::float4x4::identity, math::float4x4::identity };

    mutable math::Frustum frustum_;

protected:
    math::Frustum ComputeFrustum() const;
};

class GLVU_API EnvironmentProbe : public SceneObject {
public:
    EnvironmentProbe(GraphicsDevice*);

    uint32_t arraySlot_ = UINT_MAX;
};

class GLVU_API Camera : public SceneObject
{
    friend class RenderScript;
public:
    Camera(GraphicsDevice*);

    void SetPerspective(float fov, float near, float far);
    void SetOrtho(float left, float right, float top, float bottom, float near, float far);
    void SetFullScreenOrtho() { SetOrtho(-1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f); }
    void SetViewport(const math::uint4 view) { viewport_ = view; RecalcViewport(); }

    inline math::float4x4 GetView() const { return GetTransform(); }
    inline math::float4x4 GetInvView() const { return GetInvTransform(); }
    math::float4x4 GetViewProjection(bool reversedZ = false) const;
    math::float4x4 GetViewMatrix() const;
    math::float4x4 GetInvViewProjection(bool reversedZ = false) const;
    math::uint4 GetViewport() const { return viewport_; }

    bool IsOrtho() const { return isOrtho_; }
    bool IsPerspective() const { return !isOrtho_; }        

    float GetViewportWidth() const { return (float)(viewport_.z); }
    float GetViewportHeight() const { return (float)(viewport_.w); }
    float GetAspectRatio() const { return GetViewportWidth() / GetViewportHeight(); }
    float GetNear() const { return near_; }
    float GetFar() const { return far_; }
    float GetFOV() const { return fov_; }

    bool CalculateVRCombined(Camera* leftEye, Camera* rightEye, bool changePosition);

    void DrawDebug(class DebugGeometry*);

    const math::Frustum& GetFrustum() const;

protected:
    virtual void UpdateTransform() const override { SceneObject::UpdateTransform(); RecalcCamera(); }
    float GetOrthoWidth() const { return fabsf(orthoDim_.y - orthoDim_.x); }
    float GetOrthoHeight() const { return fabsf(orthoDim_.w - orthoDim_.z); }

    virtual void MarkDirty() override { SceneObject::MarkDirty(); cameraDirty_ = true; }
    void RecalcCamera() const;
    void RecalcViewport();

    math::uint4 viewport_;
    math::float4 orthoDim_;
    float fov_;
    float near_;
    float far_;
    bool isOrtho_;
    mutable math::Frustum frus_;
    mutable bool cameraDirty_;

    // Dynamic state
    math::float3 lastPosition_;
    math::float3 lastDirection_;

private:
    math::float4x4 projection_;
};

class GLVU_API DebugGeometry : public SceneObject
{
public:
    DebugGeometry(GraphicsDevice*);
    virtual ~DebugGeometry();

    void AddLine(math::float3 start, math::float3 end, unsigned color, bool depthTest);
    void AddLine(math::float3 start, math::float3 end, math::float4 color, bool depthTest);
    void AddTriangle(math::float3 a, math::float3 b, math::float3 c, unsigned color, bool depthTest);
    void AddTriangle(math::float3 a, math::float3 b, math::float3 c, math::float4 color, bool depthTest);
    void AddAxis(math::float4x4 transform, float size, bool depthTest);

    virtual void GetBatches(std::vector<Batch>& batches) override;

    void Clear();
    void UpdateBuffers();

    virtual math::AABB GetBounds() const override { return bounds_; }

private:
        

    struct Vertex {
        math::float3 pos_;
        unsigned color_;
    };

    struct Line {
        Vertex start_;
        Vertex end_;
    };

    struct Triangle {
        Vertex a_, b_, c_;
    };

    struct GeometryCollection {
        std::vector<Line> lines_;
        std::vector<Triangle> triangles_;

        std::shared_ptr<Geometry> lineGeometry_;
        std::shared_ptr<Geometry> triGeometry_;

        std::shared_ptr<Buffer> lineVertices_;
        std::shared_ptr<Buffer> triVertices_;
    };

    GeometryCollection depthTested_;
    GeometryCollection notDepthTested_;
    GeometryCollection depthGreater_;
    std::shared_ptr<Material> material_;
    math::AABB bounds_;
    bool buffersDirty_;
};

}
