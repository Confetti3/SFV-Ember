#include "launcher/ExecutableBuild.hxx"

#include <windows.h>

namespace sfve::launcher {

namespace {

bool ReadAt(HANDLE file, LONG offset, void* out, DWORD size)
{
	DWORD read = 0;
	return SetFilePointer(file, offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
		ReadFile(file, out, size, &read, nullptr) && read == size;
}

} // namespace

std::optional<uint32_t> ExecutableTimestamp(const std::wstring& path)
{
	const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return std::nullopt;
	IMAGE_DOS_HEADER dos{};
	DWORD signature = 0;
	IMAGE_FILE_HEADER header{};
	const bool read = ReadAt(file, 0, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
		ReadAt(file, dos.e_lfanew, &signature, sizeof(signature)) && signature == IMAGE_NT_SIGNATURE &&
		ReadAt(file, dos.e_lfanew + (LONG)sizeof(signature), &header, sizeof(header));
	CloseHandle(file);
	if (!read) return std::nullopt;
	return header.TimeDateStamp;
}

} // namespace sfve::launcher
