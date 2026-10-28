#include "mdLibrarySession.h"

#include "mdCombinedProcessor.h"

#include "mdLib/mddevice.h"
#include "mdLib/mdsysextransfer.h"

#include <functional>

namespace mdJucePlugin
{
	namespace
	{
		constexpr auto g_model = md::MachineModel::Machinedrum;
		// After a sample the firmware is busy CLEANING/LOADING and ignores other
		// data. A kit request is answered only once it is idle again.
		const std::vector<uint8_t> g_probe{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x53, 0x00, 0xf7};
		constexpr double g_probeIntervalMs = 1000.0;
		constexpr double g_probeTimeoutMs = 60000.0;
		constexpr size_t g_kitCount = 64;
		constexpr size_t g_patternCount = 128;

		std::vector<uint8_t> dumpRequest(const size_t _index)
		{
			return _index < g_kitCount
				? std::vector<uint8_t>{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x53, uint8_t(_index), 0xf7}
				: std::vector<uint8_t>{0xf0, 0x00, 0x20, 0x3c, 0x02, 0x00, 0x68, uint8_t(_index - g_kitCount), 0xf7};
		}

		juce::String patternLabel(const uint8_t _slot)
		{
			return juce::String::charToString(juce::juce_wchar('A' + _slot / 16))
				+ juce::String(_slot % 16 + 1).paddedLeft('0', 2);
		}

		// "KIT 11-12 14" style, contiguous slots joined.
		juce::String slotList(const std::vector<uint8_t>& _slots, const std::function<juce::String(uint8_t)>& _label)
		{
			juce::String text;
			for(size_t i = 0; i < _slots.size();)
			{
				size_t j = i;
				while(j + 1 < _slots.size() && _slots[j + 1] == _slots[j] + 1) ++j;
				text << (text.isEmpty() ? "" : " ") << _label(_slots[i]);
				if(j > i) text << "-" << _label(_slots[j]);
				i = j + 1;
			}
			return text;
		}

		double now() { return juce::Time::getMillisecondCounterHiRes(); }

		template<typename Callback>
		auto withDevice(AudioPluginAudioProcessor& _machine, Callback&& _callback)
		{
			return _machine.getPlugin().withDeviceLocked([&](synthLib::Device* _device)
			{
				return _callback(dynamic_cast<md::Device*>(_device));
			});
		}
	}

	LibrarySession::LibrarySession(CombinedProcessor& _processor) : m_processor(_processor) {}

	LibrarySession::~LibrarySession()
	{
		endCapture();
		if(!m_ticket)
			return;
		std::vector<uint8_t> retired;
		withDevice(m_processor.machinedrum(), [&](md::Device* _device)
		{
			return _device && (_device->cancelUserSysexImport(*m_ticket, retired)
				|| _device->retireUserSysexImport(*m_ticket, retired));
		});
	}

	void LibrarySession::playKit(const library::Pack& _pack, const size_t _itemIndex)
	{
		request(_pack, _pack.items.at(_itemIndex), nullptr);
	}

	void LibrarySession::playPattern(const library::Pack& _pack, const size_t _itemIndex)
	{
		const auto& pattern = _pack.items.at(_itemIndex);
		const auto kit = std::find_if(_pack.items.begin(), _pack.items.end(), [&](const auto& _item)
		{
			return _item.kind == md::packlib::ItemKind::Kit && _item.slot == pattern.kitSlot;
		});
		if(kit == _pack.items.end())
		{
			m_status = "KIT " + juce::String(pattern.kitSlot + 1) + " NOT IN THIS PACK";
			return;
		}
		request(_pack, *kit, &pattern);
	}

	void LibrarySession::request(const library::Pack& _pack, const md::packlib::Item& _kit,
		const md::packlib::Item* _pattern)
	{
		Request r;
		r.pack = _pack.name;
		setSamples(r, _pack, _kit.sampleSlots);
		r.dumps = md::packlib::retargetDump(_kit.dump, g_kitSlot);
		if(_pattern)
		{
			const auto pattern = md::packlib::retargetMdPattern(_pattern->dump, g_patternSlot, g_kitSlot);
			r.dumps.insert(r.dumps.end(), pattern.begin(), pattern.end());
			r.load = md::packlib::loadPatternCommand(g_model, g_patternSlot);
			r.loaded = _pattern->name + " LOADED";
		}
		else
		{
			r.load = md::packlib::loadKitCommand(g_model, g_kitSlot);
			r.loaded = "KIT " + juce::String(_kit.name).toUpperCase() + " LOADED";
		}
		m_next = std::move(r);
		if(m_phase == Phase::Idle)
			startNext();
	}

	void LibrarySession::importItems(const library::Pack& _pack, std::vector<size_t> _itemIndices)
	{
		if(_itemIndices.empty())
			return;
		Request r;
		r.pack = _pack.name;
		r.source = std::make_shared<const library::Pack>(_pack);
		r.selected = std::move(_itemIndices);
		m_next = std::move(r);
		if(m_phase == Phase::Idle)
			startNext();
	}

	void LibrarySession::setSamples(Request& _request, const library::Pack& _pack, const std::vector<uint8_t>& _slots)
	{
		for(const auto slot : _slots)
		{
			const auto sample = std::find_if(_pack.samples.begin(), _pack.samples.end(),
				[&](const auto& _s) { return _s.slot == slot; });
			if(sample != _pack.samples.end()) _request.samples.push_back(*sample);
			else _request.missing << (_request.missing.isEmpty() ? "" : " ") << juce::String(slot + 1);
		}
	}

	void LibrarySession::service()
	{
		if(m_phase == Phase::Idle && m_next) startNext();
		else if(m_phase == Phase::Inventory) serviceInventory();
		else if(m_phase == Phase::Samples || m_phase == Phase::Dumps) serviceTransfer();
		else if(m_phase == Phase::Probe || m_phase == Phase::Settle) serviceProbe();
	}

	void LibrarySession::startNext()
	{
		if(m_next)
			m_current = std::move(m_next);
		m_next.reset();
		if(!m_current)
			return;
		if(m_current->source && m_current->dumps.empty())
		{
			startInventory();
			return;
		}
		startCurrent();
	}

	void LibrarySession::startInventory()
	{
		// Undo the audition first, so only what is imported stays.
		if(m_changed)
		{
			m_processor.restoreLibrarySnapshot();
			m_changed = false;
		}
		if(!beginCapture())
		{
			m_next = std::move(m_current);
			m_current.reset();
			m_status = "WAIT";
			return;
		}
		m_machineItems.clear();
		m_inventoryNext = 0;
		m_phase = Phase::Inventory;
		m_probeStarted = now();
		m_probeSent = 0;
		m_status = "READING FREE SLOTS...";
	}

	// One request at a time: the firmware answers each in a few milliseconds,
	// while a burst of requests slows it down more than tenfold.
	void LibrarySession::serviceInventory()
	{
		const auto t = now();
		auto contents = md::packlib::readSyx(m_processor.takeCapturedSysex());
		for(auto& item : contents.items)
		{
			const auto index = item.kind == md::packlib::ItemKind::Kit ? size_t(item.slot) : g_kitCount + item.slot;
			if(index != m_inventoryNext)
				continue;
			item.dump.clear();
			m_machineItems.push_back(std::move(item));
			++m_inventoryNext;
			m_probeSent = 0;
		}
		if(m_inventoryNext == g_kitCount + g_patternCount)
		{
			endCapture();
			planCurrentImport();
			return;
		}
		if(t - m_probeStarted > g_probeTimeoutMs * 1.5)
		{
			endCapture();
			finish("MACHINE NOT RESPONDING");
			return;
		}
		if(m_probeSent == 0 || t - m_probeSent >= g_probeIntervalMs)
		{
			m_probeSent = t;
			sendToMachine(dumpRequest(m_inventoryNext));
		}
	}

	void LibrarySession::planCurrentImport()
	{
		auto& r = *m_current;
		const auto slots = md::packlib::freeSlots(m_machineItems);
		const auto plan = md::packlib::planImport(r.source->items, r.selected, slots.freeKits, slots.freePatterns);
		if(!plan.enoughSlots)
		{
			const auto freeKits = std::count(slots.freeKits.begin(), slots.freeKits.end(), true);
			const auto freePatterns = std::count(slots.freePatterns.begin(), slots.freePatterns.end(), true);
			finish("NOT ENOUGH FREE SLOTS: NEED " + juce::String(int(plan.kitsNeeded)) + " KITS AND "
				+ juce::String(int(plan.patternsNeeded)) + " PATTERNS, FREE " + juce::String(int(freeKits))
				+ " KITS AND " + juce::String(int(freePatterns)) + " PATTERNS");
			return;
		}
		if(plan.dumps.empty())
		{
			finish("NOTHING TO IMPORT: PATTERN KITS NOT IN THIS PACK");
			return;
		}
		setSamples(r, *r.source, plan.sampleSlots);
		r.dumps = plan.dumps;
		r.loaded = "IMPORTED KIT " + slotList(plan.kitSlots, [](uint8_t _s) { return juce::String(_s + 1).paddedLeft('0', 2); });
		if(!plan.patternSlots.empty())
			r.loaded << "  PATTERN " << slotList(plan.patternSlots, patternLabel);
		if(!plan.sampleSlots.empty())
			r.loaded << "  SAMPLES " << slotList(plan.sampleSlots, [](uint8_t _s) { return juce::String(_s + 1); });
		if(plan.skippedPatterns)
			r.loaded << "  (" << int(plan.skippedPatterns) << " PATTERNS WITHOUT KIT SKIPPED)";
		m_phase = Phase::Idle;
		startCurrent();
	}

	void LibrarySession::startCurrent()
	{
		// A restored or rebooted machine is a new one with its own samples.
		if(hardwareEpoch() != m_sentSamplesEpoch)
			m_sentSamples.clear();
		std::vector<uint8_t> samples;
		size_t count = 0;
		for(const auto& sample : m_current->samples)
		{
			const auto sent = m_sentSamples.find(sample.slot);
			if(sent != m_sentSamples.end() && sent->second == m_current->pack) continue;
			samples.insert(samples.end(), sample.bytes.begin(), sample.bytes.end());
			++count;
		}
		if(count)
		{
			if(startTransfer(std::move(samples), true))
			{
				m_phase = Phase::Samples;
				m_status = "SENDING " + juce::String(int(count)) + " SAMPLES";
			}
			return;
		}
		if(startTransfer(m_current->dumps, false))
			m_phase = Phase::Dumps;
	}

	// On failure the request stays current and service() retries it.
	bool LibrarySession::startTransfer(std::vector<uint8_t> _bytes, const bool _samples)
	{
		auto prepared = md::prepareMidiSysexTransfer(std::move(_bytes), g_model);
		if(!prepared)
		{
			finish("INVALID FILE");
			return false;
		}
		using Start = md::SysexImportStartResult;
		const auto result = withDevice(m_processor.machinedrum(), [&](md::Device* _device) -> std::optional<Start>
		{
			if(!_device) return std::nullopt;
			const auto ticket = _device->beginUserSysexImport();
			if(!ticket) return Start::Busy;
			const auto started = _device->startUserSysexImport(*ticket, *prepared, _samples);
			if(started == Start::Started) m_ticket = ticket;
			else
			{
				std::vector<uint8_t> retired;
				_device->cancelUserSysexImport(*ticket, retired);
			}
			return started;
		});
		if(result == Start::Started)
		{
			m_changed = true;
			return true;
		}
		// Retry later; an import that already started is not given up for an audition.
		if(!m_next || m_current->source)
			m_next = std::move(m_current);
		m_current.reset();
		m_status = result == Start::NotReady || result == Start::Initializing || result == Start::Restoring
			? "WAIT: MACHINE IS BOOTING" : "WAIT";
		if(!result) finish("MACHINE NOT AVAILABLE");
		return false;
	}

	void LibrarySession::serviceTransfer()
	{
		const auto progress = withDevice(m_processor.machinedrum(), [](md::Device* _device)
		{
			return _device ? std::optional(_device->userSysexImportProgress()) : std::nullopt;
		});
		if(!progress || progress->ticket != *m_ticket || progress->stage == md::SysexImportStage::Invalidated)
		{
			m_ticket.reset();
			finish("MACHINE RESTARTED, TRY AGAIN");
			return;
		}
		using Stage = md::SysexImportStage;
		if(m_phase == Phase::Samples && progress->state == md::MidiSysexTransferState::Sending && progress->total)
			m_status = "SENDING SAMPLES " + juce::String(int(progress->sent * 100 / progress->total)) + "%";
		if(progress->stage != Stage::DeliveredUnverified && progress->stage != Stage::Failed
			&& progress->stage != Stage::Cancelled)
			return;

		std::vector<uint8_t> retired;
		withDevice(m_processor.machinedrum(), [&](md::Device* _device)
		{
			return _device && _device->retireUserSysexImport(*m_ticket, retired);
		});
		m_ticket.reset();
		if(progress->stage != Stage::DeliveredUnverified)
		{
			finish(progress->stage == Stage::Failed ? "SEND FAILED" : "SEND CANCELLED");
			return;
		}
		if(m_phase == Phase::Samples)
		{
			if(hardwareEpoch() != m_sentSamplesEpoch)
				m_sentSamples.clear();
			m_sentSamplesEpoch = hardwareEpoch();
			for(const auto& sample : m_current->samples)
				m_sentSamples[sample.slot] = m_current->pack;
			startProbe(Phase::Probe);
			m_status = "MACHINE IS STORING SAMPLES...";
			return;
		}
		if(m_current->source)
		{
			// The last pattern is stored once the firmware answers again.
			startProbe(Phase::Settle);
			m_status = "STORING...";
			return;
		}
		sendToMachine(m_current->load);
		finish(m_current->missing.isEmpty() ? m_current->loaded
			: m_current->loaded + "  MISSING SAMPLES: " + m_current->missing);
	}

	void LibrarySession::startProbe(const Phase _phase)
	{
		m_phase = _phase;
		m_probeStarted = now();
		m_probeSent = 0;
	}

	void LibrarySession::serviceProbe()
	{
		const auto t = now();
		if(m_ownsCapture && !m_processor.takeCapturedSysex().empty())
		{
			endCapture();
			if(m_phase == Phase::Settle)
			{
				m_processor.commitLibrarySnapshot();
				m_changed = false;
				finish(m_current->missing.isEmpty() ? m_current->loaded
					: m_current->loaded + "  MISSING SAMPLES: " + m_current->missing);
				return;
			}
			m_phase = Phase::Idle;
			// A new audition replaces an audition, never an import half done.
			if(m_next && !m_current->source) startNext();
			else if(startTransfer(m_current->dumps, false))
			{
				m_phase = Phase::Dumps;
				m_status = "SENDING KITS AND PATTERNS...";
			}
			return;
		}
		if(t - m_probeStarted > g_probeTimeoutMs)
		{
			endCapture();
			finish("MACHINE NOT RESPONDING");
			return;
		}
		if(t - m_probeSent < g_probeIntervalMs)
			return;
		m_probeSent = t;
		if(beginCapture())
			sendToMachine(g_probe);
	}

	// Only our own capture is read and ended, never a SysEx export in progress.
	bool LibrarySession::beginCapture()
	{
		if(!m_ownsCapture)
			m_ownsCapture = m_processor.beginSysexCapture(g_model, false);
		return m_ownsCapture;
	}

	void LibrarySession::endCapture()
	{
		if(m_ownsCapture)
			(void)m_processor.endSysexCapture();
		m_ownsCapture = false;
	}

	void LibrarySession::sendToMachine(const std::vector<uint8_t>& _sysex)
	{
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		event.sysex = _sysex;
		m_processor.machinedrum().addMidiEvent(event);
	}

	uint64_t LibrarySession::hardwareEpoch()
	{
		return withDevice(m_processor.machinedrum(), [](md::Device* _device)
		{
			return _device ? _device->hardwareEpoch() : uint64_t(0);
		});
	}

	void LibrarySession::finish(const juce::String& _status)
	{
		m_status = _status;
		m_phase = Phase::Idle;
		m_current.reset();
	}
}
