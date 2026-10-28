#pragma once

#include "mdLibraryScanner.h"

#include "mdLib/mdpanelscript.h"
#include "mdLib/mdsyseximport.h"

#include <functional>

#include <map>
#include <memory>
#include <optional>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	class CombinedProcessor;

	// Auditions and imports library kits and patterns on one machine, message
	// thread only. Auditions use the last kit and pattern slots; MD samples go
	// to their own slots. The Monomachine takes dumps only in SYSEX RECEIVE,
	// which the session opens and leaves with panel key sequences. One transfer
	// runs at a time; a new request replaces one that has not started yet.
	class LibrarySession
	{
	public:
		LibrarySession(CombinedProcessor& _processor, md::MachineModel _model);
		~LibrarySession();

		void playKit(const library::Pack& _pack, size_t _itemIndex);
		void playPattern(const library::Pack& _pack, size_t _itemIndex);
		// Copies the selected kits and patterns, with their kits and samples, to
		// free slots. The audition is undone first, and what is imported stays
		// when the library closes.
		void importItems(const library::Pack& _pack, std::vector<size_t> _itemIndices);
		void service();
		const juce::String& status() const { return m_status; }
		// True once anything was sent since the last import, so closing needs a restore.
		bool changedMachine() const { return m_changed; }

	private:
		struct Request
		{
			juce::String pack;
			std::vector<md::packlib::Sample> samples;
			std::vector<uint8_t> dumps;
			std::vector<uint8_t> load;
			std::vector<uint8_t> verify;      // last dump, read back before reporting
			juce::String loaded;
			juce::String missing;
			// Imports plan their slots once the machine's free slots are known.
			std::shared_ptr<const library::Pack> source;
			std::vector<size_t> selected;
		};
		// Probe waits for the firmware after samples, Settle reads the last dump
		// back, Panel plays a key sequence.
		enum class Phase { Idle, Inventory, Samples, Probe, Dumps, Settle, Panel };

		void request(const library::Pack& _pack, const md::packlib::Item& _kit,
			const md::packlib::Item* _pattern);
		void setSamples(Request& _request, const library::Pack& _pack, const std::vector<uint8_t>& _slots);
		void startNext();
		void startCurrent();
		void startInventory();
		void serviceInventory();
		void planCurrentImport();
		bool startTransfer(std::vector<uint8_t> _bytes, bool _receiveModeConfirmed);
		void serviceTransfer();
		void startProbe(Phase _phase);
		bool beginCapture();
		void endCapture();
		void serviceProbe();
		void sendToMachine(const std::vector<uint8_t>& _sysex);
		void finish(const juce::String& _status);
		uint64_t hardwareEpoch();
		AudioPluginAudioProcessor& machine();
		size_t kitCount() const;
		uint8_t auditionKitSlot() const { return uint8_t(kitCount() - 1); }
		void sendDumps();
		void afterDumps();
		void verified();
		void fail(const juce::String& _status);
		void runPanel(std::vector<md::PanelStep> _script, double _delayMs, std::function<void()> _then);
		void servicePanel();

		CombinedProcessor& m_processor;
		const md::MachineModel m_model;
		std::optional<Request> m_current;
		std::optional<Request> m_next;
		Phase m_phase = Phase::Idle;
		std::optional<md::SysexImportTicket> m_ticket;
		std::map<uint8_t, juce::String> m_sentSamples;  // slot -> pack it came from
		uint64_t m_sentSamplesEpoch = 0;                // machine they were sent to
		size_t m_inventoryNext = 0;                     // kits, then patterns
		std::vector<md::PanelStep> m_panel;
		size_t m_panelNext = 0;
		double m_panelAt = 0;
		std::function<void()> m_afterPanel;
		bool m_inReceive = false;                       // MM left in SYSEX RECEIVE
		std::vector<md::packlib::Item> m_machineItems;  // inventory replies, without dumps
		bool m_ownsCapture = false;
		double m_probeStarted = 0;
		double m_probeSent = 0;
		bool m_changed = false;
		juce::String m_status;
	};
}
