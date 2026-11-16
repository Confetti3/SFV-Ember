#pragma once

// The paks this copy's game mounts, judged against the stock set
// (docs/design/beta10/desync.md, "Game data"). Pure: PakList finds the files.
//
// The game's data (its move data, its balance version lists, its looks)
// comes from .pak files, and the engine mounts every .pak it finds, in any
// subfolder, of three folders (IDA, build 13173604: FPakPlatformFile's
// Initialize, rva 0x234FF50, takes the folder list of rva 0x234F9D0 and
// mounts each file a recursive visit finds): the game's Content/Paks, the
// engine's Content/Paks, and the game's Saved/Paks (the user folder's, as
// the game keeps its saves: %LOCALAPPDATA%\StreetFighterV\Saved). A pak that
// is not one of Steam's (a mod, an altered or stale file) can replace any
// asset, so a copy with one may compute a match with other data than its
// opponent's. The stock set is Steam's own: the build's depot manifest,
// checked equal to an untouched install, names only Content/Paks files.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace sfve::mod::paks {

enum class Folder : uint8_t {
	Content, // <game>/StreetFighterV/Content/Paks
	Engine,  // <game>/Engine/Content/Paks
	Saved,   // the game's Saved/Paks
};

struct Found {
	Folder folder = Folder::Content;
	std::string path; // below the folder's Paks, '/' between folders
	uint64_t size = 0;
};

struct StockPak {
	const char* name;
	uint64_t size;
};
extern const StockPak kStock[];
extern const size_t kStockCount;

enum class Verdict : uint8_t {
	Stock,     // a stock name, its stock size, in Content/Paks itself
	OtherSize, // a stock name with another size: an altered or stale file
	NotStock,  // anything else: another name, a subfolder, another folder
};
struct Judged {
	Found pak;
	Verdict verdict = Verdict::NotStock;
};
struct Census {
	std::vector<Judged> paks;          // as found
	std::vector<std::string> missing;  // stock paks not found
};

Census Judge(const std::vector<Found>& found);
bool AllStock(const Census& census);
// One line for the log's start and every report: "69 of 69 stock paks", then
// what is not stock, what has another size and what is missing.
std::string Summary(const Census& census);
// One pak's line: "Content/pakchunk0-WindowsNoEditor.pak, 6336284478 bytes: stock".
std::string Line(const Judged& judged);

} // namespace sfve::mod::paks
