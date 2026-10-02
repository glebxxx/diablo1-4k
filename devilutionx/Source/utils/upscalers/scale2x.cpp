/**
 * @file utils/upscalers/scale2x.cpp
 *
 * Scale2x (EPX / AdvMAME2x) and Scale3x (AdvMAME3x) magnification.
 *
 * Written from the published rules of the algorithms:
 * - EPX: Eric Johnston, LucasArts, 1992.
 * - Scale2x / Scale3x: Andrea Mazzoleni, https://www.scale2x.it/algorithm
 *
 * Source neighbourhood (E is the current pixel):
 *
 *     A B C
 *     D E F
 *     G H I
 */
#include "utils/upscalers/upscalers.hpp"

namespace devilution {
namespace upscalers {

namespace {

/** @brief Rows above, at and below `y`, clamped to the image. */
struct RowTriple {
	const uint32_t *above;
	const uint32_t *row;
	const uint32_t *below;
};

RowTriple GetRows(const ConstImageView &src, int y)
{
	return {
		src.Row(y > 0 ? y - 1 : 0),
		src.Row(y),
		src.Row(y + 1 < src.height ? y + 1 : y),
	};
}

} // namespace

void Scale2x(const ConstImageView &src, const ImageView &dst)
{
	const int lastX = src.width - 1;
	for (int y = 0; y < src.height; ++y) {
		const RowTriple rows = GetRows(src, y);
		uint32_t *out0 = dst.Row(2 * y);
		uint32_t *out1 = dst.Row(2 * y + 1);
		for (int x = 0; x < src.width; ++x) {
			const int xl = x > 0 ? x - 1 : 0;
			const int xr = x < lastX ? x + 1 : lastX;
			const uint32_t b = rows.above[x];
			const uint32_t d = rows.row[xl];
			const uint32_t e = rows.row[x];
			const uint32_t f = rows.row[xr];
			const uint32_t h = rows.below[x];
			if (b != h && d != f) {
				out0[2 * x] = d == b ? d : e;
				out0[2 * x + 1] = b == f ? f : e;
				out1[2 * x] = d == h ? d : e;
				out1[2 * x + 1] = h == f ? f : e;
			} else {
				out0[2 * x] = e;
				out0[2 * x + 1] = e;
				out1[2 * x] = e;
				out1[2 * x + 1] = e;
			}
		}
	}
}

void Scale3x(const ConstImageView &src, const ImageView &dst)
{
	const int lastX = src.width - 1;
	for (int y = 0; y < src.height; ++y) {
		const RowTriple rows = GetRows(src, y);
		uint32_t *out0 = dst.Row(3 * y);
		uint32_t *out1 = dst.Row(3 * y + 1);
		uint32_t *out2 = dst.Row(3 * y + 2);
		for (int x = 0; x < src.width; ++x) {
			const int xl = x > 0 ? x - 1 : 0;
			const int xr = x < lastX ? x + 1 : lastX;
			const uint32_t a = rows.above[xl];
			const uint32_t b = rows.above[x];
			const uint32_t c = rows.above[xr];
			const uint32_t d = rows.row[xl];
			const uint32_t e = rows.row[x];
			const uint32_t f = rows.row[xr];
			const uint32_t g = rows.below[xl];
			const uint32_t h = rows.below[x];
			const uint32_t i = rows.below[xr];
			uint32_t *o0 = out0 + 3 * x;
			uint32_t *o1 = out1 + 3 * x;
			uint32_t *o2 = out2 + 3 * x;
			if (b != h && d != f) {
				o0[0] = d == b ? d : e;
				o0[1] = (d == b && e != c) || (b == f && e != a) ? b : e;
				o0[2] = b == f ? f : e;
				o1[0] = (d == b && e != g) || (d == h && e != a) ? d : e;
				o1[1] = e;
				o1[2] = (b == f && e != i) || (h == f && e != c) ? f : e;
				o2[0] = d == h ? d : e;
				o2[1] = (d == h && e != i) || (h == f && e != g) ? h : e;
				o2[2] = h == f ? f : e;
			} else {
				o0[0] = o0[1] = o0[2] = e;
				o1[0] = o1[1] = o1[2] = e;
				o2[0] = o2[1] = o2[2] = e;
			}
		}
	}
}

} // namespace upscalers
} // namespace devilution
