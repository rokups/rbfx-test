//
// Copyright (c) 2017-2020 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "../Graphics/Graphics.h"
#include "../Graphics/Renderer.h"
#include "../IO/Log.h"
#include "../RenderPipeline/CameraProcessor.h"
#include "../RenderPipeline/InstancingBuffer.h"
#include "../RenderPipeline/LightProcessor.h"
#include "../RenderPipeline/PipelineStateBuilder.h"
#include "../RenderPipeline/SceneProcessor.h"
#include "../RenderPipeline/ShadowMapAllocator.h"

#include <EASTL/span.h>

#include "../DebugNew.h"

namespace Urho3D
{

namespace
{

int GetTextureColorSpaceHint(bool linearInput, bool srgbTexture)
{
    return static_cast<int>(linearInput) + static_cast<int>(srgbTexture);
}

CullMode GetEffectiveCullMode(CullMode mode, bool isCameraReversed)
{
    if (mode == CULL_NONE || !isCameraReversed)
        return mode;

    return mode == CULL_CW ? CULL_CCW : CULL_CW;
}

CullMode GetEffectiveCullMode(CullMode passCullMode, CullMode materialCullMode, bool isCameraReversed)
{
    const CullMode cullMode = passCullMode != MAX_CULLMODES ? passCullMode : materialCullMode;
    return GetEffectiveCullMode(cullMode, isCameraReversed);
}

}

PipelineStateBuilder::PipelineStateBuilder(Context* context,
    const SceneProcessor* sceneProcessor, const CameraProcessor* cameraProcessor,
    const ShadowMapAllocator* shadowMapAllocator, const InstancingBuffer* instancingBuffer)
    : Object(context)
    , sceneProcessor_(sceneProcessor)
    , cameraProcessor_(cameraProcessor)
    , shadowMapAllocator_(shadowMapAllocator)
    , instancingBuffer_(instancingBuffer)
    , graphics_(GetSubsystem<Graphics>())
    , renderer_(GetSubsystem<Renderer>())
    , compositor_(MakeShared<ShaderProgramCompositor>(context_))
{
}

void PipelineStateBuilder::OnSettingsUpdated()
{
    compositor_->SetSettings(sceneProcessor_->GetSettings(),
        shadowMapAllocator_->GetSettings(), instancingBuffer_->GetSettings(),
        cameraProcessor_->IsCameraOrthographic());
}

SharedPtr<PipelineState> PipelineStateBuilder::CreateBatchPipelineState(
    const BatchStateCreateKey& key, const BatchStateCreateContext& ctx)
{
    Light* light = key.pixelLight_ ? key.pixelLight_->GetLight() : nullptr;
    const bool hasShadow = key.pixelLight_ && key.pixelLight_->HasShadow();

    const BatchCompositorPass* batchCompositorPass = sceneProcessor_->GetUserPass(ctx.pass_);
    const bool isShadowPass = batchCompositorPass == nullptr && ctx.subpassIndex_ == BatchCompositor::ShadowSubpass;
    const bool isLightVolumePass = batchCompositorPass == nullptr && ctx.subpassIndex_ == BatchCompositor::LitVolumeSubpass;

    ClearState();

    if (isShadowPass)
    {
        compositor_->ProcessShadowBatch(shaderProgramDesc_,
            key.geometry_, key.geometryType_, key.material_, key.pass_, light);
        ApplyShadowPass(ctx.shadowSplitIndex_, key.pixelLight_, key.material_, key.pass_);
    }
    else if (isLightVolumePass)
    {
        compositor_->ProcessLightVolumeBatch(shaderProgramDesc_,
            key.geometry_, key.geometryType_, key.pass_);
        ApplyLightVolumePass(key.pixelLight_);
    }
    else if (batchCompositorPass)
    {
        const auto subpass = static_cast<BatchCompositorSubpass>(ctx.subpassIndex_);
        compositor_->ProcessUserBatch(shaderProgramDesc_, batchCompositorPass->GetFlags(),
            key.drawable_, key.geometry_, key.geometryType_, key.material_, key.pass_, light, hasShadow, subpass);
        ApplyUserPass(batchCompositorPass, subpass, key.material_, key.pass_, key.drawable_);
    }

    if (shaderProgramDesc_.isInstancingUsed_)
        pipelineStateDesc_.InitializeInputLayoutAndPrimitiveType(key.geometry_, instancingBuffer_->GetVertexBuffer());
    else
        pipelineStateDesc_.InitializeInputLayoutAndPrimitiveType(key.geometry_);

    FinalizeDescription(key.pass_);
    return renderer_->GetOrCreatePipelineState(pipelineStateDesc_);
}

void PipelineStateBuilder::ClearState()
{
    pipelineStateDesc_ = {};
    shaderProgramDesc_.vertexShaderName_.clear();
    shaderProgramDesc_.vertexShaderDefines_.clear();
    shaderProgramDesc_.pixelShaderName_.clear();
    shaderProgramDesc_.pixelShaderDefines_.clear();
    shaderProgramDesc_.commonShaderDefines_.clear();
}

void PipelineStateBuilder::ApplyShadowPass(unsigned splitIndex, const LightProcessor* lightProcessor,
    const Material* material, const Pass* materialPass)
{
    const CookedLightParams& lightParams = lightProcessor->GetParams();
    const float biasMultiplier = lightParams.shadowDepthBiasMultiplier_[splitIndex];
    const BiasParameters& biasParameters = lightProcessor->GetLight()->GetShadowBias();

    if (shadowMapAllocator_->GetSettings().enableVarianceShadowMaps_)
    {
        pipelineStateDesc_.colorWriteEnabled_ = true;
        pipelineStateDesc_.constantDepthBias_ = 0.0f;
        pipelineStateDesc_.slopeScaledDepthBias_ = 0.0f;
    }
    else
    {
        pipelineStateDesc_.colorWriteEnabled_ = false;
        pipelineStateDesc_.constantDepthBias_ = biasMultiplier * biasParameters.constantBias_;
        pipelineStateDesc_.slopeScaledDepthBias_ = biasMultiplier * biasParameters.slopeScaledBias_;
    }

    pipelineStateDesc_.depthWriteEnabled_ = materialPass->GetDepthWrite();
    pipelineStateDesc_.depthCompareFunction_ = materialPass->GetDepthTestMode();

    pipelineStateDesc_.cullMode_ = GetEffectiveCullMode(materialPass->GetCullMode(), material->GetShadowCullMode(), false);

    // TODO(renderer): Revisit this place
    // Perform further modification of depth bias on OpenGL ES, as shadow calculations' precision is limited
/*#ifdef GL_ES_VERSION_2_0
    const float multiplier = renderer_->GetMobileShadowBiasMul();
    const float addition = renderer_->GetMobileShadowBiasAdd();
    desc.constantDepthBias_ = desc.constantDepthBias_ * multiplier + addition;
    desc.slopeScaledDepthBias_ *= multiplier;
#endif*/
}

void PipelineStateBuilder::ApplyLightVolumePass(const LightProcessor* lightProcessor)
{
    const Light* light = lightProcessor->GetLight();

    pipelineStateDesc_.colorWriteEnabled_ = true;
    pipelineStateDesc_.blendMode_ = light->IsNegative() ? BLEND_SUBTRACT : BLEND_ADD;

    if (light->GetLightType() != LIGHT_DIRECTIONAL)
    {
        if (lightProcessor->DoesOverlapCamera())
        {
            pipelineStateDesc_.cullMode_ = GetEffectiveCullMode(CULL_CW, cameraProcessor_->IsCameraReversed());
            pipelineStateDesc_.depthCompareFunction_ = CMP_GREATER;
        }
        else
        {
            pipelineStateDesc_.cullMode_ = GetEffectiveCullMode(CULL_CCW, cameraProcessor_->IsCameraReversed());
            pipelineStateDesc_.depthCompareFunction_ = CMP_LESSEQUAL;
        }
    }
    else
    {
        pipelineStateDesc_.cullMode_ = CULL_NONE;
        pipelineStateDesc_.depthCompareFunction_ = CMP_ALWAYS;
    }

    pipelineStateDesc_.stencilTestEnabled_ = true;
    pipelineStateDesc_.stencilCompareFunction_ = CMP_NOTEQUAL;
    pipelineStateDesc_.stencilCompareMask_ = light->GetLightMaskEffective() & PORTABLE_LIGHTMASK;
    pipelineStateDesc_.stencilReferenceValue_ = 0;
}

void PipelineStateBuilder::ApplyUserPass(const BatchCompositorPass* compositorPass, BatchCompositorSubpass subpass,
     const Material* material, const Pass* materialPass, const Drawable* drawable)
{
    pipelineStateDesc_.depthWriteEnabled_ = materialPass->GetDepthWrite();
    pipelineStateDesc_.depthCompareFunction_ = materialPass->GetDepthTestMode();

    pipelineStateDesc_.colorWriteEnabled_ = true;
    pipelineStateDesc_.blendMode_ = materialPass->GetBlendMode();
    pipelineStateDesc_.alphaToCoverageEnabled_ = materialPass->GetAlphaToCoverage();
    // TODO(renderer): Revisit this place
    pipelineStateDesc_.constantDepthBias_ = material->GetDepthBias().constantBias_;
    pipelineStateDesc_.slopeScaledDepthBias_ = material->GetDepthBias().slopeScaledBias_;

    // TODO(renderer): Implement fill mode
    pipelineStateDesc_.fillMode_ = FILL_SOLID;
    pipelineStateDesc_.cullMode_ = GetEffectiveCullMode(materialPass->GetCullMode(),
        material->GetCullMode(), cameraProcessor_->IsCameraReversed());

    const bool isDeferred = subpass == BatchCompositorSubpass::Deferred;
    if (isDeferred && compositorPass->GetFlags().Test(DrawableProcessorPassFlag::DeferredLightMaskToStencil))
    {
        pipelineStateDesc_.stencilTestEnabled_ = true;
        pipelineStateDesc_.stencilOperationOnPassed_ = OP_REF;
        pipelineStateDesc_.stencilWriteMask_ = PORTABLE_LIGHTMASK;
        pipelineStateDesc_.stencilReferenceValue_ = drawable->GetLightMaskInZone() & PORTABLE_LIGHTMASK;
    }
}

void PipelineStateBuilder::FinalizeDescription(const Pass* materialPass)
{
    shaderProgramDesc_.vertexShaderDefines_ += shaderProgramDesc_.commonShaderDefines_;
    shaderProgramDesc_.pixelShaderDefines_ += shaderProgramDesc_.commonShaderDefines_;
    pipelineStateDesc_.vertexShader_ = graphics_->GetShader(VS, shaderProgramDesc_.vertexShaderName_, shaderProgramDesc_.vertexShaderDefines_);
    pipelineStateDesc_.pixelShader_ = graphics_->GetShader(PS, shaderProgramDesc_.pixelShaderName_, shaderProgramDesc_.pixelShaderDefines_);
}

}
