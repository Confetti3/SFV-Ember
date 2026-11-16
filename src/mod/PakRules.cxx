#include "mod/PakRules.hxx"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <iterator>
#include <map>

namespace sfve::mod::paks {

// Steam's depot 310951 for build 13173604 (manifest 4281346506506612445, the
// install's appmanifest_310950.acf), its Content/Paks entries by name and size,
// each checked equal to the files of an untouched install. The two DLC depots
// the install also holds (1172540, 1474240) bring wallpapers only.
const StockPak kStock[] = {
	{"pakchunk0-WindowsNoEditor.pak", 6336284478},
	{"pakchunk1-WindowsNoEditor.pak", 106124559},
	{"pakchunk2-WindowsNoEditor.pak", 122905868},
	{"pakchunk3-WindowsNoEditor.pak", 49446354},
	{"pakchunk4-WindowsNoEditor.pak", 83879691},
	{"pakchunk5-WindowsNoEditor.pak", 62172111},
	{"pakchunk6-WindowsNoEditor.pak", 120760672},
	{"pakchunk7-WindowsNoEditor.pak", 89341913},
	{"pakchunk8-WindowsNoEditor.pak", 121340303},
	{"pakchunk9-WindowsNoEditor.pak", 76898710},
	{"pakchunk10-WindowsNoEditor.pak", 87299199},
	{"pakchunk11-WindowsNoEditor.pak", 90148747},
	{"pakchunk12-WindowsNoEditor.pak", 123775818},
	{"pakchunk13-WindowsNoEditor.pak", 73861262},
	{"pakchunk14-WindowsNoEditor.pak", 105796292},
	{"pakchunk15-WindowsNoEditor.pak", 112675873},
	{"pakchunk16-WindowsNoEditor.pak", 71595162},
	{"pakchunk17-WindowsNoEditor.pak", 81475779},
	{"pakchunk18-WindowsNoEditor.pak", 113103548},
	{"pakchunk19-WindowsNoEditor.pak", 114702232},
	{"pakchunk20-WindowsNoEditor.pak", 94676409},
	{"pakchunk21-WindowsNoEditor.pak", 408820353},
	{"pakchunk22-WindowsNoEditor.pak", 453641994},
	{"pakchunk23-WindowsNoEditor.pak", 308421160},
	{"pakchunk24-WindowsNoEditor.pak", 301938983},
	{"pakchunk25-WindowsNoEditor.pak", 687347030},
	{"pakchunk26-WindowsNoEditor.pak", 946191320},
	{"pakchunk27-WindowsNoEditor.pak", 234009085},
	{"pakchunk28-WindowsNoEditor.pak", 229644395},
	{"pakchunk29-WindowsNoEditor.pak", 532199527},
	{"pakchunk30-WindowsNoEditor.pak", 608256553},
	{"pakchunk31-WindowsNoEditor.pak", 334351473},
	{"pakchunk32-WindowsNoEditor.pak", 555399273},
	{"pakchunk33-WindowsNoEditor.pak", 425647959},
	{"pakchunk34-WindowsNoEditor.pak", 343798943},
	{"pakchunk35-WindowsNoEditor.pak", 313964217},
	{"pakchunk36-WindowsNoEditor.pak", 530134494},
	{"pakchunk37-WindowsNoEditor.pak", 377685644},
	{"pakchunk38-WindowsNoEditor.pak", 266343665},
	{"pakchunk39-WindowsNoEditor.pak", 418945465},
	{"pakchunk40-WindowsNoEditor.pak", 352968573},
	{"pakchunk41-WindowsNoEditor.pak", 146626909},
	{"pakchunk42-WindowsNoEditor.pak", 57648452},
	{"pakchunk43-WindowsNoEditor.pak", 70572491},
	{"pakchunk44-WindowsNoEditor.pak", 156530906},
	{"pakchunk45-WindowsNoEditor.pak", 233982848},
	{"pakchunk46-WindowsNoEditor.pak", 64782556},
	{"pakchunk47-WindowsNoEditor.pak", 75780282},
	{"pakchunk48-WindowsNoEditor.pak", 105380904},
	{"pakchunk49-WindowsNoEditor.pak", 160080807},
	{"pakchunk50-WindowsNoEditor.pak", 20340007},
	{"pakchunk51-WindowsNoEditor.pak", 114501682},
	{"pakchunk52-WindowsNoEditor.pak", 113835910},
	{"pakchunk53-WindowsNoEditor.pak", 6345880019},
	{"pakchunk54-WindowsNoEditor.pak", 569307227},
	{"pakchunk55-WindowsNoEditor.pak", 368657476},
	{"pakchunk56-WindowsNoEditor.pak", 212777940},
	{"pakchunk57-WindowsNoEditor.pak", 471660403},
	{"pakchunk58-WindowsNoEditor.pak", 126614009},
	{"pakchunk59-WindowsNoEditor.pak", 692474038},
	{"pakchunk60-WindowsNoEditor.pak", 297808312},
	{"pakchunk61-WindowsNoEditor.pak", 112618204},
	{"pakchunk62-WindowsNoEditor.pak", 122586584},
	{"pakchunk63-WindowsNoEditor.pak", 178920000},
	{"pakchunk64-WindowsNoEditor.pak", 201556316},
	{"pakchunk65-WindowsNoEditor.pak", 101888198},
	{"pakchunk66-WindowsNoEditor.pak", 317809167},
	{"pakchunk67-WindowsNoEditor.pak", 9481578295},
	{"pakchunk68-WindowsNoEditor.pak", 10572555412},
};
const size_t kStockCount = std::size(kStock);

namespace {

const char* FolderName(Folder folder)
{
	switch (folder) {
	case Folder::Content: return "Content";
	case Folder::Engine: return "Engine";
	case Folder::Saved: return "Saved";
	}
	return "?";
}

const StockPak* StockNamed(const std::string& name)
{
	for (const StockPak& stock : kStock) {
		if (_stricmp(stock.name, name.c_str()) == 0) return &stock;
	}
	return nullptr;
}

std::string Where(const Found& pak)
{
	return std::string(FolderName(pak.folder)) + "/" + pak.path;
}

} // namespace

Census Judge(const std::vector<Found>& found)
{
	Census census;
	std::map<std::string, bool> seen;
	for (const Found& pak : found) {
		const StockPak* stock = pak.folder == Folder::Content ? StockNamed(pak.path) : nullptr;
		const Verdict verdict = !stock ? Verdict::NotStock : stock->size == pak.size ? Verdict::Stock : Verdict::OtherSize;
		if (stock) seen[stock->name] = true;
		census.paks.push_back({pak, verdict});
	}
	for (const StockPak& stock : kStock) {
		if (!seen.count(stock.name)) census.missing.push_back(stock.name);
	}
	return census;
}

bool AllStock(const Census& census)
{
	return census.missing.empty() &&
		std::all_of(census.paks.begin(), census.paks.end(), [](const Judged& j) { return j.verdict == Verdict::Stock; });
}

std::string Summary(const Census& census)
{
	size_t stock = 0;
	std::string other, notStock;
	for (const Judged& j : census.paks) {
		const std::string one = Where(j.pak) + " (" + std::to_string(j.pak.size) + " bytes)";
		if (j.verdict == Verdict::Stock) stock++;
		else if (j.verdict == Verdict::OtherSize) other += (other.empty() ? "" : ", ") + one;
		else notStock += (notStock.empty() ? "" : ", ") + one;
	}
	std::string text = std::to_string(stock) + " of " + std::to_string(kStockCount) + " stock paks";
	if (!notStock.empty()) text += "; NOT STOCK: " + notStock;
	if (!other.empty()) text += "; a stock name with another size: " + other;
	if (!census.missing.empty()) {
		text += "; missing:";
		for (const std::string& name : census.missing) text += " " + name;
	}
	return text;
}

std::string Line(const Judged& judged)
{
	const char* verdict = judged.verdict == Verdict::Stock ? "stock" : judged.verdict == Verdict::OtherSize ? "a stock name with another size" : "NOT STOCK";
	return Where(judged.pak) + ", " + std::to_string(judged.pak.size) + " bytes: " + verdict;
}

} // namespace sfve::mod::paks
