// End-to-end check of the library import path against the real Monomachine
// firmware: free-slot inventory, planImport with re-encoded patterns, the
// panel script that opens SYSEX RECEIVE, and a readback of what landed where.
#include "mdLib/mdhardware.h"
#include "mdLib/mdpacklibrary.h"
#include "mdLib/mdpanelscript.h"
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
	constexpr auto g_mm = md::MachineModel::Monomachine;

	[[noreturn]] void fail(const char* _message)
	{
		std::fprintf(stderr, "mmLibraryImportFirmwareTest: %s\n", _message);
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

	void advance(md::Hardware& _hw, uint32_t _frames)
	{
		for(; _frames >= 64; _frames -= 64) _hw.advance(64);
		if(_frames) _hw.advance(_frames);
	}

	void run(md::Hardware& _hw, const std::vector<md::PanelStep>& _script)
	{
		for(const auto& step : _script)
		{
			require(_hw.trySendPanelEvent(step.row, step.mask), "panel event rejected");
			advance(_hw, step.waitFrames);
		}
	}

	Bytes request(md::Hardware& _hw, uint8_t _command, uint8_t _slot, uint8_t _reply)
	{
		std::vector<synthLib::SMidiEvent> events;
		_hw.readMidiOut(events);
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
		event.sysex = {0xf0, 0x00, 0x20, 0x3c, 0x03, 0x00, _command, _slot, 0xf7};
		require(_hw.sendMidi(event), "request not queued");
		for(int i = 0; i < 20000; ++i)
		{
			_hw.advance(64);
			events.clear();
			_hw.readMidiOut(events);
			for(const auto& e : events)
				if(e.sysex.size() > 8 && e.sysex[6] == _reply) return e.sysex;
		}
		return {};
	}
}

int main(int _argc, char** _argv)
{
	const auto* romPath = std::getenv("GEARMULATOR_MM_FIRMWARE_BIN");
	const std::string packs = _argc > 1 ? _argv[1] : "";
	const auto packBytes = load(packs + "/MM/Nicholas-Lem-Collection/SysEx/Nicholas Lem MnM Sound Pack(patterns+kits).syx");
	if(!romPath || !*romPath || packBytes.empty())
	{
		std::puts("mmLibraryImportFirmwareTest: SKIP (MM firmware or Nicholas Lem pack not supplied)");
		return 77;
	}
	const auto pack = md::packlib::readSyx(packBytes).items;
	size_t patternIndex = 0;
	while(patternIndex < pack.size() && pack[patternIndex].kind != md::packlib::ItemKind::Pattern) ++patternIndex;
	require(patternIndex < pack.size(), "pack has no pattern");

	md::Hardware hw(load(romPath), romPath, g_mm);
	require(hw.isValid(), "firmware did not construct");
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(4);
	while(!hw.isFirmwareMidiReady() || !hw.isAudioReady() || hw.getFrontPanelSnapshot().countLitPixels() <= 2000)
	{
		hw.advance(64);
		require(std::chrono::steady_clock::now() < deadline, "boot timed out");
	}
	advance(hw, 2 * 44100);

	std::vector<md::packlib::Item> machine;
	for(int k = 0; k < 128; ++k)
		for(auto& item : md::packlib::readSyx(request(hw, 0x53, uint8_t(k), 0x52)).items) machine.push_back(item);
	for(int p = 0; p < 128; ++p)
		for(auto& item : md::packlib::readSyx(request(hw, 0x68, uint8_t(p), 0x67)).items) machine.push_back(item);
	require(machine.size() == 256, "inventory incomplete");
	const auto slots = md::packlib::freeSlots(machine);
	// Factory 1.32b: kits and patterns 0-63 used, 64-127 erased or empty.
	require(slots.freeKits.size() == 128 && !slots.freeKits[63] && slots.freeKits[64] && slots.freeKits[127],
		"factory kit occupancy misread");
	require(!slots.freePatterns[63] && slots.freePatterns[64], "factory pattern occupancy misread");

	const auto plan = md::packlib::planImport(pack, {patternIndex}, slots.freeKits, slots.freePatterns);
	require(plan.enoughSlots && plan.kitSlots == Bytes{64} && plan.patternSlots == Bytes{64}, "unexpected plan");

	run(hw, md::mmSysexReceiveScript());
	auto prepared = md::prepareMidiSysexTransfer(plan.dumps, g_mm);
	require(prepared && hw.startMidiSysexTransfer(*prepared), "transfer did not start");
	for(;;)
	{
		hw.advance(64);
		const auto state = hw.getMidiSysexTransferProgress().state;
		if(state == md::MidiSysexTransferState::Complete) break;
		require(state != md::MidiSysexTransferState::Failed && state != md::MidiSysexTransferState::WaitingForReceiveMode,
			"transfer did not complete");
	}
	Bytes retired;
	hw.retireMidiSysexTransferPayload(retired);
	advance(hw, 44100);
	run(hw, md::exitMenusScript(g_mm));

	const auto& sourcePattern = pack[patternIndex];
	const auto sourceKit = std::find_if(pack.begin(), pack.end(), [&](const auto& _i)
	{
		return _i.kind == md::packlib::ItemKind::Kit && _i.slot == sourcePattern.kitSlot;
	});
	const auto kit = md::packlib::readSyx(request(hw, 0x53, 64, 0x52)).items;
	require(kit.size() == 1 && kit[0].name == sourceKit->name, "imported kit not in slot 65");
	const auto pattern = md::packlib::readSyx(request(hw, 0x68, 64, 0x67)).items;
	require(pattern.size() == 1 && pattern[0].kitSlot == 64, "imported pattern does not point at its kit");
	auto expected = md::packlib::mmPayload(sourcePattern.dump);
	(*expected)[0x426] = 64;
	require(md::packlib::mmPayload(pattern[0].dump) == expected, "imported pattern data changed");

	// Audition: the load commands switch the machine to the new slots.
	for(const auto& command : {md::packlib::loadPatternCommand(g_mm, 64)})
	{
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
		event.sysex = command;
		require(hw.sendMidi(event), "load command not queued");
	}
	advance(hw, 44100);
	const auto status = request(hw, 0x70, 0x04, 0x72);
	require(status.size() > 8 && status[8] == 64, "load pattern command ignored");

	std::printf("mmLibraryImportFirmwareTest: ok (kit '%s' -> 65, pattern -> E01)\n", kit[0].name.c_str());
	return 0;
}
