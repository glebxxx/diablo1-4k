#include "engine/render/gl1/gl1_hd_key.hpp"

#include <algorithm>

#include <picosha2.h>

#include "utils/clx_decode.hpp"

namespace devilution {

namespace {

constexpr char HexDigits[] = "0123456789abcdef";

void SetPixel(HdIndexedFrame &frame, int x, int y, uint8_t index)
{
	if (x < 0 || x >= frame.width || y < 0 || y >= frame.height)
		return;
	frame.pixels[static_cast<size_t>(y) * frame.width + x] = index;
}

HdIndexedFrame MakeTransparentFrame(int width, int height)
{
	HdIndexedFrame frame;
	frame.width = static_cast<uint16_t>(width);
	frame.height = static_cast<uint16_t>(height);
	frame.pixels.assign(static_cast<size_t>(width) * height, HdTransparentPixel);
	return frame;
}

int HexValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

} // namespace

HdIndexedFrame DecodeClxToIndexed(ClxSprite clx, const uint8_t *trn)
{
	const int width = clx.width();
	const int height = clx.height();
	HdIndexedFrame frame = MakeTransparentFrame(width, height);

	const uint8_t *src = clx.pixelData();
	const uint8_t *const srcEnd = src + clx.pixelDataSize();

	// CLX rows are stored bottom to top; commands may span rows.
	int row = height - 1;
	int x = 0;
	const auto put = [&](uint8_t index) {
		SetPixel(frame, x, row, trn != nullptr ? trn[index] : index);
		++x;
	};
	while (src < srcEnd && row >= 0) {
		const uint8_t v = *src++;
		if (!IsClxOpaque(v)) {
			x += v;
		} else if (IsClxOpaqueFill(v)) {
			const uint8_t fillWidth = GetClxOpaqueFillWidth(v);
			if (src >= srcEnd)
				break;
			const uint8_t index = *src++;
			for (int i = 0; i < fillWidth; ++i)
				put(index);
		} else {
			const uint8_t runLength = GetClxOpaquePixelsWidth(v);
			for (int i = 0; i < runLength && src < srcEnd; ++i)
				put(*src++);
		}
		while (x >= width && row >= 0) {
			x -= width;
			--row;
		}
	}
	return frame;
}

HdIndexedFrame DecodeDunTileToIndexed(const uint8_t *frameData, TileType tileType, int height)
{
	constexpr int Width = DunFrameWidth;
	constexpr int XStep = 2;
	constexpr int LowerHeight = DunFrameHeight / 2;
	constexpr int TriangleUpperHeight = (DunFrameHeight / 2) - 1;
	constexpr int TrapezoidUpperHeight = DunFrameHeight / 2;

	HdIndexedFrame frame = MakeTransparentFrame(Width, height);
	const uint8_t *src = frameData;
	int row = height - 1; // Tile data is stored bottom to top.

	const auto copyRow = [&](int startX, int rowWidth) {
		for (int x = 0; x < rowWidth; ++x)
			SetPixel(frame, startX + x, row, *src++);
		--row;
	};

	switch (tileType) {
	case TileType::Square:
		for (int i = 0; i < height; ++i)
			copyRow(0, Width);
		break;
	case TileType::TransparentSquare:
		for (int i = 0; i < height; ++i) {
			int x = 0;
			while (x < Width) {
				const auto run = static_cast<int8_t>(*src++);
				if (run < 0) {
					x -= run;
					continue;
				}
				for (int j = 0; j < run; ++j)
					SetPixel(frame, x++, row, *src++);
			}
			--row;
		}
		break;
	case TileType::LeftTriangle:
		for (int i = 0; i < LowerHeight; ++i)
			copyRow(Width - XStep * (i + 1), XStep * (i + 1));
		for (int i = 0; i < TriangleUpperHeight; ++i)
			copyRow(XStep * (i + 1), Width - XStep * (i + 1));
		break;
	case TileType::RightTriangle:
		for (int i = 0; i < LowerHeight; ++i)
			copyRow(0, XStep * (i + 1));
		for (int i = 0; i < TriangleUpperHeight; ++i)
			copyRow(0, Width - XStep * (i + 1));
		break;
	case TileType::LeftTrapezoid:
		for (int i = 0; i < LowerHeight; ++i)
			copyRow(Width - XStep * (i + 1), XStep * (i + 1));
		for (int i = 0; i < TrapezoidUpperHeight; ++i)
			copyRow(0, Width);
		break;
	case TileType::RightTrapezoid:
		for (int i = 0; i < LowerHeight; ++i)
			copyRow(0, XStep * (i + 1));
		for (int i = 0; i < TrapezoidUpperHeight; ++i)
			copyRow(0, Width);
		break;
	}
	return frame;
}

HdKey ComputeHdKey(const HdIndexedFrame &frame)
{
	std::vector<uint8_t> message;
	message.reserve(4 + frame.pixels.size() * 2);
	const auto putU16 = [&](uint16_t value) {
		message.push_back(static_cast<uint8_t>(value & 0xFF));
		message.push_back(static_cast<uint8_t>(value >> 8));
	};
	putU16(frame.width);
	putU16(frame.height);
	for (const uint16_t pixel : frame.pixels)
		putU16(pixel);

	HdKey key;
	picosha2::hash256(message.begin(), message.end(), key.begin(), key.end());
	return key;
}

std::string HdKeyToHex(const HdKey &key)
{
	std::string hex;
	hex.reserve(key.size() * 2);
	for (const uint8_t byte : key) {
		hex += HexDigits[byte >> 4];
		hex += HexDigits[byte & 0xF];
	}
	return hex;
}

std::optional<HdKey> HdKeyFromHex(std::string_view hex)
{
	HdKey key;
	if (hex.size() != key.size() * 2)
		return std::nullopt;
	for (size_t i = 0; i < key.size(); ++i) {
		const int hi = HexValue(hex[2 * i]);
		const int lo = HexValue(hex[2 * i + 1]);
		if (hi < 0 || lo < 0)
			return std::nullopt;
		key[i] = static_cast<uint8_t>((hi << 4) | lo);
	}
	return key;
}

bool UsesAnimatedIndices(const HdIndexedFrame &frame, HdAnimatedRange range)
{
	if (range.first > range.last)
		return false;
	return std::any_of(frame.pixels.begin(), frame.pixels.end(),
	    [range](uint16_t pixel) { return range.Contains(pixel); });
}

} // namespace devilution
