#include "engine/render/ui_layer.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace devilution {

namespace {

/** @brief Colours below this index are colour-cycled in some levels and are not used as key replacements. */
constexpr unsigned FirstKeyReplacementColor = 32;

constexpr std::array<uint8_t, 256> InitialKeyRemap()
{
	std::array<uint8_t, 256> remap {};
	for (unsigned i = 0; i < remap.size(); ++i)
		remap[i] = static_cast<uint8_t>(i);
	// Before a palette is loaded, map the keys to black.
	remap[UiKeyTransparent] = 0;
	remap[UiKeyHalf] = 0;
	return remap;
}

int ColorDistance(const SDL_Color &a, const SDL_Color &b)
{
	const int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
	const int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
	const int db = static_cast<int>(a.b) - static_cast<int>(b.b);
	return (dr * dr) + (dg * dg) + (db * db);
}

uint8_t NearestKeyReplacement(const SDL_Color *palette, uint8_t key)
{
	unsigned best = FirstKeyReplacementColor;
	int bestDistance = std::numeric_limits<int>::max();
	for (unsigned i = FirstKeyReplacementColor; i < 256; ++i) {
		const int distance = ColorDistance(palette[key], palette[i]);
		if (distance < bestDistance) {
			best = i;
			bestDistance = distance;
		}
	}
	return static_cast<uint8_t>(best);
}

} // namespace

UiLayerState UiLayer = { false, nullptr, nullptr, InitialKeyRemap() };

void SetUiLayerTarget(const void *uiPixels, uint8_t *halfPlane)
{
	UiLayer.uiPixels = uiPixels;
	UiLayer.halfPlane = halfPlane;
}

void SetPresentLayered(bool presentLayered)
{
	UiLayer.presentLayered = presentLayered;
}

void UpdateUiKeyRemap(const SDL_Color *palette)
{
	std::array<uint8_t, 256> &remap = UiLayer.keyRemap;
	for (unsigned i = 0; i < remap.size(); ++i)
		remap[i] = static_cast<uint8_t>(i);
	remap[UiKeyTransparent] = NearestKeyReplacement(palette, UiKeyTransparent);
	remap[UiKeyHalf] = NearestKeyReplacement(palette, UiKeyHalf);
}

void ApplyTrnToSurface(const Surface &out, const uint8_t *trn, bool skipCyclingColors)
{
	for (int y = 0; y < out.h(); ++y) {
		uint8_t *dst = out.at(0, y);
		for (int x = 0; x < out.w(); ++x, ++dst) {
			if (!skipCyclingColors || *dst >= 32)
				*dst = trn[*dst];
		}
	}
}

void ApplyTrnToKeyedUiLayer(const Surface &out, const uint8_t *trn, bool skipCyclingColors)
{
	const UiKeyedTarget target = GetUiKeyedTarget();
	for (int y = 0; y < out.h(); ++y) {
		uint8_t *dst = out.at(0, y);
		for (int x = 0; x < out.w(); ++x, ++dst) {
			const uint8_t color = *dst;
			if (color == UiKeyTransparent)
				continue;
			if (color == UiKeyHalf) {
				uint8_t &half = *target.HalfAt(dst);
				if (!skipCyclingColors || half >= 32)
					half = trn[half];
				continue;
			}
			if (!skipCyclingColors || color >= 32)
				*dst = target.keyRemap[trn[color]];
		}
	}
}

} // namespace devilution
