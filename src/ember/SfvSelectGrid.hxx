#pragma once

// The fighter board's layout: which cell of SFV's own select grid each fighter
// stands in, as the hand-made table data/select-grid.json holds it (read from
// the game's own select screen; embedded in the mod like the pick space). The
// board (SfvSelectionScreen) places its native tiles from this table and
// nothing else, so a re-cut table changes the board without code.
//
// Pure: no game code and no ImGui, so the tests check the parse and the
// lookups. Cells are whole numbers on a grid of `columns` columns; a row may
// be shifted sideways by an offset in cell widths, which is how SFV's slanted
// rows stagger. Fighters are named by their code (picks::Fighter::code), the
// same strings as data/pick-space.json.
//
// data/select-grid.json:
//   {
//     "columns": 16,                      // cells per row, 1 or more
//     "rowOffsets": [0, 0, 0],            // optional; one number per row, in cell
//                                         // widths (fractions allowed); a row
//                                         // past the list has offset 0
//     "fighters": [ {"code": "RYU", "column": 0, "row": 0}, ... ],
//     "random":   {"column": 13, "row": 2},  // Random's cell, optional
//     "eleven":   {"column": 23, "row": 1}   // the game's ELEVEN's cell, optional
//   }
// One cell per code, and no two entries (fighters, Random or ELEVEN) in one cell; a
// document that breaks either rule, names a column outside 0..columns-1 or a
// negative row, or is not this shape does not parse.
//
// A catalog fighter the table lacks is appended to the grid (FitToRoster) and
// logged by the caller, so a new fighter is on the board before the table is
// re-cut.
//
// Stream C fills SfvSelectGrid.cxx. This header is final.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sfve::grid {

// A cell of the grid: the column from the left, the row from the top.
struct Cell {
	int column = 0;
	int row = 0;
	bool operator==(const Cell&) const = default;
};

struct Entry {
	std::string code; // "RYU"
	Cell cell;
};

struct SelectGrid {
	int columns = 0;                 // 0: no grid (nothing parsed)
	std::vector<float> rowOffsets;   // per row, in cell widths; empty is all 0
	std::vector<Entry> fighters;     // in the table's order
	std::optional<Cell> random;      // Random's cell, if the table gives one
	std::optional<Cell> eleven;      // ELEVEN's cell, if the table gives one (the board's Eleven card)
};

// What CodeAt answers for Random's cell. Not a fighter code (those are three
// characters).
inline constexpr std::string_view kRandomCellCode = "RANDOM";
// And for ELEVEN's cell: not a fighter code either, and not Random's.
inline constexpr std::string_view kElevenCellCode = "ELEVEN";

// Parses the document above into `grid`. False, with the reason in `error`
// and `grid` left as it was, for text that is not that document or breaks one
// of its rules.
bool ParseSelectGrid(std::string_view json, SelectGrid& grid, std::string& error);

// The table embedded from data/select-grid.json (SFVE's CMake embeds it as
// bytes): parsed once, on first use, by any thread. An empty grid (columns 0)
// if the embedded text does not parse.
const SelectGrid& EmbeddedSelectGrid();

// The fighter's cell; none for a code the table does not have.
std::optional<Cell> CellOf(const SelectGrid& grid, std::string_view code);
// The other way: the code of the fighter in `cell`, kRandomCellCode for
// Random's cell, kElevenCellCode for ELEVEN's; none for an empty cell.
std::optional<std::string> CodeAt(const SelectGrid& grid, Cell cell);
// The grid's rows: one past the highest row any entry stands in; 0 for none.
int RowCount(const SelectGrid& grid);
// The row's sideways shift in cell widths; 0 for a row without one.
float RowOffset(const SelectGrid& grid, int row);

// Brings the table to the roster: every code of `roster` (the catalog's
// fighters, in the catalog's order) the grid lacks is added in the first free
// cells after the last occupied one in row order (left to right, then the
// next row), Random's and ELEVEN's cells left where they are. Entries for codes the roster
// lacks stay in the table and are simply never looked up. Returns the codes
// it added, in the order added, for the log; empty when none, and for a grid
// with no columns (which cannot hold them).
std::vector<std::string> FitToRoster(SelectGrid& grid, const std::vector<std::string>& roster);

} // namespace sfve::grid
