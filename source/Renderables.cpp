//****************************************************************************
//
//  File:       Renderables.cpp
//  License:    MIT
//  Project:    GLVU
//  Contents:   Crude helper classes for getting up off the ground.
//
//  Usage:      Not intended for serious usage.
//
//****************************************************************************

#include "Renderables.h"

#include "GraphicsDevice.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#undef min

using namespace math;
using namespace std;

namespace GLVU
{

//****************************************************************************
//
//  Function:   SceneObject::SceneObject
//
//  Purpose:    Construct, setup for identity location.
//
//****************************************************************************
SceneObject::SceneObject(GraphicsDevice* device) :
    device_(device),
    position_({ 0,0,0 }), 
    rotation_(Quat::identity), 
    scale_({ 1, 1, 1 })
{
    UpdateTransform();
}

//****************************************************************************
//
//  Function:   SceneObject::SceneObject
//
//  Purpose:    Construct at a specific point.
//
//****************************************************************************
SceneObject::SceneObject(GraphicsDevice* device, const float3& pos, const Quat& rot, const float3& scale) :
    device_(device),
    position_(pos), 
    rotation_(rot), 
    scale_(scale)
{
    UpdateTransform();
}

//****************************************************************************
//
//  Function:   SceneObject::GetTransform
//
//  Purpose:    Potentially need to update the transform.
//
//  Return:     4x4 mat that transforms a local point into world-space.
//
//****************************************************************************
float4x4 SceneObject::GetTransform() const
{
    if (transformDirty_)
        UpdateTransform();
    return transform_;
}

//****************************************************************************
//
//  Function:   SceneObject::GetInvTransform
//
//  Purpose:    Potentially need to update the transform.
//
//  Return:     4x4 inv mat that transforms an input into our local space.
//
//****************************************************************************
float4x4 SceneObject::GetInvTransform() const
{
    if (transformDirty_)
        UpdateTransform();
    return invTransform_;
}

//****************************************************************************
//
//  Function:   SceneObject::GetDirection
//
//  Purpose:    Returns the local-forward vector in worldspace (unitZ)
//
//****************************************************************************
float3 SceneObject::GetDirection() const
{
    return GetRotation() * float3(0, 0, 1);
}

//****************************************************************************
//
//  Function:   SceneObject::GetRight
//
//  Purpose:    Returns the local-up vector in worldspace (unitY)
//
//****************************************************************************
float3 SceneObject::GetUp() const
{
    return GetRotation() * float3(0, 1, 0);
}

//****************************************************************************
//
//  Function:   SceneObject::GetRight
//
//  Purpose:    Returns the local-right vector in worldspace (unitX)
//
//****************************************************************************
float3 SceneObject::GetRight() const
{
    return GetRotation() * float3(1, 0, 0);
}

//****************************************************************************
//
//  Function:   SceneObject::Translate
//
//  Purpose:    THIS IS NOT SET_POSITION!
//
//****************************************************************************
void SceneObject::Translate(const math::float3& offset, bool isLocal)
{
    if (!isLocal)
        SetPosition(position_ + offset);
    else
        SetPosition(position_ + (rotation_ * offset));
}

//****************************************************************************
//
//  Function:   SceneObject::Rotate
//
//  Purpose:    ??
//
//****************************************************************************
void SceneObject::Rotate(const math::Quat& offset, bool isLocal)
{
    if (!isLocal)
        SetRotation((offset * rotation_).Normalized());
    else
        SetRotation((rotation_ * offset).Normalized());
}

//****************************************************************************
//
//  Function:   SceneObject::Scale
//
//  Purpose:    Scales the object.
//
//****************************************************************************
void SceneObject::Scale(const math::float3& scaleBy, bool isLocal)
{
    if (isLocal)
        SetScale(scale_ + scaleBy);
    else
        SetScale(scale_ * scaleBy);
}

//****************************************************************************
//
//  Function:   SceneObject::RotateAround
//
//  Purpose:    Rotate the object around the given point.
//
//****************************************************************************
void SceneObject::RotateAround(const math::float3& pt, math::Quat& rot)
{
    float3 outerPt;
    auto oldRot = rotation_;

    if (false)
    {
        outerPt = GetTransform().TransformPos(pt);
        rotation_ = (rotation_ * rot).Normalized();
    }
    else
    {
        outerPt = pt;
        rotation_ = (rot * rotation_).Normalized();
    }

    float3 oldRelativePos = oldRot.Inverted() * (position_ - outerPt);
    position_ = rotation_ * oldRelativePos + outerPt;
    transformDirty_ = true;
}

//****************************************************************************
//
//  Function:   SceneObject::LookAt
//
//  Purpose:    Reorient object to look at the given point.
//
//****************************************************************************
void SceneObject::LookAt(const float3& pt)
{
    rotation_ = Quat::LookAt(float3::unitZ, (pt - GetPosition()).Normalized(), float3::unitY, float3::unitY);
    transformDirty_ = true;
}

//****************************************************************************
//
//  Function:   SceneObject::LookAtDir
//
//  Purpose:    Reorient object to look into the given direction.
//
//****************************************************************************
void SceneObject::LookAtDir(const math::float3& dir)
{
    rotation_ = Quat::LookAt(GetDirection(), dir.Normalized(), float3::unitY, float3::unitY);
}

//****************************************************************************
//
//  Function:   SceneObject::UpdateTransform
//
//  Purpose:    Recompute cached transform and inverse matrices.
//
//****************************************************************************
void SceneObject::UpdateTransform() const 
{
    transform_ = float4x4::FromTRS(position_, rotation_, scale_);
    invTransform_ = transform_.Inverted();
    transformDirty_ = false;
}

//****************************************************************************
//
//  Function:   Scene::Scene
//
//  Purpose:    Construct, nothing to see here.
//
//****************************************************************************
Scene::Scene()
{

}

//****************************************************************************
//
//  Function:   Scene::~Scene
//
//  Purpose:    Destruct, wipe contents.
//
//****************************************************************************
Scene::~Scene()
{
    contents_.clear();
    lights_.clear();
}

//****************************************************************************
//
//  Function:   Scene::Add
//
//  Purpose:    Adds the given object to the scene. Lights go into a seperate
//              list to ease some of tests.
//
//****************************************************************************
void Scene::Insert(shared_ptr<SceneObject> object)
{
    if (auto light = dynamic_pointer_cast<Light>(object))
        lights_.push_back(light);
    else
        contents_.push_back(object);
}

//****************************************************************************
//
//  Function:   Scene::Remove
//
//  Purpose:    Removes the given object form the scene.
//
//****************************************************************************
void Scene::Remove(shared_ptr<SceneObject> object)
{
    if (auto light = dynamic_pointer_cast<Light>(object))
    {
        auto found = find(lights_.begin(), lights_.end(), light);
        if (found != lights_.end())
            lights_.erase(found);
    }
    else
    {
        auto found = find(contents_.begin(), contents_.end(), object);
        if (found != contents_.end())
            contents_.erase(found);
    }
}

//****************************************************************************
//
//  Function:   Scene::GetBatches
//
//  Purpose:    Grabs drawable batches from the scene contents that are found
//              inside of the provided frustum.
//
//              This function should naturally do something with an acceleration
//              structure like an Octree or at least a Quadtree.
//
//  Return:     list of batches
//
//****************************************************************************
vector<Batch> Scene::GetBatches(const math::Frustum& frustum)
{
    vector<Batch> batches;
    for (size_t i = 0; i < contents_.size(); ++i)
    //for (auto object : contents_)
    {
        auto& object = contents_[i];
        //if (frustum.Intersects(object->GetBounds()))
		if (frustum.Intersects(object->GetBounds()))
        //if (frustum.IsInsideFast(object->GetBounds()))
            object->GetBatches(batches);
    }
    return batches;
}

//****************************************************************************
//
//  Function:   Scene::GetLights
//
//  Purpose:    Grabs the lights contained by the given frustum.
//
//              In practice an acceleration structure should be used.
//
//  Return:     list of light pointers
//
//****************************************************************************
vector<shared_ptr<Light> > Scene::GetLights(const math::Frustum& frustum)
{
    vector<shared_ptr<Light>> lights;
    for (auto& light : lights_)
    {
        if (frustum.IsInsideFast(light->GetBounds()))
        //if (frustum.Intersects(light->GetBounds()))
            lights.push_back(light);
    }
    return lights;
}

//****************************************************************************
//
//  Function:   Scene::GetBatches
//
//  Purpose:    Grabs the batches that are contained by the bounds of the light.
//
//              In practice an acceleration structure should be used.
//
//  Return:     List of all batches
//
//****************************************************************************
vector<Batch> Scene::GetBatches(const Light* forLight, unsigned shadowMapIndex, AABB& totalBnds)
{
    vector<Batch> batches;

    Plane lightPlane(forLight->GetPosition(), float3::unitZ);

    for (auto obj : contents_)
    {
        auto bnds = obj->GetBounds();
        if (forLight->Contains(bnds))
        {
            if (forLight->GetKind() == Light::POINT)
            {
                auto sdf = lightPlane.SignedDistance(bnds);
                bool intersects = lightPlane.Intersects(bnds);
#if defined(GLVU_VK)
                if ((intersects || sdf >= 0) && shadowMapIndex == 1)
                    obj->GetBatches(batches);
                else if ((intersects || sdf <= 0) && shadowMapIndex == 0)
                    obj->GetBatches(batches);
#else
                //if ((intersects || sdf >= 0) && shadowMapIndex == 0)
                //    obj->GetBatches(batches);
                //else if ((intersects || sdf <= 0) && shadowMapIndex == 1)
                    obj->GetBatches(batches);
                    totalBnds.Enclose(bnds);
#endif
            }
            else
                obj->GetBatches(batches);
        }
    }
    return batches;
}

//****************************************************************************
//
//  Function:   Light::SetFOV
//
//  Purpose:    Sets the FOV for this light, although it's small enough to be
//				okay for placement in header, it conflicts with #define min in windows
//
//****************************************************************************
void Light::SetFOV(float fov) 
{ 
	fov_ = std::min(fov_, 120.0f); 
	lightDirty_ = true; 
}

//****************************************************************************
//
//  Function:   Light::Contains
//
//  Purpose:    Utility
//
//  Return:     True if the box is contained/intersected by the light.
//
//****************************************************************************
bool Light::Contains(const math::AABB& bounds) const
{
    // errr???
    if (kind_ == DIRECTIONAL)
        return true;
    else if (kind_ == SPOT)
        return ComputeFrustum().IsInsideFast(bounds);
    else if (kind_ == POINT || kind_ == AREA_SPHERE)
        return Sphere(GetPosition(), radius_).Intersects(bounds);
    return false;
}

//****************************************************************************
//
//  Function:   Light::Contains
//
//  Purpose:    Utility
//
//  Return:     True if the point is contained by the light.
//
//****************************************************************************
bool Light::Contains(const math::float3& pt) const
{
    // errr???
    if (kind_ == DIRECTIONAL)
        return true;
    else if (kind_ == POINT || kind_ == AREA_SPHERE)
        return Sphere(GetPosition(), radius_).Contains(pt);
    else if (kind_ == SPOT)
        return ComputeFrustum().Contains(pt);
    return false;
}

//****************************************************************************
//
//  Function:   Light::Contains
//
//  Purpose:    Selecting whether a light is relevant to a given camera.
//              Can't process *all* the lights in a scene.
//
//  Return:     true if this light intersects with the camera.
//
//****************************************************************************
bool Light::Contains(const Camera* cam) const
{
    float d = FLT_MAX;
    if (kind_ == DIRECTIONAL)
        return true;
    else if (kind_ == POINT || kind_ == AREA_SPHERE)
        d = Sphere(GetPosition(), radius_ * 1.25f).Distance(cam->GetPosition());
    else if (kind_ == SPOT)
        d = ComputeFrustum().Distance(cam->GetPosition());
    return d < cam->GetNear() * 2;
}

//****************************************************************************
//
//  Function:   Light::Contains
//
//  Purpose:    Selecting whether a light is relevant to a given frustum.
//
//  Return:     true if this light intersects with the frustum.
//
//****************************************************************************
bool Light::Contains(const math::Frustum& frus) const
{
    if (kind_ == DIRECTIONAL)
        return true;
    else if (kind_ == POINT || kind_ == AREA_SPHERE)
        return Sphere(GetPosition(), radius_ * 1.25f).Intersects(frus);
    else if (kind_ == SPOT)
        return ComputeFrustum().Intersects(frus);
    return false;
}

//****************************************************************************
//
//  Function:   Light::GetBounds
//
//  Purpose:    The bounds is used for shadow-maps and tiled/clustered lighting.
//
//  Return:     AABB that encloses this light, or chaos if a directional light.
//
//****************************************************************************
AABB Light::GetBounds() const 
{
    if (kind_ == POINT)
        return AABB(Sphere(GetPosition(), radius_));
    else if (kind_ == SPOT)
        return ComputeFrustum().MinimalEnclosingAABB();
    else
        return AABB(float3(FLT_MIN, FLT_MIN, FLT_MIN), float3(FLT_MAX, FLT_MAX, FLT_MAX));
}

//****************************************************************************
//
//  Function:   Light::GetNumberOfShadowMaps
//
//  Purpose:    Internal utility. Doesn't take into account *actual* shadowing
//              needed.
//
//  Return:     The number of shadow-maps needed to render for this light.
//
//****************************************************************************
unsigned Light::GetNumberOfShadowMaps() const
{
    if (kind_ == POINT) return 2;
    if (kind_ == SPOT) return 1;
    if (kind_ == DIRECTIONAL) return 2;
    return 1;
}

//****************************************************************************
//
//  Function:   Light::SetupShadowCamera
//
//  Purpose:    Configures the given camera object as needed for rendering a shadow
//              map (of a given index) for this light.
//              Touches some state because of the need for an AABB to fit directional
//              lights to the contents.
//
//****************************************************************************
void Light::SetupShadowCamera(Camera* camera, uint32_t shadowMapIndex, const AABB& casterBounds) const
{
    if (kind_ == SPOT)
    {
        camera->SetPosition(GetPosition());
        camera->SetRotation(GetRotation());
        camera->SetPerspective(fov_, 0.001f, radius_);
    }
    else if (kind_ == POINT)
    {
        camera->SetPosition(GetPosition());
        camera->SetRotation(Quat::identity);
        if (shadowMapIndex == 1)
            camera->Rotate(Quat(float3::unitY, DegToRad(180)), false);

        camera->SetPerspective(90.0, 0.00001f, radius_);
    }
    else if (kind_ == DIRECTIONAL)
    {
        camera->SetPosition(GetPosition());
        camera->SetPosition(float3(0, 0, 0));
        camera->SetRotation(GetRotation());

        AABB bnds = casterBounds;
        if (!bnds.IsFinite())
            bnds = lastCasterBounds_;
        else
            lastCasterBounds_ = bnds;

        auto boxMin = camera->ToLocalSpace(bnds.minPoint);
        auto boxMax = boxMin;

        for (int i = 0; i < 8; ++i)
        {
            auto boxSample = camera->ToLocalSpace(bnds.CornerPoint(i));
            boxMin = boxMin.Min(boxSample);
            boxMax = boxMax.Max(boxSample);
        }

        auto scl = boxMax.z - boxMin.z;
        camera->SetOrtho(boxMin.x, boxMax.x, boxMin.y, boxMax.y, boxMin.z, boxMax.z);
    }

    shadowMatrix_[shadowMapIndex] = camera->GetInvViewProjection();
}

//****************************************************************************
//
//  Function:   Light::SetShadowDomain
//
//  Purpose:    Sets the UV and viewport coordinates for a shadow-map of the given index,
//              spot-lights only use 1, but point and directional lights use up to 2
//
//****************************************************************************
void Light::SetShadowDomain(shared_ptr<Texture> sm, uint4 viewRect, float4 sampleRect, unsigned mapIndex)
{
    assert(mapIndex < 2);
    shadowMap_ = sm;
    shadowViewRect_ = viewRect;
    shadowSampleRect_[mapIndex] = sampleRect;
}

//****************************************************************************
//
//  Function:   Light::ComputeFrustum
//
//  Purpose:    Calculates a frustum for a spotlight.
//
//  Return:     a MathGeoLib frustum object fitting the light as a spot-light.
//
//****************************************************************************
Frustum Light::ComputeFrustum() const
{
    //if (lightDirty_)
    {
        lightDirty_ = false;

        Frustum f;
        f.ConfigurePerspectiveLH_OGL(GetPosition(), GetUp(), GetDirection(), DegToRad(fov_), 0.0001f, radius_);
        frustum_ = f;
    }
    return frustum_;
}

//****************************************************************************
//
//  Function:   Camera::Camera
//
//  Purpose:    Contruct.
//
//****************************************************************************
Camera::Camera(GraphicsDevice* device) : 
    SceneObject(device) 
{
}

//****************************************************************************
//
//  Function:   Camera::GetViewProjection
//
//  Purpose:    Gets the VP matrix, with any coordinate system conversions needed.
//
//  Return:     4x4 combined matrix
//
//****************************************************************************
float4x4 Camera::GetViewProjection(bool reversedZ) const
{
    //if (cameraDirty_)
        RecalcCamera();

#if defined(GLVU_GL) || defined(GLVU_GLES3)
    return frus_.ComputeViewProjMatrix();
#elif defined(GLVU_VK)
    static const float4x4 viewShapeMat = float4x4(
        1, 0, 0, 0,
        0, -1, 0, 0,
        0, 0, 0.5f, 0.5f,
        0, 0, 0, 1);
    return viewShapeMat * frus_.ComputeViewProjMatrix();
#elif defined(GLVU_DX11) || defined(GLVU_DX12)
	static const float4x4 viewShapeMat = float4x4(
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0, //squash and shift
		0, 0, 0, 1);
	return viewShapeMat * frus_.ComputeViewProjMatrix();
#else
	return frus_.ComputeViewProjMatrix();
#endif
}

//****************************************************************************
//
//  Function:   Camera::GetViewMatrix
//
//  Purpose:    Gets the orthonormal transform matrix that deals with the translation
//              and rotation necessary to bring something into camera space.
//
//  Return:     4x4 orthonormal mat
//
//****************************************************************************
math::float4x4 Camera::GetViewMatrix() const
{
    RecalcCamera();
    return frus_.ComputeViewMatrix();
}

//****************************************************************************
//
//  Function:   Camera::GetInvViewProjection
//
//  Purpose:    Vestigial, calls GetViewProjection
//
//  DEPRECATED
//
//****************************************************************************
float4x4 Camera::GetInvViewProjection(bool reverseZ) const
{
    if (cameraDirty_)
        RecalcCamera();
    return GetViewProjection(reverseZ);
}

//****************************************************************************
//
//  Function:   Camera::SetPerspective
//
//  Purpose:    Configures the frustum as a perspective camera, performs FOV-degrees conversion
//
//****************************************************************************
void Camera::SetPerspective(float fov, float nearDist, float farDist)
{
    isOrtho_ = false;
    fov_ = fov;
    near_ = nearDist;
    far_ = farDist;

    projection_ = float4x4::zero;

    float h = (1.0f / tanf(DegToRad(fov * 0.5f))) * 1.0f;
    float w = h / GetAspectRatio();
        
    projection_ = float4x4::OpenGLPerspProjLH(near_, far_, w, h);

    RecalcCamera();
}

//****************************************************************************
//
//  Function:   Camera::SetOrtho
//
//  Purpose:    Configures the frustum as an orthographic camera.
//
//****************************************************************************
void Camera::SetOrtho(float left, float right, float top, float bottom, float nearDist, float farDist)
{
    isOrtho_ = true;
    orthoDim_ = float4 { left, right, top, bottom };
    near_ = nearDist;
    far_ = farDist;
    projection_ = float4x4::OpenGLOrthoProjLH(near_, far_, (right - left), (top - bottom));// .Inverted();

    RecalcCamera();
}

//****************************************************************************
//
//  Function:   Camera::RecalcViewport
//
//  Purpose:    ??
//
//****************************************************************************
void Camera::RecalcViewport()
{
    if (!isOrtho_)
        SetPerspective(fov_, near_, far_);
    else
        SetOrtho(orthoDim_.x, orthoDim_.y, orthoDim_.z, orthoDim_.w, near_, far_);
}

//****************************************************************************
//
//  Function:   Camera::DrawDebug
//
//  Purpose:    Writes the frustum box-edges to a debug-geometry.
//
//****************************************************************************
void Camera::DrawDebug(DebugGeometry* debug)
{
    for (int i = 0; i < 12; ++i)
    {
        auto edge = frus_.Edge(i);
        debug->AddLine(edge.a, edge.b, float4(0, 0, 1, 1), false);
        //debug->AddLine(edge.a, edge.b, float4(1, 1, 0, 1), false);
    }
}

//****************************************************************************
//
//  Function:   Camera::GetFrustum
//
//  Purpose:    Returns the frustum object, potentially recalculating it.
//
//****************************************************************************
const math::Frustum& Camera::GetFrustum() const 
{ 
    //if (cameraDirty_) 
    RecalcCamera(); 
    return frus_; 
}

//****************************************************************************
//
//  Function:   Camera::RecalcCamera
//
//  Purpose:    Recomputes the frustum object.
//
//****************************************************************************
void Camera::RecalcCamera() const
{
    if (IsPerspective())
    {
        frus_.SetKind(FrustumSpaceGL, FrustumLeftHanded);
        frus_.SetPos(GetPosition());
        frus_.SetUp(GetUp());
        frus_.SetFront(GetDirection());

        float aspect = GetAspectRatio();
        frus_.SetViewPlaneDistances(near_, far_);
        frus_.SetPerspective(DegToRad(fov_), DegToRad(fov_) / aspect);
    }
    else
    {
        frus_.SetKind(FrustumSpaceGL, FrustumLeftHanded);
        frus_.SetPos(GetPosition());
        frus_.SetUp(GetUp());
        frus_.SetFront(GetDirection());

        float aspect = GetAspectRatio();
        frus_.SetViewPlaneDistances(near_, far_);
        frus_.SetOrthographic(GetOrthoWidth(), GetOrthoHeight());
    }
    cameraDirty_ = false;
}

//****************************************************************************
//
//  Function:   Camera::CalculateVRCombined
//
//  Purpose:    Calculates a combined frustum for VR, can move this camera or not.
//
//****************************************************************************
bool Camera::CalculateVRCombined(Camera* leftEye, Camera* rightEye, bool changePosition)
{
    if (leftEye->GetFOV() + rightEye->GetFOV())
        return false;

    auto newEyePt = GetPosition();

    // might not want to change the position?
    if (changePosition)
    {
        // get rays for projection space coordinates
        auto leftRay = leftEye->frus_.UnProject(-1, 0);
        auto rightRay = rightEye->frus_.UnProject(1, 0);

        newEyePt = leftRay.ClosestPoint(rightRay);
    }

    auto angleBetweenRad = (leftEye->GetPosition() - newEyePt).AngleBetween(rightEye->GetPosition() - newEyePt);
    SetPerspective(RadToDeg(angleBetweenRad), leftEye->GetNear(), leftEye->GetFar());

    if (changePosition)
    {
        SetPosition(newEyePt);

        auto lookPos = leftEye->GetPosition().Lerp(rightEye->GetPosition(), 0.5f);
        LookAt(lookPos);
    }

    return true;
}

//****************************************************************************
//
//  Function:   StaticModel::StaticModel
//
//  Purpose:    Construct.
//
//****************************************************************************
StaticModel::StaticModel(GraphicsDevice* device) : SceneObject(device)
{

}

//****************************************************************************
//
//  Function:   StaticModel::Load
//
//  Purpose:    Loads an OBJ file, just a utility and not intended for
//              real world usage. The geometries are unique.
//
//  Return:     Newly loaded model, or null if failure.
//
//****************************************************************************
shared_ptr<StaticModel> StaticModel::Load(GraphicsDevice* device, const char* fileName)
{
    shared_ptr<StaticModel> ret;
    {
        tinyobj::attrib_t attrib;
        vector<tinyobj::shape_t> shapes;
        vector<tinyobj::material_t> materials;

        string warn;
        string err;

        if (tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, fileName))
        { 
            ret.reset(new StaticModel(device));

            // core of this loop is from tiny_obj
            for (size_t s = 0; s < shapes.size(); s++) 
            {
                struct V {
                    float3 pos;
                    float3 norm;
                    float2 uv;
                };
                vector<V> vertices;
                vertices.reserve(shapes[s].mesh.num_face_vertices.size() * 3);

                // Loop over faces(polygon)
                size_t index_offset = 0;
                math::AABB bounds;
                bounds.SetNegativeInfinity();
                for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) 
                {
                    int fv = shapes[s].mesh.num_face_vertices[f];

                    // Loop over vertices in the face.
                    for (size_t v = 0; v < fv; v++) 
                    {
                        // access to vertex
                        tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                        tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
                        tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
                        tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
                            
                        tinyobj::real_t nx = 0.0f;
                        tinyobj::real_t ny = 0.0f;
                        tinyobj::real_t nz = 0.0f;

                        tinyobj::real_t tx = 0.0f;
                        tinyobj::real_t ty = 0.0f;

                        if (idx.normal_index != -1)
                        {
                            nx = attrib.normals[3 * idx.normal_index + 0];
                            ny = attrib.normals[3 * idx.normal_index + 1];
                            nz = attrib.normals[3 * idx.normal_index + 2];
                        }
                            
                        if (idx.texcoord_index != -1)
                        {
                            tx = attrib.texcoords[2 * idx.texcoord_index + 0];
                            ty = attrib.texcoords[2 * idx.texcoord_index + 1];
                        }

                        vertices.push_back({ { vx, vy, vz }, { nx, ny, nz }, { tx, ty } });                            
                        bounds.Enclose({ vx, vy, vz });
                    }

                    index_offset += fv;

                    shapes[s].mesh.material_ids[f];
                }

                shared_ptr<Buffer> vbo = device->CreateVertexBuffer();
                vbo->SetData(vertices.data(), vertices.size() * sizeof(V));

                shared_ptr<Geometry> geo(new Geometry());
                geo->vertexBuffers_.push_back(vbo);
                geo->indexBuffer_ = device->GetSequentialIndexBuffer();
                geo->vertexCount_ = vertices.size();
                geo->primType_ = TRIANGLE_LIST;
                geo->primCount_ = vertices.size() / 3;
                geo->indexCount_ = geo->primCount_ * 3;
                geo->bounds_ = bounds;

                geo->layout_ = device->GetLayout_PosNormUV();

                ret->geometries_.push_back(geo);
            }
        }

        if (warn.length() > 0)
            device->LogMessage(warn.c_str(), GLVU_WARNING);
        if (err.length() > 0)
            device->LogMessage(err.c_str(), GLVU_ERROR);
    }
    return ret;
}

//****************************************************************************
//
//  Function:   StaticModel::GetBatches
//
//  Purpose:    Converts each provided geometry into a batch.
//
//****************************************************************************
void StaticModel::GetBatches(vector<Batch>& batches)
{
    if (transformDirty_)
        UpdateTransform();

    if (batches_.size() != geometries_.size())
    {
        batches_.clear();
        for (uint32_t i = 0; i < geometries_.size(); ++i)
            batches_.push_back(Batch{
                geometries_[i].get(),
                materials_[0].get(),
                &transform_,
                1,
                false,
                true
            });
    }

    if (batches_.size() > 0)
    {
        for (uint32_t i = 0; i < batches_.size(); ++i)
            batches.emplace_back(batches_[i]);
    }
}

//****************************************************************************
//
//  Function:   StaticModel::UpdateTransform
//
//  Purpose:    Bounding-box needs to be recalculated whenever transform changes.
//
//****************************************************************************
inline AABB BoxTransform(const AABB& src, const float4x4& transform)
{
    float3 newCenter = transform.TransformPos(src.CenterPoint());
    float3 oldEdge = src.Size() * 0.5f;
    float3 newEdge = float3(
        Abs(transform.v[0][0]) * oldEdge.x + Abs(transform.v[0][1]) * oldEdge.y + Abs(transform.v[0][2]) * oldEdge.z,
        Abs(transform.v[1][0]) * oldEdge.x + Abs(transform.v[1][1]) * oldEdge.y + Abs(transform.v[1][2]) * oldEdge.z,
        Abs(transform.v[2][0]) * oldEdge.x + Abs(transform.v[2][1]) * oldEdge.y + Abs(transform.v[2][2]) * oldEdge.z
    );

    return AABB(newCenter - newEdge, newCenter + newEdge);
}

void StaticModel::UpdateTransform() const
{
    SceneObject::UpdateTransform();
    bounds_.SetNegativeInfinity();
    for (unsigned i = 0; i < geometries_.size(); ++i)
    {
        auto bnds = geometries_[i]->bounds_;
        bnds.TransformAsAABB(transform_);
        bounds_.Enclose(bnds);
        //auto bnds = geometries_[i]->bounds_.Transform(transform_);
        //bnds.Scale(bnds.Centroid(), 2);
        //bounds_.Enclose(bnds.MinimalEnclosingAABB());
        //bounds_.Enclose(BoxTransform(geometries_[i]->bounds_, transform_));
    }
}

//****************************************************************************
//
//  Function:   StaticModel::Clone
//
//  Purpose:    Duplicates this object into a new one, does not clone materials
//              or geometries, making this shallow.
//
//  Return:     A shallow-copy of this object.
//
//****************************************************************************
shared_ptr<StaticModel> StaticModel::Clone() const
{
    shared_ptr<StaticModel> ret(new StaticModel(device_));
    ret->position_ = position_;
    ret->rotation_ = rotation_;
    ret->scale_ = scale_;
    ret->transform_ = transform_;
    ret->invTransform_ = invTransform_;
    ret->transformDirty_ = transformDirty_;

    ret->geometries_ = geometries_;
    ret->materials_ = materials_;

    return ret;
}

InstanceCluster::InstanceCluster(GraphicsDevice* device) :
    SceneObject(device), boundsDirty_(true)
{

}

uint32_t InstanceCluster::PushInstancePosition(math::float3 p, math::Quat r, math::float3 s)
{
    boundsDirty_ = true;
    transforms_.push_back(float4x4::FromTRS(p, r, s));
    return transforms_.size() - 1;
}

//****************************************************************************
//
//  Function:   InstanceCluster::SetInstancePosition
//
//  Purpose:    Sets the transform (from a decomposed from) for an instance
//              at the given index.
//
//****************************************************************************
void InstanceCluster::SetInstancePosition(uint32_t idx, float3 p, Quat r, float3 s)
{
    boundsDirty_ = true;
    transforms_[idx] = float4x4::FromTRS(p, r, s);
}

//****************************************************************************
//
//  Function:   InstanceCluster::SetInstancePosition
//
//  Purpose:    Sets the transform of given instance index from this cluster.
//
//****************************************************************************
void InstanceCluster::SetInstancePosition(uint32_t idx, float4x4 m)
{
    boundsDirty_ = true;
    transforms_[idx] = m;
}

//****************************************************************************
//
//  Function:   InstanceCluster::SetInstanceCount
//
//  Purpose:    Determines the number of transforms that should be stored,
//              this is not just an allocation but also an implied count so
//              each transform must be used.
//
//****************************************************************************
void InstanceCluster::SetInstanceCount(uint32_t ct)
{
    boundsDirty_ = true;
    transforms_.resize(ct);
}

//****************************************************************************
//
//  Function:   InstanceCluster::GetBatches
//
//  Purpose:    Grabs the batches, as one batch-item per geometry - but with
//              the relevant number of transforms.
//
//****************************************************************************
void InstanceCluster::GetBatches(vector<Batch>& batches)
{
    if (transforms_.size() > 0)
    {
        batches.push_back({
            geometry_.get(),
            material_.get(),
            (float4x4*)transforms_.data(),
            (uint32_t)transforms_.size(),
            false,
            true
            });
    }
}

math::AABB InstanceCluster::GetBounds() const
{
    if (boundsDirty_)
    {
        auto srcBnds = geometry_->bounds_;
        AABB outBnds;

        for (int i = 0; i < transforms_.size(); ++i)
        {
            auto rBnds = srcBnds;
            rBnds.TransformAsAABB(transforms_[i]);
            if (i == 0)
                outBnds = rBnds;
            else
                outBnds.Enclose(rBnds);
        }

        bounds_ = outBnds;
        boundsDirty_ = false;
    }
    return bounds_;
}

void InstanceCluster::DrawDebug(DebugGeometry* debugGeo) const
{
    auto bnds = GetBounds();
    for (int i = 0; i < bnds.NumEdges(); ++i)
    {
        auto l = bnds.Edge(i);
        debugGeo->AddLine(l.a, l.b, float4(0, 1, 0, 1), false);
    }
}

//****************************************************************************
//
//  Function:   DebugGeometry::DebugGeometry
//
//  Purpose:    Construct, foolishly loads the material.
//
//****************************************************************************
DebugGeometry::DebugGeometry(GraphicsDevice* device) : 
    SceneObject(device), 
    buffersDirty_(false)
{
    material_ = Material::Load(device, "DebugDraw.mat");
    Clear();
}

//****************************************************************************
//
//  Function:   DebugGeometry::~DebugGeometry
//
//  Purpose:    Destruct.
//
//****************************************************************************
DebugGeometry::~DebugGeometry()
{

}

//****************************************************************************
//
//  Function:   DebugGeometry::Clear
//
//  Purpose:    Wipes the CPU stage data, but doesn't touch the GPU objects.
//
//****************************************************************************
void DebugGeometry::Clear()
{
#define CLEARV(V) V.lines_.clear(); V.triangles_.clear()
        
    CLEARV(depthTested_);
    CLEARV(notDepthTested_);
    CLEARV(depthGreater_);

    bounds_.SetNegativeInfinity();
    buffersDirty_ = true;
}

//****************************************************************************
//
//  Function:   DebugGeometry::AddLine
//
//  Purpose:    Pushes a line into the list and updates bounds to fit if needed.
//
//****************************************************************************
void DebugGeometry::AddLine(math::float3 start, math::float3 end, unsigned color, bool depthTest)
{
    (depthTest ? depthTested_ : notDepthTested_).lines_.push_back({ { start, color }, { end, color } });
    bounds_.Enclose(start);
    bounds_.Enclose(end);
    buffersDirty_ = true;
}

//****************************************************************************
//
//  Function:   DebugGeometry::AddLine
//
//  Purpose:    Wrapper, converts color
//
//****************************************************************************
void DebugGeometry::AddLine(math::float3 start, math::float3 end, math::float4 color, bool depthTest)
{
    unsigned c = ((unsigned)(color.w * 255.0f) << 24) | ((unsigned)(color.z * 255.0f) << 16) | ((unsigned)(color.y * 255.0f) << 8) | ((unsigned)(color.x * 255.0f));
    AddLine(start, end, c, depthTest);
}

//****************************************************************************
//
//  Function:   DebugGeometry::AddTriangle
//
//  Purpose:    Pushes a triangle to the object and ensures the bounds encloses the
//              drawn debug-geometry.
//
//****************************************************************************
void DebugGeometry::AddTriangle(math::float3 a, math::float3 b, math::float3 c, unsigned color, bool depthTest) 
{
    bounds_.Enclose(a);
    bounds_.Enclose(b);
    bounds_.Enclose(c);

    (depthTest ? depthTested_ : notDepthTested_).triangles_.push_back({ { a, color }, { b, color }, { c, color} });
    buffersDirty_ = true;
}

//****************************************************************************
//
//  Function:   DebugGeometry::AddTriangle
//
//  Purpose:    Wrapper, does color conversion.
//
//****************************************************************************
void DebugGeometry::AddTriangle(float3 a, float3 b, float3 c, float4 color, bool depthTest)
{
    unsigned col = ((unsigned)(color.w * 255.0f) << 24) | ((unsigned)(color.z * 255.0f) << 16) | ((unsigned)(color.y * 255.0f) << 8) | ((unsigned)(color.x * 255.0f));
    AddTriangle(a, b, c, col, depthTest);
}

//****************************************************************************
//
//  Function:   DebugGeometry::AddAxis
//
//  Purpose:    Draws an RGB axis coordinate indicator
//
//****************************************************************************
void DebugGeometry::AddAxis(math::float4x4 transform, float size, bool depthTest)
{
    auto origin = transform.TranslatePart();
    auto foreward = transform.MulDir(float3::unitZ);
    auto right = transform.MulDir(float3::unitX);
    auto up = transform.MulDir(float3::unitY);

    AddLine(origin, origin + foreward * size, 0xFFFF0000, depthTest);
    AddLine(origin, origin + right * size, 0xFF0000FF, depthTest);
    AddLine(origin, origin + up * size, 0xFF00FF00, depthTest);
}

//****************************************************************************
//
//  Function:   DebugGeometry::GetBatches
//
//  Purpose:    Gets the batches for this, if necessary it udpates the buffers.
//
//****************************************************************************
void DebugGeometry::GetBatches(vector<Batch>& batches)
{
    if (buffersDirty_)
        UpdateBuffers();
    buffersDirty_ = false;

    static auto handleGrp = [](const DebugGeometry* self, const GeometryCollection& col, vector<Batch>& batches, shared_ptr<Material> mat) {
        if (col.lines_.size() > 0)
        {
            batches.push_back(Batch {
                col.lineGeometry_.get(),
                mat.get(),
                &self->transform_,
                1,
                false,
                false,
                FLT_MAX
            });
        }
        if (col.triangles_.size() > 0)
        {
            batches.push_back({
                col.triGeometry_.get(),
                mat.get(),
                &self->transform_,
                1,
                false,
                false,
                FLT_MAX
            });
        }
    };

    handleGrp(this, notDepthTested_, batches, material_);
    handleGrp(this, depthTested_, batches, material_);
    handleGrp(this, depthGreater_, batches, material_);
}

//****************************************************************************
//
//  Function:   DebugGeometry::UpdateBuffers
//
//  Purpose:    Allocates and transfers the CPU staged data into the GPU buffers.
//
//****************************************************************************
void DebugGeometry::UpdateBuffers()
{
    static auto update = [](GraphicsDevice* device, GeometryCollection& col)
    {
        if (col.lines_.size())
        {
            if (col.lineVertices_ == nullptr)
                col.lineVertices_ = device->CreateVertexBuffer();

            col.lineVertices_->SetSize(col.lines_.size() * sizeof(Line));
            col.lineVertices_->SetData(col.lines_.data(), col.lines_.size() * sizeof(Line));
            col.lineGeometry_ = Geometry::Create(LINE_LIST, device->GetLayout_PosColor(), col.lineVertices_, nullptr);
        }
        else
            col.lineGeometry_.reset();

        if (col.triangles_.size())
        {
            if (col.triVertices_ == nullptr)
                col.triVertices_ = device->CreateVertexBuffer();

            col.triVertices_->SetSize(col.triangles_.size() * sizeof(Triangle));
            col.triVertices_->SetData(col.triangles_.data(), (uint32_t)col.triangles_.size() * sizeof(Triangle));
            col.triGeometry_ = Geometry::Create(TRIANGLE_LIST, device->GetLayout_PosColor(), col.triVertices_, nullptr);
        }
    };

    update(device_, notDepthTested_);
    update(device_, depthTested_);
    update(device_, depthGreater_);
}
}