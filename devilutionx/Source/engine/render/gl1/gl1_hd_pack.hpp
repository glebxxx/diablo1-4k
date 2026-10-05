/**
 * @file gl1_hd_pack.hpp
 *
 * HD texture packs: index, image loading and the texture memory budget.
 *
 * Nothing here calls OpenGL, so all of it is unit tested without a GL context.
 * The pack format is documented in docs/hd-pack-format.md.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/render/gl1/gl1_hd_key.hpp"

namespace devilution {

/** @brief The only scale HD packs are made at. */
constexpr int HdPackScale = 4;

struct HdKeyHash {
	size_t operator()(const HdKey &key) const noexcept
	{
		// The key is a cryptographic hash, so any 8 of its bytes are well distributed.
		size_t result = 0;
		for (size_t i = 0; i < sizeof(size_t); ++i)
			result = (result << 8) | key[i];
		return result;
	}
};

/** @brief One frame of an HD pack. */
struct HdPackEntry {
	/** Image file (WebP). */
	std::string path;
	/** Size of the original 1x frame. */
	uint16_t width = 0;
	uint16_t height = 0;
	/** Where the image goes in the HD canvas (`width * HdPackScale` by `height * HdPackScale`). */
	int bboxX = 0;
	int bboxY = 0;
	int bboxWidth = 0;
	int bboxHeight = 0;
};

/** @brief Maps frame keys to HD images, from every `manifest.json` below the pack directories. */
class HdPackIndex {
public:
	/**
	 * @brief Adds every pack (subdirectory) of `packsRoot`, in alphabetical order.
	 * A key found in an earlier pack wins.
	 */
	void AddPacksIn(const std::string &packsRoot);

	/** @brief Adds one pack directory. */
	void AddPack(const std::string &packDir);

	/**
	 * @brief Adds the frames of one manifest. Image paths are relative to `baseDir`.
	 * @return Number of frames added, or nullopt if the manifest is invalid.
	 */
	std::optional<size_t> AddManifest(std::string_view json, const std::string &baseDir, std::string *error = nullptr);

	[[nodiscard]] const HdPackEntry *Find(const HdKey &key) const;
	[[nodiscard]] size_t size() const { return entries_.size(); }
	[[nodiscard]] bool empty() const { return entries_.empty(); }
	void Clear() { entries_.clear(); }

private:
	std::unordered_map<HdKey, HdPackEntry, HdKeyHash> entries_;
};

/** @brief A decoded HD image: premultiplied RGBA, `HdPackScale` times the 1x frame size. */
struct HdImage {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;
};

/** @brief Decodes a WebP image into straight-alpha RGBA. */
std::optional<HdImage> DecodeWebp(std::span<const uint8_t> data);

/**
 * @brief Builds the HD texture for a frame from a decoded pack image.
 *
 * The image is placed into an empty canvas at the entry's bounding box. Its alpha is then
 * limited to the 1x silhouette dilated by one 1x pixel, so upscaling halos never leave the
 * area where the 1x sprite is clickable, multiplied by `alpha1x` (per 1x pixel, 255 = keep;
 * used for the blended parts of masked tiles; empty = none), and premultiplied.
 *
 * @return nullopt if the image or the box does not match the frame.
 */
std::optional<HdImage> ComposeHdImage(const HdPackEntry &entry, const HdImage &decoded,
    const HdIndexedFrame &frame, std::span<const uint8_t> alpha1x, std::string *error = nullptr);

/** @brief Reads, decodes and composes the image of `entry`. */
std::optional<HdImage> LoadHdImage(const HdPackEntry &entry, const HdIndexedFrame &frame,
    std::span<const uint8_t> alpha1x, std::string *error = nullptr);

/**
 * @brief Least-recently-used accounting for a memory budget.
 *
 * The owner calls `Touch` whenever an item is used and `TakeEvictions` to learn which items
 * to free. The most recently used item is never evicted, even if it alone exceeds the budget.
 */
template <typename Key, typename Hash = std::hash<Key>>
class HdLru {
public:
	explicit HdLru(size_t budgetBytes = 0)
	    : budget_(budgetBytes)
	{
	}

	void SetBudget(size_t budgetBytes) { budget_ = budgetBytes; }
	[[nodiscard]] size_t budget() const { return budget_; }
	[[nodiscard]] size_t usedBytes() const { return used_; }
	[[nodiscard]] size_t size() const { return order_.size(); }
	[[nodiscard]] bool Contains(const Key &key) const { return items_.contains(key); }

	/** @brief Marks `key` as most recently used, adding it with `bytes` if new. */
	void Touch(const Key &key, size_t bytes)
	{
		auto it = items_.find(key);
		if (it != items_.end()) {
			order_.splice(order_.begin(), order_, it->second);
			return;
		}
		order_.emplace_front(key, bytes);
		items_.emplace(key, order_.begin());
		used_ += bytes;
	}

	void Remove(const Key &key)
	{
		auto it = items_.find(key);
		if (it == items_.end())
			return;
		used_ -= it->second->second;
		order_.erase(it->second);
		items_.erase(it);
	}

	/** @brief Removes least recently used items until the total fits the budget. */
	std::vector<Key> TakeEvictions()
	{
		std::vector<Key> evicted;
		while (used_ > budget_ && order_.size() > 1) {
			const auto &[key, bytes] = order_.back();
			evicted.push_back(key);
			used_ -= bytes;
			items_.erase(key);
			order_.pop_back();
		}
		return evicted;
	}

	void Clear()
	{
		order_.clear();
		items_.clear();
		used_ = 0;
	}

private:
	using Order = std::list<std::pair<Key, size_t>>;
	Order order_;
	std::unordered_map<Key, typename Order::iterator, Hash> items_;
	size_t budget_;
	size_t used_ = 0;
};

} // namespace devilution
