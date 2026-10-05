/**
 * @file ru_text_fit_test.cpp
 *
 * Checks that the Russian UI strings in Translations/ru.po fit the boxes they
 * are drawn in.
 *
 * The boxes are listed in test/fixtures/ru_text_fit/constraints.tsv (see the
 * comment at the top of that file). Widths come from the engine's own
 * GetLineWidth() and WordWrapString() with the bundled fonts, so no game data
 * is needed. Every row is also checked for the English msgid as a baseline.
 *
 * Invoke with --report=<path> to also write a Markdown report of every Russian
 * string that is more than 25 % wider than its English source.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/assets.hpp"
#include "engine/render/text_render.hpp"
#include "utils/str_cat.hpp"
#include "utils/utf8.hpp"

#ifndef DEVILUTIONX_RU_PO_PATH
#error "DEVILUTIONX_RU_PO_PATH must be defined"
#endif
#ifndef DEVILUTIONX_RU_TEXT_FIT_CONSTRAINTS_PATH
#error "DEVILUTIONX_RU_TEXT_FIT_CONSTRAINTS_PATH must be defined"
#endif

// Invoke with --report=<path> to write the Markdown report to <path>.
static std::string ReportPath;

namespace devilution {
namespace {

constexpr char RuPoPath[] = DEVILUTIONX_RU_PO_PATH;
constexpr char ConstraintsPath[] = DEVILUTIONX_RU_TEXT_FIT_CONSTRAINTS_PATH;

/** Strings that grow by more than this many percent are listed in the report. */
constexpr int ReportExcessPercent = 25;

std::optional<std::string> ReadFile(const char *path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) return std::nullopt;
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

/** Splits on '\n', keeping empty lines. */
std::vector<std::string_view> SplitLines(std::string_view text)
{
	std::vector<std::string_view> lines;
	while (true) {
		const size_t eol = text.find('\n');
		lines.push_back(text.substr(0, eol));
		if (eol == std::string_view::npos) break;
		text.remove_prefix(eol + 1);
	}
	return lines;
}

std::optional<int> ParseInt(std::string_view text)
{
	int value;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc() || end != text.data() + text.size()) return std::nullopt;
	return value;
}

// ---------------------------------------------------------------------------
// PO parser
// ---------------------------------------------------------------------------

struct PoEntry {
	std::string msgctxt;
	std::string msgid;
	std::string msgidPlural;
	/** msgstr, or msgstr[0..n] for plural entries. */
	std::vector<std::string> msgstr;
	/** Source locations from the "#:" comments. */
	std::vector<std::string> references;
	bool fuzzy = false;

	[[nodiscard]] bool IsTranslated() const
	{
		return !fuzzy && !msgstr.empty()
		    && std::none_of(msgstr.begin(), msgstr.end(), [](const std::string &s) { return s.empty(); });
	}
};

int HexDigitValue(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/** Appends the contents of a C string literal ("...") to `out`, resolving escapes. */
void AppendPoString(std::string_view literal, std::string &out)
{
	const size_t begin = literal.find('"');
	const size_t end = literal.rfind('"');
	if (begin == std::string_view::npos || end <= begin) return;
	const std::string_view s = literal.substr(begin + 1, end - begin - 1);
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] != '\\' || i + 1 == s.size()) {
			out += s[i];
			continue;
		}
		const char c = s[++i];
		switch (c) {
		case 'n': out += '\n'; break;
		case 't': out += '\t'; break;
		case 'r': out += '\r'; break;
		case 'a': out += '\a'; break;
		case 'b': out += '\b'; break;
		case 'f': out += '\f'; break;
		case 'v': out += '\v'; break;
		case 'x': {
			int value = 0;
			for (int digits = 0; digits < 2 && i + 1 < s.size() && HexDigitValue(s[i + 1]) >= 0; ++digits)
				value = value * 16 + HexDigitValue(s[++i]);
			out += static_cast<char>(value);
			break;
		}
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7': {
			int value = c - '0';
			for (int digits = 1; digits < 3 && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '7'; ++digits)
				value = value * 8 + (s[++i] - '0');
			out += static_cast<char>(value);
			break;
		}
		default: out += c; break; // \" \\ \' \?
		}
	}
}

/**
 * Parses a gettext PO file. Obsolete entries ("#~") and the header are left
 * out; fuzzy entries are kept with `fuzzy` set.
 */
std::vector<PoEntry> ParsePo(std::string_view content)
{
	std::vector<PoEntry> entries;
	PoEntry entry;
	bool hasKeyword = false;
	bool hasMsgstr = false;
	bool obsolete = false;
	std::string *field = nullptr;

	const auto finishEntry = [&]() {
		if (hasKeyword && !obsolete && !entry.msgid.empty())
			entries.push_back(std::move(entry));
		entry = {};
		hasKeyword = false;
		hasMsgstr = false;
		obsolete = false;
		field = nullptr;
	};

	while (!content.empty()) {
		const size_t eol = content.find('\n');
		std::string_view line = content.substr(0, eol);
		content.remove_prefix(eol == std::string_view::npos ? content.size() : eol + 1);
		if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
		while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
			line.remove_prefix(1);

		if (line.empty()) {
			finishEntry();
			continue;
		}
		if (line.front() == '#') {
			// An obsolete entry ends at the first line that is not "#~", even without a blank line.
			if (hasMsgstr || (obsolete && !line.starts_with("#~"))) finishEntry();
			if (line.starts_with("#~")) {
				obsolete = true;
			} else if (line.starts_with("#,")) {
				if (line.find("fuzzy") != std::string_view::npos) entry.fuzzy = true;
			} else if (line.starts_with("#:")) {
				std::string_view refs = line.substr(2);
				while (!refs.empty()) {
					const size_t start = refs.find_first_not_of(" \t");
					if (start == std::string_view::npos) break;
					refs.remove_prefix(start);
					const size_t stop = refs.find_first_of(" \t");
					entry.references.emplace_back(refs.substr(0, stop));
					refs.remove_prefix(stop == std::string_view::npos ? refs.size() : stop);
				}
			}
			continue;
		}
		if (line.front() == '"') {
			if (field != nullptr) AppendPoString(line, *field);
			continue;
		}

		const size_t space = line.find_first_of(" \t");
		const std::string_view keyword = line.substr(0, space);
		const std::string_view value = space == std::string_view::npos ? std::string_view {} : line.substr(space);
		if (obsolete || ((keyword == "msgctxt" || keyword == "msgid") && hasMsgstr)) finishEntry();
		if (keyword == "msgctxt") {
			field = &entry.msgctxt;
		} else if (keyword == "msgid") {
			field = &entry.msgid;
		} else if (keyword == "msgid_plural") {
			field = &entry.msgidPlural;
		} else if (keyword == "msgstr" || (keyword.starts_with("msgstr[") && keyword.ends_with("]"))) {
			size_t index = 0;
			if (keyword != "msgstr") {
				const std::optional<int> parsed = ParseInt(keyword.substr(7, keyword.size() - 8));
				if (!parsed.has_value() || *parsed < 0) {
					field = nullptr;
					continue;
				}
				index = static_cast<size_t>(*parsed);
			}
			if (entry.msgstr.size() <= index) entry.msgstr.resize(index + 1);
			field = &entry.msgstr[index];
			hasMsgstr = true;
		} else {
			field = nullptr;
			continue;
		}
		hasKeyword = true;
		AppendPoString(value, *field);
	}
	finishEntry();
	return entries;
}

std::string CatalogKey(std::string_view msgctxt, std::string_view msgid)
{
	// Same key layout as gettext's MO files: context, EOT, message.
	return msgctxt.empty() ? std::string(msgid) : StrCat(msgctxt, "\x04", msgid);
}

struct Catalog {
	std::vector<PoEntry> entries;
	std::unordered_map<std::string, size_t> byKey;

	explicit Catalog(std::vector<PoEntry> parsed)
	    : entries(std::move(parsed))
	{
		for (size_t i = 0; i < entries.size(); ++i)
			byKey.emplace(CatalogKey(entries[i].msgctxt, entries[i].msgid), i);
	}

	[[nodiscard]] const PoEntry *Find(std::string_view msgctxt, std::string_view msgid) const
	{
		const auto it = byKey.find(CatalogKey(msgctxt, msgid));
		return it == byKey.end() ? nullptr : &entries[it->second];
	}
};

// ---------------------------------------------------------------------------
// Constraint table
// ---------------------------------------------------------------------------

enum class FitMode : uint8_t {
	None,       // Drawn as is.
	KerningFit, // UiFlags::KerningFitSpacing.
	Wrap,       // WordWrapString() into at most `maxLines` lines.
};

struct Constraint {
	std::string msgid;
	std::string msgctxt;
	int fontSize;
	GameFontTables font;
	int spacing;
	int maxWidth;
	FitMode mode;
	int maxLines;
	std::string source;
	int lineNumber;
};

std::optional<GameFontTables> FontFromSize(int size)
{
	switch (size) {
	case 12: return GameFont12;
	case 24: return GameFont24;
	case 30: return GameFont30;
	case 42: return GameFont42;
	case 46: return GameFont46;
	case 22: return FontSizeDialog;
	default: return std::nullopt;
	}
}

/** Resolves the "\t", "\n" and "\\" escapes of the constraint table. */
std::optional<std::string> UnescapeTsvField(std::string_view field)
{
	std::string out;
	for (size_t i = 0; i < field.size(); ++i) {
		if (field[i] != '\\') {
			out += field[i];
			continue;
		}
		if (++i == field.size()) return std::nullopt;
		switch (field[i]) {
		case 't': out += '\t'; break;
		case 'n': out += '\n'; break;
		case '\\': out += '\\'; break;
		default: return std::nullopt;
		}
	}
	return out;
}

constexpr std::string_view ConstraintsHeader = "msgid\tmsgctxt\tfont_size\tspacing\tmax_width\tfit\tsource";

/** Parses the constraint table. Returns an error message on failure. */
std::optional<std::string> ParseConstraints(std::string_view content, std::vector<Constraint> &out)
{
	bool seenHeader = false;
	int lineNumber = 0;
	for (std::string_view line : SplitLines(content)) {
		++lineNumber;
		if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
		if (line.empty() || line.front() == '#') continue;
		if (!seenHeader) {
			if (line != ConstraintsHeader) return StrCat("line ", lineNumber, ": expected the header \"", ConstraintsHeader, "\"");
			seenHeader = true;
			continue;
		}
		std::vector<std::string_view> fields;
		while (true) {
			const size_t tab = line.find('\t');
			fields.push_back(line.substr(0, tab));
			if (tab == std::string_view::npos) break;
			line.remove_prefix(tab + 1);
		}
		if (fields.size() != 7) return StrCat("line ", lineNumber, ": expected 7 fields, got ", fields.size());

		const std::optional<std::string> msgid = UnescapeTsvField(fields[0]);
		const std::optional<std::string> msgctxt = UnescapeTsvField(fields[1]);
		const std::optional<int> fontSize = ParseInt(fields[2]);
		const std::optional<int> spacing = ParseInt(fields[3]);
		const std::optional<int> maxWidth = ParseInt(fields[4]);
		if (!msgid.has_value() || msgid->empty() || !msgctxt.has_value())
			return StrCat("line ", lineNumber, ": bad msgid or msgctxt");
		if (!fontSize.has_value() || !FontFromSize(*fontSize).has_value())
			return StrCat("line ", lineNumber, ": bad font_size \"", fields[2], "\"");
		if (!spacing.has_value() || *spacing < 0)
			return StrCat("line ", lineNumber, ": bad spacing \"", fields[3], "\"");
		if (!maxWidth.has_value() || *maxWidth <= 0)
			return StrCat("line ", lineNumber, ": bad max_width \"", fields[4], "\"");

		Constraint constraint {
			.msgid = *msgid,
			.msgctxt = *msgctxt,
			.fontSize = *fontSize,
			.font = *FontFromSize(*fontSize),
			.spacing = *spacing,
			.maxWidth = *maxWidth,
			.mode = FitMode::None,
			.maxLines = 1,
			.source = std::string(fields[6]),
			.lineNumber = lineNumber,
		};
		const std::string_view fit = fields[5];
		if (fit == "yes") {
			constraint.mode = FitMode::KerningFit;
		} else if (fit.starts_with("wrap:")) {
			const std::optional<int> maxLines = ParseInt(fit.substr(5));
			if (!maxLines.has_value() || *maxLines < 1)
				return StrCat("line ", lineNumber, ": bad fit \"", fit, "\"");
			constraint.mode = FitMode::Wrap;
			constraint.maxLines = *maxLines;
		} else if (fit != "no") {
			return StrCat("line ", lineNumber, ": bad fit \"", fit, "\"");
		}
		if (constraint.source.empty()) return StrCat("line ", lineNumber, ": empty source");
		out.push_back(std::move(constraint));
	}
	if (!seenHeader) return "missing header";
	return std::nullopt;
}

// ---------------------------------------------------------------------------
// Oracle
// ---------------------------------------------------------------------------

/**
 * A copy of AdjustSpacingToFitHorizontally() in
 * Source/engine/render/text_render.cpp, which is internal to that file.
 * Keep the two in sync.
 */
int AdjustSpacingToFitHorizontally(int &lineWidth, int maxSpacing, int charactersInLine, int availableWidth)
{
	if (lineWidth <= availableWidth || charactersInLine < 2)
		return maxSpacing;

	const int overhang = lineWidth - availableWidth;
	const int spacingRedux = (overhang + charactersInLine - 2) / (charactersInLine - 1);
	lineWidth -= spacingRedux * (charactersInLine - 1);
	return maxSpacing - spacingRedux;
}

struct Measurement {
	bool fits = true;
	/** Widest line at the row's spacing (after wrapping in wrap mode). */
	int width = 0;
	/** Widest line at spacing 0. With KerningFitSpacing the line fits when this is <= max_width. */
	int minWidth = 0;
	/** Number of lines drawn. */
	int lines = 0;
	/** Smallest spacing KerningFitSpacing ends up with. */
	int spacing = 0;
};

Measurement Measure(std::string_view text, const Constraint &constraint)
{
	Measurement m { .spacing = constraint.spacing };
	std::string wrapped;
	if (constraint.mode == FitMode::Wrap) {
		wrapped = WordWrapString(text, static_cast<unsigned>(constraint.maxWidth), constraint.font, constraint.spacing);
		text = wrapped;
	}
	for (const std::string_view line : SplitLines(text)) {
		++m.lines;
		int charactersInLine = 0;
		int lineWidth = GetLineWidth(line, constraint.font, constraint.spacing, &charactersInLine);
		m.width = std::max(m.width, lineWidth);
		m.minWidth = std::max(m.minWidth, GetLineWidth(line, constraint.font, 0));
		if (constraint.mode == FitMode::KerningFit) {
			const int spacing = AdjustSpacingToFitHorizontally(lineWidth, constraint.spacing, charactersInLine, constraint.maxWidth);
			m.spacing = std::min(m.spacing, spacing);
			if (spacing < 0 || lineWidth > constraint.maxWidth) m.fits = false;
		} else if (lineWidth > constraint.maxWidth) {
			m.fits = false;
		}
	}
	if (constraint.mode == FitMode::Wrap && m.lines > constraint.maxLines) m.fits = false;
	return m;
}

/** The width that is compared to max_width: at spacing 0 for KerningFitSpacing rows. */
int EffectiveWidth(const Constraint &constraint, const Measurement &m)
{
	return constraint.mode == FitMode::KerningFit ? m.minWidth : m.width;
}

std::string Quote(std::string_view text)
{
	std::string out = "\"";
	for (const char c : text) {
		if (c == '\n') {
			out += "\\n";
		} else if (c == '"' || c == '\\') {
			out += '\\';
			out += c;
		} else {
			out += c;
		}
	}
	out += '"';
	return out;
}

std::string DescribeFailure(const Constraint &constraint, std::string_view text, const Measurement &m)
{
	std::string out = StrCat("constraints.tsv:", constraint.lineNumber, " (", constraint.source, ", font ",
	    constraint.fontSize, ", spacing ", constraint.spacing, "): ", Quote(constraint.msgid), " -> ", Quote(text), ": ");
	switch (constraint.mode) {
	case FitMode::None:
		StrAppend(out, "width ", m.width, " px > ", constraint.maxWidth, " px");
		break;
	case FitMode::KerningFit:
		StrAppend(out, "KerningFitSpacing squeezes the spacing to ", m.spacing, " (width ", m.minWidth,
		    " px at spacing 0 > ", constraint.maxWidth, " px)");
		break;
	case FitMode::Wrap:
		StrAppend(out, m.lines, " lines (max ", constraint.maxLines, "), widest ", m.width, " px (max ",
		    constraint.maxWidth, " px)");
		break;
	}
	return out;
}

std::vector<std::string_view> EnglishTexts(const PoEntry &entry)
{
	std::vector<std::string_view> texts { entry.msgid };
	if (!entry.msgidPlural.empty()) texts.emplace_back(entry.msgidPlural);
	return texts;
}

std::vector<std::string_view> RussianTexts(const PoEntry &entry)
{
	return { entry.msgstr.begin(), entry.msgstr.end() };
}

/**
 * The font files that GetLineWidth() loads to measure the English and Russian texts of the
 * constraint table, named as GetFontPath() in text_render.cpp names them: "fonts\<size>-<row>.clx",
 * where <row> is the code point divided by 256.
 */
std::set<std::string> RequiredFontFiles(const Catalog &catalog, const std::vector<Constraint> &constraints)
{
	std::set<std::string> files;
	for (const Constraint &constraint : constraints) {
		const PoEntry *entry = catalog.Find(constraint.msgctxt, constraint.msgid);
		if (entry == nullptr) continue;
		std::vector<std::string_view> texts = EnglishTexts(*entry);
		const std::vector<std::string_view> russian = RussianTexts(*entry);
		texts.insert(texts.end(), russian.begin(), russian.end());
		for (const std::string_view text : texts) {
			for (const char32_t c : Utf8CodePoints(text)) {
				if (c == Utf8DecodeError) break;
				if (c == U'\n' || c == U'\u200B') continue; // GetLineWidth() draws no glyph for these.
				files.insert(StrCat("fonts\\", constraint.fontSize, "-", AsHexPad2(static_cast<uint16_t>(c >> 8)), ".clx"));
			}
		}
	}
	return files;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

struct Data {
	std::optional<Catalog> catalog;
	std::vector<Constraint> constraints;
	std::string error;
};

const Data &LoadData()
{
	static const Data data = []() {
		Data result;
		const std::optional<std::string> po = ReadFile(RuPoPath);
		if (!po.has_value()) {
			result.error = StrCat("cannot read ", RuPoPath);
			return result;
		}
		result.catalog.emplace(ParsePo(*po));
		const std::optional<std::string> tsv = ReadFile(ConstraintsPath);
		if (!tsv.has_value()) {
			result.error = StrCat("cannot read ", ConstraintsPath);
			return result;
		}
		if (const std::optional<std::string> error = ParseConstraints(*tsv, result.constraints); error.has_value())
			result.error = StrCat(ConstraintsPath, ": ", *error);
		return result;
	}();
	return data;
}

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------

struct ReportFont {
	GameFontTables font;
	int fontSize;
	int spacing;
};

/**
 * The font a string is most likely drawn with: the first constraint row for it,
 * otherwise a guess from its source locations.
 */
ReportFont LikelyFont(const PoEntry &entry, const std::vector<Constraint> &constraints)
{
	for (const Constraint &constraint : constraints) {
		if (constraint.msgid == entry.msgid && constraint.msgctxt == entry.msgctxt)
			return { constraint.font, constraint.fontSize, constraint.spacing };
	}
	for (const std::string &ref : entry.references) {
		if (ref.starts_with("Source/gamemenu.cpp") || ref.starts_with("Source/gmenu.cpp"))
			return { GameFont46, 46, 2 };
	}
	for (const std::string &ref : entry.references) {
		if (ref.starts_with("Source/DiabloUI/") || ref.starts_with("Source/options.cpp"))
			return { GameFont24, 24, 1 };
	}
	return { GameFont12, 12, 1 };
}

int WidestLine(std::string_view text, GameFontTables font, int spacing)
{
	int width = 0;
	for (const std::string_view line : SplitLines(text))
		width = std::max(width, GetLineWidth(line, font, spacing));
	return width;
}

int WidestText(const std::vector<std::string_view> &texts, GameFontTables font, int spacing)
{
	int width = 0;
	for (const std::string_view text : texts)
		width = std::max(width, WidestLine(text, font, spacing));
	return width;
}

/** Formats a string for a Markdown table cell. */
std::string MarkdownCell(std::string_view text, size_t maxCodepoints = 60)
{
	std::string out;
	size_t codepoints = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
			if (codepoints == maxCodepoints) {
				out += "…";
				break;
			}
			++codepoints;
		}
		if (c == '\n') {
			out += "\\n";
		} else if (c == '|') {
			out += "\\|";
		} else if (c == '`') {
			out += '\'';
		} else {
			out += c;
		}
	}
	return StrCat("`", out, "`");
}

std::string MarkdownLocations(const std::vector<std::string> &references)
{
	constexpr size_t MaxShown = 3;
	std::string out;
	for (size_t i = 0; i < references.size() && i < MaxShown; ++i) {
		if (i != 0) out += ", ";
		out += references[i];
	}
	if (references.size() > MaxShown) StrAppend(out, " (+", references.size() - MaxShown, ")");
	return out;
}

std::string_view FitModeName(const Constraint &constraint)
{
	switch (constraint.mode) {
	case FitMode::KerningFit: return "fit";
	case FitMode::Wrap: return "wrap";
	default: return "";
	}
}

std::string BuildReport(const Catalog &catalog, const std::vector<Constraint> &constraints)
{
	std::string out;
	StrAppend(out,
	    "# Russian text fit\n"
	    "\n"
	    "<!-- Generated by devilutionx/test/ru_text_fit_test.cpp. Do not edit by hand. -->\n"
	    "\n"
	    "Widths of the Russian strings in `devilutionx/Translations/ru.po`, measured with the engine's ",
	    "`GetLineWidth()` and the fonts bundled with DevilutionX. To regenerate this file, build with ",
	    "`-DBUILD_TESTING=ON` and run from the repository root:\n"
	    "\n"
	    "```\n"
	    "build/ru_text_fit_test --report=docs/i18n/ru-text-fit.md\n"
	    "```\n"
	    "\n");

	// Constraint table summary.
	struct Row {
		const Constraint *constraint;
		std::string_view text;
		Measurement m;
	};
	// Rows that do not fit sort first: a wrap row can fail on its line count while its widest line has room.
	const auto tightness = [](const Constraint &constraint, const Measurement &m) {
		return std::pair { m.fits, constraint.maxWidth - EffectiveWidth(constraint, m) };
	};
	std::vector<Row> rows;
	size_t russianFailures = 0;
	size_t englishFailures = 0;
	for (const Constraint &constraint : constraints) {
		const PoEntry *entry = catalog.Find(constraint.msgctxt, constraint.msgid);
		if (entry == nullptr) continue;
		// Failures are counted once per row, for English on the same rows as EnglishFits.
		bool englishFits = true;
		for (const std::string_view text : EnglishTexts(*entry))
			englishFits = Measure(text, constraint).fits && englishFits;
		if (!englishFits) ++englishFailures;
		if (!entry->IsTranslated()) continue;
		std::optional<Row> tightest;
		for (const std::string_view text : RussianTexts(*entry)) {
			const Measurement m = Measure(text, constraint);
			if (!tightest || tightness(constraint, m) < tightness(constraint, tightest->m))
				tightest = Row { &constraint, text, m };
		}
		if (!tightest) continue;
		if (!tightest->m.fits) ++russianFailures;
		rows.push_back(*tightest);
	}
	std::stable_sort(rows.begin(), rows.end(), [&](const Row &a, const Row &b) {
		return tightness(*a.constraint, a.m) < tightness(*b.constraint, b.m);
	});

	StrAppend(out, "## Constraint table\n\n`devilutionx/test/fixtures/ru_text_fit/constraints.tsv` lists ", constraints.size(),
	    " places where a string is drawn in a box of known width. Rows that do not fit: Russian ", russianFailures,
	    ", English ", englishFailures, ".\n\n");
	constexpr size_t TightestShown = 40;
	StrAppend(out,
	    "The ", std::min(TightestShown, rows.size()), " rows with the least room to spare; rows that do not fit come first. ",
	    "For `fit` rows (`KerningFitSpacing`) the width is the width at spacing 0, ",
	    "for `wrap` rows the widest wrapped line.\n"
	    "\n"
	    "| Room | Width | Box | Font | Fit | English | Russian | Drawn at |\n"
	    "|---:|---:|---:|---|---|---|---|---|\n");
	for (size_t i = 0; i < rows.size() && i < TightestShown; ++i) {
		const Row &row = rows[i];
		const Constraint &c = *row.constraint;
		const int width = EffectiveWidth(c, row.m);
		StrAppend(out, "| ", c.maxWidth - width, " | ", width, " | ", c.maxWidth, " | ", c.fontSize, "/", c.spacing, " | ",
		    FitModeName(c), " | ", MarkdownCell(c.msgid), " | ", MarkdownCell(row.text), " | ", c.source, " |\n");
	}

	// Strings that grew the most.
	struct Growth {
		const PoEntry *entry;
		ReportFont font;
		int englishWidth;
		int russianWidth;
		std::string_view russian;
	};
	std::vector<Growth> growths;
	size_t translated = 0;
	for (const PoEntry &entry : catalog.entries) {
		if (!entry.IsTranslated()) continue;
		++translated;
		const ReportFont font = LikelyFont(entry, constraints);
		const int englishWidth = WidestText(EnglishTexts(entry), font.font, font.spacing);
		if (englishWidth <= 0) continue;
		int russianWidth = 0;
		std::string_view russian;
		for (const std::string_view text : RussianTexts(entry)) {
			const int width = WidestLine(text, font.font, font.spacing);
			if (width > russianWidth || russian.empty()) {
				russianWidth = width;
				russian = text;
			}
		}
		if (static_cast<int64_t>(russianWidth) * 100 > static_cast<int64_t>(englishWidth) * (100 + ReportExcessPercent))
			growths.push_back({ &entry, font, englishWidth, russianWidth, russian });
	}
	std::stable_sort(growths.begin(), growths.end(), [](const Growth &a, const Growth &b) {
		// Compare russian/english ratios without rounding.
		const int64_t lhs = static_cast<int64_t>(a.russianWidth) * b.englishWidth;
		const int64_t rhs = static_cast<int64_t>(b.russianWidth) * a.englishWidth;
		if (lhs != rhs) return lhs > rhs;
		return a.russianWidth - a.englishWidth > b.russianWidth - b.englishWidth;
	});

	StrAppend(out, "\n## Strings more than ", ReportExcessPercent, " % wider than English\n\n",
	    growths.size(), " of ", translated, " translated strings, sorted by excess. ",
	    "Each string is measured at its likely font: the font of its first row in the constraint table, ",
	    "otherwise 46/2 for the game menu, 24/1 for `Source/DiabloUI/` and `Source/options.cpp`, and 12/1 for the rest ",
	    "(font size/spacing). Multi-line strings use their widest line, plural entries their widest form. ",
	    "Long texts such as dialogue are word-wrapped by the engine, so a large excess there only means more lines. ",
	    "Strings are cut at 60 characters; locations come from the `#:` comments in `ru.po`.\n"
	    "\n"
	    "| Excess | English px | Russian px | Font | English | Russian | Locations |\n"
	    "|---:|---:|---:|---|---|---|---|\n");
	for (const Growth &growth : growths) {
		const int percent = (growth.russianWidth - growth.englishWidth) * 100 / growth.englishWidth;
		const PoEntry &entry = *growth.entry;
		std::string english = MarkdownCell(entry.msgid);
		if (!entry.msgctxt.empty()) english = StrCat(english, " (", entry.msgctxt, ")");
		StrAppend(out, "| +", percent, " % | ", growth.englishWidth, " | ", growth.russianWidth, " | ", growth.font.fontSize, "/",
		    growth.font.spacing, " | ", english, " | ", MarkdownCell(growth.russian), " | ", MarkdownLocations(entry.references), " |\n");
	}
	return out;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(RuTextFitPoParserTest, ParsesEntries)
{
	const std::vector<PoEntry> entries = ParsePo(
	    "# Header comment\n"
	    "msgid \"\"\n"
	    "msgstr \"\"\n"
	    "\"Language: ru\\n\"\n"
	    "\n"
	    "#: Source/a.cpp:1 Source/b.cpp:2\n"
	    "#: Source/c.cpp:3\n"
	    "msgid \"Simple\"\n"
	    "msgstr \"Просто\"\n"
	    "\n"
	    "#. Extracted comment\n"
	    "msgctxt \"spell\"\n"
	    "msgid \"\"\n"
	    "\"Multi \"\n"
	    "\"line\"\n"
	    "msgstr \"\"\n"
	    "\"Tab\\there\\n\"\n"
	    "\"\\\"quoted\\\" \\\\ \\101\\x42\"\n"
	    "\r\n"
	    "msgid \"{:d} Charge\"\n"
	    "msgid_plural \"{:d} Charges\"\n"
	    "msgstr[0] \"{:d} заряд\"\n"
	    "msgstr[1] \"{:d} заряда\"\n"
	    "msgstr[2] \"{:d} зарядов\"\n"
	    "\n"
	    "#, fuzzy, c++-format\n"
	    "#| msgid \"Old\"\n"
	    "msgid \"Fuzzy\"\n"
	    "msgstr \"Нечётко\"\n"
	    "\n"
	    "#, fuzzy\n"
	    "#~ msgid \"Obsolete\"\n"
	    "#~ msgstr \"Устарело\"\n"
	    "\n"
	    "msgid \"No blank line before the next entry\"\n"
	    "msgstr \"\"\n"
	    "#: Source/d.cpp:4\n"
	    "msgid \"Untranslated\"\n"
	    "msgstr \"\"\n"
	    "#~ msgid \"Obsolete without a blank line\"\n"
	    "#~ msgstr \"Устарело\"\n"
	    "#: Source/e.cpp:5\n"
	    "msgid \"After obsolete\"\n"
	    "msgstr \"После устаревшего\"\n"
	    "#~ msgid \"Obsolete again\"\n"
	    "#~ msgstr \"Снова устарело\"\n"
	    "msgid \"Right after obsolete\"\n"
	    "msgstr \"Сразу после устаревшего\"\n");

	ASSERT_EQ(entries.size(), 8U);

	EXPECT_EQ(entries[0].msgid, "Simple");
	EXPECT_EQ(entries[0].msgctxt, "");
	ASSERT_EQ(entries[0].msgstr.size(), 1U);
	EXPECT_EQ(entries[0].msgstr[0], "Просто");
	EXPECT_EQ(entries[0].references, (std::vector<std::string> { "Source/a.cpp:1", "Source/b.cpp:2", "Source/c.cpp:3" }));
	EXPECT_TRUE(entries[0].IsTranslated());

	EXPECT_EQ(entries[1].msgctxt, "spell");
	EXPECT_EQ(entries[1].msgid, "Multi line");
	ASSERT_EQ(entries[1].msgstr.size(), 1U);
	EXPECT_EQ(entries[1].msgstr[0], "Tab\there\n\"quoted\" \\ AB");

	EXPECT_EQ(entries[2].msgid, "{:d} Charge");
	EXPECT_EQ(entries[2].msgidPlural, "{:d} Charges");
	EXPECT_EQ(entries[2].msgstr, (std::vector<std::string> { "{:d} заряд", "{:d} заряда", "{:d} зарядов" }));

	EXPECT_EQ(entries[3].msgid, "Fuzzy");
	EXPECT_TRUE(entries[3].fuzzy);
	EXPECT_FALSE(entries[3].IsTranslated());

	EXPECT_EQ(entries[4].msgid, "No blank line before the next entry");
	EXPECT_FALSE(entries[4].fuzzy);
	EXPECT_TRUE(entries[4].references.empty());
	EXPECT_FALSE(entries[4].IsTranslated());

	EXPECT_EQ(entries[5].msgid, "Untranslated");
	EXPECT_EQ(entries[5].references, (std::vector<std::string> { "Source/d.cpp:4" }));
	EXPECT_FALSE(entries[5].IsTranslated());

	EXPECT_EQ(entries[6].msgid, "After obsolete");
	EXPECT_EQ(entries[6].references, (std::vector<std::string> { "Source/e.cpp:5" }));
	EXPECT_TRUE(entries[6].IsTranslated());

	EXPECT_EQ(entries[7].msgid, "Right after obsolete");
	EXPECT_TRUE(entries[7].references.empty());
	EXPECT_TRUE(entries[7].IsTranslated());

	const Catalog catalog(entries);
	EXPECT_NE(catalog.Find("spell", "Multi line"), nullptr);
	EXPECT_EQ(catalog.Find("", "Multi line"), nullptr);
	EXPECT_EQ(catalog.Find("", "Obsolete"), nullptr);
	EXPECT_EQ(catalog.Find("", "Obsolete without a blank line"), nullptr);
	EXPECT_EQ(catalog.Find("", "Obsolete again"), nullptr);
}

TEST(RuTextFitPoParserTest, ParsesConstraintRows)
{
	std::vector<Constraint> constraints;
	const std::optional<std::string> error = ParseConstraints(
	    "# comment\n"
	    "msgid\tmsgctxt\tfont_size\tspacing\tmax_width\tfit\tsource\r\n"
	    "Tab\\there\\\\\tspell\t12\t1\t100\tno\tSource/a.cpp:1\r\n"
	    "Gold: \t\t24\t2\t50\tyes\tSource/b.cpp:2\n"
	    "Wrapped\\nline\t\t46\t0\t80\twrap:3\tSource/c.cpp:3\n",
	    constraints);
	ASSERT_FALSE(error.has_value()) << *error;
	ASSERT_EQ(constraints.size(), 3U);
	EXPECT_EQ(constraints[0].msgid, "Tab\there\\");
	EXPECT_EQ(constraints[0].msgctxt, "spell");
	EXPECT_EQ(constraints[0].font, GameFont12);
	EXPECT_EQ(constraints[0].mode, FitMode::None);
	EXPECT_EQ(constraints[0].source, "Source/a.cpp:1");
	EXPECT_EQ(constraints[1].msgid, "Gold: ");
	EXPECT_EQ(constraints[1].font, GameFont24);
	EXPECT_EQ(constraints[1].spacing, 2);
	EXPECT_EQ(constraints[1].mode, FitMode::KerningFit);
	EXPECT_EQ(constraints[2].msgid, "Wrapped\nline");
	EXPECT_EQ(constraints[2].font, GameFont46);
	EXPECT_EQ(constraints[2].mode, FitMode::Wrap);
	EXPECT_EQ(constraints[2].maxLines, 3);
	EXPECT_EQ(constraints[2].lineNumber, 5);

	std::vector<Constraint> bad;
	EXPECT_TRUE(ParseConstraints("msgid\tmsgctxt\tfont_size\tspacing\tmax_width\tfit\tsource\nA\t\t13\t1\t10\tno\tx\n", bad).has_value());
	EXPECT_TRUE(ParseConstraints("msgid\tmsgctxt\tfont_size\tspacing\tmax_width\tfit\tsource\nA\t\t12\t1\t10\tmaybe\tx\n", bad).has_value());
	EXPECT_TRUE(ParseConstraints("A\t\t12\t1\t10\tno\tx\n", bad).has_value());
}

TEST(RuTextFitOracleTest, KerningFitMirrorsEngine)
{
	// 10 characters, 30 px too wide at spacing 2: the spacing drops by ceil(30 / 9) = 4.
	int lineWidth = 130;
	EXPECT_EQ(AdjustSpacingToFitHorizontally(lineWidth, 2, 10, 100), -2);
	EXPECT_EQ(lineWidth, 94);
	lineWidth = 100;
	EXPECT_EQ(AdjustSpacingToFitHorizontally(lineWidth, 2, 10, 100), 2);
	lineWidth = 130;
	EXPECT_EQ(AdjustSpacingToFitHorizontally(lineWidth, 2, 1, 100), 2);

	// Squeezing to spacing >= 0 works exactly when the text fits at spacing 0.
	const Constraint constraint {
		.msgid = "x", .msgctxt = "", .fontSize = 12, .font = GameFont12, .spacing = 2, .maxWidth = 0, .mode = FitMode::KerningFit, .maxLines = 1, .source = "x", .lineNumber = 0
	};
	const std::string_view text = "Character Information";
	const int widthAtZero = GetLineWidth(text, GameFont12, 0);
	ASSERT_GT(widthAtZero, 0);
	Constraint fits = constraint;
	fits.maxWidth = widthAtZero;
	EXPECT_TRUE(Measure(text, fits).fits);
	EXPECT_EQ(Measure(text, fits).spacing, 0);
	Constraint overlaps = constraint;
	overlaps.maxWidth = widthAtZero - 1;
	EXPECT_FALSE(Measure(text, overlaps).fits);
	EXPECT_EQ(Measure(text, overlaps).spacing, -1);
}

class RuTextFitTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		const Data &data = LoadData();
		ASSERT_EQ(data.error, "");
		ASSERT_TRUE(data.catalog.has_value());
		// GetLineWidth() measures the glyphs of a missing font file as '?' and only logs an error,
		// so without these files the tests would compare the wrong widths and still pass.
		std::string missing;
		for (const std::string &path : RequiredFontFiles(*data.catalog, data.constraints)) {
			if (!FindAsset(path).ok()) StrAppend(missing, " ", path);
		}
		ASSERT_EQ(missing, "") << "missing font files (built by the ru_text_fit_test_resources target)";
	}

	static void TearDownTestSuite()
	{
		UnloadFonts();
	}
};

TEST_F(RuTextFitTest, ConstraintTableIsValid)
{
	const Data &data = LoadData();
	EXPECT_GE(data.constraints.size(), 40U);
	for (const Constraint &constraint : data.constraints) {
		const PoEntry *entry = data.catalog->Find(constraint.msgctxt, constraint.msgid);
		if (entry == nullptr) {
			ADD_FAILURE() << "constraints.tsv:" << constraint.lineNumber << ": " << Quote(constraint.msgid)
			              << (constraint.msgctxt.empty() ? "" : StrCat(" (msgctxt ", Quote(constraint.msgctxt), ")"))
			              << " is not in ru.po";
		} else if (!entry->IsTranslated()) {
			ADD_FAILURE() << "constraints.tsv:" << constraint.lineNumber << ": " << Quote(constraint.msgid)
			              << " is not translated in ru.po";
		}
	}
}

TEST_F(RuTextFitTest, EnglishFits)
{
	const Data &data = LoadData();
	for (const Constraint &constraint : data.constraints) {
		const PoEntry *entry = data.catalog->Find(constraint.msgctxt, constraint.msgid);
		if (entry == nullptr) continue;
		for (const std::string_view text : EnglishTexts(*entry)) {
			const Measurement m = Measure(text, constraint);
			if (!m.fits) ADD_FAILURE() << DescribeFailure(constraint, text, m);
		}
	}
}

TEST_F(RuTextFitTest, RussianFits)
{
	const Data &data = LoadData();
	for (const Constraint &constraint : data.constraints) {
		const PoEntry *entry = data.catalog->Find(constraint.msgctxt, constraint.msgid);
		if (entry == nullptr || !entry->IsTranslated()) continue;
		for (const std::string_view text : RussianTexts(*entry)) {
			const Measurement m = Measure(text, constraint);
			if (!m.fits) ADD_FAILURE() << DescribeFailure(constraint, text, m);
		}
	}
}

bool WriteReport(const std::string &path)
{
	const Data &data = LoadData();
	if (!data.error.empty() || !data.catalog.has_value()) {
		std::cerr << data.error << std::endl;
		return false;
	}
	const std::string report = BuildReport(*data.catalog, data.constraints);
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file << report;
	file.close();
	if (!file) {
		std::cerr << "cannot write " << path << std::endl;
		return false;
	}
	std::clog << "Wrote " << path << std::endl;
	return true;
}

} // namespace
} // namespace devilution

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	for (int i = 1; i < argc; ++i) {
		const std::string_view arg = argv[i];
		if (arg.starts_with("--report=") && arg.size() > 9) {
			ReportPath = arg.substr(9);
		} else {
			std::cerr << "unknown argument: " << arg << "\nUsage: " << argv[0] << " [--report=<path>]\n";
			return 64;
		}
	}
	const int result = RUN_ALL_TESTS();
	if (!ReportPath.empty() && !devilution::WriteReport(ReportPath)) return 1;
	return result;
}
