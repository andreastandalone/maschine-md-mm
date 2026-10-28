#pragma once

#include "mdLib/mdpacklibrary.h"

#include <juce_core/juce_core.h>

#include <vector>

// Turns the Library folder into packs: every directory or zip directory that
// directly holds .syx files is one pack, named by its path in upper case.
namespace mdJucePlugin::library
{
	struct Pack
	{
		juce::String name;
		std::vector<md::packlib::Item> items;
		std::vector<md::packlib::Sample> samples;
	};

	// <data root>/Library, beside the machines' own data folders; created if missing.
	juce::File libraryFolder(const juce::File& _machineDataFolder);

	// Sorted by name; kits first then patterns, each by slot. Packs without
	// items for _model are left out.
	std::vector<Pack> scan(const juce::File& _root, md::MachineModel _model);
}
