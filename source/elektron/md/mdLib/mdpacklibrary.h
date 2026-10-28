#pragma once

#include "mdtypes.h"

#include <cstdint>
#include <string>
#include <vector>

// Reads kits, patterns and SDS samples out of user pack files and moves
// dumps to another slot, so a pack can be auditioned without overwriting.
namespace md::packlib
{
	enum class ItemKind : uint8_t { Kit, Pattern };

	struct Item
	{
		MachineModel model = MachineModel::Machinedrum;
		ItemKind kind = ItemKind::Kit;
		uint8_t slot = 0;
		std::string name;
		uint8_t kitSlot = 0;                 // patterns only
		std::vector<uint8_t> dump;
		std::vector<uint8_t> sampleSlots;    // MD kits only, sorted unique
		bool empty = false;                  // erased or unnamed kit, pattern without trigs
	};

	struct Sample
	{
		uint8_t slot = 0;
		std::vector<uint8_t> bytes;          // SDS header, optional name, all packets
	};

	struct FileContents
	{
		std::vector<Item> items;
		std::vector<Sample> samples;
	};

	// Never throws: invalid, truncated, foreign and unsupported messages are skipped.
	FileContents readSyx(const std::vector<uint8_t>& _bytes);

	std::vector<uint8_t> retargetDump(std::vector<uint8_t> _dump, uint8_t _slot);
	std::vector<uint8_t> retargetMdPattern(std::vector<uint8_t> _dump, uint8_t _slot, uint8_t _kitSlot);

	// Where a selection of pack items goes on the machine: kits to free kit
	// slots, patterns to free pattern slots, each pattern pointing at the new
	// slot of its kit. Nothing is planned when the free slots do not suffice.
	struct ImportPlan
	{
		std::vector<uint8_t> dumps;          // kits first, then patterns
		std::vector<uint8_t> kitSlots;       // destinations, in import order
		std::vector<uint8_t> patternSlots;
		std::vector<uint8_t> sampleSlots;    // samples the kits need, original slots
		size_t kitsNeeded = 0;
		size_t patternsNeeded = 0;
		size_t skippedPatterns = 0;          // their kit is not in the pack
		bool enoughSlots = true;
	};

	ImportPlan planImport(const std::vector<Item>& _pack, const std::vector<size_t>& _selected,
		const std::vector<bool>& _freeKits, const std::vector<bool>& _freePatterns);

	std::vector<uint8_t> loadKitCommand(MachineModel _model, uint8_t _slot);
	std::vector<uint8_t> loadPatternCommand(MachineModel _model, uint8_t _slot);
}
