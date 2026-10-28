#pragma once

#include "mdMaschineController.h"
#include "mdCombinedMidiRouter.h"
#include "mdPluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace mdJucePlugin
{
	class CombinedProcessor final : public juce::AudioProcessor
	{
	public:
		CombinedProcessor();
		~CombinedProcessor() override;

		void setFocusedModel(md::MachineModel _model) { m_maschine.setFocusedModel(_model); }

		AudioPluginAudioProcessor& machinedrum() { return m_machinedrum; }
		AudioPluginAudioProcessor& monomachine() { return m_monomachine; }

		struct SysexCapture
		{
			md::MachineModel model;
			std::vector<uint8_t> bytes;
			bool incomplete = false;
		};
		bool beginSysexCapture(md::MachineModel _model);
		std::optional<SysexCapture> endSysexCapture();
		bool isSysexCapturing() const { return m_captureEnabled.load(); }

		void prepareToPlay(double _sampleRate, int _maximumBlockSize) override;
		void releaseResources() override;
		void processBlock(juce::AudioBuffer<float>& _audio,
			juce::MidiBuffer& _midi) override;
		bool isBusesLayoutSupported(const BusesLayout& _layouts) const override;

		juce::AudioProcessorEditor* createEditor() override;
		bool hasEditor() const override { return true; }
		const juce::String getName() const override { return "Maschine MD-MM"; }
		bool acceptsMidi() const override { return true; }
		bool producesMidi() const override { return true; }
		bool isMidiEffect() const override { return false; }
		double getTailLengthSeconds() const override { return 0.0; }

		int getNumPrograms() override { return 1; }
		int getCurrentProgram() override { return 0; }
		void setCurrentProgram(int) override {}
		const juce::String getProgramName(int) override { return {}; }
		void changeProgramName(int, const juce::String&) override {}
		void getStateInformation(juce::MemoryBlock& _destination) override;
		void setStateInformation(const void* _data, int _size) override;

	private:
		// Host position read once per block on the audio thread. The children
		// read this copy, so MM's worker thread never calls into the host.
		struct BlockPlayHead final : juce::AudioPlayHead
		{
			juce::Optional<PositionInfo> getPosition() const override { return position; }
			juce::Optional<PositionInfo> position;
		};

		// One machine rendered on its own thread, one host block ahead of output.
		struct Engine
		{
			Engine(AudioPluginAudioProcessor& _processor, std::atomic<bool>& _fastBootActive)
				: processor(_processor), fastBootActive(_fastBootActive) {}

			AudioPluginAudioProcessor& processor;
			std::atomic<bool>& fastBootActive;
			juce::AudioBuffer<float> audio;
			juce::MidiBuffer midi;
			juce::AudioBuffer<float> delay;
			juce::WaitableEvent ready;
			juce::WaitableEvent finished;
			std::thread thread;
			bool rendering = false;
		};

		static BusesProperties combinedBuses();
		void runWorker(Engine& _engine);
		void finishPendingBlock();
		void captureSysex(const juce::MidiBuffer& _midi, md::MachineModel _model);
		void runFastBoot(AudioPluginAudioProcessor& _processor,
			std::atomic<bool>& _active);
		void stopFastBootWorkers();

		AudioPluginAudioProcessor m_machinedrum;
		AudioPluginAudioProcessor m_monomachine;
		maschine::Controller m_maschine;
		BlockPlayHead m_blockPlayHead;
		std::atomic<bool> m_stopFastBoot{false};
		std::atomic<bool> m_mdFastBootActive{false};
		std::atomic<bool> m_mmFastBootActive{false};
		Engine m_md{m_machinedrum, m_mdFastBootActive};
		Engine m_mm{m_monomachine, m_mmFastBootActive};
		// Rendered audio waits in each engine's delay ring for one maximum block,
		// so every callback reads a block the workers finished during the previous one.
		int m_delayRead = 0;
		int m_delayWrite = 0;
		int m_pendingSamples = 0;
		juce::MidiBuffer m_pendingMidi;
		CombinedMidiRouter m_midiRouter;
		std::mutex m_captureMutex;
		std::vector<uint8_t> m_captureBytes;
		std::atomic<bool> m_captureEnabled{false};
		std::atomic<bool> m_captureIncomplete{false};
		std::atomic<md::MachineModel> m_captureModel{md::MachineModel::Machinedrum};
		bool m_capturePreviousHostRoute = false;
		std::atomic<bool> m_stoppingWorker{false};
		std::thread m_mdFastBootWorker;
		std::thread m_mmFastBootWorker;
		int m_maximumBlockSize = 0;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CombinedProcessor)
	};
}
