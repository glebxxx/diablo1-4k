#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <webp/encode.h>
#include <webp/types.h>

#include "engine/render/gl1/gl1_hd_dump.hpp"
#include "engine/render/gl1/gl1_hd_json.hpp"
#include "engine/render/gl1/gl1_hd_key.hpp"
#include "engine/render/gl1/gl1_hd_pack.hpp"
#include "utils/png.h"
#include "utils/sdl_compat.h"

namespace devilution {
namespace {

namespace fs = std::filesystem;

constexpr uint16_t T = HdTransparentPixel;
constexpr int S = HdPackScale;

HdIndexedFrame MakeFrame(uint16_t width, uint16_t height, std::vector<uint16_t> pixels)
{
	HdIndexedFrame frame;
	frame.width = width;
	frame.height = height;
	frame.pixels = std::move(pixels);
	return frame;
}

/** @brief A straight-alpha RGBA image filled with one colour. */
HdImage SolidImage(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	HdImage image;
	image.width = width;
	image.height = height;
	for (int i = 0; i < width * height; ++i)
		image.rgba.insert(image.rgba.end(), { r, g, b, a });
	return image;
}

std::vector<uint8_t> EncodeWebpLossless(const HdImage &image)
{
	uint8_t *output = nullptr;
	const size_t size = WebPEncodeLosslessRGBA(image.rgba.data(), image.width, image.height, image.width * 4, &output);
	std::vector<uint8_t> result(output, output + size);
	WebPFree(output);
	return result;
}

void WriteFile(const fs::path &path, const void *data, size_t size)
{
	fs::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
}

void WriteFile(const fs::path &path, const std::string &text)
{
	WriteFile(path, text.data(), text.size());
}

std::string ReadFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

std::string ManifestJson(const std::string &framesJson, int scale = S)
{
	return R"({"asset": "test/synthetic.clx", "archive": "none", "scale": )" + std::to_string(scale)
	    + R"(, "frames": [)" + framesJson + "]}";
}

std::string FrameJson(const HdIndexedFrame &frame, const std::string &file, const std::string &extra = "")
{
	return R"({"group": 0, "frame": 0, "width": )" + std::to_string(frame.width)
	    + R"(, "height": )" + std::to_string(frame.height)
	    + R"(, "sha256": ")" + HdKeyToHex(ComputeHdKey(frame))
	    + R"(", "file": ")" + file + "\"" + extra + "}";
}

class HdPackTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		root_ = fs::temp_directory_path() / ("dvx_hd_pack_test_" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
		fs::remove_all(root_);
		fs::create_directories(root_);
	}

	void TearDown() override
	{
		fs::remove_all(root_);
	}

	fs::path root_;
};

// --- JSON ---

TEST(HdJsonTest, ParsesDocument)
{
	std::string error;
	const std::optional<HdJsonValue> value = ParseHdJson(
	    R"( {"a": [1, -2.5e1, true, false, null], "s": "x\"\\\/\né😀", "o": {}} )", &error);
	ASSERT_TRUE(value.has_value()) << error;
	const HdJsonValue &a = *value->Find("a");
	ASSERT_EQ(a.AsArray().size(), 5U);
	EXPECT_EQ(a.AsArray()[0].AsNumber(), 1);
	EXPECT_EQ(a.AsArray()[1].AsNumber(), -25);
	EXPECT_TRUE(a.AsArray()[2].AsBool());
	EXPECT_EQ(a.AsArray()[4].type(), HdJsonValue::Type::Null);
	EXPECT_EQ(*value->GetString("s"), "x\"\\/\n\xC3\xA9\xF0\x9F\x98\x80");
	EXPECT_TRUE(value->Find("o")->IsObject());
	EXPECT_EQ(value->Find("missing"), nullptr);
}

TEST(HdJsonTest, GetIntChecksRangeAndWholeNumbers)
{
	const std::optional<HdJsonValue> value = ParseHdJson(R"({"a": 4, "b": 4.5, "c": "4", "d": 100})");
	ASSERT_TRUE(value.has_value());
	EXPECT_EQ(value->GetInt("a", 0, 10), 4);
	EXPECT_FALSE(value->GetInt("b", 0, 10).has_value());
	EXPECT_FALSE(value->GetInt("c", 0, 10).has_value());
	EXPECT_FALSE(value->GetInt("d", 0, 10).has_value());
}

TEST(HdJsonTest, RejectsInvalidDocuments)
{
	for (const char *text : { "", "{", R"({"a" 1})", "[1,]", "[1 2]", R"("\x")", "01", "1.", "tru", "[] []", R"("\ud800")" }) {
		std::string error;
		EXPECT_FALSE(ParseHdJson(text, &error).has_value()) << text;
		EXPECT_FALSE(error.empty()) << text;
	}
	std::string deep(100, '[');
	deep += std::string(100, ']');
	EXPECT_FALSE(ParseHdJson(deep).has_value());
}

// --- Index ---

TEST(HdPackIndexTest, AddsFramesFromManifest)
{
	const HdIndexedFrame a = MakeFrame(2, 1, { 1, 2 });
	const HdIndexedFrame b = MakeFrame(1, 1, { 3 });
	HdPackIndex index;
	std::string error;
	const std::optional<size_t> added = index.AddManifest(
	    ManifestJson(FrameJson(a, "0_0.webp") + "," + FrameJson(b, "sub/0_1.webp", R"(, "bbox": [1, 2, 3, 2])")),
	    "base", &error);
	ASSERT_EQ(added, 2U) << error;

	const HdPackEntry *entryA = index.Find(ComputeHdKey(a));
	ASSERT_NE(entryA, nullptr);
	EXPECT_EQ(entryA->path, "base" DIRECTORY_SEPARATOR_STR "0_0.webp");
	EXPECT_EQ(entryA->width, 2);
	EXPECT_EQ(entryA->bboxWidth, 2 * S);
	EXPECT_EQ(entryA->bboxHeight, 1 * S);

	const HdPackEntry *entryB = index.Find(ComputeHdKey(b));
	ASSERT_NE(entryB, nullptr);
	EXPECT_EQ(entryB->path, "base" DIRECTORY_SEPARATOR_STR "sub" DIRECTORY_SEPARATOR_STR "0_1.webp");
	EXPECT_EQ(entryB->bboxX, 1);
	EXPECT_EQ(entryB->bboxY, 2);
	EXPECT_EQ(entryB->bboxWidth, 3);
	EXPECT_EQ(entryB->bboxHeight, 2);

	EXPECT_EQ(index.Find(ComputeHdKey(MakeFrame(1, 1, { 4 }))), nullptr);
}

TEST(HdPackIndexTest, OnlyScale4IsAccepted)
{
	const HdIndexedFrame a = MakeFrame(1, 1, { 1 });
	HdPackIndex index;
	std::string error;
	EXPECT_FALSE(index.AddManifest(ManifestJson(FrameJson(a, "a.webp"), 2), "base", &error).has_value());
	EXPECT_FALSE(error.empty());
	EXPECT_TRUE(index.empty());
}

TEST(HdPackIndexTest, SkipsUnsafeOrIncompleteFrames)
{
	HdPackIndex index;
	const auto frame = [](uint16_t i) { return MakeFrame(1, 1, { i }); };
	const std::string frames = FrameJson(frame(1), "../escape.webp") + ","
	    + FrameJson(frame(2), "/abs.webp") + ","
	    + FrameJson(frame(3), "a\\\\b.webp") + ","
	    + FrameJson(frame(4), "a//b.webp") + ","
	    + R"({"width": 1, "height": 1, "sha256": "zz", "file": "x.webp"},)"
	    + R"({"width": 1, "height": 1, "file": "x.webp"},)"
	    + FrameJson(frame(5), "ok.webp", R"(, "bbox": [0, 0, 4])");
	EXPECT_EQ(index.AddManifest(ManifestJson(frames), "base"), 0U);
}

TEST_F(HdPackTest, EarlierPackWins)
{
	const HdIndexedFrame a = MakeFrame(1, 1, { 1 });
	WriteFile(root_ / "a-pack" / "monsters" / "manifest.json", ManifestJson(FrameJson(a, "first.webp")));
	WriteFile(root_ / "b-pack" / "manifest.json", ManifestJson(FrameJson(a, "second.webp")));
	HdPackIndex index;
	index.AddPacksIn(root_.string());
	const HdPackEntry *entry = index.Find(ComputeHdKey(a));
	ASSERT_NE(entry, nullptr);
	EXPECT_EQ(fs::path(entry->path), root_ / "a-pack" / "monsters" / "first.webp");
}

// --- Images ---

TEST(HdImageTest, ComposeKeepsOpaqueInteriorAndPremultiplies)
{
	// One opaque 1x pixel in the middle of a 3x3 frame.
	const HdIndexedFrame frame = MakeFrame(3, 3, { T, T, T, T, 9, T, T, T, T });
	HdPackEntry entry;
	entry.width = 3;
	entry.height = 3;
	entry.bboxWidth = 3 * S;
	entry.bboxHeight = 3 * S;
	const HdImage decoded = SolidImage(3 * S, 3 * S, 200, 100, 50, 128);

	std::string error;
	const std::optional<HdImage> image = ComposeHdImage(entry, decoded, frame, {}, &error);
	ASSERT_TRUE(image.has_value()) << error;
	ASSERT_EQ(image->width, 3 * S);
	// The dilated silhouette covers the whole 3x3 frame, so every pixel keeps alpha 128.
	for (size_t i = 0; i < image->rgba.size(); i += 4) {
		EXPECT_EQ(image->rgba[i + 0], (200 * 128 + 127) / 255);
		EXPECT_EQ(image->rgba[i + 1], (100 * 128 + 127) / 255);
		EXPECT_EQ(image->rgba[i + 2], (50 * 128 + 127) / 255);
		EXPECT_EQ(image->rgba[i + 3], 128);
	}
}

TEST(HdImageTest, AlphaIsClampedToDilatedSilhouette)
{
	// 5x1 frame, only pixel 0 opaque: the dilated silhouette is pixels 0 and 1.
	const HdIndexedFrame frame = MakeFrame(5, 1, { 9, T, T, T, T });
	HdPackEntry entry;
	entry.width = 5;
	entry.height = 1;
	entry.bboxWidth = 5 * S;
	entry.bboxHeight = 1 * S;
	const std::optional<HdImage> image = ComposeHdImage(entry, SolidImage(5 * S, S, 255, 255, 255, 255), frame, {});
	ASSERT_TRUE(image.has_value());
	for (int y = 0; y < S; ++y) {
		for (int x = 0; x < 5 * S; ++x) {
			const uint8_t alpha = image->rgba[(static_cast<size_t>(y) * 5 * S + x) * 4 + 3];
			EXPECT_EQ(alpha, x < 2 * S ? 255 : 0) << x << "," << y;
		}
	}
}

TEST(HdImageTest, BboxPlacesCroppedImage)
{
	const HdIndexedFrame frame = MakeFrame(2, 2, { 1, 1, 1, 1 });
	HdPackEntry entry;
	entry.width = 2;
	entry.height = 2;
	entry.bboxX = 3;
	entry.bboxY = 5;
	entry.bboxWidth = 2;
	entry.bboxHeight = 1;
	const std::optional<HdImage> image = ComposeHdImage(entry, SolidImage(2, 1, 10, 20, 30, 255), frame, {});
	ASSERT_TRUE(image.has_value());
	for (int y = 0; y < 2 * S; ++y) {
		for (int x = 0; x < 2 * S; ++x) {
			const bool inside = x >= 3 && x < 5 && y == 5;
			EXPECT_EQ(image->rgba[(static_cast<size_t>(y) * 2 * S + x) * 4 + 3], inside ? 255 : 0) << x << "," << y;
		}
	}
}

TEST(HdImageTest, Alpha1xMultipliesPerSourcePixel)
{
	const HdIndexedFrame frame = MakeFrame(2, 1, { 1, 1 });
	HdPackEntry entry;
	entry.width = 2;
	entry.height = 1;
	entry.bboxWidth = 2 * S;
	entry.bboxHeight = S;
	const std::vector<uint8_t> alpha1x { 255, 128 };
	const std::optional<HdImage> image = ComposeHdImage(entry, SolidImage(2 * S, S, 255, 0, 0, 255), frame, alpha1x);
	ASSERT_TRUE(image.has_value());
	EXPECT_EQ(image->rgba[3], 255);
	EXPECT_EQ(image->rgba[(S + 1) * 4 + 3], 128);
	EXPECT_EQ(image->rgba[(S + 1) * 4 + 0], 128); // premultiplied red
}

TEST(HdImageTest, RejectsMismatchedSizes)
{
	const HdIndexedFrame frame = MakeFrame(2, 2, { 1, 1, 1, 1 });
	HdPackEntry entry;
	entry.width = 2;
	entry.height = 2;
	entry.bboxWidth = 2 * S;
	entry.bboxHeight = 2 * S;
	std::string error;
	EXPECT_FALSE(ComposeHdImage(entry, SolidImage(2 * S - 1, 2 * S, 0, 0, 0, 255), frame, {}, &error).has_value());
	EXPECT_FALSE(error.empty());

	HdPackEntry wrongFrame = entry;
	wrongFrame.width = 3;
	EXPECT_FALSE(ComposeHdImage(wrongFrame, SolidImage(2 * S, 2 * S, 0, 0, 0, 255), frame, {}).has_value());

	HdPackEntry outside = entry;
	outside.bboxX = 1;
	EXPECT_FALSE(ComposeHdImage(outside, SolidImage(2 * S, 2 * S, 0, 0, 0, 255), frame, {}).has_value());
}

TEST(HdImageTest, DecodesWebp)
{
	HdImage image = SolidImage(3, 2, 1, 2, 3, 255);
	image.rgba[4 * 4 + 3] = 0;
	const std::vector<uint8_t> webp = EncodeWebpLossless(image);
	const std::optional<HdImage> decoded = DecodeWebp(webp);
	ASSERT_TRUE(decoded.has_value());
	EXPECT_EQ(decoded->width, 3);
	EXPECT_EQ(decoded->height, 2);
	EXPECT_EQ(decoded->rgba[0], 1);
	EXPECT_EQ(decoded->rgba[4 * 4 + 3], 0);

	const std::vector<uint8_t> garbage { 'R', 'I', 'F', 'F', 0, 0, 0, 0 };
	EXPECT_FALSE(DecodeWebp(garbage).has_value());
}

TEST_F(HdPackTest, LoadsImageFromPack)
{
	const HdIndexedFrame frame = MakeFrame(2, 1, { 7, 8 });
	WriteFile(root_ / "pack" / "a" / "manifest.json", ManifestJson(FrameJson(frame, "img/0_0.webp")));
	const std::vector<uint8_t> webp = EncodeWebpLossless(SolidImage(2 * S, S, 40, 80, 120, 255));
	WriteFile(root_ / "pack" / "a" / "img" / "0_0.webp", webp.data(), webp.size());

	HdPackIndex index;
	index.AddPacksIn(root_.string());
	const HdPackEntry *entry = index.Find(ComputeHdKey(frame));
	ASSERT_NE(entry, nullptr);
	std::string error;
	const std::optional<HdImage> image = LoadHdImage(*entry, frame, {}, &error);
	ASSERT_TRUE(image.has_value()) << error;
	EXPECT_EQ(image->width, 2 * S);
	EXPECT_EQ(image->height, S);
	EXPECT_EQ(image->rgba[0], 40);
	EXPECT_EQ(image->rgba[3], 255);

	// A missing file is reported, not fatal.
	HdPackEntry missing = *entry;
	missing.path += ".gone";
	EXPECT_FALSE(LoadHdImage(missing, frame, {}, &error).has_value());
}

// --- Memory budget ---

TEST(HdLruTest, EvictsLeastRecentlyUsedFirst)
{
	HdLru<int> lru(300);
	lru.Touch(1, 100);
	lru.Touch(2, 100);
	lru.Touch(3, 100);
	EXPECT_TRUE(lru.TakeEvictions().empty());
	lru.Touch(1, 100); // 2 is now the least recently used
	lru.Touch(4, 100);
	EXPECT_EQ(lru.TakeEvictions(), (std::vector<int> { 2 }));
	EXPECT_EQ(lru.usedBytes(), 300U);
	lru.Touch(5, 150);
	EXPECT_EQ(lru.TakeEvictions(), (std::vector<int> { 3, 1 }));
	EXPECT_EQ(lru.usedBytes(), 250U);
	EXPECT_FALSE(lru.Contains(1));
	EXPECT_TRUE(lru.Contains(4));
}

TEST(HdLruTest, NeverEvictsTheMostRecentItem)
{
	HdLru<int> lru(100);
	lru.Touch(1, 50);
	lru.Touch(2, 500);
	EXPECT_EQ(lru.TakeEvictions(), (std::vector<int> { 1 }));
	EXPECT_TRUE(lru.Contains(2));
	lru.Remove(2);
	EXPECT_EQ(lru.usedBytes(), 0U);
}

// --- Dump mode ---

TEST_F(HdPackTest, DumpRoundTrip)
{
	std::array<SDL_Color, 256> palette {};
	for (int i = 0; i < 256; ++i)
		palette[i] = SDL_Color { static_cast<Uint8>(i), static_cast<Uint8>(255 - i), static_cast<Uint8>(i / 2), 255 };
	const HdIndexedFrame frame = MakeFrame(3, 2, { T, 10, 20, 30, 40, T });
	const HdKey key = ComputeHdKey(frame);
	const std::string hex = HdKeyToHex(key);

	HdDumper dumper((root_ / "dump").string());
	ASSERT_TRUE(dumper.enabled());
	ASSERT_TRUE(dumper.Dump(key, frame, palette, { "sprite", -1, false, 1 }));
	EXPECT_FALSE(dumper.Dump(key, frame, palette, { "sprite", -1, false, 1 })) << "each key is dumped once";

	const fs::path png = root_ / "dump" / "1x" / hex.substr(0, 2) / (hex + ".png");
	ASSERT_TRUE(fs::exists(png));
	EXPECT_EQ(ReadFile(root_ / "dump" / "manifest.tsv"),
	    "key\tkind\twidth\theight\ttile_type\ttrn\tlevel_type\n" + hex + "\tsprite\t3\t2\t-1\tnone\t1\n");

	// Read the dumped 1x frame back, upscale it 4x (nearest neighbour) and pack it.
	SDL_Surface *loaded = LoadPNG(png.string().c_str());
	ASSERT_NE(loaded, nullptr);
	SDL_Surface *rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
	SDL_FreeSurface(loaded);
	ASSERT_NE(rgba, nullptr);
	ASSERT_EQ(rgba->w, 3);
	ASSERT_EQ(rgba->h, 2);
	HdImage upscaled;
	upscaled.width = 3 * S;
	upscaled.height = 2 * S;
	for (int y = 0; y < 2 * S; ++y) {
		const auto *row = static_cast<const uint8_t *>(rgba->pixels) + static_cast<ptrdiff_t>(y / S) * rgba->pitch;
		for (int x = 0; x < 3 * S; ++x)
			upscaled.rgba.insert(upscaled.rgba.end(), row + (x / S) * 4, row + (x / S) * 4 + 4);
	}
	SDL_FreeSurface(rgba);
	EXPECT_EQ(upscaled.rgba[(static_cast<size_t>(0) * 3 * S + S) * 4 + 0], palette[10].r);
	EXPECT_EQ(upscaled.rgba[3], 0); // transparent 1x pixel stays transparent

	const std::vector<uint8_t> webp = EncodeWebpLossless(upscaled);
	WriteFile(root_ / "packs" / "test" / "manifest.json",
	    ManifestJson(R"({"width": 3, "height": 2, "sha256": ")" + hex + R"(", "file": "f.webp"})"));
	WriteFile(root_ / "packs" / "test" / "f.webp", webp.data(), webp.size());

	HdPackIndex index;
	index.AddPacksIn((root_ / "packs").string());
	const HdPackEntry *entry = index.Find(key);
	ASSERT_NE(entry, nullptr);
	const std::optional<HdImage> image = LoadHdImage(*entry, frame, {});
	ASSERT_TRUE(image.has_value());
	EXPECT_EQ(image->rgba[(static_cast<size_t>(S) * 3 * S + 0) * 4 + 1], palette[30].g);
}

} // namespace
} // namespace devilution
