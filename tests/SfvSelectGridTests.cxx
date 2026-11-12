// The fighter board's grid table (SfvSelectGrid), checked without the game.
// Step 0 holds the frozen interface; stream C adds the parse, lookup and
// roster cases.

#include <doctest/doctest.h>

#include "ember/SfvSelectGrid.hxx"

using namespace sfve::grid;

TEST_CASE("an empty select grid has no cells")
{
	const SelectGrid grid;
	CHECK(grid.columns == 0);
	CHECK(!CellOf(grid, "RYU").has_value());
	CHECK(RowCount(grid) == 0);
	CHECK(RowOffset(grid, 0) == 0.f);
}
