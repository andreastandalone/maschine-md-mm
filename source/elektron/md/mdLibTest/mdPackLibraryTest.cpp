#include "mdLib/mdpacklibrary.h"
#include "mdLib/mdsysexfile.h"

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <iterator>

namespace
{
	using Bytes = std::vector<uint8_t>;

	void expect(bool condition, const char* message)
	{
		if(condition) return;
		std::cerr << "mdPackLibraryTest: " << message << '\n';
		std::exit(1);
	}

	void sealDump(Bytes& _dump)
	{
		const auto size = _dump.size();
		uint32_t sum = 0;
		for(size_t i = 9; i < size - 5; ++i) sum += _dump[i];
		_dump[size - 5] = uint8_t((sum >> 7) & 0x7f);
		_dump[size - 4] = uint8_t(sum & 0x7f);
		_dump[size - 3] = uint8_t(((size - 10) >> 7) & 0x7f);
		_dump[size - 2] = uint8_t((size - 10) & 0x7f);
	}

	Bytes emptyDump(uint8_t _command, size_t _size, uint8_t _slot)
	{
		Bytes d(_size, 0);
		const uint8_t header[] = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, _command, 0x04, 0x01, _slot};
		std::copy(std::begin(header), std::end(header), d.begin());
		d.back() = 0xf7;
		return d;
	}

	// Elektron 7-bit packing: one byte holding the next seven bytes' MSBs, first byte at bit 6.
	Bytes pack7(const Bytes& _raw)
	{
		Bytes out;
		for(size_t i = 0; i < _raw.size(); i += 7)
		{
			const size_t n = std::min<size_t>(7, _raw.size() - i);
			uint8_t msb = 0;
			for(size_t j = 0; j < n; ++j) msb |= uint8_t((_raw[i + j] >> 7) << (6 - j));
			out.push_back(msb);
			for(size_t j = 0; j < n; ++j) out.push_back(_raw[i + j] & 0x7f);
		}
		return out;
	}

	Bytes mdKit(uint8_t _slot, const char* _name, const std::vector<uint32_t>& _machines)
	{
		auto d = emptyDump(0x52, 1233, _slot);
		for(size_t i = 0; _name[i] && i < 16; ++i) d[0x0a + i] = uint8_t(_name[i]);
		Bytes raw(64, 0);
		for(size_t t = 0; t < _machines.size() && t < 16; ++t)
			for(size_t b = 0; b < 4; ++b) raw[t * 4 + b] = uint8_t(_machines[t] >> (24 - 8 * b));
		const auto packed = pack7(raw);
		expect(packed.size() == 74, "machine block helper is not 74 bytes");
		std::copy(packed.begin(), packed.end(), d.begin() + 0x1aa);
		sealDump(d);
		return d;
	}

	Bytes mdPattern(uint8_t _slot, uint8_t _kit)
	{
		auto d = emptyDump(0x67, 5410, _slot);
		d[0xb5] = _kit;
		sealDump(d);
		return d;
	}

	Bytes sdsHeader(uint16_t _slot, uint32_t _words)
	{
		Bytes h{0xf0, 0x7e, 0x00, 0x01, uint8_t(_slot & 0x7f), uint8_t(_slot >> 7), 16,
			0x2c, 0x5a, 0x01, uint8_t(_words & 0x7f), uint8_t((_words >> 7) & 0x7f), uint8_t(_words >> 14),
			0, 0, 0, uint8_t(_words & 0x7f), uint8_t((_words >> 7) & 0x7f), uint8_t(_words >> 14), 0x7f, 0xf7};
		return h;
	}

	Bytes sdsPacket(uint8_t _number)
	{
		Bytes p(127, 0);
		p[0] = 0xf0; p[1] = 0x7e; p[3] = 0x02; p[4] = _number;
		uint8_t checksum = 0;
		for(size_t i = 1; i < 125; ++i) checksum ^= p[i];
		p[125] = checksum;
		p[126] = 0xf7;
		return p;
	}

	void append(Bytes& _to, const Bytes& _from) { _to.insert(_to.end(), _from.begin(), _from.end()); }

	// stdio, not <filesystem>: the deployment target predates macOS 10.15.
	Bytes readFile(const std::string& _path)
	{
		Bytes bytes;
		if(auto* f = std::fopen(_path.c_str(), "rb"))
		{
			for(int c; (c = std::fgetc(f)) != EOF;) bytes.push_back(uint8_t(c));
			std::fclose(f);
		}
		return bytes;
	}

	size_t count(const md::packlib::FileContents& _c, md::packlib::ItemKind _kind)
	{
		return size_t(std::count_if(_c.items.begin(), _c.items.end(), [&](const auto& i) { return i.kind == _kind; }));
	}

	bool valid(const Bytes& _dump)
	{
		return md::parseMidiSysexFile(_dump, md::MachineModel::Machinedrum) == md::MidiSysexStreamValidation::Valid;
	}
}

int main(int _argc, char** _argv)
{
	using namespace md::packlib;
	const auto kit = mdKit(5, "TEST KIT", {0x80, 0x81, 0x1c, 0x81});

	// checksumRoundTrip
	{
		const auto c = readSyx(kit);
		expect(c.items.size() == 1 && c.items[0].kind == ItemKind::Kit, "kit not recognised");
		expect(c.items[0].slot == 5, "kit slot wrong");
		expect(c.items[0].name == "TEST KIT", "kit name wrong");
		expect(c.items[0].sampleSlots == std::vector<uint8_t>{0, 1}, "kit sample slots wrong");
		expect(c.items[0].model == md::MachineModel::Machinedrum, "kit model wrong");
	}
	// retargetKeepsValid
	{
		const auto moved = retargetDump(kit, 63);
		expect(moved[9] == 63 && valid(moved), "retargeted kit invalid");
	}
	// retargetPattern
	{
		const auto p = mdPattern(10, 3);
		const auto c = readSyx(p);
		expect(c.items.size() == 1 && c.items[0].kind == ItemKind::Pattern, "pattern not recognised");
		expect(c.items[0].kitSlot == 3 && c.items[0].name == "PATTERN A11", "pattern fields wrong");
		const auto moved = retargetMdPattern(p, 127, 63);
		expect(moved[9] == 127 && moved[0xb5] == 63 && valid(moved), "retargeted pattern invalid");
	}
	// truncatedTail
	{
		auto bytes = kit;
		append(bytes, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x52, 0x04});
		expect(readSyx(bytes).items.size() == 1, "truncated tail lost the complete kit");
	}
	// samplesGrouped
	{
		Bytes bytes = sdsHeader(2, 80);
		append(bytes, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x73, 0x02, 'K', 'I', 'C', 'K', 0xf7});
		append(bytes, sdsPacket(0));
		append(bytes, sdsPacket(1));
		const auto c = readSyx(bytes);
		expect(c.samples.size() == 1 && c.samples[0].slot == 2, "sample not grouped");
		expect(c.samples[0].bytes.size() == bytes.size(), "sample bytes incomplete");
		expect(c.items.empty(), "sample name read as item");
	}
	// nameAfterPackets: a trailing name would stall the transfer in receive mode
	{
		Bytes sample = sdsHeader(3, 80);
		append(sample, sdsPacket(0));
		append(sample, sdsPacket(1));
		Bytes bytes = sample;
		append(bytes, {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x73, 0x03, 'S', 'N', 'A', 'R', 0xf7});
		const auto c = readSyx(bytes);
		expect(c.samples.size() == 1 && c.samples[0].bytes == sample, "trailing sample name kept in sample");
	}
	// emptySlots: a kit without a name and a pattern without trigs are free
	{
		expect(!readSyx(kit).items[0].empty, "named kit read as empty");
		expect(readSyx(mdKit(7, "", {})).items[0].empty, "unnamed kit not empty");
		// The firmware marks an erased kit slot by 0x7F in the first name byte;
		// the rest of the old name stays behind.
		expect(readSyx(mdKit(8, "\x7fNPUT A FX", {})).items[0].empty, "erased kit not empty");
		expect(readSyx(mdPattern(4, 0)).items[0].empty, "pattern without trigs not empty");
		auto used = mdPattern(4, 0);
		used[0x0b] = 0x01;
		sealDump(used);
		expect(!readSyx(used).items[0].empty, "pattern with a trig read as empty");
	}
	// planImport: kits go to free kit slots, patterns to free pattern slots and
	// follow their kit; a pattern whose kit is not in the pack is skipped
	{
		std::vector<Item> pack;
		for(const auto& d : {mdKit(3, "A", {0x80, 0x81}), mdKit(4, "B", {}), mdPattern(0, 3), mdPattern(1, 3), mdPattern(2, 9)})
			pack.push_back(readSyx(d).items[0]);
		std::vector<bool> freeKits(64, false), freePatterns(128, false);
		freeKits[10] = freeKits[12] = true;
		freePatterns[20] = freePatterns[21] = freePatterns[22] = true;
		const auto plan = planImport(pack, {2, 3, 1, 4}, freeKits, freePatterns);
		expect(plan.enoughSlots, "free slots judged insufficient");
		expect(plan.kitSlots == std::vector<uint8_t>{10, 12}, "kit destinations wrong");
		expect(plan.patternSlots == std::vector<uint8_t>{20, 21}, "pattern destinations wrong");
		expect(plan.skippedPatterns == 1, "pattern without its kit not skipped");
		expect(plan.sampleSlots == std::vector<uint8_t>{0, 1}, "kit samples not collected");
		const auto sent = readSyx(plan.dumps);
		expect(sent.items.size() == 4 && sent.items[0].slot == 10 && sent.items[1].slot == 12, "kit dumps not retargeted");
		expect(sent.items[2].slot == 20 && sent.items[2].kitSlot == 10 && sent.items[3].kitSlot == 10, "patterns do not follow their kit");

		std::vector<bool> oneKit(64, false);
		oneKit[10] = true;
		const auto short_ = planImport(pack, {2, 1}, oneKit, freePatterns);
		expect(!short_.enoughSlots && short_.kitsNeeded == 2 && short_.dumps.empty(), "missing slots not reported");
	}
	// junk and other models
	{
		expect(readSyx({0x12, 0x34, 0xf0, 0x01, 0xf7}).items.empty(), "junk produced items");
		auto mm = kit;
		mm[4] = 0x03;
		expect(readSyx(mm).items.empty(), "MM kit with MD layout accepted as MD");
	}
	// loadCommands
	{
		expect(loadKitCommand(md::MachineModel::Machinedrum, 63) == Bytes{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x58, 0x3f, 0xf7}, "kit load command wrong");
		expect(loadPatternCommand(md::MachineModel::Machinedrum, 127) == Bytes{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x57, 0x7f, 0xf7}, "pattern load command wrong");
	}
	// realPacks
	if(_argc < 2 || readFile(std::string(_argv[1]) + "/MD/MD909/MD909UW.syx").empty())
	{
		std::cout << "mdPackLibraryTest: real packs missing, skipped\n";
		return 77;
	}
	{
		const std::string root = _argv[1];
		const auto md909 = readSyx(readFile(root + "/MD/MD909/MD909UW.syx"));
		expect(count(md909, ItemKind::Kit) == 13, "MD909 kit count");
		expect(count(md909, ItemKind::Pattern) == 128, "MD909 pattern count");
		for(const auto& item : md909.items)
			if(item.kind == ItemKind::Kit && item.slot <= 6)
				expect(item.sampleSlots == std::vector<uint8_t>{0, 1, 2, 3}, "MD909 kit sample slots");
		const auto ch = readSyx(readFile(root + "/MD/MD909/09CH-SLOT1.syx"));
		expect(ch.samples.size() == 1 && ch.samples[0].slot == 0, "09CH sample slot");
		// Booster Pack is Monomachine data (product 0x03) with a truncated tail.
		expect(readSyx(readFile(root + "/MD/Booster-Pack/SysEx/The Room.syx")).items.empty(), "MM pack listed as MD");
	}
	std::cout << "mdPackLibraryTest: ok\n";
	return 0;
}
