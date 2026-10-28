#include "mdLibrarySession.h"

#include "mdCombinedProcessor.h"

#include "mdLib/mddevice.h"
#include "mdLib/mdsysextransfer.h"

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
		if(m_phase == Phase::Probe)
			(void)m_processor.endSysexCapture();
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
			m_status = "KIT " + juce::String(pattern.kitSlot + 1) + " NON TROVATO NEL PACK";
			return;
		}
		request(_pack, *kit, &pattern);
	}

	void LibrarySession::request(const library::Pack& _pack, const md::packlib::Item& _kit,
		const md::packlib::Item* _pattern)
	{
		Request r;
		r.pack = _pack.name;
		for(const auto slot : _kit.sampleSlots)
		{
			const auto sample = std::find_if(_pack.samples.begin(), _pack.samples.end(),
				[&](const auto& _s) { return _s.slot == slot; });
			if(sample != _pack.samples.end()) r.samples.push_back(*sample);
			else r.missing << (r.missing.isEmpty() ? "" : " ") << juce::String(slot + 1);
		}
		r.dumps = md::packlib::retargetDump(_kit.dump, g_kitSlot);
		if(_pattern)
		{
			const auto pattern = md::packlib::retargetMdPattern(_pattern->dump, g_patternSlot, g_kitSlot);
			r.dumps.insert(r.dumps.end(), pattern.begin(), pattern.end());
			r.load = md::packlib::loadPatternCommand(g_model, g_patternSlot);
			r.loaded = _pattern->name + " CARICATO";
		}
		else
		{
			r.load = md::packlib::loadKitCommand(g_model, g_kitSlot);
			r.loaded = "KIT " + juce::String(_kit.name).toUpperCase() + " CARICATO";
		}
		m_next = std::move(r);
		if(m_phase == Phase::Idle)
			startNext();
	}

	void LibrarySession::service()
	{
		if(m_phase == Phase::Idle && m_next) startNext();
		else if(m_phase == Phase::Samples || m_phase == Phase::Dumps) serviceTransfer();
		else if(m_phase == Phase::Probe) serviceProbe();
	}

	void LibrarySession::startNext()
	{
		if(m_next)
			m_current = std::move(m_next);
		m_next.reset();
		if(!m_current)
			return;
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
				m_status = "INVIO " + juce::String(int(count)) + " CAMPIONI";
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
			finish("FILE NON VALIDO");
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
		m_next = std::move(m_current);
		m_current.reset();
		m_status = result == Start::NotReady || result == Start::Initializing || result == Start::Restoring
			? "ATTENDI: LA MACCHINA SI STA AVVIANDO" : "ATTENDI";
		if(!result) finish("MACCHINA NON DISPONIBILE");
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
			finish("MACCHINA RIAVVIATA, RIPROVA");
			return;
		}
		using Stage = md::SysexImportStage;
		if(m_phase == Phase::Samples && progress->state == md::MidiSysexTransferState::Sending && progress->total)
			m_status = "INVIO CAMPIONI " + juce::String(int(progress->sent * 100 / progress->total)) + "%";
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
			finish(progress->stage == Stage::Failed ? "INVIO FALLITO" : "INVIO ANNULLATO");
			return;
		}
		if(m_phase == Phase::Samples)
		{
			if(hardwareEpoch() != m_sentSamplesEpoch)
				m_sentSamples.clear();
			m_sentSamplesEpoch = hardwareEpoch();
			for(const auto& sample : m_current->samples)
				m_sentSamples[sample.slot] = m_current->pack;
			m_phase = Phase::Probe;
			m_probeStarted = now();
			m_probeSent = 0;
			m_status = "LA MACCHINA SALVA I CAMPIONI...";
			return;
		}
		sendToMachine(m_current->load);
		finish(m_current->missing.isEmpty() ? m_current->loaded
			: m_current->loaded + "  CAMPIONI MANCANTI: " + m_current->missing);
	}

	void LibrarySession::serviceProbe()
	{
		const auto t = now();
		if(m_processor.isSysexCapturing() && m_processor.hasCapturedSysex())
		{
			(void)m_processor.endSysexCapture();
			m_phase = Phase::Idle;
			if(m_next) startNext();
			else if(startTransfer(m_current->dumps, false)) m_phase = Phase::Dumps;
			return;
		}
		if(t - m_probeStarted > g_probeTimeoutMs)
		{
			(void)m_processor.endSysexCapture();
			finish("LA MACCHINA NON RISPONDE");
			return;
		}
		if(t - m_probeSent < g_probeIntervalMs)
			return;
		m_probeSent = t;
		if(m_processor.isSysexCapturing() || m_processor.beginSysexCapture(g_model))
			sendToMachine(g_probe);
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
