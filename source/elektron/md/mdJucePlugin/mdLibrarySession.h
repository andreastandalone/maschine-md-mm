#pragma once

#include "mdLibraryScanner.h"

#include "mdLib/mdsyseximport.h"

#include <map>
#include <memory>
#include <optional>

namespace mdJucePlugin
{
	class CombinedProcessor;

	// Auditions library kits and patterns on the Machinedrum, message thread
	// only. Samples go to their own slots, the kit to slot 63 and patterns to
	// slot 127. One transfer runs at a time; a new request replaces one that
	// has not started yet.
	class LibrarySession
	{
	public:
		static constexpr uint8_t g_kitSlot = 63;
		static constexpr uint8_t g_patternSlot = 127;

		explicit LibrarySession(CombinedProcessor& _processor);
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
			juce::String loaded;
			juce::String missing;
			// Imports plan their slots once the machine's free slots are known.
			std::shared_ptr<const library::Pack> source;
			std::vector<size_t> selected;
		};
		// Probe waits for the firmware after samples, Settle after an import.
		enum class Phase { Idle, Inventory, Samples, Probe, Dumps, Settle };

		void request(const library::Pack& _pack, const md::packlib::Item& _kit,
			const md::packlib::Item* _pattern);
		void setSamples(Request& _request, const library::Pack& _pack, const std::vector<uint8_t>& _slots);
		void startNext();
		void startCurrent();
		void startInventory();
		void serviceInventory();
		void planCurrentImport();
		bool startTransfer(std::vector<uint8_t> _bytes, bool _samples);
		void serviceTransfer();
		void startProbe(Phase _phase);
		void serviceProbe();
		void sendToMachine(const std::vector<uint8_t>& _sysex);
		void finish(const juce::String& _status);
		uint64_t hardwareEpoch();

		CombinedProcessor& m_processor;
		std::optional<Request> m_current;
		std::optional<Request> m_next;
		Phase m_phase = Phase::Idle;
		std::optional<md::SysexImportTicket> m_ticket;
		std::map<uint8_t, juce::String> m_sentSamples;  // slot -> pack it came from
		uint64_t m_sentSamplesEpoch = 0;                // machine they were sent to
		size_t m_inventoryNext = 0;                     // 0-63 kits, then 64-191 patterns
		std::vector<bool> m_freeKits;
		std::vector<bool> m_freePatterns;
		double m_probeStarted = 0;
		double m_probeSent = 0;
		bool m_changed = false;
		juce::String m_status;
	};
}
