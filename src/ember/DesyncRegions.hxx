#pragma once

// A mismatch's differing regions from the two copies' region lists
// (core/RegionExchange.hxx), for the report (DesyncReport.hxx's
// DifferingRegion). Pure; apart from DesyncReport.cxx, which the launcher
// builds too without the rollback core.

#include "core/StartAgreement.hxx"
#include "ember/DesyncReport.hxx"

#include <string>
#include <vector>

namespace sfve::adapter::desync {

// The regions of `mine` (this copy's list, its regions' names by their
// place in its layout, `names`) that differ from `theirs` (the other copy's
// list), paired as the start agreement pairs them (start_state::
// PairRegions): by stable identity, else by hash, else by a size only one
// region has; one that pairs with none of the other's is listed unpaired.
// In this copy's layout order.
std::vector<DifferingRegion> DifferingRegions(const std::vector<start_state::Entry>& mine, const std::vector<std::string>& names,
	const std::vector<start_state::Entry>& theirs);

} // namespace sfve::adapter::desync
