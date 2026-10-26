#include "gx/vk/vk_pipeline_key_util.h"
#include "gx/vk/vk_pipeline_blend.h"

namespace gpu {

PipelineKey pipelineKeyFor(const Item& it, const ShaderUid& uid) {
    DecodedPipelineKey d{};
    d.uid = uid;
    d.vtxFlags = it.flags;
    d.depthTest = (it.depthTest != 0);
    d.depthWrite = (it.depthTest && it.depthWrite);
    d.depthCompareOp = toCanonicalDepthFunc(it.depthFunc);

    d.blendEnable = (it.blend != 0) || (it.dstAlpha != 0);
    d.srcColorBlendFactor = toCanonicalBlendFactor(it.blend ? it.srcRGB : 1);
    d.dstColorBlendFactor = toCanonicalBlendFactor(it.blend ? it.dstRGB : 0);
    d.colorBlendOp = toCanonicalBlendOp(it.blend ? it.eq : 0x8006);
    d.srcAlphaBlendFactor = toCanonicalBlendFactor(it.dstAlpha ? 0x8003 : (it.blend ? it.srcRGB : 1));
    d.dstAlphaBlendFactor = toCanonicalBlendFactor(it.dstAlpha ? 0 : (it.blend ? it.dstRGB : 0));
    d.alphaBlendOp = toCanonicalBlendOp(it.dstAlpha ? 0x8006 : (it.blend ? it.eq : 0x8006));
    d.colorWriteMask = it.colorMask & 0x0Fu;
    d.blendConstantAlpha = it.dstAlphaValue;

    d.cullMode = toCanonicalCullMode(it.cull != 0, it.cullFace);
    d.topology = toCanonicalTopology(it.prim);
    d.frontFace = CanonicalFrontFace::CounterClockwise;
    return encodePipelineKey(d);
}

}  // namespace gpu
