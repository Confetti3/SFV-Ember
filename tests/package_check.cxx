// sfve_package_check.exe: SFV Ember's package validation, as Updater.exe and
// the update check run it, for scripts. The library's PackageInstallerTest
// has the same two read-only modes for SF4 Ember's packages, which
// scripts/test-updater-download-validation.ps1 (the library's) drives; this
// one validates by SFV Ember's inventory (src/launcher/PackageInventory.inc).
//
//   sfve_package_check --validate <package folder>
//   sfve_package_check --extract <zip> <folder>

#include "launcher/update/PackageInstaller.hxx"
#include "launcher/update/ZipExtract.hxx"

#include <atomic>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv)
{
	std::string error;
	if (argc == 3 && std::wstring(argv[1]) == L"--validate") {
		if (sf4e::launcher::ValidatePackageFolder(argv[2], error)) return 0;
		std::cerr << error << '\n';
		return 2;
	}
	if (argc == 4 && std::wstring(argv[1]) == L"--extract") {
		const std::atomic<bool> keep{false};
		if (sf4e::launcher::ExtractZip(argv[2], argv[3], error, keep)) return 0;
		std::cerr << error << '\n';
		return 2;
	}
	std::cerr << "usage: sfve_package_check --validate <folder> | --extract <zip> <folder>\n";
	return 1;
}
