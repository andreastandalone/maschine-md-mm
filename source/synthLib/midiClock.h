#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace synthLib
{
	class Plugin;

	class MidiClock
	{
	public:
		explicit MidiClock(Plugin& _plugin) : m_plugin(_plugin) {}

		void process(double _bpm, double _ppqPos, bool _isPlaying, size_t _sampleCount, bool _ppqKnown = true);

		void restart();

		// Keep sending clock at the host tempo while the transport is stopped, so
		// a device synced to external clock follows tempo without being started.
		void setRunWhileStopped(const bool _run) { m_runWhileStopped = _run; }

	private:
		void stop();
		void start(double _ppqPos);
		bool updateBpm(double _bpm, double _rate);
		void processStopped(double _bpm, size_t _sampleCount);

		Plugin& m_plugin;

		std::atomic<bool> m_runWhileStopped{false};
		double m_stoppedClockOffset = 0.0;
		bool m_isPlaying = false;
		int64_t m_nextClockTick = 0;
		double m_expectedPpq = 0.0;
		double m_lastBpm = 0.0;
	};
}
