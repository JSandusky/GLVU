#include "RenderScript.h"
#include "RenderScript_Types.h"
#include "ShaderConstants.h"
#include "Renderables.h"

namespace GLVU
{

//****************************************************************************
//
//  Function:   SetupViewData
//
//  Purpose:    Performs common calculations regarding viewports and targets
//              that need to be put into the ViewData structure used for
//              shader constants. Eliminates what would otherwise be a lot
//              of repetitive code.
//
//****************************************************************************
void SetupViewData(ViewData& target, math::uint4 viewport, RenderTargetInfo* srcTex, RenderTargetInfo* destTex)
{
    if (srcTex)
        target.inputTexSize = float2(srcTex->width_, srcTex->height_);
    else
        target.inputTexSize = float2(viewport[2], viewport[3]);

    if (destTex)
        target.outputTexSize = float2(destTex->width_, destTex->height_);
    else
        target.outputTexSize = float2(viewport[2], viewport[3]);

    target.invInputTexSize = float2(1, 1) /  target.inputTexSize;
    target.invOutputTexSize = float2(1, 1) / target.outputTexSize;
}

//****************************************************************************
//
//  Function:   SetupViewbufferData
//
//  Purpose:    Performs repetitive configuration of ViewBufferData structure
//              used for shader constants. Also configures for VR.
//
//****************************************************************************
void SetupViewbufferData(const View& view, ViewBufferData& data)
{
    data.viewProj[0] = view.cameras_[0]->GetViewProjection(view.TestFlag(ViewFlag_ReverseZ));
    data.invViewProj[0] = view.cameras_[0]->GetInvViewProjection(view.TestFlag(ViewFlag_ReverseZ));
    
    data.viewport = view.viewport_;
    view.cameras_[0]->SetViewport(view.viewport_);

    data.nearFar = { view.cameras_[0]->GetNear(), view.cameras_[0]->GetFar(), 0, 0 };

    data.viewPos[0] = { view.cameras_[0]->GetPosition(), 0.0f };
    data.viewDir[0] = float4 { view.cameras_[0]->GetDirection(), 1.0f };
    data.viewUp[0] = float4 { view.cameras_[0]->GetUp(), 1.0f };

    if (view.cameras_[1])
    {
        data.viewProj[1] = view.cameras_[1]->GetViewProjection(view.TestFlag(ViewFlag_ReverseZ));
        data.invViewProj[1] = view.cameras_[1]->GetInvViewProjection(view.TestFlag(ViewFlag_ReverseZ));

        data.viewPos[1] = { view.cameras_[0]->GetPosition(), 0.0f };
        data.viewDir[1] = float4 { view.cameras_[0]->GetDirection(), 1.0f };
        data.viewUp[1] = float4 { view.cameras_[0]->GetUp(), 1.0f };
    }
}

//****************************************************************************
//
//  Function:   SetupViewbufferData
//
//  Purpose:    Camera tied version of utility function to setup ViewBufferData
//              shader constant structure. This function is currently only used
//              for the offscreen rendering pass (used for low resolution lighting).
//
//****************************************************************************
void SetupViewbufferData(const Camera* camera, ViewBufferData& data, int targetEye)
{
    if (camera == nullptr)
        return;
    data.viewProj[targetEye] = camera->GetViewProjection();
    data.invViewProj[targetEye] = camera->GetInvViewProjection();
    data.viewPos[targetEye] = camera->GetPosition().ToPos4();
    data.viewDir[targetEye] = camera->GetDirection().ToDir4();
    data.viewUp[targetEye] = camera->GetUp().ToDir4();
    data.nearFar = float4 { camera->GetNear(), camera->GetFar(), 0.0f, 0.0f };
}

}
