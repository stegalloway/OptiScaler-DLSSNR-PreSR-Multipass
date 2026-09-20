// Use the actual extracted EncodeInput and the existing COM/dispatch fixture.
// Run this identical fixture against the unmodified parent and the candidate.
#define main OriginalEncodeChecks
#include "EncodeTests.cpp"
#undef main

int main()
{
    if (OriginalEncodeChecks()) return 1;
    unsigned assertions = 0, failures = 0;
    auto require = [&](bool value, const char* label) {
        ++assertions;
        if (!value) { ++failures; std::cout << "FAIL: " << label << '\n'; }
    };
    for (const int fail : { DlssNrMode_Meter, DlssNrMode_AutoExposure, DlssNrMode_Downsample })
    {
        *Config::Instance() = {};
        const unsigned source = fail == DlssNrMode_Downsample ? 1 : 3;
        Config::Instance()->DlssNrWhitePointSource.val = source;
        DlssNr_Dx12::State s;
        ID3D12Resource target, base, hdr, gameExposure;
        gameExposure.desc.Width = gameExposure.desc.Height = 1;
        ID3D12Device device;
        ID3D12GraphicsCommandList commands;
        s.nr.colorCopy = &base;
        s.nr.hdrCopy = &hdr;
        auto frame = [&]() {
            // Run's next-frame preparation makes these model copies writable.
            base.state = hdr.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            DlssNr_Dx12::State::EncodeContext c { &commands, &device, &target, target.state, {} };
            c.frame.ExposureTexture = &gameExposure;
            c.frame.ExposureState = gameExposure.state;
            require(s.EncodeInput(c), "optional exposure failure must retain the successful encode");
            require(c.modelInput == &base, "current model input remains valid");
            require(gameExposure.state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "game exposure state restored");
            return c;
        };
        auto good = frame();
        require(good.exposure == s.nr.exposure && good.exposure != nullptr, "live success publishes exposure");

        Config::Instance()->DlssNrHoldFrame.val = true;
        s.shader.failMode = fail;
        auto firstFailure = frame();
        require(firstFailure.exposure == nullptr, "failed capture frame uses manual fallback");
        require(s.nr.exposureReadable, "readable state alone is not a valid write");
        const auto callsBeforeRetry = s.shader.calls.size();
        const auto staleWrites = s.nr.exposure->writes;
        auto secondFailure = frame();
        require(secondFailure.exposure == nullptr, "held frame must not republish failed/stale exposure");
        require(s.shader.calls.size() > callsBeforeRetry + 1, "invalid held exposure must retry calculation");

        s.shader.failMode = -1;
        auto recovered = frame();
        require(recovered.exposure == s.nr.exposure, "recovered calculation publishes exposure");
        require(s.nr.exposure->writes > staleWrites, "recovery must perform a new successful exposure write");

        s.shader.failMode = fail;
        const auto callsBeforeHold = s.shader.calls.size();
        const auto validWrites = s.nr.exposure->writes;
        auto held = frame();
        require(held.exposure == s.nr.exposure, "valid held exposure is reused");
        require(s.shader.calls.size() == callsBeforeHold + 1, "valid hold runs encode but no exposure calculation");
        require(s.nr.exposure->writes == validWrites, "valid held exposure is unchanged");

        Config::Instance()->DlssNrWhitePointSource.val = source == 3 ? 1 : 3;
        s.shader.failMode = -1;
        auto changedSource = frame();
        require(changedSource.exposure == s.nr.exposure, "new source calculation succeeds");
        require(s.nr.exposureSource == Config::Instance()->DlssNrWhitePointSource.val, "hold does not alias old source");
        Config::Instance()->DlssNrWhitePointSource.val = 0;
        auto manual = frame();
        require(manual.exposure == nullptr, "manual source never inherits another source's held exposure");
    }
    std::cout << assertions << " exposure-hold checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
