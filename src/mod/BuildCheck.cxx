#include "mod/BuildCheck.hxx"

#include <stdio.h>
#include <string.h>

namespace sfve::mod {

namespace {

const IMAGE_NT_HEADERS* NtHeaders(HMODULE image)
{
	const auto* base = reinterpret_cast<const uint8_t*>(image);
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (!image || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

} // namespace

bool ImageTimestamp(HMODULE image, uint32_t* out)
{
	const IMAGE_NT_HEADERS* nt = NtHeaders(image);
	if (!nt) return false;
	*out = nt->FileHeader.TimeDateStamp;
	return true;
}

BuildState CheckBuild(HMODULE exe, const GameBuild* build, std::string* detail)
{
	char text[256];
	uint32_t stamp = 0;
	if (!ImageTimestamp(exe, &stamp)) {
		*detail = "the exe has no readable PE header";
		return BuildState::Mismatch;
	}
	if (!build || build->siteCount == 0) {
		snprintf(text, sizeof(text), "exe timestamp 0x%08X; no verified build table yet", stamp);
		*detail = text;
		return BuildState::NoBuildTable;
	}
	if (stamp != build->peTimestamp) {
		snprintf(text, sizeof(text), "exe timestamp 0x%08X, offsets are for %s (0x%08X)", stamp, build->label,
			build->peTimestamp);
		*detail = text;
		return BuildState::Mismatch;
	}
	const IMAGE_NT_HEADERS* nt = NtHeaders(exe);
	const auto* base = reinterpret_cast<const uint8_t*>(exe);
	for (size_t i = 0; i < build->siteCount; i++) {
		const CodeSite& site = build->sites[i];
		if (site.rva + sizeof(site.bytes) > nt->OptionalHeader.SizeOfImage ||
			memcmp(base + site.rva, site.bytes, sizeof(site.bytes)) != 0) {
			snprintf(text, sizeof(text), "%s at rva 0x%X does not match", site.name, site.rva);
			*detail = text;
			return BuildState::Mismatch;
		}
	}
	snprintf(text, sizeof(text), "%s, %zu sites verified", build->label, build->siteCount);
	*detail = text;
	return BuildState::Verified;
}

const char* BuildStateName(BuildState state)
{
	switch (state) {
	case BuildState::NoBuildTable: return "no build table";
	case BuildState::Mismatch: return "mismatch";
	case BuildState::Verified: return "verified";
	}
	return "?";
}

} // namespace sfve::mod
