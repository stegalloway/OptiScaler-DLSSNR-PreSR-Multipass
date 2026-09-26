#include <cassert>
#include <cstdint>
#include <cstdio>

#include "../OptiScaler/framegen/dlssg/MfgLatencyAnalysis.h"

static void Fill(MfgLatency::Frame (&frames)[64], uint64_t first)
{
    for (uint64_t i = 0; i < 64; ++i)
    {
        const uint64_t t = (first + i + 1) * 10000;
        auto& f = frames[i];
        f.frame_id = first + i;
        f.input_sample_time = t;
        f.simulation_start_time = t + 100;
        f.simulation_end_time = t + 1100;
        f.render_submit_start_time = t + 1200;
        f.render_submit_end_time = t + 1700;
        f.present_start_time = t + 1800;
        f.present_end_time = t + 2000;
        f.os_render_queue_start_time = t + 2200;
        f.gpu_render_start_time = t + 5200;
        f.gpu_render_end_time = t + 9000;
        f.gpu_active_render_time_us = 3800;
        f.gpu_frame_time_us = 10000;
        f.ai_frame_time_us = 1500;
    }
}

int main()
{
    MfgLatency::Frame frames[64] {};
    MfgLatency::History history {};
    Fill(frames, 100);

    auto report = MfgLatency::Analyze(frames, 10000000, 1000, 7, history);
    assert(report.timestamp_units == MfgLatency::Units::kMicroseconds);
    assert(!report.fresh);
    assert(!report.source_timing_confident);
    assert((report.source_timing_issue_mask & MfgLatency::kTimingNotFresh) != 0);

    Fill(frames, 164);
    report = MfgLatency::Analyze(frames, 10000000, 1500, 7, history);
    assert(report.fresh);
    assert(report.source_timing_confident);
    assert(report.queue_timing_confident);
    assert(report.source_interval_us == 10000);
    assert(report.median_queue_wait_us == 3000);
    assert(report.median_pipeline_latency_us == 8900);
    assert(report.median_gpu_frame_time_us == 10000);
    assert(report.median_ai_frame_time_us == 1500);

    Fill(frames, 228);
    for (size_t i = 32; i < 64; ++i)
    {
        const uint64_t shift = (i % 2 == 0) ? 8000 : 0;
        frames[i].present_start_time += shift;
        frames[i].present_end_time += shift;
    }
    report = MfgLatency::Analyze(frames, 10000000, 2000, 7, history);
    assert(!report.source_timing_confident);
    assert((report.source_timing_issue_mask &
            (MfgLatency::kTimingPresentCadenceMismatch |
             MfgLatency::kTimingPresentCadenceUnstable)) != 0);

    std::puts("PASS: MFG latency monitor analysis is bounded and read-only");
    return 0;
}
