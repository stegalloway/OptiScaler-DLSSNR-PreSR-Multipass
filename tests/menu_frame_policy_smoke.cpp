#include "../OptiScaler/menu/MenuFramePolicy.h"

static_assert(!MenuFramePolicy::SpatialOutlinesNeedFrame(false, false, false, false));
static_assert(!MenuFramePolicy::SpatialOutlinesNeedFrame(true, false, true, true));
static_assert(!MenuFramePolicy::SpatialOutlinesNeedFrame(false, true, true, true));
static_assert(!MenuFramePolicy::SpatialOutlinesNeedFrame(true, true, false, false));
static_assert(MenuFramePolicy::SpatialOutlinesNeedFrame(true, true, true, false));
static_assert(MenuFramePolicy::SpatialOutlinesNeedFrame(true, true, false, true));
static_assert(MenuFramePolicy::SpatialOutlinesNeedFrame(true, true, true, true));

int main() { return 0; }
