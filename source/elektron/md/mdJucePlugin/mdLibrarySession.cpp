#include "mdLibrarySession.h"

#include "mdCombinedProcessor.h"

#include "mdLib/mddevice.h"
#include "mdLib/mdsysextransfer.h"

#include <functional>

namespace mdJucePlugin
{
	namespace
	{
		constexpr double g_probeIntervalMs = 1000.0;
		constexpr double g_probeTimeoutMs = 60000.0;
		constexpr size_t g_patternCount = 128;
		constexpr uint8_t g_patternSlot = 127;
		// The Monomachine stores the last dump while still in SYSEX RECEIVE.
		constexpr double g_receiveSettleMs = 1000.0;
		constexpr double g_verifyTimeoutMs = 10000.0;

		// The last message of a run of dumps.
		std::vector<uint8_t> lastDump(const std::vector<uint8_t>& _dumps)
		{
			const auto start = std::find(_dumps.rbegin(), _dumps.rend(), uint8_t{0xf0});
			return start == _dumps.rend() ? std::vector<uint8_t>{} : std::vector<uint8_t>(start.base() - 1, _dumps.end());
		}

		std::vector<uint8_t> dumpRequest(const md::MachineModel _model, const uint8_t _command, const uint8_t _slot)
		{
			const uint8_t product = _model == md::MachineModel::Monomachine ? 0x03 : 0x02;
			return {0xf0, 0x00, 0x20, 0x3c, product, 0x00, _command, _slot, 0xf7};
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

	LibrarySession::LibrarySession(CombinedProcessor& _processor, const md::MachineModel _model)
		: m_processor(_processor), m_model(_model) {}

	LibrarySession::~LibrarySession()
	{
		endCapture();
		// Never leave a key held down by a key sequence cut short.
		if(m_phase == Phase::Panel)
		{
			std::map<uint8_t, uint8_t> held;
			for(size_t i = 0; i < m_panelNext; ++i) held[m_panel[i].row] = m_panel[i].mask;
			for(const auto& [row, mask] : held)
				if(mask) (void)machine().sendPanelEvent(row, 0);
		}
		if(!m_ticket)
			return;
		std::vector<uint8_t> retired;
		withDevice(machine(), [&](md::Device* _device)
		{
			return _device && (_device->cancelUserSysexImport(*m_ticket, retired)
				|| _device->retireUserSysexImport(*m_ticket, retired));
		});
	}

	void LibrarySession::playKit(const library::Pack& _pack, const size_t _itemIndex)
	{
		if(const auto* kit = kitOf(_pack, _pack.items.at(_itemIndex)))
			request(_pack, *kit, nullptr);
	}

	void LibrarySession::playPattern(const library::Pack& _pack, const size_t _itemIndex)
	{
		const auto& pattern = _pack.items.at(_itemIndex);
		if(const auto* kit = kitOf(_pack, pattern))
			request(_pack, *kit, &pattern);
	}

	// A kit is its own kit; a pattern's kit must be in the same pack.
	const md::packlib::Item* LibrarySession::kitOf(const library::Pack& _pack, const md::packlib::Item& _item)
	{
		if(_item.kind == md::packlib::ItemKind::Kit)
			return &_item;
		for(const auto& kit : _pack.items)
			if(kit.kind == md::packlib::ItemKind::Kit && kit.slot == _item.kitSlot)
				return &kit;
		m_status = "KIT " + juce::String(_item.kitSlot + 1) + " NOT IN THIS PACK";
		return nullptr;
	}

	void LibrarySession::request(const library::Pack& _pack, const md::packlib::Item& _kit,
		const md::packlib::Item* _pattern)
	{
		Request r;
		r.pack = _pack.name;
		setSamples(r, _pack, _kit.sampleSlots);
		r.dumps = md::packlib::retargetDump(_kit.dump, auditionKitSlot());
		if(_pattern)
		{
			const auto pattern = md::packlib::retargetPattern(*_pattern, g_patternSlot, auditionKitSlot());
			r.dumps.insert(r.dumps.end(), pattern.begin(), pattern.end());
			r.load = md::packlib::loadPatternCommand(m_model, g_patternSlot);
			r.loaded = _pattern->name + " LOADED";
		}
		else
		{
			r.load = md::packlib::loadKitCommand(m_model, auditionKitSlot());
			r.loaded = "KIT " + juce::String(_kit.name).toUpperCase() + " LOADED";
		}
		r.verify = lastDump(r.dumps);
		m_next = std::move(r);
		if(m_phase == Phase::Idle)
			startNext();
	}

	void LibrarySession::importItems(const library::Pack& _pack, std::vector<size_t> _itemIndices)
	{
		if(_itemIndices.empty())
			return;
		// Kits are known from the pack alone: no need to read the machine first.
		const auto all = std::vector<bool>(128, true);
		if(md::packlib::planImport(_pack.items, _itemIndices, all, all).dumps.empty())
		{
			m_status = "NOTHING TO IMPORT: PATTERN KITS NOT IN THIS PACK";
			return;
		}
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
		else if(m_phase == Phase::Panel) servicePanel();
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
			m_processor.restoreLibrarySnapshot(m_model);
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
			const auto index = item.kind == md::packlib::ItemKind::Kit ? size_t(item.slot) : kitCount() + item.slot;
			if(index != m_inventoryNext)
				continue;
			item.dump.clear();
			m_machineItems.push_back(std::move(item));
			++m_inventoryNext;
			m_probeSent = 0;
			m_probeStarted = t;
		}
		if(m_inventoryNext == kitCount() + g_patternCount)
		{
			endCapture();
			planCurrentImport();
			return;
		}
		if(t - m_probeStarted > g_verifyTimeoutMs)
		{
			endCapture();
			finish("MACHINE NOT RESPONDING");
			return;
		}
		if(m_probeSent == 0 || t - m_probeSent >= g_probeIntervalMs)
		{
			m_probeSent = t;
			sendToMachine(m_inventoryNext < kitCount() ? dumpRequest(m_model, 0x53, uint8_t(m_inventoryNext))
				: dumpRequest(m_model, 0x68, uint8_t(m_inventoryNext - kitCount())));
		}
	}

	void LibrarySession::planCurrentImport()
	{
		auto& r = *m_current;
		auto slots = md::packlib::freeSlots(m_machineItems);
		// Auditions keep writing there while the library is open.
		slots.freeKits[auditionKitSlot()] = slots.freePatterns[g_patternSlot] = false;
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
		setSamples(r, *r.source, plan.sampleSlots);
		r.dumps = plan.dumps;
		r.verify = lastDump(r.dumps);
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
		sendDumps();
	}

	void LibrarySession::sendDumps()
	{
		m_phase = Phase::Idle;
		if(m_model == md::MachineModel::Machinedrum)
		{
			if(startTransfer(m_current->dumps, false))
				m_phase = Phase::Dumps;
			return;
		}
		if(!md::prepareMidiSysexTransfer(m_current->dumps, m_model))
			return finish("INVALID FILE");
		// From here on the machine leaves its main screen; closing the library
		// restores it even if nothing gets stored.
		m_changed = true;
		m_inReceive = true;
		m_status = "OPENING SYSEX RECEIVE...";
		runPanel(md::mmSysexReceiveScript(), 0, [this]
		{
			if(startTransfer(m_current->dumps, true))
			{
				m_phase = Phase::Dumps;
				m_status = "SENDING KITS AND PATTERNS...";
			}
		});
	}

	// The last dump is read back before anything is reported: the machine may
	// have ignored the data (an MM outside SYSEX RECEIVE) or still be storing it.
	void LibrarySession::afterDumps()
	{
		m_inReceive = false;
		startProbe(Phase::Settle);
		m_status = "VERIFYING...";
	}

	void LibrarySession::verified()
	{
		if(m_current->source)
		{
			m_processor.commitLibrarySnapshot(m_model);
			m_changed = false;
		}
		else
			sendToMachine(m_current->load);
		finish(m_current->missing.isEmpty() ? m_current->loaded
			: m_current->loaded + "  MISSING SAMPLES: " + m_current->missing);
	}

	// A failure while the MM sits in SYSEX RECEIVE leaves that screen first.
	void LibrarySession::fail(const juce::String& _status)
	{
		if(!m_inReceive)
			return finish(_status);
		m_inReceive = false;
		runPanel(md::exitMenusScript(m_model), 0, [this, _status] { finish(_status); });
	}

	void LibrarySession::runPanel(std::vector<md::PanelStep> _script, const double _delayMs, std::function<void()> _then)
	{
		m_panel = std::move(_script);
		m_panelNext = 0;
		m_panelAt = now() + _delayMs;
		m_afterPanel = std::move(_then);
		m_phase = Phase::Panel;
	}

	// Plays the key sequence in real time; the machine runs in real time too.
	void LibrarySession::servicePanel()
	{
		while(m_panelNext < m_panel.size() && now() >= m_panelAt)
		{
			const auto& step = m_panel[m_panelNext];
			if(!machine().sendPanelEvent(step.row, step.mask))
				return;
			m_panelAt = now() + step.waitFrames * 1000.0 / md::g_samplerate;
			++m_panelNext;
		}
		if(m_panelNext < m_panel.size() || now() < m_panelAt)
			return;
		m_phase = Phase::Idle;
		const auto then = std::move(m_afterPanel);
		then();
	}

	// A busy or booting machine gets the request back in the queue for service() to retry.
	bool LibrarySession::startTransfer(std::vector<uint8_t> _bytes, const bool _receiveModeConfirmed)
	{
		auto prepared = md::prepareMidiSysexTransfer(std::move(_bytes), m_model);
		if(!prepared)
		{
			finish("INVALID FILE");
			return false;
		}
		using Start = md::SysexImportStartResult;
		const auto result = withDevice(machine(), [&](md::Device* _device) -> std::optional<Start>
		{
			if(!_device) return std::nullopt;
			const auto ticket = _device->beginUserSysexImport();
			if(!ticket) return Start::Busy;
			const auto started = _device->startUserSysexImport(*ticket, *prepared, _receiveModeConfirmed);
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
		if(!result)
		{
			finish("MACHINE NOT AVAILABLE");
			return false;
		}
		// Retry on the next tick; an import that already started is not given up for an audition.
		if(!m_next || m_current->source)
			m_next = std::move(m_current);
		m_current.reset();
		m_status = result == Start::NotReady || result == Start::Initializing || result == Start::Restoring
			? "WAIT: MACHINE IS BOOTING" : "WAIT";
		return false;
	}

	void LibrarySession::serviceTransfer()
	{
		const auto progress = withDevice(machine(), [](md::Device* _device)
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
		withDevice(machine(), [&](md::Device* _device)
		{
			return _device && _device->retireUserSysexImport(*m_ticket, retired);
		});
		m_ticket.reset();
		if(progress->stage != Stage::DeliveredUnverified)
		{
			fail(progress->stage == Stage::Failed ? "SEND FAILED" : "SEND CANCELLED");
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
		if(m_model == md::MachineModel::Monomachine)
			runPanel(md::exitMenusScript(m_model), g_receiveSettleMs, [this] { afterDumps(); });
		else
			afterDumps();
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
		if(m_ownsCapture)
		{
			const auto reply = m_processor.takeCapturedSysex();
			if(m_phase == Phase::Probe && !reply.empty())
			{
				endCapture();
				m_phase = Phase::Idle;
				// A new audition replaces an audition, never an import half done.
				if(m_next && !m_current->source) startNext();
				else
				{
					m_status = "SENDING KITS AND PATTERNS...";
					sendDumps();
				}
				return;
			}
			if(m_phase == Phase::Settle)
			{
				const auto sent = md::packlib::readSyx(m_current->verify).items;
				for(const auto& item : md::packlib::readSyx(reply).items)
					if(!sent.empty() && md::packlib::sameContent(sent.front(), item))
					{
						endCapture();
						return verified();
					}
			}
		}
		else
			m_probeStarted = t;   // waiting for the other machine's capture is not the machine's fault
		if(t - m_probeStarted > (m_phase == Phase::Settle ? g_verifyTimeoutMs : g_probeTimeoutMs))
		{
			endCapture();
			finish(m_phase == Phase::Probe ? "MACHINE NOT RESPONDING"
				: m_model == md::MachineModel::Monomachine
					? "MM DID NOT RECEIVE: OPEN GLOBAL > FILE > SYSEX RECEIVE AND TRY AGAIN"
					: "MACHINE DID NOT STORE THE DATA, TRY AGAIN");
			return;
		}
		if(t - m_probeSent < g_probeIntervalMs)
			return;
		m_probeSent = t;
		if(!beginCapture())
			return;
		// After samples the MD is busy CLEANING/LOADING and ignores other data;
		// a kit request is answered only once the firmware is idle again. The
		// verification asks for the slot the last dump went to.
		const auto& last = m_current->verify;
		if(m_phase == Phase::Settle && last.size() > 9)
			sendToMachine(dumpRequest(m_model, uint8_t(last[6] + 1), last[9]));
		else
			sendToMachine(dumpRequest(m_model, 0x53, 0));
	}

	// Only our own capture is read and ended, never a SysEx export in progress.
	bool LibrarySession::beginCapture()
	{
		if(!m_ownsCapture)
			m_ownsCapture = m_processor.beginSysexCapture(m_model, false);
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
		machine().addMidiEvent(event);
	}

	uint64_t LibrarySession::hardwareEpoch()
	{
		return withDevice(machine(), [](md::Device* _device)
		{
			return _device ? _device->hardwareEpoch() : uint64_t(0);
		});
	}

	AudioPluginAudioProcessor& LibrarySession::machine()
	{
		return m_processor.machine(m_model);
	}

	size_t LibrarySession::kitCount() const
	{
		return m_model == md::MachineModel::Monomachine ? 128 : 64;
	}

	void LibrarySession::finish(const juce::String& _status)
	{
		m_status = _status;
		m_phase = Phase::Idle;
		m_current.reset();
	}
}
