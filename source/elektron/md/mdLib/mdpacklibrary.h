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

	std::vector<uint8_t> loadKitCommand(MachineModel _model, uint8_t _slot);
	std::vector<uint8_t> loadPatternCommand(MachineModel _model, uint8_t _slot);
}
