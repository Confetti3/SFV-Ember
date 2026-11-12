// The fighter board's grid table (SfvSelectGrid), checked without the game:
// the table this checkout ships places every fighter of the pick space once,
// in a cell of its own, in the shape of SFV's own select screen; a fighter
// the table lacks is appended after the last cell taken; and a table that
// breaks a rule does not parse.

#include <doctest/doctest.h>

#include "ember/SfvPickSpace.hxx"
#include "ember/SfvSelectGrid.hxx"

#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace sfve::grid;

namespace {

std::string ReadFile(const char* path)
{
	std::ifstream file(path, std::ios::binary);
	REQUIRE(file);
	return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

SelectGrid Parsed(const std::string& json)
{
	SelectGrid grid;
	std::string error;
	INFO(json);
	REQUIRE(ParseSelectGrid(json, grid, error));
	CHECK(error.empty());
	return grid;
}

// The parse's refusal of `json`, which leaves the grid as it was.
std::string Refusal(const std::string& json)
{
	SelectGrid grid;
	grid.columns = 7;
	std::string error;
	INFO(json);
	CHECK_FALSE(ParseSelectGrid(json, grid, error));
	CHECK(grid.columns == 7);
	CHECK(grid.fighters.empty());
	CHECK_FALSE(error.empty());
	return error;
}

} // namespace

TEST_CASE("an empty select grid has no cells")
{
	const SelectGrid grid;
	CHECK(grid.columns == 0);
	CHECK(!CellOf(grid, "RYU").has_value());
	CHECK(RowCount(grid) == 0);
	CHECK(RowOffset(grid, 0) == 0.f);
	CHECK(!CodeAt(grid, {0, 0}).has_value());
}

TEST_CASE("the shipped table places every fighter of the pick space once, each in a cell of its own, and Random")
{
	const SelectGrid grid = Parsed(ReadFile(SFVE_SELECT_GRID_PATH));
	sfve::picks::PickSpace space;
	std::string build, error;
	REQUIRE(sfve::picks::FromJson(ReadFile(SFVE_PICK_SPACE_PATH), space, build, error));
	REQUIRE(!space.fighters.empty());
	std::set<std::string> codes;
	std::set<std::pair<int, int>> cells;
	for (const Entry& entry : grid.fighters) {
		CAPTURE(entry.code);
		CHECK(codes.insert(entry.code).second);
		CHECK(cells.insert({entry.cell.column, entry.cell.row}).second);
		CHECK(entry.cell.column >= 0);
		CHECK(entry.cell.column < grid.columns);
	}
	REQUIRE(grid.random.has_value());
	CHECK(cells.insert({grid.random->column, grid.random->row}).second);
	CHECK(CodeAt(grid, *grid.random) == std::string(kRandomCellCode));
	for (const sfve::picks::Fighter& fighter : space.fighters) {
		CAPTURE(fighter.code);
		REQUIRE(CellOf(grid, fighter.code).has_value());
		CHECK(CodeAt(grid, *CellOf(grid, fighter.code)) == fighter.code);
	}
	// Nothing to append for the pick space's own roster.
	std::vector<std::string> roster;
	for (const sfve::picks::Fighter& fighter : space.fighters) roster.push_back(fighter.code);
	SelectGrid fitted = grid;
	CHECK(FitToRoster(fitted, roster).empty());
	CHECK(fitted.fighters.size() == grid.fighters.size());
	// The mod embeds the same table.
	const SelectGrid& embedded = EmbeddedSelectGrid();
	CHECK(embedded.columns == grid.columns);
	REQUIRE(embedded.fighters.size() == grid.fighters.size());
	for (size_t i = 0; i < grid.fighters.size(); i++) {
		CHECK(embedded.fighters[i].code == grid.fighters[i].code);
		CHECK(embedded.fighters[i].cell == grid.fighters[i].cell);
	}
	CHECK(embedded.random == grid.random);
}

TEST_CASE("a table's cells, rows and row offsets read back as written")
{
	const SelectGrid grid = Parsed(R"({"columns": 4, "rowOffsets": [0, 0.5],
		"fighters": [{"code": "RYU", "column": 0, "row": 0}, {"code": "KEN", "column": 3, "row": 0},
			{"code": "CNL", "column": 1, "row": 2}],
		"random": {"column": 2, "row": 1}})");
	CHECK(grid.columns == 4);
	CHECK(CellOf(grid, "KEN") == Cell{3, 0});
	CHECK(CellOf(grid, "CNL") == Cell{1, 2});
	CHECK(!CellOf(grid, "GUL").has_value());
	CHECK(CodeAt(grid, {0, 0}) == std::string("RYU"));
	CHECK(CodeAt(grid, {2, 1}) == std::string(kRandomCellCode));
	CHECK(!CodeAt(grid, {1, 0}).has_value());
	CHECK(RowCount(grid) == 3);
	CHECK(RowOffset(grid, 0) == 0.f);
	CHECK(RowOffset(grid, 1) == 0.5f);
	CHECK(RowOffset(grid, 2) == 0.f); // past the list
	CHECK(RowOffset(grid, -1) == 0.f);
	// Random alone makes rows too; no offsets and no Random are fine.
	CHECK(RowCount(Parsed(R"({"columns": 2, "fighters": [], "random": {"column": 0, "row": 4}})")) == 5);
	const SelectGrid bare = Parsed(R"({"columns": 1, "fighters": [{"code": "RYU", "column": 0, "row": 0}]})");
	CHECK(!bare.random.has_value());
	CHECK(bare.rowOffsets.empty());
}

TEST_CASE("a catalog fighter the table lacks is appended after the last cell taken, Random left in place")
{
	SelectGrid grid = Parsed(R"({"columns": 3,
		"fighters": [{"code": "RYU", "column": 0, "row": 0}, {"code": "OLD", "column": 1, "row": 0}],
		"random": {"column": 2, "row": 0}})");
	const std::vector<std::string> added = FitToRoster(grid, {"RYU", "KEN", "CNL", "GUL", "ZGF"});
	CHECK(added == std::vector<std::string>{"KEN", "CNL", "GUL", "ZGF"});
	CHECK(CellOf(grid, "KEN") == Cell{0, 1});
	CHECK(CellOf(grid, "CNL") == Cell{1, 1});
	CHECK(CellOf(grid, "GUL") == Cell{2, 1});
	CHECK(CellOf(grid, "ZGF") == Cell{0, 2});
	CHECK(grid.random == Cell{2, 0});
	// A code the roster lacks stays, never looked up; the next fit adds nothing.
	CHECK(CellOf(grid, "OLD") == Cell{1, 0});
	CHECK(FitToRoster(grid, {"RYU", "KEN", "CNL", "GUL", "ZGF"}).empty());
	CHECK(RowCount(grid) == 3);
	// Random as the last cell taken: the first newcomer goes after it.
	SelectGrid late = Parsed(R"({"columns": 2, "fighters": [{"code": "RYU", "column": 0, "row": 0}],
		"random": {"column": 1, "row": 1}})");
	CHECK(FitToRoster(late, {"RYU", "KEN"}) == std::vector<std::string>{"KEN"});
	CHECK(CellOf(late, "KEN") == Cell{0, 2});
	// An empty table holds them from its first cell; a grid with no columns none.
	SelectGrid empty = Parsed(R"({"columns": 2, "fighters": []})");
	CHECK(FitToRoster(empty, {"RYU", "KEN", "CNL"}).size() == 3);
	CHECK(CellOf(empty, "RYU") == Cell{0, 0});
	CHECK(CellOf(empty, "CNL") == Cell{0, 1});
	SelectGrid none;
	CHECK(FitToRoster(none, {"RYU"}).empty());
	CHECK(none.fighters.empty());
}

TEST_CASE("a table that breaks a rule does not parse, and the grid stays as it was")
{
	Refusal("");
	Refusal("not json");
	Refusal("[]");
	Refusal(R"({"fighters": []})");                       // no columns
	Refusal(R"({"columns": 2})");                          // no fighters
	Refusal(R"({"columns": 0, "fighters": []})");          // no column
	Refusal(R"({"columns": -1, "fighters": []})");
	Refusal(R"({"columns": 1.5, "fighters": []})");
	Refusal(R"({"columns": "16", "fighters": []})");
	Refusal(R"({"columns": 2, "fighters": {}})");
	Refusal(R"({"columns": 2, "fighters": [], "colums": 2})"); // a misspelt key
	Refusal(R"({"columns": 2, "fighters": [], "rowOffsets": [0, "a"]})");
	Refusal(R"({"columns": 2, "fighters": [], "rowOffsets": 1})");
	// A fighter: a code and a cell of the grid, nothing else.
	Refusal(R"({"columns": 2, "fighters": [{"code": "RYU", "column": 2, "row": 0}]})");
	Refusal(R"({"columns": 2, "fighters": [{"code": "RYU", "column": 0, "row": -1}]})");
	Refusal(R"({"columns": 2, "fighters": [{"code": "RYU", "column": 0}]})");
	Refusal(R"({"columns": 2, "fighters": [{"code": "", "column": 0, "row": 0}]})");
	Refusal(R"({"columns": 2, "fighters": [{"code": 7, "column": 0, "row": 0}]})");
	Refusal(R"({"columns": 2, "fighters": [{"code": "RYU", "column": 0, "row": 0, "costume": 1}]})");
	// One cell per code, one entry per cell, Random's among them.
	CHECK(Refusal(R"({"columns": 2, "fighters": [{"code": "RYU", "column": 0, "row": 0},
		{"code": "RYU", "column": 1, "row": 0}]})") == "RYU has two cells");
	Refusal(R"({"columns": 2, "fighters": [{"code": "RYU", "column": 0, "row": 0}, {"code": "KEN", "column": 0, "row": 0}]})");
	Refusal(R"({"columns": 2, "fighters": [{"code": "RYU", "column": 1, "row": 0}], "random": {"column": 1, "row": 0}})");
	Refusal(R"({"columns": 2, "fighters": [], "random": {"column": 2, "row": 0}})");
	Refusal(R"({"columns": 2, "fighters": [], "random": {"code": "RND", "column": 0, "row": 0}})");
}

TEST_CASE("the shipped table has the shape of the game's own select screen: two rows split by Random")
{
	const SelectGrid grid = Parsed(ReadFile(SFVE_SELECT_GRID_PATH));
	REQUIRE(grid.random.has_value());
	const Cell random = *grid.random;
	CHECK(random == Cell{11, 0});
	CHECK(RowCount(grid) == 2);
	// The second row sits about half a tile left of the first.
	CHECK(RowOffset(grid, 0) == 0.f);
	CHECK(RowOffset(grid, 1) < -.4f);
	CHECK(RowOffset(grid, 1) > -.7f);
	// Each side of Random: 11 on the first row, 12 and 11 on the second
	// (the game's ELEVEN, last on the right, is not a fighter of ours: its
	// cell stays empty).
	int counts[2][2] = {};
	for (const Entry& entry : grid.fighters) {
		CAPTURE(entry.code);
		REQUIRE(entry.cell.row >= 0);
		REQUIRE(entry.cell.row < 2);
		counts[entry.cell.row][entry.cell.column > random.column ? 1 : 0]++;
	}
	CHECK(counts[0][0] == 11);
	CHECK(counts[0][1] == 11);
	CHECK(counts[1][0] == 12);
	CHECK(counts[1][1] == 11);
	CHECK(!CodeAt(grid, {23, 1}).has_value());
	// The tiles beside Random and at the ends, as the game shows them.
	CHECK(CellOf(grid, "RYU") == Cell{10, 0});
	CHECK(CellOf(grid, "KEN") == Cell{12, 0});
	CHECK(CellOf(grid, "NSH") == Cell{11, 1});
	CHECK(CellOf(grid, "VEG") == Cell{12, 1});
	CHECK(CellOf(grid, "Z39") == Cell{0, 0});
	CHECK(CellOf(grid, "Z41") == Cell{22, 0});
	CHECK(CellOf(grid, "Z42") == Cell{0, 1});
	CHECK(CellOf(grid, "Z40") == Cell{22, 1});
	// Every first-row column but Random's is taken; the second row is whole
	// up to the last fighter's.
	for (int column = 0; column <= 22; column++) {
		CAPTURE(column);
		CHECK(CodeAt(grid, {column, 0}).has_value());
		CHECK(CodeAt(grid, {column, 1}).has_value());
	}
}
