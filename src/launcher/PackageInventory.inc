// SFV Ember's package: the files a package must hold (REQUIRED), may hold
// (OPTIONAL) and no longer holds (OBSOLETE), in the Ember library's
// common/PackageInventory.inc form. The build names this file to the
// library (EMBER_PACKAGE_INVENTORY), so Updater.exe and the update check
// validate SFV Ember's packages by it; scripts/package.ps1 stages it.
// Ember's button prompts and flags (assets\input, assets\flags) are allowed
// by the library's pattern and need no line here.
SF4E_PACKAGE_REQUIRED("Launcher.exe")
SF4E_PACKAGE_REQUIRED("Updater.exe")
SF4E_PACKAGE_REQUIRED("sfv_ember.dll")
SF4E_PACKAGE_REQUIRED("sf4-net.exe")
SF4E_PACKAGE_REQUIRED("ember-discord.exe")
SF4E_PACKAGE_REQUIRED("discord_partner_sdk.dll")
SF4E_PACKAGE_REQUIRED("discord-build.json")
SF4E_PACKAGE_REQUIRED("assets\\brand\\sfv-ember-menu-background.png")
SF4E_PACKAGE_REQUIRED("assets\\input\\License.txt")
SF4E_PACKAGE_REQUIRED("assets\\flags\\LICENSE")
SF4E_PACKAGE_REQUIRED("notices\\THIRD_PARTY_LICENSES.txt")
SF4E_PACKAGE_REQUIRED("notices\\Inter-OFL.txt")
SF4E_PACKAGE_REQUIRED("notices\\NotoSansCJK-OFL.txt")
SF4E_PACKAGE_REQUIRED("notices\\Dear-ImGui-MIT.txt")
SF4E_PACKAGE_REQUIRED("notices\\flag-icons-MIT.txt")
SF4E_PACKAGE_REQUIRED("notices\\Ember-LICENSE.txt")
SF4E_PACKAGE_REQUIRED("notices\\Ember-ATTRIBUTION.md")
SF4E_PACKAGE_REQUIRED("notices\\SFV-Ember-LICENSE.txt")
SF4E_PACKAGE_REQUIRED("notices\\Discord-SDK.txt")
SF4E_PACKAGE_REQUIRED("PackageInventory.inc")
SF4E_PACKAGE_REQUIRED("START_HERE.md")
SF4E_PACKAGE_REQUIRED("MANIFEST.txt")
SF4E_PACKAGE_REQUIRED("BUILD_INFO.txt")
SF4E_PACKAGE_REQUIRED("build-provenance.json")
