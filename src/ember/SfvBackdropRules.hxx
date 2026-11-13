#pragma once

// The rules of the menu's artwork backdrop (docs/design/beta8/backdrop.md),
// with no game, no Direct3D and no ImGui, so the tests check all of them:
// - the art set: data/menu-art.json, embedded, which lists the game's own
//   full-screen illustrations by family (the gallery's special and ending
//   pictures on; its MODE03 set and the character stories' off until the
//   laptop's contact sheets are seen), each path the object's path under
//   /Game/ without its extension;
// - the cycle: a shuffled order with no picture twice in a row, a picture
//   held for a while and then cross-faded into the next, at most two
//   pictures alive (the one shown and the next), one that does not load
//   skipped, and the clock running only while the backdrop is on screen;
// - the scrim each page gets: light and left-heavy behind a narrow column
//   (Home), heavier and even behind a dense page;
// - the cover fit of a picture to the screen.
// SfvBackdrop runs them: it loads through the game (mod/GameArt) and draws
// behind the shell's pages (ui::SetMenuBackdropDrawer).

#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::backdrop {

// --- The art set (data/menu-art.json) ---

struct Family {
	std::string name;
	bool use = false;              // its pictures are in the cycle; all are in the development build's load report
	std::vector<std::string> paths; // under /Game/, without extension: "GameMode/Gallery/Illust/special/end_sf1_spl_001"
};
struct ArtSet {
	std::vector<Family> families;
};

// A path as the set lists it: two or more names of letters, digits and
// underscores, joined by '/', at most 192 characters; no "/Game/" before it,
// no extension, no "." or ".." anywhere.
bool ArtPathValid(std::string_view path);
// The object's path the game loads it by: "/Game/<path>.<last name>"; empty
// for a path that is not valid.
std::wstring ObjectPathOf(std::string_view path);

// {"version": 1, "families": [{"name", "use", "note"?, "paths"}]}: a name
// once, every path valid and listed once in the whole set. False (the set as
// it was, and why in `error`) for anything else.
bool ParseArtSet(std::string_view json, ArtSet& set, std::string& error);
// The embedded data/menu-art.json, parsed once; empty if it does not parse
// (the backdrop then stays as plain as before).
const ArtSet& EmbeddedArtSet();
// The paths of the families in use, in the file's order.
std::vector<std::string> UsedPaths(const ArtSet& set);
// Every path of every family, in the file's order, with its family's name.
struct ListedPath {
	std::string family, path;
	bool used = false;
};
std::vector<ListedPath> AllPaths(const ArtSet& set);

// --- The page's scrim ---

// The kind of page the backdrop is behind: a narrow column of rows with the
// screen open beside it, or a page that fills the screen (boards, galleries,
// tables, settings), which is every page not named a column.
enum class PageKind { Column, Dense };
// By the shell's screen ("home", "online", "training" are columns).
PageKind PageKindOf(std::string_view screen);
const char* PageKindName(PageKind kind);

// The theme's ink over the picture, 0 to 1 at each corner.
struct Scrim {
	float topLeft = 1, topRight = 1, bottomRight = 1, bottomLeft = 1;
	bool operator==(const Scrim&) const = default;
};
Scrim ScrimOf(PageKind kind);
// From `from` to `to` by `t` (0 to 1, clamped), each corner alike.
Scrim Mix(const Scrim& from, const Scrim& to, float t);
// The scrim at a point of the screen (`x` and `y` from 0 at the left and top
// to 1), between its corners as the drawn gradient blends them.
float ScrimAt(const Scrim& scrim, float x, float y);

// The pictures are drawn through a warm grey (a multiply), so a cold
// picture sits on the theme's gold-on-ink ramp with the rest of the page.
struct Tint {
	unsigned char r, g, b;
};
inline constexpr Tint kArtTint{240, 232, 216};
// How long a change of page kind takes the scrim from one to the other.
inline constexpr double kScrimSeconds = 0.35;

// --- Cover fit ---

// The part of a `width` by `height` picture (texture coordinates, 0 to 1)
// that fills a `targetWidth` by `targetHeight` rect at the picture's own
// shape: all of the picture's height and its centre's width on a narrower
// screen, all of its width on a wider one, the cut placed by `anchorX` and
// `anchorY` (0 keeps the left or top edge, 1 the right or bottom). The whole
// picture for a size that is not positive.
struct UvRect {
	float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};
// Faces sit high in the game's illustrations: a wide screen's cut keeps a
// little more of the top than of the bottom.
inline constexpr float kAnchorX = 0.5f, kAnchorY = 0.4f;
UvRect CoverFit(int width, int height, float targetWidth, float targetHeight, float anchorX = kAnchorX,
	float anchorY = kAnchorY);

// --- The cycle ---

struct Timing {
	double hold = 25;   // a picture's time on screen, once faded in
	double fade = 1.2;  // the cross-fade
	double retry = 1.0; // after a picture that did not load, before the next is tried
	double settle = 2.0; // after a fade, before the next picture is loaded (the load stalls the game a moment)
};
// The most a frame may move the cycle's clock: a long frame (a hitch, the
// window away) never skips a fade.
inline constexpr double kMostStep = 0.25;
// The fade's curve, 0 to 1: smoothstep.
float FadeCurve(float progress);

// Which picture shows, which loads next and when they change. Entries are
// the indexes of the paths it was made with. Not thread-safe: SfvBackdrop
// holds it under its lock.
class Cycle {
public:
	Cycle() = default;
	Cycle(int entries, std::uint32_t seed, Timing timing = {});

	// The entry the loader should load now; none while it loads one, while
	// two are alive, during a fade or its settling time, or after a failure
	// until the retry time has passed.
	std::optional<int> Wanted() const;
	// The loader took the wanted entry.
	void Started(int entry);
	// The loader's answer for `entry`: loaded, or not (skipped for the rest
	// of the session). True when the cycle keeps the picture; false when it
	// does not want it any more (Reset since), and the loader lets it go.
	bool Loaded(int entry, bool ok);
	// The backdrop was on screen for `seconds` (clamped to kMostStep).
	void Advance(double seconds);
	// Fade to the next picture as soon as it has loaded, whatever the hold.
	void Step();
	// Everything let go (the setting turned off): what was alive goes to
	// TakeReleases, an entry still loading is refused when it arrives, and
	// the order goes on where it was.
	void Reset();

	// What to draw: the picture under (alpha 1, or none) and the one fading
	// in over it (alpha 0 to 1, or none).
	struct Layer {
		int entry = -1;
		float alpha = 0;
	};
	struct Frame {
		Layer under, over;
		bool Empty() const { return under.entry < 0 && over.entry < 0; }
	};
	Frame Shown() const;
	// Entries whose pictures to let go now, once each.
	std::vector<int> TakeReleases();

	// The entries loading or loaded, a refused one not yet arrived included:
	// never more than two.
	int Alive() const;
	// Every entry failed (or there are none): nothing will ever show.
	bool Exhausted() const;
	int Current() const { return current_.ready ? current_.entry : -1; }
	int Next() const { return next_.entry; }
	bool NextReady() const { return next_.ready; }
	bool Fading() const { return fading_; }
	double Held() const { return held_; }
	int Failed() const { return failedCount_; }
	int Entries() const { return entries_; }

private:
	struct Slot {
		int entry = -1;
		bool loading = false, ready = false;
	};
	int Pick();
	// A next picture chosen when there is none, outside a fade.
	void Refill();
	void StartFade();

	int entries_ = 0;
	Timing timing_;
	std::mt19937 random_;
	std::vector<int> order_;
	size_t cursor_ = 0;
	int lastPicked_ = -1;
	std::vector<bool> failed_;
	int failedCount_ = 0;
	Slot current_, next_;
	int stale_ = -1; // loading when Reset came: refused when it arrives, and alive until then
	bool fading_ = false, stepWanted_ = false;
	double clock_ = 0, faded_ = 0, held_ = 0, settleUntil_ = 0, retryAt_ = 0;
	std::vector<int> releases_;
};

} // namespace sfve::backdrop
