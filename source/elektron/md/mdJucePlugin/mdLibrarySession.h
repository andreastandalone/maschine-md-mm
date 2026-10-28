#pragma once

#include "mdLibraryScanner.h"

#include "mdLib/mdsyseximport.h"

#include <map>
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
		void service();
		const juce::String& status() const { return m_status; }
		// True once anything was sent, so closing needs a restore.
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
		};
		enum class Phase { Idle, Samples, Probe, Dumps };

		void request(const library::Pack& _pack, const md::packlib::Item& _kit,
			const md::packlib::Item* _pattern);
		void startNext();
		bool startTransfer(std::vector<uint8_t> _bytes, bool _samples);
		void serviceTransfer();
		void serviceProbe();
		void sendToMachine(const std::vector<uint8_t>& _sysex);
		void finish(const juce::String& _status);

		CombinedProcessor& m_processor;
		std::optional<Request> m_current;
		std::optional<Request> m_next;
		Phase m_phase = Phase::Idle;
		std::optional<md::SysexImportTicket> m_ticket;
		std::map<uint8_t, juce::String> m_sentSamples;  // slot -> pack it came from
		double m_probeStarted = 0;
		double m_probeSent = 0;
		bool m_changed = false;
		juce::String m_status;
	};
}
