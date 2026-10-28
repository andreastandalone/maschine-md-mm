#include "mdLibraryScanner.h"

#include <cstdlib>
#include <iostream>

namespace
{
	using Bytes = std::vector<uint8_t>;

	void expect(bool condition, const char* message)
	{
		if(condition) return;
		std::cerr << "mdLibraryScannerTest: " << message << '\n';
		std::exit(1);
	}

	Bytes mdDump(uint8_t _command, size_t _size)
	{
		Bytes d(_size, 0);
		const uint8_t header[] = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, _command, 0x04, 0x01, 0x00};
		std::copy(std::begin(header), std::end(header), d.begin());
		d[_size - 3] = uint8_t(((_size - 10) >> 7) & 0x7f);
		d[_size - 2] = uint8_t((_size - 10) & 0x7f);
		d.back() = 0xf7;
		return d;
	}

	void write(const juce::File& _file, const Bytes& _bytes)
	{
		expect(_file.getParentDirectory().createDirectory().wasOk(), "cannot create test folder");
		expect(_file.replaceWithData(_bytes.data(), _bytes.size()), "cannot write test file");
	}
}

int main()
{
	using namespace mdJucePlugin::library;
	const auto root = juce::File::createTempFile("mdLibraryScannerTest");
	expect(root.createDirectory().wasOk(), "cannot create temp root");

	write(root.getChildFile("A/Sub/kit.syx"), mdDump(0x52, 1233));
	write(root.getChildFile("junk.syx"), {0x12, 0x34, 0xf0, 0x99, 0x01});
	{
		const auto pattern = root.getChildFile("pat.syx");
		write(pattern, mdDump(0x67, 5410));
		juce::ZipFile::Builder builder;
		builder.addFile(pattern, 0, "x/pat.syx");
		juce::FileOutputStream out(root.getChildFile("B.zip"));
		double progress = 0;
		expect(out.openedOk() && builder.writeToStream(out, &progress), "cannot write zip");
		pattern.deleteFile();
	}

	const auto packs = scan(root, md::MachineModel::Machinedrum);
	expect(packs.size() == 2, "expected two packs");
	expect(packs[0].name == "A / SUB" && packs[0].items.size() == 1, "folder pack wrong");
	expect(packs[1].name == "B / X" && packs[1].items.size() == 1, "zip pack wrong");
	expect(packs[1].items[0].kind == md::packlib::ItemKind::Pattern, "zip item kind wrong");
	expect(scan(root, md::MachineModel::Monomachine).empty(), "MD packs listed for MM");
	expect(scan(root.getChildFile("missing"), md::MachineModel::Machinedrum).empty(), "missing root not empty");

	root.deleteRecursively();
	std::cout << "mdLibraryScannerTest: ok\n";
	return 0;
}
