// MV-reprojected temporal accumulation of the pre-SR NR residual.
// Mode 0 blends (edited - original) into history; invalid reprojection fades in from zero.
// Mode 1 applies the signed residual after upscaling.
// Bindings match the main NR shader; the full mirrored ABI uses a 512-byte backing CBV.

#ifdef VK_MODE
[[vk::binding(0, 0)]]
cbuffer Params : register(b0, space0)
#else
cbuffer Params : register(b0)
#endif
{
    // Frozen revision-3 ABI: explicit offsets throughout (FXC forbids mixed packing).
    uint gMode : packoffset(c0.x);
    float gWhitePoint : packoffset(c0.y);
    uint gWidth : packoffset(c0.z);
    uint gHeight : packoffset(c0.w);
    float gTransferStrength : packoffset(c1.x);
    float gColourStrength : packoffset(c1.y);
    uint gDebugView : packoffset(c1.z);
    float gMaxRatio : packoffset(c1.w);
    uint gPassthrough : packoffset(c2.x);
    float gMvScaleX : packoffset(c2.y);
    float gMvScaleY : packoffset(c2.z);
    uint gGuideWidth : packoffset(c2.w);
    uint gGuideHeight : packoffset(c3.x);
    uint gCompareMode : packoffset(c3.y);
    float gCompareSplit : packoffset(c3.z);
    float gCompareZoom : packoffset(c3.w);
    uint gCompareSwap : packoffset(c4.x);
    uint gTransfer : packoffset(c4.y);
    float gDebugScale : packoffset(c4.z);
    uint gReversibleMode : packoffset(c4.w);
    uint gApplyModel : packoffset(c5.x);
    uint gReserved : packoffset(c5.y);
    float gResidualScale : packoffset(c5.z);
    uint gSkinProtection : packoffset(c5.w);
    uint gShowSkinMask : packoffset(c6.x);
    float gSkinDetail : packoffset(c6.y);
    float gSkinColour : packoffset(c6.z);
    float gEnvironmentDetail : packoffset(c6.w);
    float gEnvironmentColour : packoffset(c7.x);
    float gResidualBlend : packoffset(c7.y);
    uint gResidualHistoryValid : packoffset(c7.z);
    uint gResidualMotionBaseX : packoffset(c7.w);
    uint gResidualMotionBaseY : packoffset(c8.x);
    float gReplaceDetailUnused : packoffset(c8.y);
    float gModelWorkScaleUnused : packoffset(c8.z);
    float gResidualConfidenceSensitivity : packoffset(c8.w);
    uint gExposureModeUnused : packoffset(c9.x);
    float gPreExposureUnused : packoffset(c9.y);
    float gExposureTrimUnused : packoffset(c9.z);
    float gExposureProtectionUnused : packoffset(c9.w);
    uint gExposureAnchorCountUnused : packoffset(c10.x);
    uint gExposureSourceWidthUnused : packoffset(c10.y);
    uint gExposureSourceHeightUnused : packoffset(c10.z);
    uint gExposurePaddingUnused : packoffset(c10.w);
    float4 gExposureAnchorsUnused[4] : packoffset(c11);
    float gResidualMotionSign : packoffset(c15.x);
    uint gResidualFrameWidth : packoffset(c15.y);
    uint gResidualFrameHeight : packoffset(c15.z);
    uint gResidualOutputWidth : packoffset(c15.w);
    uint gResidualOutputHeight : packoffset(c16.x);
};

// Same registers and the same SPIR-V binding numbers as dlssnr.hlsl, including the slots these
// modes do not read (gExposure t4, gKeep u1) -- DispatchResidualPass binds a stand-in into them
// exactly as DispatchPass does, and a future Vulkan host path needs the numbering to line up.
#ifdef VK_MODE
[[vk::binding(1, 0)]]
#endif
Texture2D<float4>   gSource   : register(t0);  // accumulate: the untouched pre-SR frame. apply: the RR+SR output.
#ifdef VK_MODE
[[vk::binding(2, 0)]]
#endif
Texture2D<float4>   gModel    : register(t1);  // accumulate: the NR-edited frame. apply: the upscaled delta layer.
#ifdef VK_MODE
[[vk::binding(3, 0)]]
#endif
Texture2D<float4>   gOriginal : register(t2);  // accumulate: the previous history layer.
#ifdef VK_MODE
[[vk::binding(4, 0)]]
#endif
Texture2D<float4>   gMotion   : register(t3);  // raw game motion; active size, offsets and scale come from the host.
#ifndef VK_MODE
Texture2D<float4>   gExposure : register(t4);  // unused here; bound for descriptor-table parity.
#endif
#ifdef VK_MODE
[[vk::binding(5, 0)]]
#endif
RWTexture2D<float4> gTarget   : register(u0);  // accumulate: the new history layer. apply: the composed frame.
#ifdef VK_MODE
[[vk::binding(6, 0)]]
#endif
RWTexture2D<float4> gKeep     : register(u1);  // unused here; bound for descriptor-table parity.
#ifdef VK_MODE
[[vk::binding(7, 0)]]
#endif
SamplerState        gLinear   : register(s0);  // history is sampled at the reprojected coordinate.

float  SanitizeFinite(float v, float fallback)   { return isfinite(v) ? v : fallback; }
float3 SanitizeFinite3(float3 v, float3 fallback)
{
    return float3(SanitizeFinite(v.x, fallback.x), SanitizeFinite(v.y, fallback.y),
                  SanitizeFinite(v.z, fallback.z));
}

float EditScalar(float3 d) { return (d.x + d.y + d.z) * (1.0 / 3.0); }
float EditMagnitude(float3 d) { d = abs(d); return (d.x + d.y + d.z) * (1.0 / 3.0); }

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gWidth || id.y >= gHeight)
        return;

    if (gMode == 0)
    {
        float3 delta = SanitizeFinite3(gModel.Load(int3(id.xy, 0)).rgb -
                                       gSource.Load(int3(id.xy, 0)).rgb, float3(0.0, 0.0, 0.0));

        float2 uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);
        uint2 guideSize = uint2(gGuideWidth, gGuideHeight);
        uint2 guidePos = min(uint2(uv * guideSize), guideSize - 1) +
                         uint2(gResidualMotionBaseX, gResidualMotionBaseY);
        float2 motion = gMotion.Load(int3(guidePos, 0)).xy * float2(gMvScaleX, gMvScaleY);
        float2 prevUV = uv + motion;

        bool valid = gResidualHistoryValid != 0 && all(isfinite(motion)) && all(abs(motion) < 2.0) &&
                     all(prevUV >= 0.0) && all(prevUV <= 1.0);

        float3 history = valid ? gOriginal.SampleLevel(gLinear, prevUV, 0).rgb : float3(0.0, 0.0, 0.0);
        history = SanitizeFinite3(history, float3(0.0, 0.0, 0.0));

        // Invalid reprojection: history is 0, so the pixel fades in from no edit at the normal blend
        // rate over the next frames. A cold start/cut also fades in, without sampling uninitialized history.
        float a = clamp(gResidualBlend, 0.0, 1.0);
        if (gResidualConfidenceSensitivity > 0.0)
            a = lerp(a, 1.0, saturate(length(delta - history) / gResidualConfidenceSensitivity));

        gTarget[id.xy] = float4(lerp(history, delta, a), 1.0);
        return;
    }

    if (gMode == 1)
    {
        float4 base  = gSource.Load(int3(id.xy, 0));
        float2 uv = (float2(id.xy) + 0.5) / float2(gWidth, gHeight);
        float3 delta = SanitizeFinite3(gModel.SampleLevel(gLinear, uv, 0).rgb, float3(0.0, 0.0, 0.0));

        gTarget[id.xy] = float4(max(base.rgb + delta * gTransferStrength, 0.0), base.a);
        return;
    }

    if (gMode == 2)
    {
        float3 delta = SanitizeFinite3(gModel.Load(int3(id.xy,0)).rgb - gSource.Load(int3(id.xy,0)).rgb,float3(0.0,0.0,0.0));
        const bool interior = id.x > 0 && id.y > 0 && id.x + 1 < gWidth && id.y + 1 < gHeight;

        float sum = 0.0, sum2 = 0.0;
        [unroll] for (int oy=-1; oy<=1; ++oy)
        [unroll] for (int ox=-1; ox<=1; ++ox)
        {
            int2 p = clamp(int2(id.xy)+int2(ox,oy),int2(0,0),int2((int)gWidth-1,(int)gHeight-1));
            float3 d = SanitizeFinite3(gModel.Load(int3(p,0)).rgb - gSource.Load(int3(p,0)).rgb,float3(0.0,0.0,0.0));
            float s = EditScalar(d); sum += s; sum2 += s*s;
        }
        float mean = sum / 9.0;
        float sigma = sqrt(max(sum2 / 9.0 - mean*mean,0.0));

        bool guideOk = gGuideWidth != 0 && gGuideHeight != 0;
        float2 uv = (float2(id.xy) + 0.5) / float2(gWidth,gHeight);
        float2 motion = float2(0.0,0.0);
        float2 prevUV = uv;
        bool motionFinite = false;
        float maxMotionDisagreementPx = 1e20;
        if (guideOk)
        {
            uint2 gs = uint2(gGuideWidth,gGuideHeight);
            uint2 gp = min(uint2(uv * gs),gs - 1) + uint2(gResidualMotionBaseX,gResidualMotionBaseY);
            motion = gMotion.Load(int3(gp,0)).xy * float2(gMvScaleX,gMvScaleY) * gResidualMotionSign;
            prevUV = uv + motion;
            motionFinite = all(isfinite(motion));
            float2 motionPxExtent = float2(
                gResidualOutputWidth != 0 ? gResidualOutputWidth : gWidth,
                gResidualOutputHeight != 0 ? gResidualOutputHeight : gHeight);
            float2 centerPx = motion * motionPxExtent;
            maxMotionDisagreementPx = 0.0;
            [unroll] for (int my=-1; my<=1; ++my)
            [unroll] for (int mx=-1; mx<=1; ++mx)
            {
                int2 op = clamp(int2(id.xy)+int2(mx,my),int2(0,0),int2((int)gWidth-1,(int)gHeight-1));
                float2 ouv = (float2(op)+0.5) / float2(gWidth,gHeight);
                uint2 ogp = min(uint2(ouv * gs),gs - 1) + uint2(gResidualMotionBaseX,gResidualMotionBaseY);
                float2 om = gMotion.Load(int3(ogp,0)).xy * float2(gMvScaleX,gMvScaleY) * gResidualMotionSign;
                if (!all(isfinite(om))) maxMotionDisagreementPx = 1e20;
                else maxMotionDisagreementPx = max(maxMotionDisagreementPx,length(om * motionPxExtent - centerPx));
            }
        }

        bool reprojectionValid = gResidualHistoryValid != 0 && interior && guideOk && motionFinite &&
                                 all(prevUV >= 0.0) && all(prevUV <= 1.0);
        bool motionStable = reprojectionValid && maxMotionDisagreementPx <= max(gMaxRatio,0.0);
        float3 history = reprojectionValid ? SanitizeFinite3(gOriginal.SampleLevel(gLinear,prevUV,0).rgb,float3(0.0,0.0,0.0)) : delta;
        float k = max(gResidualBlend,0.0);
        bool acceptHistory = motionStable && abs(EditScalar(history) - mean) <= k * max(sigma,1e-6);

        // Core invariant: binary selection only. Never alpha-blend current and history.
        float3 chosen = acceptHistory ? history : delta;
        float3 base = gSource.Load(int3(id.xy,0)).rgb;
        float3 visibleChosen = max(base + chosen,0.0) - base;
        gTarget[id.xy] = float4(visibleChosen,1.0);
        return;
    }

    // Startup MV convention validation. gWidth/gHeight are the 64x64 diagnostic grid;
    // gResidualFrameWidth/Height are the real frame dimensions. Each cell compares current clean SR
    // with previous clean SR unwarped and at both possible MV signs. Alpha=1 marks a useful moving sample.
    if (gMode == 3)
    {
        if (gResidualFrameWidth == 0 || gResidualFrameHeight == 0 || gGuideWidth == 0 || gGuideHeight == 0)
        {
            gTarget[id.xy] = float4(0.0,0.0,0.0,0.0);
            return;
        }
        float2 gridUv = (float2(id.xy) + 0.5) / float2(gWidth,gHeight);
        uint2 frameSize = uint2(gResidualFrameWidth,gResidualFrameHeight);
        uint2 outputSize = uint2(
            gResidualOutputWidth != 0 ? gResidualOutputWidth : gResidualFrameWidth,
            gResidualOutputHeight != 0 ? gResidualOutputHeight : gResidualFrameHeight);
        uint2 curPos = min(uint2(gridUv * frameSize),frameSize - 1);
        uint2 gs = uint2(gGuideWidth,gGuideHeight);
        uint2 gp = min(uint2(gridUv * gs),gs - 1) + uint2(gResidualMotionBaseX,gResidualMotionBaseY);
        float2 motionUv = gMotion.Load(int3(gp,0)).xy * float2(gMvScaleX,gMvScaleY);
        float2 motionPx = motionUv * float2(outputSize);
        float magPx = length(motionPx);
        float2 plusUv = gridUv + motionUv;
        float2 minusUv = gridUv - motionUv;
        bool valid = all(isfinite(motionUv)) && magPx >= 0.75 && magPx <= 96.0 &&
                     all(plusUv >= 0.0) && all(plusUv <= 1.0) &&
                     all(minusUv >= 0.0) && all(minusUv <= 1.0);
        if (!valid)
        {
            gTarget[id.xy] = float4(0.0,0.0,0.0,0.0);
            return;
        }
        float3 cur = SanitizeFinite3(gSource.Load(int3(curPos,0)).rgb,float3(0.0,0.0,0.0));
        float3 prev0 = SanitizeFinite3(gOriginal.SampleLevel(gLinear,gridUv,0).rgb,cur);
        float3 prevPlus = SanitizeFinite3(gOriginal.SampleLevel(gLinear,plusUv,0).rgb,cur);
        float3 prevMinus = SanitizeFinite3(gOriginal.SampleLevel(gLinear,minusUv,0).rgb,cur);
        gTarget[id.xy] = float4(EditMagnitude(cur-prev0),EditMagnitude(cur-prevPlus),EditMagnitude(cur-prevMinus),1.0);
        return;
    }

    gTarget[id.xy] = gSource.Load(int3(id.xy, 0));
}
