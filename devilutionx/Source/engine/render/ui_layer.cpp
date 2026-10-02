#include "engine/render/ui_layer.hpp"

#include <limits>

namespace devilution {

UiLayerState UiLayer;

std::array<uint8_t, 256> UiKeyRemap = [] {
	std::array<uint8_t, 256> result;
	for (unsigned i = 0; i < 256; ++i)
		result[i] = static_cast<uint8_t>(i);
	return result;
}();

void RebuildUiKeyRemap(const SDL_Color *palette)
{
	for (unsigned i = 0; i < 256; ++i)
		UiKeyRemap[i] = static_cast<uint8_t>(i);
	for (const uint8_t key : { UiKeyTransparent, UiKeyHalf }) {
		const SDL_Color &color = palette[key];
		int bestDistance = std::numeric_limits<int>::max();
		uint8_t best = 32;
		for (unsigned i = 32; i < 256; ++i) {
			const int dr = palette[i].r - color.r;
			const int dg = palette[i].g - color.g;
			const int db = palette[i].b - color.b;
			const int distance = dr * dr + dg * dg + db * db;
			if (distance < bestDistance) {
				bestDistance = distance;
				best = static_cast<uint8_t>(i);
			}
		}
		UiKeyRemap[key] = best;
	}
}

} // namespace devilution
