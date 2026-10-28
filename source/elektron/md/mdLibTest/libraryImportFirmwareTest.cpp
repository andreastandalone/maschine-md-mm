// End-to-end check of the library import path against the real Machinedrum
// firmware: free-slot inventory by dump requests, planImport, samples on the
// fast wire, retargeted dumps, and a readback of what landed where.
#include "mdLib/mdhardware.h"
#include "mdLib/mdpacklibrary.h"
#include "mdLib/mdsysextransfer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	using Bytes = std::vector<uint8_t>;

	[[noreturn]] void fail(const char* _message)
	{
		std::fprintf(stderr, "mdLibraryImportFirmwareTest: %s\n", _message);
		std::exit(1);
	}

	void require(bool _condition, const char* _message) { if(!_condition) fail(_message); }

	Bytes load(const std::string& _path)
	{
		Bytes bytes;
		if(auto* f = std::fopen(_path.c_str(), "rb"))
		{
			for(int c; (c = std::fgetc(f)) != EOF;) bytes.push_back(uint8_t(c));
			std::fclose(f);
		}
		return bytes;
	}

	double seconds(md::Hardware& _hw) { return double(_hw.getUC().getCycles()) / 40'000'000.0; }

	// Sends a dump request and returns the matching reply, as the library does.
	Bytes request(md::Hardware& _hw, uint8_t _command, uint8_t _slot)
	{
		std::vector<synthLib::SMidiEvent> events;
		_hw.readMidiOut(events);
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
		event.sysex = {0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, _command, _slot, 0xf7};
		require(_hw.sendMidi(event), "request not queued");
		for(int i = 0; i < 20000; ++i)
		{
			_hw.advance(64);
			events.clear();
			_hw.readMidiOut(events);
			for(const auto& e : events)
				if(e.sysex.size() > 15 && e.sysex[6] == _command - 1 && e.sysex[9] == _slot)
					return e.sysex;
		}
		return {};
	}

	void transfer(md::Hardware& _hw, Bytes _bytes)
	{
		auto prepared = md::prepareMidiSysexTransfer(std::move(_bytes));
		require(prepared && _hw.startMidiSysexTransfer(*prepared), "transfer did not start");
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
		for(;;)
		{
			_hw.advance(64);
			const auto state = _hw.getMidiSysexTransferProgress().state;
			if(state == md::MidiSysexTransferState::Complete) break;
			require(state != md::MidiSysexTransferState::Failed && state != md::MidiSysexTransferState::WaitingForReceiveMode
				&& std::chrono::steady_clock::now() < deadline, "transfer did not complete");
		}
		Bytes retired;
		_hw.retireMidiSysexTransferPayload(retired);
	}

	// The firmware answers only once it has finished storing what it received.
	void waitUntilAnswering(md::Hardware& _hw)
	{
		for(int attempt = 0; attempt < 60; ++attempt)
			if(!request(_hw, 0x53, 0).empty()) return;
		fail("firmware never answered after the transfer");
	}
}

int main(int _argc, char** _argv)
{
	const auto* romPath = std::getenv("GEARMULATOR_MD_FIRMWARE_BIN");
	const std::string packs = _argc > 1 ? _argv[1] : "";
	const auto dub1 = packs + "/MD/Deep-Rhythms/SysEx/dub1/";
	if(!romPath || !*romPath || load(dub1 + "DUB1_SONG_PAT_KIT.syx").empty())
	{
		std::puts("mdLibraryImportFirmwareTest: SKIP (MD firmware or Deep Rhythms pack not supplied)");
		return 77;
	}

	// The pack as the scanner sees it: every file of the dub1 folder.
	md::packlib::FileContents pack;
	for(const auto* name : {"DUB1_SONG_PAT_KIT.syx", "01_DUB1_BD01.syx", "02_DUB1_SD01.syx", "03_DUB1_SD02.syx",
		"04_DUB1_SD03.syx", "05_DUB1_SD04.syx", "06_DUB1_SD05.syx", "07_DUB1_SD06.syx", "08_DUB1_TOM1.syx",
		"09_DUB1_TOM2.syx", "10_DUB1_RS01.syx", "11_DUB1_RS02.syx", "12_DUB1_RS03.syx", "13_DUB1_HH01.syx",
		"14_DUB1_HH02.syx", "15_DUB1_OH01.syx", "16_DUB1_CC01.syx", "17_DUB1_CLV1.syx", "18_DUB1_CBS1.syx",
		"19_DUB1_CBS2.syx", "20_DUB1_FX01.syx", "21_DUB1_FX02.syx", "22_DUB1_FX03.syx"})
	{
		auto contents = md::packlib::readSyx(load(dub1 + name));
		pack.items.insert(pack.items.end(), contents.items.begin(), contents.items.end());
		pack.samples.insert(pack.samples.end(), contents.samples.begin(), contents.samples.end());
	}
	size_t patternIndex = pack.items.size();
	for(size_t i = 0; i < pack.items.size() && patternIndex == pack.items.size(); ++i)
		if(pack.items[i].kind == md::packlib::ItemKind::Pattern) patternIndex = i;
	require(patternIndex < pack.items.size() && pack.samples.size() == 22, "dub1 pack not read");

	md::Hardware hw(load(romPath), romPath);
	require(hw.isValid(), "firmware did not construct");
	const auto bootDeadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
	while(!hw.isFirmwareMidiReady())
	{
		hw.advance(64);
		require(std::chrono::steady_clock::now() < bootDeadline, "boot timed out");
	}
	waitUntilAnswering(hw);

	// Inventory, one request at a time.
	const auto inventoryStart = seconds(hw);
	std::vector<bool> freeKits(64), freePatterns(128);
	for(uint8_t k = 0; k < 64; ++k)
	{
		const auto items = md::packlib::readSyx(request(hw, 0x53, k)).items;
		require(items.size() == 1, "kit dump missing");
		freeKits[k] = items[0].empty;
	}
	for(uint8_t p = 0; p < 128; ++p)
	{
		const auto items = md::packlib::readSyx(request(hw, 0x68, p)).items;
		require(items.size() == 1, "pattern dump missing");
		freePatterns[p] = items[0].empty;
	}
	std::printf("inventory: %.2f s, %zu free kits, %zu free patterns\n", seconds(hw) - inventoryStart,
		size_t(std::count(freeKits.begin(), freeKits.end(), true)),
		size_t(std::count(freePatterns.begin(), freePatterns.end(), true)));
	// Factory 1.63 UW: kits 0-15 used, 16-63 erased; patterns 0-63 used, 64-127 empty.
	require(!freeKits[0] && !freeKits[15] && freeKits[16] && freeKits[63], "factory kit occupancy misread");
	require(!freePatterns[0] && !freePatterns[63] && freePatterns[64] && freePatterns[127], "factory pattern occupancy misread");

	const auto plan = md::packlib::planImport(pack.items, {patternIndex}, freeKits, freePatterns);
	require(plan.enoughSlots && plan.kitSlots == Bytes{16} && plan.patternSlots == Bytes{64}, "unexpected plan");
	require(!plan.sampleSlots.empty(), "dub1 kit uses no samples");

	const auto importStart = seconds(hw);
	Bytes samples;
	for(const auto& sample : pack.samples)
		for(const auto slot : plan.sampleSlots)
			if(sample.slot == slot) samples.insert(samples.end(), sample.bytes.begin(), sample.bytes.end());
	transfer(hw, samples);
	std::printf("samples (%zu): %.2f s\n", plan.sampleSlots.size(), seconds(hw) - importStart);
	waitUntilAnswering(hw);
	transfer(hw, plan.dumps);
	waitUntilAnswering(hw);
	std::printf("import total: %.2f s\n", seconds(hw) - importStart);

	// Readback: the kit and the pattern are where the plan put them, the pattern
	// points at the new kit, and the pattern's own data is unchanged.
	const auto& sourcePattern = pack.items[patternIndex];
	const md::packlib::Item* sourceKit = nullptr;
	for(const auto& item : pack.items)
		if(item.kind == md::packlib::ItemKind::Kit && item.slot == sourcePattern.kitSlot) sourceKit = &item;
	const auto kit = md::packlib::readSyx(request(hw, 0x53, 16)).items;
	require(kit.size() == 1 && kit[0].name == sourceKit->name, "imported kit not in slot 17");
	require(kit[0].sampleSlots == sourceKit->sampleSlots, "imported kit lost its sample machines");
	const auto pattern = md::packlib::readSyx(request(hw, 0x68, 64)).items;
	require(pattern.size() == 1 && pattern[0].kitSlot == 16, "imported pattern does not point at its kit");
	require(std::equal(sourcePattern.dump.begin() + 10, sourcePattern.dump.begin() + 0xb5,
		pattern[0].dump.begin() + 10), "imported pattern data changed");
	std::printf("mdLibraryImportFirmwareTest: ok (kit '%s' -> 17, pattern -> E01, samples %zu)\n",
		kit[0].name.c_str(), plan.sampleSlots.size());
	return 0;
}
