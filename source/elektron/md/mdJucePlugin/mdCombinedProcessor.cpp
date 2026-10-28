#include "mdCombinedProcessor.h"

#include "mdCombinedEditor.h"
#include "mdLib/mddevice.h"
#include "mdLib/mdsysexfile.h"

#include "dsp56kBase/threadtools.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>

namespace mdJucePlugin
{
	namespace
	{
		constexpr uint32_t g_stateMagic = 0x4d444d4d;
		constexpr int g_stateVersion = 1;

		// Each machine renders the hardware's six output jacks, A to F.
		constexpr int g_machineChannels = 6;

		// Host buses after the mix, mirroring each machine's routing: the MD
		// sends a track to the A/B main or to any single jack, while the MM
		// routes tracks to stereo buses A/B, C/D and E/F.
		struct MachineOutput
		{
			const char* name;
			bool monomachine;
			int firstChannel;
			int channels;
		};
		constexpr std::array<MachineOutput, 10> g_machineOutputs
		{{
			{ "Machinedrum Main", false, 0, 2 },
			{ "Machinedrum A", false, 0, 1 },
			{ "Machinedrum B", false, 1, 1 },
			{ "Machinedrum C", false, 2, 1 },
			{ "Machinedrum D", false, 3, 1 },
			{ "Machinedrum E", false, 4, 1 },
			{ "Machinedrum F", false, 5, 1 },
			{ "Monomachine Main", true, 0, 2 },
			{ "Monomachine C-D", true, 2, 2 },
			{ "Monomachine E-F", true, 4, 2 },
		}};

		juce::AudioChannelSet channelSet(const int _channels)
		{
			return _channels == 1 ? juce::AudioChannelSet::mono()
				: juce::AudioChannelSet::stereo();
		}


		void prepareChild(juce::AudioProcessor& _processor,
			const double _sampleRate, const int _blockSize)
		{
			juce::AudioProcessor::BusesLayout layout;
			layout.inputBuses.add(juce::AudioChannelSet::stereo());
			for(int bus = 0; bus < g_machineChannels / 2; ++bus)
				layout.outputBuses.add(juce::AudioChannelSet::stereo());
			_processor.setBusesLayout(layout);
			_processor.setRateAndBufferSizeDetails(_sampleRate, _blockSize);
			_processor.prepareToPlay(_sampleRate, _blockSize);
		}
	}

	juce::AudioProcessor::BusesProperties CombinedProcessor::combinedBuses()
	{
		auto buses = BusesProperties()
			.withInput("Input A/B", juce::AudioChannelSet::stereo(), true)
			.withOutput("Mix", juce::AudioChannelSet::stereo(), true);
		for(const auto& output : g_machineOutputs)
			buses = buses.withOutput(output.name, channelSet(output.channels), false);
		return buses;
	}

	CombinedProcessor::CombinedProcessor()
		: AudioProcessor(combinedBuses())
		, m_machinedrum(md::MachineModel::Machinedrum, false)
		, m_monomachine(md::MachineModel::Monomachine, false)
		, m_maschine(m_machinedrum, m_monomachine)
	{
		// Each child turns host tempo and transport into MIDI clock for its
		// machine; the firmware's global sync settings decide whether to follow.
		// Clock keeps running while the host is stopped, so tempo always follows.
		for(auto* child : {&m_machinedrum, &m_monomachine})
		{
			child->setPlayHead(&m_blockPlayHead);
			child->getPlugin().setMidiClockRunWhileStopped(true);
		}
		for(auto* engine : {&m_md, &m_mm})
			engine->thread = std::thread([this, engine] { runWorker(*engine); });
	}

	CombinedProcessor::~CombinedProcessor()
	{
		if(isSysexCapturing())
			(void)endSysexCapture();
		stopFastBootWorkers();
		m_stoppingWorker.store(true, std::memory_order_release);
		for(auto* engine : {&m_md, &m_mm})
		{
			engine->ready.signal();
			if(engine->thread.joinable())
				engine->thread.join();
		}
	}

	bool CombinedProcessor::beginSysexCapture(const md::MachineModel _model)
	{
		if(isSysexCapturing())
			return false;
		{
			std::lock_guard lock(m_captureMutex);
			m_captureBytes.clear();
			m_captureBytes.reserve(md::g_midiSysexTransferMaxBytes);
			m_captureIncomplete.store(false, std::memory_order_relaxed);
			m_captureModel.store(_model, std::memory_order_release);
		}
		auto& machine = _model == md::MachineModel::Machinedrum
			? m_machinedrum : m_monomachine;
		auto& routing = machine.getMidiRoutingMatrix();
		using Source = synthLib::MidiEventSource;
		using Type = synthLib::MidiRoutingMatrix::EventType;
		m_capturePreviousHostRoute = routing.enabled(Source::Device,
			Source::Host, Type::SysEx);
		m_captureEnabled.store(true, std::memory_order_release);
		routing.setEnabled(Source::Device, Source::Host, Type::SysEx, true);
		return true;
	}

	std::optional<CombinedProcessor::SysexCapture>
	CombinedProcessor::endSysexCapture()
	{
		if(!m_captureEnabled.exchange(false, std::memory_order_acq_rel))
			return std::nullopt;
		const auto model = m_captureModel.load(std::memory_order_acquire);
		auto& machine = model == md::MachineModel::Machinedrum
			? m_machinedrum : m_monomachine;
		using Source = synthLib::MidiEventSource;
		using Type = synthLib::MidiRoutingMatrix::EventType;
		machine.getMidiRoutingMatrix().setEnabled(Source::Device,
			Source::Host, Type::SysEx, m_capturePreviousHostRoute);
		std::lock_guard lock(m_captureMutex);
		return SysexCapture{model, std::move(m_captureBytes),
			m_captureIncomplete.load(std::memory_order_relaxed)};
	}

	void CombinedProcessor::captureSysex(const juce::MidiBuffer& _midi,
		const md::MachineModel _model)
	{
		if(!m_captureEnabled.load(std::memory_order_acquire)
			|| m_captureModel.load(std::memory_order_acquire) != _model)
			return;
		for(const auto event : _midi)
		{
			const auto* bytes = event.data;
			const auto size = static_cast<size_t>(event.numBytes);
			if(size < 9 || bytes[0] != 0xf0 || bytes[size - 1] != 0xf7)
				continue;
			// Only user dumps and sample dumps belong in an exported file.
			const auto elektronHeader = bytes[1] == 0x00
				&& bytes[2] == 0x20 && bytes[3] == 0x3c
				&& bytes[4] == (_model == md::MachineModel::Machinedrum ? 2 : 3);
			const auto elektron = elektronHeader
				&& ((size >= 15 && (bytes[6] == 0x50 || bytes[6] == 0x52
					|| bytes[6] == 0x67 || bytes[6] == 0x69
					|| bytes[6] == 0x5d))
					|| (size == 13 && bytes[6] == 0x73));
			const auto sample = _model == md::MachineModel::Machinedrum
				&& bytes[1] == 0x7e && (bytes[3] == 1 || bytes[3] == 2);
			if(!elektron && !sample)
				continue;
			std::unique_lock lock(m_captureMutex, std::try_to_lock);
			if(!lock.owns_lock())
			{
				if(m_captureEnabled.load(std::memory_order_relaxed))
					m_captureIncomplete.store(true, std::memory_order_relaxed);
				continue;
			}
			if(!m_captureEnabled.load(std::memory_order_relaxed))
				return;
			if(size > md::g_midiSysexTransferMaxBytes - m_captureBytes.size())
			{
				m_captureIncomplete.store(true, std::memory_order_relaxed);
				continue;
			}
			m_captureBytes.insert(m_captureBytes.end(), bytes, bytes + size);
		}
	}

	void CombinedProcessor::stopFastBootWorkers()
	{
		m_stopFastBoot.store(true, std::memory_order_release);
		if(m_mdFastBootWorker.joinable())
			m_mdFastBootWorker.join();
		if(m_mmFastBootWorker.joinable())
			m_mmFastBootWorker.join();
		m_mdFastBootActive.store(false, std::memory_order_release);
		m_mmFastBootActive.store(false, std::memory_order_release);
	}

	void CombinedProcessor::runFastBoot(AudioPluginAudioProcessor& _processor,
		std::atomic<bool>& _active)
	{
		constexpr uint32_t chunkFrames = 256;
		constexpr uint32_t maximumFrames = md::g_samplerate * 25;
		constexpr uint32_t stableFrames = md::g_samplerate * 3 / 2;
		auto& plugin = _processor.getPlugin();
		const auto started = std::chrono::steady_clock::now();
		uint32_t frames = 0;
		uint32_t readySince = 0;
		bool finished = false;
		bool active = false;

		while(!m_stopFastBoot.load(std::memory_order_acquire)
			&& std::chrono::steady_clock::now() - started < std::chrono::seconds(30)
			&& frames < maximumFrames && !finished)
		{
			// A restored project may still be booting its replacement hardware.
			// Let its normal audio path finish before taking ownership of boot.
			const auto state = plugin.tryWithDeviceLocked(
				[](synthLib::Device* const _device)
				{
					const auto* const device = dynamic_cast<md::Device*>(_device);
					if(!device || !device->isValid()) return -1;
					if(device->isProjectStateRestorePending()) return 0;
					if(device->getHardware().isFactoryFlashInitializationExpected())
						return -1;
					if(device->getHardware().isFirmwareMidiReady()
						&& device->getFrontPanelSnapshot().countLitPixels() >= 2000)
						return 2;
					return 1;
				});
			if(!state || *state == 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				continue;
			}
			if(*state < 0)
				break;
			if(*state == 2 && !active)
				break;

			if(!active)
			{
				_active.store(true, std::memory_order_release);
				active = true;
			}

			const auto advanced = plugin.withDeviceLocked(
				[&](synthLib::Device* const _device)
				{
					auto* const device = dynamic_cast<md::Device*>(_device);
					if(!device || device->isProjectStateRestorePending())
						return false;
					device->getHardware().advance(chunkFrames);
					return true;
				});
			if(!advanced)
				break;
			frames += chunkFrames;

			if((frames % 2048) != 0)
				continue;
			finished = plugin.withDeviceLocked(
				[&](synthLib::Device* const _device)
				{
					const auto* const device = dynamic_cast<md::Device*>(_device);
					if(!device) return false;
					const auto& hardware = device->getHardware();
					const bool ready = hardware.isFirmwareMidiReady()
						&& hardware.getFrontPanelSnapshot().countLitPixels() >= 2000;
					if(!ready)
						readySince = 0;
					else if(readySince == 0)
						readySince = frames;
					return readySince != 0 && frames - readySince >= stableFrames;
				});
		}

		_active.store(false, std::memory_order_release);
		if(active)
		{
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count();
			std::fprintf(stderr, "[MD-MM] %s fast boot: %u frames in %lld ms (%s)\n",
				_processor.getModel() == md::MachineModel::Machinedrum ? "MD" : "MM",
				frames, static_cast<long long>(elapsed), finished ? "ready" : "fallback");
		}
	}

	void CombinedProcessor::runWorker(Engine& _engine)
	{
		dsp56k::ThreadTools::setCurrentThreadPriority(
			dsp56k::ThreadPriority::Highest);
		for(;;)
		{
			_engine.ready.wait();
			if(m_stoppingWorker.load(std::memory_order_acquire))
				return;
			static_cast<juce::AudioProcessor&>(_engine.processor).processBlock(
				_engine.audio, _engine.midi);
			_engine.finished.signal();
		}
	}

	// Wait for the block started by the previous callback and queue its audio
	// and MIDI for output. The workers are idle when this returns.
	void CombinedProcessor::finishPendingBlock()
	{
		if(m_pendingSamples == 0)
			return;
		for(auto* engine : {&m_md, &m_mm})
		{
			if(engine->rendering)
				engine->finished.wait();
			engine->rendering = false;
		}
		captureSysex(m_md.midi, md::MachineModel::Machinedrum);
		captureSysex(m_mm.midi, md::MachineModel::Monomachine);

		const auto capacity = m_md.delay.getNumSamples();
		const auto first = std::min(m_pendingSamples, capacity - m_delayWrite);
		for(auto* engine : {&m_md, &m_mm})
		{
			for(int channel = 0; channel < g_machineChannels; ++channel)
			{
				engine->delay.copyFrom(channel, m_delayWrite, engine->audio, channel, 0, first);
				engine->delay.copyFrom(channel, 0, engine->audio, channel, first,
					m_pendingSamples - first);
			}
			m_pendingMidi.addEvents(engine->midi, 0, m_pendingSamples, 0);
		}
		m_delayWrite = (m_delayWrite + m_pendingSamples) % capacity;
		m_pendingSamples = 0;
	}

	bool CombinedProcessor::isBusesLayoutSupported(
		const BusesLayout& _layouts) const
	{
		for(size_t i = 0; i < g_machineOutputs.size(); ++i)
		{
			const auto output = _layouts.getChannelSet(false, static_cast<int>(i) + 1);
			if(!output.isDisabled() && output != channelSet(g_machineOutputs[i].channels))
				return false;
		}
		return _layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
			&& (_layouts.getMainInputChannelSet().isDisabled()
				|| _layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo());
	}

	void CombinedProcessor::prepareToPlay(const double _sampleRate,
		const int _maximumBlockSize)
	{
		stopFastBootWorkers();
		finishPendingBlock();
		m_maximumBlockSize = std::max(1, _maximumBlockSize);
		for(auto* engine : {&m_md, &m_mm})
		{
			engine->audio.setSize(g_machineChannels, m_maximumBlockSize, false, true);
			engine->midi.ensureSize(4096);
			engine->delay.setSize(g_machineChannels, 2 * m_maximumBlockSize, false, true);
			engine->delay.clear();
		}
		m_pendingMidi.ensureSize(8192);
		m_pendingMidi.clear();
		m_delayRead = 0;
		m_delayWrite = m_maximumBlockSize;
		m_midiRouter.reset();
		prepareChild(m_machinedrum, _sampleRate, m_maximumBlockSize);
		prepareChild(m_monomachine, _sampleRate, m_maximumBlockSize);
		setLatencySamples(m_maximumBlockSize + std::max(m_machinedrum.getLatencySamples(),
			m_monomachine.getLatencySamples()));
		if(juce::JUCEApplicationBase::isStandaloneApp())
		{
			m_stopFastBoot.store(false, std::memory_order_release);
			m_mdFastBootWorker = std::thread([this]
				{ runFastBoot(m_machinedrum, m_mdFastBootActive); });
			m_mmFastBootWorker = std::thread([this]
				{ runFastBoot(m_monomachine, m_mmFastBootActive); });
		}
	}

	void CombinedProcessor::releaseResources()
	{
		stopFastBootWorkers();
		finishPendingBlock();
		static_cast<juce::AudioProcessor&>(m_machinedrum).releaseResources();
		static_cast<juce::AudioProcessor&>(m_monomachine).releaseResources();
	}

	void CombinedProcessor::processBlock(juce::AudioBuffer<float>& _audio,
		juce::MidiBuffer& _midi)
	{
		juce::ScopedNoDenormals noDenormals;
		const auto samples = _audio.getNumSamples();
		if(samples <= 0)
			return;
		if(samples > m_maximumBlockSize || _audio.getNumChannels() < 2)
		{
			_audio.clear();
			_midi.clear();
			return;
		}

		// The machines render on their workers one block ahead of the host, so
		// the audio thread never waits for the slower one. setLatencySamples
		// reports the extra block for host delay compensation.
		finishPendingBlock();

		for(auto* engine : {&m_md, &m_mm})
		{
			engine->audio.setSize(g_machineChannels, samples, false, false, true);
			engine->audio.clear();
			for(int channel = 0; channel < 2; ++channel)
				engine->audio.copyFrom(channel, 0, _audio, channel, 0, samples);
			engine->midi.clear();
		}
		const auto* const hostPlayHead = getPlayHead();
		m_blockPlayHead.position = hostPlayHead ? hostPlayHead->getPosition()
			: juce::Optional<juce::AudioPlayHead::PositionInfo>();
		const auto focused = m_maschine.focusedModel();
		const auto selectedMmTrack = m_maschine.selectedMonomachineTrack();
		const auto mmNoteChannel = m_monomachine.getMonomachineNoteChannel(selectedMmTrack);
		for(const auto event : _midi)
		{
			const auto message = event.getMessage();
			const auto* const bytes = message.getRawData();
			const auto size = message.getRawDataSize();
			if(!bytes || size <= 0 || event.samplePosition >= samples)
				continue;
			const auto destinations = m_midiRouter.route(bytes[0],
				size > 1 ? bytes[1] : 0, size > 2 ? bytes[2] : 0, focused);
			if(destinations & CombinedMidiRouter::machinedrum)
				m_md.midi.addEvent(message, event.samplePosition);
			if(destinations & CombinedMidiRouter::monomachine)
			{
				const auto channels = m_midiRouter.monomachineChannels(bytes[0],
					size > 1 ? bytes[1] : 0, size > 2 ? bytes[2] : 0,
					mmNoteChannel);
				if(bytes[0] >= 0xf0)
					m_mm.midi.addEvent(message, event.samplePosition);
				else
				{
					for(int channel = 0; channel < 16; ++channel)
					{
						if((channels & (1u << channel)) == 0)
							continue;
						auto routed = message;
						routed.setChannel(channel + 1);
						m_mm.midi.addEvent(routed, event.samplePosition);
					}
				}
			}
		}

		// Output the block the workers rendered during the previous callback.
		const auto capacity = m_md.delay.getNumSamples();
		const auto first = std::min(samples, capacity - m_delayRead);
		const auto addDelayed = [&](juce::AudioBuffer<float>& _destination,
			const int _destinationChannel, const Engine& _engine,
			const int _machineChannel, const float _gain)
		{
			_destination.addFrom(_destinationChannel, 0, _engine.delay,
				_machineChannel, m_delayRead, first, _gain);
			_destination.addFrom(_destinationChannel, first, _engine.delay,
				_machineChannel, 0, samples - first, _gain);
		};

		// The mix always carries both A/B pairs, halved to keep headroom, whatever
		// buses the host activates. The other buses carry machine jacks at full level.
		for(size_t i = 0; i < g_machineOutputs.size(); ++i)
		{
			const auto& source = g_machineOutputs[i];
			auto output = getBusBuffer(_audio, false, static_cast<int>(i) + 1);
			if(output.getNumChannels() < source.channels)
				continue;
			output.clear();
			for(int channel = 0; channel < source.channels; ++channel)
				addDelayed(output, channel, source.monomachine ? m_mm : m_md,
					source.firstChannel + channel, 1.0f);
		}
		for(int channel = 0; channel < 2; ++channel)
		{
			_audio.clear(channel, 0, samples);
			addDelayed(_audio, channel, m_md, channel, 0.5f);
			addDelayed(_audio, channel, m_mm, channel, 0.5f);
		}
		m_delayRead = (m_delayRead + samples) % capacity;

		_midi.clear();
		for(const auto event : m_pendingMidi)
			_midi.addEvent(event.getMessage(), std::min(event.samplePosition, samples - 1));
		m_pendingMidi.clear();

		// Start rendering this block. Machines still fast-booting stay silent.
		m_pendingSamples = samples;
		for(auto* engine : {&m_md, &m_mm})
		{
			engine->rendering = !engine->fastBootActive.load(std::memory_order_acquire);
			if(engine->rendering)
				engine->ready.signal();
			else
			{
				engine->audio.clear();
				engine->midi.clear();
			}
		}
	}

	juce::AudioProcessorEditor* CombinedProcessor::createEditor()
	{
		return new CombinedEditor(*this);
	}

	void CombinedProcessor::getStateInformation(juce::MemoryBlock& _destination)
	{
		juce::MemoryBlock mdState;
		juce::MemoryBlock mmState;
		static_cast<juce::AudioProcessor&>(m_machinedrum).getStateInformation(mdState);
		static_cast<juce::AudioProcessor&>(m_monomachine).getStateInformation(mmState);
		juce::MemoryOutputStream stream(_destination, false);
		stream.writeInt(static_cast<int>(g_stateMagic));
		stream.writeInt(g_stateVersion);
		stream.writeByte(m_maschine.focusedModel() == md::MachineModel::Monomachine
			? 1 : 0);
		stream.writeInt(static_cast<int>(mdState.getSize()));
		stream.write(mdState.getData(), mdState.getSize());
		stream.writeInt(static_cast<int>(mmState.getSize()));
		stream.write(mmState.getData(), mmState.getSize());
	}

	void CombinedProcessor::setStateInformation(const void* const _data,
		const int _size)
	{
		if(!_data || _size <= 0)
			return;
		juce::MemoryInputStream stream(_data, static_cast<size_t>(_size), false);
		if(static_cast<uint32_t>(stream.readInt()) != g_stateMagic
			|| stream.readInt() != g_stateVersion)
			return;
		m_maschine.setFocusedModel(stream.readByte() != 0
			? md::MachineModel::Monomachine : md::MachineModel::Machinedrum);
		const auto mdSize = stream.readInt();
		if(mdSize < 0 || static_cast<int64_t>(mdSize) > stream.getNumBytesRemaining())
			return;
		juce::MemoryBlock mdState(static_cast<size_t>(mdSize));
		if(stream.read(mdState.getData(), mdState.getSize()) != mdSize)
			return;
		const auto mmSize = stream.readInt();
		if(mmSize < 0 || static_cast<int64_t>(mmSize) > stream.getNumBytesRemaining())
			return;
		juce::MemoryBlock mmState(static_cast<size_t>(mmSize));
		if(stream.read(mmState.getData(), mmState.getSize()) != mmSize)
			return;
		static_cast<juce::AudioProcessor&>(m_machinedrum).setStateInformation(
			mdState.getData(), static_cast<int>(mdState.getSize()));
		static_cast<juce::AudioProcessor&>(m_monomachine).setStateInformation(
			mmState.getData(), static_cast<int>(mmState.getSize()));
	}
}
