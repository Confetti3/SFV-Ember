#include "ember/DesyncRegions.hxx"

#include <map>

namespace sfve::adapter::desync {

std::vector<DifferingRegion> DifferingRegions(const std::vector<start_state::Entry>& mine, const std::vector<std::string>& names,
	const std::vector<start_state::Entry>& theirs)
{
	const start_state::Pairing pairing = start_state::PairRegions(mine, theirs);
	std::map<uint32_t, uint64_t> mineHash, theirHash;
	for (const start_state::Entry& e : mine) mineHash[e.index] = e.hash;
	for (const start_state::Entry& e : theirs) theirHash[e.index] = e.hash;
	const auto name = [&](uint32_t index) { return index < names.size() ? names[index] : "region " + std::to_string(index); };
	std::map<uint32_t, DifferingRegion> byPlace;
	for (const start_state::Pairing::Pair& p : pairing.differing) {
		byPlace[p.local] = {name(p.local), p.size, mineHash[p.local], true, theirHash[p.reference]};
	}
	for (const uint32_t local : pairing.unpairedLocal) {
		uint64_t size = 0;
		for (const start_state::Entry& e : mine) {
			if (e.index == local) size = e.size;
		}
		byPlace[local] = {name(local), size, mineHash[local], false, 0};
	}
	std::vector<DifferingRegion> out;
	for (auto& [place, region] : byPlace) out.push_back(std::move(region));
	return out;
}

} // namespace sfve::adapter::desync
