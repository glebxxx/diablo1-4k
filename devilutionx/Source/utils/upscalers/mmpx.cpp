/**
 * @file utils/upscalers/mmpx.cpp
 *
 * MMPX style-preserving pixel-art magnification (2x).
 *
 * Reference: Morgan McGuire and Mara Gagiu, "MMPX Style-Preserving Pixel-Art Magnification",
 * Journal of Computer Graphics Techniques (JCGT), vol. 10, no. 2, 83-104, 2021.
 * https://jcgt.org/published/0010/02/04/
 * https://casual-effects.com/research/McGuire2021PixelArt/
 *
 * This is a port of the reference implementation, which carries the following notice:
 *
 *   Copyright 2020 Morgan McGuire & Mara Gagiu.
 *   Available under the MIT license.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 *   and associated documentation files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in all copies or
 *   substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 *   BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 *   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Differences from the reference: brightness ignores the top byte (our pixels are opaque XRGB),
 * and pitched source/destination buffers are supported.
 *
 * Source neighbourhood (E is the current pixel), output pixels J K / L M:
 *
 *         P
 *       A B C
 *     Q D E F R
 *       G H I
 *         S
 */
#include <algorithm>

#include "utils/upscalers/upscalers.hpp"

namespace devilution {
namespace upscalers {

namespace {

uint32_t Luma(uint32_t color)
{
	return ((color >> 16) & 0xFF) + ((color >> 8) & 0xFF) + (color & 0xFF);
}

bool AllEq2(uint32_t b, uint32_t a0, uint32_t a1)
{
	return ((b ^ a0) | (b ^ a1)) == 0;
}

bool AllEq3(uint32_t b, uint32_t a0, uint32_t a1, uint32_t a2)
{
	return ((b ^ a0) | (b ^ a1) | (b ^ a2)) == 0;
}

bool AllEq4(uint32_t b, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
	return ((b ^ a0) | (b ^ a1) | (b ^ a2) | (b ^ a3)) == 0;
}

bool AnyEq3(uint32_t b, uint32_t a0, uint32_t a1, uint32_t a2)
{
	return b == a0 || b == a1 || b == a2;
}

bool NoneEq2(uint32_t b, uint32_t a0, uint32_t a1)
{
	return b != a0 && b != a1;
}

bool NoneEq4(uint32_t b, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
	return b != a0 && b != a1 && b != a2 && b != a3;
}

/** @brief Reads source pixels within 3 rows of the current row, clamping coordinates to the image. */
class ClampedSource {
public:
	explicit ClampedSource(const ConstImageView &src)
	    : src_(src)
	{
	}

	void SetRow(int y)
	{
		y_ = y;
		for (int dy = -3; dy <= 3; ++dy) {
			rows_[dy + 3] = src_.Row(std::clamp(y + dy, 0, src_.height - 1));
		}
	}

	/** @brief The pixel at (`x`, `y`), where `y` is within 3 rows of the row passed to `SetRow`. */
	[[nodiscard]] uint32_t operator()(int x, int y) const
	{
		return rows_[y - y_ + 3][std::clamp(x, 0, src_.width - 1)];
	}

private:
	const ConstImageView &src_;
	int y_ = 0;
	const uint32_t *rows_[7] = {};
};

} // namespace

void Mmpx2x(const ConstImageView &src, const ImageView &dst)
{
	ClampedSource at(src);
	for (int y = 0; y < src.height; ++y) {
		at.SetRow(y);
		uint32_t *out0 = dst.Row(2 * y);
		uint32_t *out1 = dst.Row(2 * y + 1);

		// The 3x3 neighbourhood and Q, R slide along the row.
		uint32_t a = at(-1, y - 1), b = at(0, y - 1), c = at(1, y - 1);
		uint32_t q = at(-2, y), d = at(-1, y), e = at(0, y), f = at(1, y), r = at(2, y);
		uint32_t g = at(-1, y + 1), h = at(0, y + 1), i = at(1, y + 1);

		for (int x = 0; x < src.width; ++x) {
			uint32_t j = e, k = e, l = e, m = e;

			if (((a ^ e) | (b ^ e) | (c ^ e) | (d ^ e) | (f ^ e) | (g ^ e) | (h ^ e) | (i ^ e)) != 0) {
				const uint32_t p = at(x, y - 2);
				const uint32_t s = at(x, y + 2);
				const uint32_t bl = Luma(b), dl = Luma(d), el = Luma(e), fl = Luma(f), hl = Luma(h);

				// 1:1 slope rules
				if ((d == b && d != h && d != f) && (el >= dl || e == a) && AnyEq3(e, a, c, g) && (el < dl || a != d || e != p || e != q)) j = d;
				if ((b == f && b != d && b != h) && (el >= bl || e == c) && AnyEq3(e, a, c, i) && (el < bl || c != b || e != p || e != r)) k = b;
				if ((h == d && h != f && h != b) && (el >= hl || e == g) && AnyEq3(e, a, g, i) && (el < hl || g != h || e != s || e != q)) l = h;
				if ((f == h && f != b && f != d) && (el >= fl || e == i) && AnyEq3(e, c, g, i) && (el < fl || i != h || e != r || e != s)) m = f;

				// Intersection rules
				if ((e != f && AllEq4(e, c, i, d, q) && AllEq2(f, b, h)) && f != at(x + 3, y)) k = m = f;
				if ((e != d && AllEq4(e, a, g, f, r) && AllEq2(d, b, h)) && d != at(x - 3, y)) j = l = d;
				if ((e != h && AllEq4(e, g, i, b, p) && AllEq2(h, d, f)) && h != at(x, y + 3)) l = m = h;
				if ((e != b && AllEq4(e, a, c, h, s) && AllEq2(b, d, f)) && b != at(x, y - 3)) j = k = b;

				// Triangle tip rules
				if (bl < el && AllEq4(e, g, h, i, s) && NoneEq4(e, a, d, c, f)) j = k = b;
				if (hl < el && AllEq4(e, a, b, c, p) && NoneEq4(e, d, g, i, f)) l = m = h;
				if (fl < el && AllEq4(e, a, d, g, q) && NoneEq4(e, b, c, i, h)) k = m = f;
				if (dl < el && AllEq4(e, c, f, i, r) && NoneEq4(e, b, a, g, h)) j = l = d;

				// 2:1 slope rules
				if (h != b) {
					if (h != a && h != e && h != c) {
						if (AllEq3(h, g, f, r) && NoneEq2(h, d, at(x + 2, y - 1))) l = m;
						if (AllEq3(h, i, d, q) && NoneEq2(h, f, at(x - 2, y - 1))) m = l;
					}
					if (b != i && b != g && b != e) {
						if (AllEq3(b, a, f, r) && NoneEq2(b, d, at(x + 2, y + 1))) j = k;
						if (AllEq3(b, c, d, q) && NoneEq2(b, f, at(x - 2, y + 1))) k = j;
					}
				}
				if (f != d) {
					if (d != i && d != e && d != c) {
						if (AllEq3(d, a, h, s) && NoneEq2(d, b, at(x + 1, y + 2))) j = l;
						if (AllEq3(d, g, b, p) && NoneEq2(d, h, at(x + 1, y - 2))) l = j;
					}
					if (f != e && f != a && f != g) {
						if (AllEq3(f, c, h, s) && NoneEq2(f, b, at(x - 1, y + 2))) k = m;
						if (AllEq3(f, i, b, p) && NoneEq2(f, h, at(x - 1, y - 2))) m = k;
					}
				}
			}

			out0[2 * x] = j;
			out0[2 * x + 1] = k;
			out1[2 * x] = l;
			out1[2 * x + 1] = m;

			a = b;
			b = c;
			c = at(x + 2, y - 1);
			q = d;
			d = e;
			e = f;
			f = r;
			r = at(x + 3, y);
			g = h;
			h = i;
			i = at(x + 2, y + 1);
		}
	}
}

} // namespace upscalers
} // namespace devilution
