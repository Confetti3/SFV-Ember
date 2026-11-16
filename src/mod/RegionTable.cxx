#include "mod/RegionTable.hxx"

#include "mod/GameMemory.hxx"
#include "mod/SfvBattleLayouts.hxx"

#include <windows.h>

#include <stdio.h>

#include <xxhash.h>

namespace sfve::mod {

namespace {

constexpr size_t kEntryBytes = 16; // {void* ptr; uint64 size}

// XXH3 of `size` bytes at `at`; false when they cannot be read (a table freed
// and its memory given back).
bool TryHash(uintptr_t at, size_t size, uint64_t* out)
{
	__try {
		*out = XXH3_64bits(reinterpret_cast<const void*>(at), size);
		return true;
	} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
		return false;
	}
}

// Where the entries of `manager`'s table lie now; false when that cannot be
// read, or reads as no table.
bool ReadBounds(const uint8_t* manager, uintptr_t* begin, size_t* entries)
{
	uintptr_t end = 0;
	if (!TryRead(manager + sfv::manager::kRegionsBegin, begin) || !TryRead(manager + sfv::manager::kRegionsEnd, &end) ||
		end < *begin || (end - *begin) % kEntryBytes != 0) {
		return false;
	}
	*entries = (end - *begin) / kEntryBytes;
	return true;
}

} // namespace

bool RegionTable::Bind(const uint8_t* manager)
{
	Forget();
	uintptr_t begin = 0;
	size_t entries = 0;
	uint64_t hash = 0;
	if (!manager || !ReadBounds(manager, &begin, &entries) || !entries || !TryHash(begin, entries * kEntryBytes, &hash)) {
		return false;
	}
	manager_ = manager;
	entries_ = entries;
	hash_ = hash;
	return true;
}

const char* RegionTable::Changed() const
{
	if (!manager_) return nullptr;
	if (changed_[0]) return changed_;
	uintptr_t begin = 0;
	size_t entries = 0;
	if (!ReadBounds(manager_, &begin, &entries)) {
		snprintf(changed_, sizeof(changed_), "the game's rollback region table can no longer be read (%zu entries at the bind)",
			entries_);
		return changed_;
	}
	// Cleared, to be built again: the regions themselves are the units', which
	// the table only lists, so nothing the layout copies has changed yet.
	if (entries == 0) return nullptr;
	uint64_t hash = 0;
	if (entries == entries_ && TryHash(begin, entries * kEntryBytes, &hash) && hash == hash_) return nullptr;
	if (entries != entries_) {
		snprintf(changed_, sizeof(changed_), "the game's rollback region table lists %zu regions, %zu at the bind", entries,
			entries_);
	} else {
		snprintf(changed_, sizeof(changed_), "the game's rollback region table no longer lists the %zu regions it did at the bind",
			entries_);
	}
	return changed_;
}

} // namespace sfve::mod
