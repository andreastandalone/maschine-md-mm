// The library session inside the whole combined plug-in, paced like a host:
// audio blocks on their own thread in real time, service() on this one as the
// view's timer does. Runs from a copy of the user's machine folders under a
// temporary HOME, so nothing the user owns is read back or written.
#include "mdCombinedProcessor.h"
#include "mdLibrarySession.h"

#include "mdLib/mddevice.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace
{
	[[noreturn]] void fail(const juce::String& _message)
	{
		std::fprintf(stderr, "mdLibrarySessionTest: %s\n", _message.toRawUTF8());
		std::exit(1);
	}

	bool firmwareReady(mdJucePlugin::AudioPluginAudioProcessor& _machine)
	{
		return _machine.getPlugin().withDeviceLocked([](synthLib::Device* _device)
		{
			auto* device = dynamic_cast<md::Device*>(_device);
			return device && device->isValid() && device->getHardware().isFirmwareMidiReady();
		});
	}
}

int main(int _argc, char** _argv)
{
	const juce::File realData = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
		.getChildFile("Gearmulator Preview");
	const juce::File library(_argc > 1 ? _argv[1] : "");
	if(!realData.getChildFile("Machinedrum/roms").isDirectory() || !library.isDirectory())
	{
		std::puts("mdLibrarySessionTest: SKIP (machine folders or library not found)");
		return 77;
	}
	const auto home = juce::File::getCurrentWorkingDirectory().getChildFile("temp")
		.getNonexistentChildFile("md-mm-library-session", "", false);
	for(const auto* machine : {"Machinedrum", "Monomachine"})
		for(const auto* folder : {"roms", "nvram"})
			if(const auto from = realData.getChildFile(machine).getChildFile(folder); from.isDirectory())
				from.copyDirectoryTo(home.getChildFile("Documents/Gearmulator Preview").getChildFile(machine).getChildFile(folder));
	library.copyDirectoryTo(home.getChildFile("Documents/Gearmulator Preview/Library"));
	setenv("HOME", home.getFullPathName().toRawUTF8(), 1);
	setenv("MDMM_MASCHINE_DISABLED", "1", 1);

	juce::ScopedJuceInitialiser_GUI gui;
	{
		mdJucePlugin::CombinedProcessor processor;
		constexpr int block = 512;
		processor.setRateAndBufferSizeDetails(44100.0, block);
		processor.prepareToPlay(44100.0, block);
		std::atomic<bool> running{true};
		std::thread audio([&]
		{
			juce::AudioBuffer<float> buffer(processor.getTotalNumOutputChannels(), block);
			juce::MidiBuffer midi;
			auto next = std::chrono::steady_clock::now();
			while(running)
			{
				buffer.clear();
				midi.clear();
				processor.processBlock(buffer, midi);
				next += std::chrono::microseconds(block * 1'000'000 / 44100);
				std::this_thread::sleep_until(next);
			}
		});
		const auto started = std::chrono::steady_clock::now();
		const auto seconds = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };
		for(auto* machine : {&processor.machinedrum(), &processor.monomachine()})
			while(!firmwareReady(*machine))
			{
				if(seconds() > 180) fail("machine never booted");
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		std::printf("booted after %.1f s\n", seconds());
		processor.openLibrary();

		// Imports the patterns of the pack whose name ends with _packName, as IMPORT does.
		const auto import = [&](const md::MachineModel _model, const char* _packName, const size_t _patterns)
		{
			auto* session = processor.librarySession(_model);
			const auto packs = mdJucePlugin::library::scan(
				home.getChildFile("Documents/Gearmulator Preview/Library"), _model);
			const auto pack = std::find_if(packs.begin(), packs.end(), [&](const auto& _p) { return _p.name.endsWith(_packName); });
			if(pack == packs.end()) fail(juce::String(_packName) + " pack not found");
			std::vector<size_t> selected;
			for(size_t i = 0; i < pack->items.size() && selected.size() < _patterns; ++i)
				if(pack->items[i].kind == md::packlib::ItemKind::Pattern) selected.push_back(i);
			session->importItems(*pack, selected);

			const auto importStart = seconds();
			juce::String shown;
			while(session->busy() && seconds() - importStart < 240)
			{
				session->service();
				if(session->status() != shown && !session->status().startsWith("SENDING SAMPLES "))
				{
					shown = session->status();
					std::printf("%6.2f s  %s\n", seconds() - importStart, shown.toRawUTF8());
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			if(!session->status().startsWith("IMPORTED") || !session->status().contains("NOW PLAYING"))
				fail(juce::String(_packName) + " import ended with: " + session->status());
		};
		import(md::MachineModel::Machinedrum, "DUB1", 4);
		import(md::MachineModel::Monomachine, "NICHOLAS-LEM-COLLECTION / SYSEX", 1);
		processor.closeLibrary();
		running = false;
		audio.join();
		processor.releaseResources();
	}
	home.deleteRecursively();
	std::puts("mdLibrarySessionTest: ok");
	return 0;
}
