#include "engine/render/gl1/gl1_hd_pack.hpp"

#include <algorithm>
#include <cstdio>

#include <webp/decode.h>

#include "engine/render/gl1/gl1_hd_json.hpp"
#include "utils/file_util.h"

namespace devilution {

namespace {

constexpr std::string_view ManifestName = "manifest.json";

void SetError(std::string *error, std::string message)
{
	if (error != nullptr)
		*error = std::move(message);
}

std::optional<std::vector<uint8_t>> ReadWholeFile(const std::string &path)
{
	FILE *file = OpenFile(path.c_str(), "rb");
	if (file == nullptr)
		return std::nullopt;
	std::vector<uint8_t> data;
	uint8_t buffer[65536];
	size_t count;
	while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
		data.insert(data.end(), buffer, buffer + count);
	const bool failed = std::ferror(file) != 0;
	std::fclose(file);
	if (failed)
		return std::nullopt;
	return data;
}

/** @brief Pack-relative POSIX path -> native path, or nullopt if it could escape the pack. */
std::optional<std::string> ResolveImagePath(std::string_view file, const std::string &baseDir)
{
	if (file.empty() || file.front() == '/' || file.find('\\') != std::string_view::npos || file.find(':') != std::string_view::npos)
		return std::nullopt;
	std::string result = baseDir;
	size_t start = 0;
	while (start <= file.size()) {
		size_t end = file.find('/', start);
		if (end == std::string_view::npos)
			end = file.size();
		const std::string_view part = file.substr(start, end - start);
		if (part.empty() || part == "." || part == "..")
			return std::nullopt;
		result += DIRECTORY_SEPARATOR_STR;
		result.append(part);
		start = end + 1;
	}
	return result;
}

void CollectManifests(const std::string &dir, std::vector<std::string> &out, int depth)
{
	if (depth > 16)
		return;
	for (const std::string &file : ListFiles(dir.c_str())) {
		if (file == ManifestName)
			out.push_back(dir);
	}
	std::vector<std::string> subdirs = ListDirectories(dir.c_str());
	std::sort(subdirs.begin(), subdirs.end());
	for (const std::string &subdir : subdirs)
		CollectManifests(dir + DIRECTORY_SEPARATOR_STR + subdir, out, depth + 1);
}

} // namespace

void HdPackIndex::AddPacksIn(const std::string &packsRoot)
{
	if (!DirectoryExists(packsRoot))
		return;
	std::vector<std::string> packs = ListDirectories(packsRoot.c_str());
	std::sort(packs.begin(), packs.end());
	for (const std::string &pack : packs)
		AddPack(packsRoot + DIRECTORY_SEPARATOR_STR + pack);
}

void HdPackIndex::AddPack(const std::string &packDir)
{
	std::vector<std::string> manifestDirs;
	CollectManifests(packDir, manifestDirs, 0);
	for (const std::string &dir : manifestDirs) {
		const std::string manifestPath = dir + DIRECTORY_SEPARATOR_STR + std::string(ManifestName);
		std::optional<std::vector<uint8_t>> data = ReadWholeFile(manifestPath);
		if (!data)
			continue;
		const std::string_view json(reinterpret_cast<const char *>(data->data()), data->size());
		AddManifest(json, dir);
	}
}

std::optional<size_t> HdPackIndex::AddManifest(std::string_view json, const std::string &baseDir, std::string *error)
{
	std::string parseError;
	std::optional<HdJsonValue> root = ParseHdJson(json, &parseError);
	if (!root || !root->IsObject()) {
		SetError(error, root ? "manifest is not an object" : parseError);
		return std::nullopt;
	}
	const std::optional<int> scale = root->GetInt("scale", 1, 64);
	if (scale != HdPackScale) {
		SetError(error, "unsupported scale (only 4 is supported)");
		return std::nullopt;
	}
	const HdJsonValue *frames = root->Find("frames");
	if (frames == nullptr || !frames->IsArray()) {
		SetError(error, "missing frames");
		return std::nullopt;
	}

	size_t added = 0;
	for (const HdJsonValue &frame : frames->AsArray()) {
		const std::optional<std::string_view> sha = frame.GetString("sha256");
		const std::optional<std::string_view> file = frame.GetString("file");
		const std::optional<int> width = frame.GetInt("width", 1, 4096);
		const std::optional<int> height = frame.GetInt("height", 1, 4096);
		if (!sha || !file || !width || !height)
			continue;
		const std::optional<HdKey> key = HdKeyFromHex(*sha);
		const std::optional<std::string> path = ResolveImagePath(*file, baseDir);
		if (!key || !path)
			continue;

		HdPackEntry entry;
		entry.path = *path;
		entry.width = static_cast<uint16_t>(*width);
		entry.height = static_cast<uint16_t>(*height);
		entry.bboxWidth = *width * HdPackScale;
		entry.bboxHeight = *height * HdPackScale;
		if (const HdJsonValue *bbox = frame.Find("bbox"); bbox != nullptr) {
			if (!bbox->IsArray() || bbox->AsArray().size() != 4)
				continue;
			int values[4];
			bool valid = true;
			for (size_t i = 0; i < 4; ++i) {
				const HdJsonValue &v = bbox->AsArray()[i];
				valid = valid && v.IsNumber() && v.AsNumber() >= 0 && v.AsNumber() <= 65536;
				values[i] = valid ? static_cast<int>(v.AsNumber()) : 0;
			}
			if (!valid)
				continue;
			entry.bboxX = values[0];
			entry.bboxY = values[1];
			entry.bboxWidth = values[2];
			entry.bboxHeight = values[3];
		}
		if (entries_.try_emplace(*key, std::move(entry)).second)
			++added;
	}
	return added;
}

const HdPackEntry *HdPackIndex::Find(const HdKey &key) const
{
	const auto it = entries_.find(key);
	return it != entries_.end() ? &it->second : nullptr;
}

std::optional<HdImage> DecodeWebp(std::span<const uint8_t> data)
{
	int width = 0;
	int height = 0;
	uint8_t *pixels = WebPDecodeRGBA(data.data(), data.size(), &width, &height);
	if (pixels == nullptr)
		return std::nullopt;
	HdImage image;
	image.width = width;
	image.height = height;
	image.rgba.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
	WebPFree(pixels);
	return image;
}

std::optional<HdImage> ComposeHdImage(const HdPackEntry &entry, const HdImage &decoded,
    const HdIndexedFrame &frame, std::span<const uint8_t> alpha1x, std::string *error)
{
	if (entry.width != frame.width || entry.height != frame.height) {
		SetError(error, "manifest size does not match the frame");
		return std::nullopt;
	}
	const int width = frame.width * HdPackScale;
	const int height = frame.height * HdPackScale;
	if (decoded.width != entry.bboxWidth || decoded.height != entry.bboxHeight) {
		SetError(error, "image size does not match its bbox");
		return std::nullopt;
	}
	if (entry.bboxX + entry.bboxWidth > width || entry.bboxY + entry.bboxHeight > height) {
		SetError(error, "bbox is outside the frame");
		return std::nullopt;
	}
	if (!alpha1x.empty() && alpha1x.size() != frame.pixels.size()) {
		SetError(error, "alpha mask size does not match the frame");
		return std::nullopt;
	}

	// The 1x silhouette, dilated by one pixel.
	std::vector<uint8_t> allowed(frame.pixels.size(), 0);
	for (int y = 0; y < frame.height; ++y) {
		for (int x = 0; x < frame.width; ++x) {
			if (!frame.IsOpaque(x, y))
				continue;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					const int nx = x + dx;
					const int ny = y + dy;
					if (nx >= 0 && nx < frame.width && ny >= 0 && ny < frame.height)
						allowed[static_cast<size_t>(ny) * frame.width + nx] = 1;
				}
			}
		}
	}

	HdImage result;
	result.width = width;
	result.height = height;
	result.rgba.assign(static_cast<size_t>(width) * height * 4, 0);
	for (int y = 0; y < entry.bboxHeight; ++y) {
		const int dstY = entry.bboxY + y;
		for (int x = 0; x < entry.bboxWidth; ++x) {
			const int dstX = entry.bboxX + x;
			const size_t src = (static_cast<size_t>(y) * decoded.width + x) * 4;
			const size_t dst = (static_cast<size_t>(dstY) * width + dstX) * 4;
			const size_t pixel1x = static_cast<size_t>(dstY / HdPackScale) * frame.width + dstX / HdPackScale;
			unsigned alpha = allowed[pixel1x] != 0 ? decoded.rgba[src + 3] : 0;
			if (!alpha1x.empty())
				alpha = (alpha * alpha1x[pixel1x] + 127) / 255;
			for (int c = 0; c < 3; ++c)
				result.rgba[dst + c] = static_cast<uint8_t>((decoded.rgba[src + c] * alpha + 127) / 255);
			result.rgba[dst + 3] = static_cast<uint8_t>(alpha);
		}
	}
	return result;
}

std::optional<HdImage> LoadHdImage(const HdPackEntry &entry, const HdIndexedFrame &frame,
    std::span<const uint8_t> alpha1x, std::string *error)
{
	const std::optional<std::vector<uint8_t>> data = ReadWholeFile(entry.path);
	if (!data) {
		SetError(error, "cannot read " + entry.path);
		return std::nullopt;
	}
	const std::optional<HdImage> decoded = DecodeWebp(*data);
	if (!decoded) {
		SetError(error, "not a WebP image: " + entry.path);
		return std::nullopt;
	}
	return ComposeHdImage(entry, *decoded, frame, alpha1x, error);
}

} // namespace devilution
