#include "mdpacklibrary.h"

#include "mdsysexfile.h"

#include <algorithm>

namespace md::packlib
{
	namespace
	{
		using Bytes = std::vector<uint8_t>;

		constexpr uint8_t g_mdProduct = 0x02;
		constexpr uint8_t g_mmProduct = 0x03;
		constexpr size_t g_mmKitName = 11;
		constexpr size_t g_mmPatternKit = 0x426;
		constexpr size_t g_mmPatternTrigs = 48;        // six tracks, 64 steps each
		constexpr uint8_t g_kitDump = 0x52;
		constexpr uint8_t g_patternDump = 0x67;
		constexpr uint8_t g_sampleName = 0x73;
		constexpr size_t g_kitName = 0x0a;
		constexpr size_t g_kitNameSize = 16;
		constexpr size_t g_kitMachines = 0x1aa;
		constexpr size_t g_kitMachinesSize = 74;
		constexpr size_t g_patternKit = 0xb5;
		constexpr size_t g_patternTrigs = 0x0a;
		constexpr size_t g_patternTrigsSize = 74;

		bool isElektron(const Bytes& _m)
		{
			return _m.size() >= 8 && _m[1] == 0x00 && _m[2] == 0x20 && _m[3] == 0x3c;
		}

		bool isSdsHeader(const Bytes& _m) { return _m.size() == 21 && _m[1] == 0x7e && _m[3] == 0x01; }
		bool isSdsPacket(const Bytes& _m) { return _m.size() >= 5 && _m[1] == 0x7e && _m[3] == 0x02; }
		bool isSampleName(const Bytes& _m) { return isElektron(_m) && _m[4] == g_mdProduct && _m[6] == g_sampleName; }

		// Complete F0..F7 messages with 7-bit bodies; a message cut short by the
		// next F0 or the end of the file is dropped.
		std::vector<Bytes> splitMessages(const Bytes& _bytes)
		{
			std::vector<Bytes> messages;
			size_t i = 0;
			while(i < _bytes.size())
			{
				if(_bytes[i] != 0xf0) { ++i; continue; }
				size_t end = i + 1;
				while(end < _bytes.size() && _bytes[end] < 0x80) ++end;
				if(end < _bytes.size() && _bytes[end] == 0xf7)
				{
					messages.emplace_back(_bytes.begin() + ptrdiff_t(i), _bytes.begin() + ptrdiff_t(end) + 1);
					++end;
				}
				i = end;
			}
			return messages;
		}

		bool validDump(const Bytes& _m, const MachineModel _model = MachineModel::Machinedrum)
		{
			return parseMidiSysexFile(_m, _model) == MidiSysexStreamValidation::Valid;
		}

		// Elektron 7-bit packing: each group starts with a byte holding the MSBs
		// of the following seven bytes, the first one at bit 6.
		Bytes unpack7(const uint8_t* _data, size_t _size)
		{
			Bytes out;
			for(size_t i = 0; i < _size; i += 8)
			{
				const uint8_t msb = _data[i];
				for(size_t j = 1; j < 8 && i + j < _size; ++j)
					out.push_back(uint8_t(_data[i + j] | (((msb >> (7 - j)) & 1) << 7)));
			}
			return out;
		}

		// ROM machines 0x80+n: n 0..31 play sample slot n, n 48..63 slot n-16.
		std::vector<uint8_t> kitSampleSlots(const Bytes& _kit)
		{
			const auto raw = unpack7(_kit.data() + g_kitMachines, g_kitMachinesSize);
			std::vector<uint8_t> slots;
			for(size_t t = 0; t + 4 <= raw.size() && t < 64; t += 4)
			{
				const uint32_t machine = uint32_t(raw[t]) << 24 | uint32_t(raw[t + 1]) << 16 | uint32_t(raw[t + 2]) << 8 | raw[t + 3];
				if(machine >= 0x80 && machine < 0xa0) slots.push_back(uint8_t(machine - 0x80));
				else if(machine >= 0xb0 && machine < 0xc0) slots.push_back(uint8_t(machine - 0x90));
			}
			std::sort(slots.begin(), slots.end());
			slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
			return slots;
		}

		std::string kitName(const Bytes& _kit)
		{
			std::string name;
			for(size_t i = 0; i < g_kitNameSize && _kit[g_kitName + i]; ++i) name += char(_kit[g_kitName + i]);
			while(!name.empty() && name.back() == ' ') name.pop_back();
			return name;
		}

		std::string patternName(uint8_t _slot)
		{
			const auto number = _slot % 16 + 1;
			return std::string("PATTERN ") + char('A' + _slot / 16) + char('0' + number / 10) + char('0' + number % 10);
		}

		void addMmDump(FileContents& _contents, Bytes&& _m)
		{
			if(!validDump(_m, MachineModel::Monomachine)) return;
			const auto payload = mmPayload(_m);
			if(!payload) return;
			Item item;
			item.model = MachineModel::Monomachine;
			item.slot = _m[9];
			if(_m[6] == g_kitDump && payload->size() >= g_mmKitName)
			{
				item.kind = ItemKind::Kit;
				// The firmware marks an erased kit slot with 0xFF as first name byte.
				if((*payload)[0] != 0xff)
					for(size_t i = 0; i < g_mmKitName && (*payload)[i]; ++i) item.name += char((*payload)[i]);
				while(!item.name.empty() && item.name.back() == ' ') item.name.pop_back();
				item.empty = item.name.empty();
			}
			else if(_m[6] == g_patternDump && payload->size() > g_mmPatternKit)
			{
				item.kind = ItemKind::Pattern;
				item.name = patternName(item.slot);
				item.kitSlot = (*payload)[g_mmPatternKit];
				item.empty = std::all_of(payload->begin(), payload->begin() + g_mmPatternTrigs, [](uint8_t _b) { return _b == 0; });
			}
			else return;
			item.dump = std::move(_m);
			_contents.items.push_back(std::move(item));
		}

		void addDump(FileContents& _contents, Bytes&& _m)
		{
			if(_m[4] == g_mmProduct) return addMmDump(_contents, std::move(_m));
			if(_m[4] != g_mdProduct || !validDump(_m)) return;
			Item item;
			item.slot = _m[9];
			if(_m[6] == g_kitDump && _m.size() > g_kitMachines + g_kitMachinesSize + 5)
			{
				item.kind = ItemKind::Kit;
				item.name = kitName(_m);
				item.sampleSlots = kitSampleSlots(_m);
				// The firmware marks an erased kit slot with 0x7F as first name byte.
				item.empty = item.name.empty() || _m[g_kitName] == 0x7f;
			}
			else if(_m[6] == g_patternDump && _m.size() > g_patternKit + 5)
			{
				item.kind = ItemKind::Pattern;
				item.name = patternName(item.slot);
				item.kitSlot = _m[g_patternKit];
				const auto trigs = unpack7(_m.data() + g_patternTrigs, g_patternTrigsSize);
				item.empty = std::all_of(trigs.begin(), trigs.end(), [](uint8_t _b) { return _b == 0; });
			}
			else return;
			item.dump = std::move(_m);
			_contents.items.push_back(std::move(item));
		}

		void addSample(FileContents& _contents, Bytes&& _sample)
		{
			if(_sample.empty() || !validDump(_sample)) return;
			_contents.samples.push_back({uint8_t(_sample[4] | _sample[5] << 7), std::move(_sample)});
		}

		void setChecksum(Bytes& _dump)
		{
			const auto size = _dump.size();
			uint32_t sum = 0;
			for(size_t i = 9; i < size - 5; ++i) sum += _dump[i];
			_dump[size - 5] = uint8_t((sum >> 7) & 0x7f);
			_dump[size - 4] = uint8_t(sum & 0x7f);
		}

		Bytes command(MachineModel _model, uint8_t _command, uint8_t _slot)
		{
			const uint8_t product = _model == MachineModel::Monomachine ? 0x03 : g_mdProduct;
			return {0xf0, 0x00, 0x20, 0x3c, product, 0x00, _command, uint8_t(_slot & 0x7f), 0xf7};
		}
	}

	FileContents readSyx(const Bytes& _bytes)
	{
		FileContents contents;
		Bytes sample;
		bool samplePackets = false;
		for(auto& m : splitMessages(_bytes))
		{
			if(isSdsHeader(m))
			{
				addSample(contents, std::move(sample));
				sample = std::move(m);
				samplePackets = false;
			}
			// A name after the packets would make the sender wait for receive
			// mode; it belongs to the sample only before its first packet.
			else if(!sample.empty() && (isSdsPacket(m) || (isSampleName(m) && !samplePackets)))
			{
				samplePackets |= isSdsPacket(m);
				sample.insert(sample.end(), m.begin(), m.end());
			}
			else
			{
				addSample(contents, std::move(sample));
				sample.clear();
				if(isElektron(m) && m.size() > 15) addDump(contents, std::move(m));
			}
		}
		addSample(contents, std::move(sample));
		return contents;
	}

	Bytes retargetDump(Bytes _dump, uint8_t _slot)
	{
		_dump[9] = _slot;
		setChecksum(_dump);
		return _dump;
	}

	Bytes retargetMdPattern(Bytes _dump, uint8_t _slot, uint8_t _kitSlot)
	{
		_dump[g_patternKit] = _kitSlot;
		return retargetDump(std::move(_dump), _slot);
	}

	Bytes retargetPattern(const Item& _pattern, const uint8_t _slot, const uint8_t _kitSlot)
	{
		if(_pattern.model == MachineModel::Machinedrum)
			return retargetMdPattern(_pattern.dump, _slot, _kitSlot);
		auto payload = mmPayload(_pattern.dump);
		if(!payload || payload->size() <= g_mmPatternKit) return {};
		(*payload)[g_mmPatternKit] = _kitSlot;
		return retargetDump(withMmPayload(_pattern.dump, *payload), _slot);
	}

	std::optional<Bytes> mmPayload(const Bytes& _dump)
	{
		if(_dump.size() < 15) return std::nullopt;
		const auto rle = unpack7(_dump.data() + 10, _dump.size() - 15);
		Bytes payload;
		for(size_t i = 0; i < rle.size(); ++i)
		{
			if(rle[i] < 0x80) { payload.push_back(rle[i]); continue; }
			const size_t count = rle[i] & 0x7f;
			if(!count || ++i >= rle.size()) return std::nullopt;
			payload.insert(payload.end(), count, rle[i]);
		}
		return payload;
	}

	Bytes withMmPayload(const Bytes& _dump, const Bytes& _payload)
	{
		// Runs of three or more, and any byte with its top bit set, become
		// (0x80 | count, value); everything else stays literal.
		Bytes rle;
		for(size_t i = 0; i < _payload.size();)
		{
			size_t run = 1;
			while(i + run < _payload.size() && run < 127 && _payload[i + run] == _payload[i]) ++run;
			if(run >= 3 || _payload[i] >= 0x80)
			{
				rle.push_back(uint8_t(0x80 | run));
				rle.push_back(_payload[i]);
			}
			else
				rle.insert(rle.end(), run, _payload[i]);
			i += run;
		}
		Bytes out(_dump.begin(), _dump.begin() + 10);
		for(size_t i = 0; i < rle.size(); i += 7)
		{
			const size_t n = std::min<size_t>(7, rle.size() - i);
			uint8_t msb = 0;
			for(size_t j = 0; j < n; ++j) msb |= uint8_t((rle[i + j] >> 7) << (6 - j));
			out.push_back(msb);
			for(size_t j = 0; j < n; ++j) out.push_back(rle[i + j] & 0x7f);
		}
		const auto length = out.size() + 5 - 10;
		out.insert(out.end(), {0, 0, uint8_t((length >> 7) & 0x7f), uint8_t(length & 0x7f), 0xf7});
		setChecksum(out);
		return out;
	}

	FreeSlots freeSlots(const std::vector<Item>& _machine)
	{
		const bool mm = !_machine.empty() && _machine.front().model == MachineModel::Monomachine;
		FreeSlots slots{std::vector<bool>(mm ? 128 : 64, false), std::vector<bool>(128, false)};
		for(const auto& item : _machine)
		{
			auto& table = item.kind == ItemKind::Kit ? slots.freeKits : slots.freePatterns;
			if(item.slot < table.size()) table[item.slot] = item.empty;
		}
		for(const auto& item : _machine)
			if(item.kind == ItemKind::Pattern && !item.empty && item.kitSlot < slots.freeKits.size())
				slots.freeKits[item.kitSlot] = false;
		return slots;
	}

	ImportPlan planImport(const std::vector<Item>& _pack, const std::vector<size_t>& _selected,
		const std::vector<bool>& _freeKits, const std::vector<bool>& _freePatterns)
	{
		const auto findKit = [&](uint8_t _slot)
		{
			return std::find_if(_pack.begin(), _pack.end(), [&](const Item& _i)
			{
				return _i.kind == ItemKind::Kit && _i.slot == _slot;
			});
		};
		std::vector<const Item*> kits, patterns;
		ImportPlan plan;
		for(const auto index : _selected)
		{
			const auto& item = _pack.at(index);
			const auto kit = item.kind == ItemKind::Kit ? _pack.begin() + ptrdiff_t(index) : findKit(item.kitSlot);
			if(kit == _pack.end()) { ++plan.skippedPatterns; continue; }
			if(std::find(kits.begin(), kits.end(), &*kit) == kits.end()) kits.push_back(&*kit);
			if(item.kind == ItemKind::Pattern) patterns.push_back(&item);
		}
		plan.kitsNeeded = kits.size();
		plan.patternsNeeded = patterns.size();

		const auto take = [](const std::vector<bool>& _free, size_t _count)
		{
			std::vector<uint8_t> slots;
			for(size_t s = 0; s < _free.size() && slots.size() < _count; ++s)
				if(_free[s]) slots.push_back(uint8_t(s));
			return slots;
		};
		plan.kitSlots = take(_freeKits, kits.size());
		plan.patternSlots = take(_freePatterns, patterns.size());
		plan.enoughSlots = plan.kitSlots.size() == kits.size() && plan.patternSlots.size() == patterns.size();
		if(!plan.enoughSlots)
		{
			plan.kitSlots.clear();
			plan.patternSlots.clear();
			return plan;
		}

		for(size_t k = 0; k < kits.size(); ++k)
		{
			const auto dump = retargetDump(kits[k]->dump, plan.kitSlots[k]);
			plan.dumps.insert(plan.dumps.end(), dump.begin(), dump.end());
			plan.sampleSlots.insert(plan.sampleSlots.end(), kits[k]->sampleSlots.begin(), kits[k]->sampleSlots.end());
		}
		for(size_t p = 0; p < patterns.size(); ++p)
		{
			const auto kit = std::find(kits.begin(), kits.end(), &*findKit(patterns[p]->kitSlot)) - kits.begin();
			const auto dump = retargetPattern(*patterns[p], plan.patternSlots[p], plan.kitSlots[size_t(kit)]);
			plan.dumps.insert(plan.dumps.end(), dump.begin(), dump.end());
		}
		std::sort(plan.sampleSlots.begin(), plan.sampleSlots.end());
		plan.sampleSlots.erase(std::unique(plan.sampleSlots.begin(), plan.sampleSlots.end()), plan.sampleSlots.end());
		return plan;
	}

	Bytes loadKitCommand(MachineModel _model, uint8_t _slot) { return command(_model, 0x58, _slot); }
	Bytes loadPatternCommand(MachineModel _model, uint8_t _slot) { return command(_model, 0x57, _slot); }
}
