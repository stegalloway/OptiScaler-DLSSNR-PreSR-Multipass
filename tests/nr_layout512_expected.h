#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include "dlssnr/DlssNr_Common.h"
// Independent literal expectations frozen in revision 3 before production editing.
struct LayoutField { const char* name; unsigned offset, size; bool isFloat; };
inline constexpr LayoutField kLayoutFields[] = {
    {"Mode", 0, 4, false},
    {"WhitePoint", 4, 4, true},
    {"Width", 8, 4, false},
    {"Height", 12, 4, false},
    {"TransferStrength", 16, 4, true},
    {"ColourStrength", 20, 4, true},
    {"DebugView", 24, 4, false},
    {"MaxRatio", 28, 4, true},
    {"Passthrough", 32, 4, false},
    {"MvScaleX", 36, 4, true},
    {"MvScaleY", 40, 4, true},
    {"GuideWidth", 44, 4, false},
    {"GuideHeight", 48, 4, false},
    {"CompareMode", 52, 4, false},
    {"CompareSplit", 56, 4, true},
    {"CompareZoom", 60, 4, true},
    {"CompareSwap", 64, 4, false},
    {"Transfer", 68, 4, false},
    {"DebugScale", 72, 4, true},
    {"ReversibleMode", 76, 4, false},
    {"ApplyModel", 80, 4, false},
    {"Reserved", 84, 4, false},
    {"ResidualScale", 88, 4, true},
    {"SkinProtection", 92, 4, false},
    {"ShowSkinMask", 96, 4, false},
    {"SkinDetail", 100, 4, true},
    {"SkinColour", 104, 4, true},
    {"EnvironmentDetail", 108, 4, true},
    {"EnvironmentColour", 112, 4, true},
    {"ResidualBlend", 116, 4, true},
    {"ResidualHistoryValid", 120, 4, false},
    {"ResidualMotionBaseX", 124, 4, false},
    {"ResidualMotionBaseY", 128, 4, false},
    {"ReplaceDetailStrength", 132, 4, true},
    {"ModelWorkScale", 136, 4, true},
    {"ResidualConfidenceSensitivity", 140, 4, true},
    {"ExposureMode", 144, 4, false},
    {"PreExposure", 148, 4, true},
    {"ExposureTrim", 152, 4, true},
    {"ExposureProtection", 156, 4, true},
    {"ExposureAnchorCount", 160, 4, false},
    {"ExposureSourceWidth", 164, 4, false},
    {"ExposureSourceHeight", 168, 4, false},
    {"ExposurePadding", 172, 4, false},
    {"ExposureAnchors", 176, 64, true},
    {"ResidualMotionSign", 240, 4, true},
    {"ResidualFrameWidth", 244, 4, false},
    {"ResidualFrameHeight", 248, 4, false},
    {"ResidualOutputWidth", 252, 4, false},
    {"ResidualOutputHeight", 256, 4, false},
};
inline constexpr const char* kShaderFiles[] = {"dlssnr.hlsl","dlssnr_residual.hlsl","dlssnr_finished_color.hlsl"};
inline constexpr const char* kShaderFields[3][50] = {
    {"gMode","gWhitePoint","gWidth","gHeight","gTransferStrength","gColourStrength","gDebugView","gMaxRatio","gPassthrough","gMvScaleX","gMvScaleY","gGuideWidth","gGuideHeight","gCompareMode","gCompareSplit","gCompareZoom","gCompareSwap","gTransfer","gDebugScale","gReversibleMode","gApplyModel","gReserved","gResidualScale","gSkinProtection","gShowSkinMask","gSkinDetail","gSkinColour","gEnvironmentDetail","gEnvironmentColour","gResidualBlendUnused","gResidualHistoryValidUnused","gResidualMotionBaseXUnused","gResidualMotionBaseYUnused","gReplaceDetailStrength","gModelWorkScale","gResidualConfidenceUnused","gExposureMode","gPreExposure","gExposureTrim","gExposureProtection","gExposureAnchorCount","gExposureSourceWidth","gExposureSourceHeight","gExposurePadding","gExposureAnchors","gResidualMotionSign","gResidualFrameWidth","gResidualFrameHeight","gResidualOutputWidth","gResidualOutputHeight"},
    {"gMode","gWhitePoint","gWidth","gHeight","gTransferStrength","gColourStrength","gDebugView","gMaxRatio","gPassthrough","gMvScaleX","gMvScaleY","gGuideWidth","gGuideHeight","gCompareMode","gCompareSplit","gCompareZoom","gCompareSwap","gTransfer","gDebugScale","gReversibleMode","gApplyModel","gReserved","gResidualScale","gSkinProtection","gShowSkinMask","gSkinDetail","gSkinColour","gEnvironmentDetail","gEnvironmentColour","gResidualBlend","gResidualHistoryValid","gResidualMotionBaseX","gResidualMotionBaseY","gReplaceDetailUnused","gModelWorkScaleUnused","gResidualConfidenceSensitivity","gExposureModeUnused","gPreExposureUnused","gExposureTrimUnused","gExposureProtectionUnused","gExposureAnchorCountUnused","gExposureSourceWidthUnused","gExposureSourceHeightUnused","gExposurePaddingUnused","gExposureAnchorsUnused","gResidualMotionSign","gResidualFrameWidth","gResidualFrameHeight","gResidualOutputWidth","gResidualOutputHeight"},
    {"mode","exposureScale","width","height","sceneIsLinear","curveUpdateWeight","curveHistoryValid","maxRatio","layoutUnusedPassthrough","layoutUnusedMvScaleX","layoutUnusedMvScaleY","layoutUnusedGuideWidth","layoutUnusedGuideHeight","layoutUnusedCompareMode","layoutUnusedCompareSplit","layoutUnusedCompareZoom","layoutUnusedCompareSwap","layoutUnusedTransfer","layoutUnusedDebugScale","layoutUnusedReversibleMode","layoutUnusedApplyModel","layoutUnusedReserved","layoutUnusedResidualScale","layoutUnusedSkinProtection","layoutUnusedShowSkinMask","layoutUnusedSkinDetail","layoutUnusedSkinColour","layoutUnusedEnvironmentDetail","layoutUnusedEnvironmentColour","layoutUnusedResidualBlend","layoutUnusedResidualHistoryValid","layoutUnusedResidualMotionBaseX","layoutUnusedResidualMotionBaseY","layoutUnusedReplaceDetailStrength","layoutUnusedModelWorkScale","layoutUnusedResidualConfidenceSensitivity","layoutUnusedExposureMode","layoutUnusedPreExposure","layoutUnusedExposureTrim","layoutUnusedExposureProtection","layoutUnusedExposureAnchorCount","layoutUnusedExposureSourceWidth","layoutUnusedExposureSourceHeight","layoutUnusedExposurePadding","layoutUnusedExposureAnchors","gResidualMotionSign","gResidualFrameWidth","gResidualFrameHeight","gResidualOutputWidth","gResidualOutputHeight"},
};
static_assert(sizeof(DlssNrConstants)==512 && alignof(DlssNrConstants)==256);
static_assert(offsetof(DlssNrConstants,Mode)==0);
static_assert(offsetof(DlssNrConstants,WhitePoint)==4);
static_assert(offsetof(DlssNrConstants,Width)==8);
static_assert(offsetof(DlssNrConstants,Height)==12);
static_assert(offsetof(DlssNrConstants,TransferStrength)==16);
static_assert(offsetof(DlssNrConstants,ColourStrength)==20);
static_assert(offsetof(DlssNrConstants,DebugView)==24);
static_assert(offsetof(DlssNrConstants,MaxRatio)==28);
static_assert(offsetof(DlssNrConstants,Passthrough)==32);
static_assert(offsetof(DlssNrConstants,MvScaleX)==36);
static_assert(offsetof(DlssNrConstants,MvScaleY)==40);
static_assert(offsetof(DlssNrConstants,GuideWidth)==44);
static_assert(offsetof(DlssNrConstants,GuideHeight)==48);
static_assert(offsetof(DlssNrConstants,CompareMode)==52);
static_assert(offsetof(DlssNrConstants,CompareSplit)==56);
static_assert(offsetof(DlssNrConstants,CompareZoom)==60);
static_assert(offsetof(DlssNrConstants,CompareSwap)==64);
static_assert(offsetof(DlssNrConstants,Transfer)==68);
static_assert(offsetof(DlssNrConstants,DebugScale)==72);
static_assert(offsetof(DlssNrConstants,ReversibleMode)==76);
static_assert(offsetof(DlssNrConstants,ApplyModel)==80);
static_assert(offsetof(DlssNrConstants,Reserved)==84);
static_assert(offsetof(DlssNrConstants,ResidualScale)==88);
static_assert(offsetof(DlssNrConstants,SkinProtection)==92);
static_assert(offsetof(DlssNrConstants,ShowSkinMask)==96);
static_assert(offsetof(DlssNrConstants,SkinDetail)==100);
static_assert(offsetof(DlssNrConstants,SkinColour)==104);
static_assert(offsetof(DlssNrConstants,EnvironmentDetail)==108);
static_assert(offsetof(DlssNrConstants,EnvironmentColour)==112);
static_assert(offsetof(DlssNrConstants,ResidualBlend)==116);
static_assert(offsetof(DlssNrConstants,ResidualHistoryValid)==120);
static_assert(offsetof(DlssNrConstants,ResidualMotionBaseX)==124);
static_assert(offsetof(DlssNrConstants,ResidualMotionBaseY)==128);
static_assert(offsetof(DlssNrConstants,ReplaceDetailStrength)==132);
static_assert(offsetof(DlssNrConstants,ModelWorkScale)==136);
static_assert(offsetof(DlssNrConstants,ResidualConfidenceSensitivity)==140);
static_assert(offsetof(DlssNrConstants,ExposureMode)==144);
static_assert(offsetof(DlssNrConstants,PreExposure)==148);
static_assert(offsetof(DlssNrConstants,ExposureTrim)==152);
static_assert(offsetof(DlssNrConstants,ExposureProtection)==156);
static_assert(offsetof(DlssNrConstants,ExposureAnchorCount)==160);
static_assert(offsetof(DlssNrConstants,ExposureSourceWidth)==164);
static_assert(offsetof(DlssNrConstants,ExposureSourceHeight)==168);
static_assert(offsetof(DlssNrConstants,ExposurePadding)==172);
static_assert(offsetof(DlssNrConstants,ExposureAnchors)==176);
static_assert(offsetof(DlssNrConstants,ResidualMotionSign)==240);
static_assert(offsetof(DlssNrConstants,ResidualFrameWidth)==244);
static_assert(offsetof(DlssNrConstants,ResidualFrameHeight)==248);
static_assert(offsetof(DlssNrConstants,ResidualOutputWidth)==252);
static_assert(offsetof(DlssNrConstants,ResidualOutputHeight)==256);
inline constexpr unsigned kWordCount=65, kSlotCount=3, kRounds=6;
inline uint32_t Sentinel(unsigned shader, unsigned round, unsigned word) {
    return 0x3f000000u + (shader+1)*0x10000u + round*0x100u + word;
}
inline DlssNrConstants MakeSentinels(unsigned shader, unsigned round) {
    DlssNrConstants result{};
    for(unsigned word=0;word<kWordCount;++word) {
        const auto value=Sentinel(shader,round,word);
        std::memcpy(reinterpret_cast<unsigned char*>(&result)+word*4,&value,4);
    }
    return result;
}
inline bool MatchesSentinels(const uint32_t* data,unsigned shader,unsigned round) {
    for(unsigned word=0;word<kWordCount;++word)
        if(data[word]!=Sentinel(shader,round,word)) return false;
    return true;
}
