#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <string_view>

#include "utils/paths.h"

namespace devilution {
namespace {

std::string Normalize(std::string path)
{
	for (char &c : path) {
		if (c == '\\')
			c = '/';
	}
	return path;
}

// Settings, saves and MPQs live in SDL_GetPrefPath("diasurgical", "devilution").
// Renaming the app bundle must not move them, or existing saves are lost.
TEST(PathsTest, PrefPathIsDiasurgicalDevilution)
{
#if !defined(_WIN32) && !defined(__APPLE__)
	const std::string dataHome = ::testing::TempDir() + "paths_test_data";
	ASSERT_EQ(setenv("XDG_DATA_HOME", dataHome.c_str(), 1), 0);
#endif
	const std::string prefPath = Normalize(paths::PrefPath());
	EXPECT_TRUE(prefPath.ends_with("/diasurgical/devilution/")) << prefPath;
	EXPECT_EQ(Normalize(paths::ConfigPath()), prefPath);
}

} // namespace
} // namespace devilution
