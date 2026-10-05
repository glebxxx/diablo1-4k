#include <gtest/gtest.h>

#include "engine/render/renderer.h"
#include "utils/ui_fwd.h"

namespace devilution {
namespace {

TEST(DisplayDpiTest, ScalingFactorWithoutRendererIsOne)
{
	// SpawnWindow computes the preferred window size (HiDPI integer scaling) before the
	// renderer exists; this must not touch the renderer.
	Renderer *const saved = CurrentRenderer;
	CurrentRenderer = nullptr;
	EXPECT_EQ(GetDpiScalingFactor(), 1.0F);
	CurrentRenderer = saved;
}

} // namespace
} // namespace devilution
