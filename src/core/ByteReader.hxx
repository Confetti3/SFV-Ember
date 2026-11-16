#pragma once

// Reads forward through a bounded buffer of bytes, as the core's wire and
// file formats have them (the start state's datagrams, StartAgreement; the
// replay file, ReplayFile): little-endian values in order. Any read past the
// end fails and moves nothing.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <bit>
#include <type_traits>
#include <vector>

namespace sfve {

class ByteReader {
public:
	ByteReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

	// The next `n` bytes, in place.
	bool Bytes(size_t n, const uint8_t** out)
	{
		if (n > Left()) return false;
		*out = data_ + at_;
		at_ += n;
		return true;
	}
	// The next sizeof(T) bytes as a T.
	template <class T>
	bool Get(T* out)
	{
		static_assert(std::is_trivially_copyable_v<T>, "only plain values are read");
		static_assert(std::endian::native == std::endian::little, "the formats are little-endian, as this machine");
		const uint8_t* p = nullptr;
		if (!Bytes(sizeof(T), &p)) return false;
		memcpy(out, p, sizeof(T));
		return true;
	}
	// The next `n` bytes, copied.
	bool Get(std::vector<uint8_t>* out, size_t n)
	{
		const uint8_t* p = nullptr;
		if (!Bytes(n, &p)) return false;
		out->assign(p, p + n);
		return true;
	}
	size_t Left() const { return size_ - at_; }

private:
	const uint8_t* data_;
	size_t size_;
	size_t at_ = 0;
};

} // namespace sfve
