#include "mdLibraryScanner.h"

#include "mdLib/mdsysexfile.h"

#include <algorithm>
#include <map>

namespace mdJucePlugin::library
{
	namespace
	{
		using PackMap = std::map<juce::String, Pack>;

		juce::String packName(juce::String _path)
		{
			_path = _path.replaceCharacter('\\', '/').trimCharactersAtEnd("/");
			return _path.isEmpty() ? juce::String("LIBRARY") : _path.replace("/", " / ").toUpperCase();
		}

		void add(PackMap& _packs, const juce::String& _name, const std::vector<uint8_t>& _bytes,
			const md::MachineModel _model)
		{
			auto contents = md::packlib::readSyx(_bytes);
			auto& pack = _packs[_name];
			pack.name = _name;
			for(auto& item : contents.items)
				if(item.model == _model) pack.items.push_back(std::move(item));
			for(auto& sample : contents.samples) pack.samples.push_back(std::move(sample));
		}

		void addZip(PackMap& _packs, const juce::File& _zip, const juce::String& _zipPath,
			const md::MachineModel _model)
		{
			juce::ZipFile zip(_zip);
			for(int i = 0; i < zip.getNumEntries(); ++i)
			{
				const auto* entry = zip.getEntry(i);
				if(!entry->filename.endsWithIgnoreCase(".syx") || entry->uncompressedSize > int64_t(md::g_midiSysexTransferMaxBytes))
					continue;
				std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(i));
				if(!in) continue;
				juce::MemoryBlock data;
				in->readIntoMemoryBlock(data);
				const auto* begin = static_cast<const uint8_t*>(data.getData());
				const auto dir = entry->filename.replaceCharacter('\\', '/').upToLastOccurrenceOf("/", false, false);
				add(_packs, packName(_zipPath + "/" + dir), {begin, begin + data.getSize()}, _model);
			}
		}
	}

	juce::File libraryFolder(const juce::File& _machineDataFolder)
	{
		const auto folder = _machineDataFolder.getParentDirectory().getChildFile("Library");
		(void)folder.createDirectory();
		return folder;
	}

	std::vector<Pack> scan(const juce::File& _root, const md::MachineModel _model)
	{
		// Path order: when two files carry the same sample slot, the first one wins every time.
		auto files = _root.findChildFiles(juce::File::findFiles, true, "*.syx;*.zip");
		files.sort();
		PackMap packs;
		for(const auto& file : files)
		{
			if(file.getSize() > int64_t(md::g_midiSysexTransferMaxBytes) && file.hasFileExtension("syx"))
				continue;
			const auto relative = file.getRelativePathFrom(_root);
			if(file.hasFileExtension("zip"))
			{
				addZip(packs, file, relative.upToLastOccurrenceOf(".", false, false), _model);
				continue;
			}
			juce::MemoryBlock data;
			if(!file.loadFileAsData(data)) continue;
			const auto* begin = static_cast<const uint8_t*>(data.getData());
			const auto dir = file.getParentDirectory();
			add(packs, packName(dir == _root ? juce::String() : dir.getRelativePathFrom(_root)),
				{begin, begin + data.getSize()}, _model);
		}

		std::vector<Pack> result;
		for(auto& [name, pack] : packs)
		{
			if(pack.items.empty()) continue;
			std::stable_sort(pack.items.begin(), pack.items.end(), [](const auto& a, const auto& b)
			{
				return a.kind != b.kind ? a.kind == md::packlib::ItemKind::Kit : a.slot < b.slot;
			});
			result.push_back(std::move(pack));
		}
		std::sort(result.begin(), result.end(), [](const Pack& a, const Pack& b)
		{
			return a.name.compareNatural(b.name) < 0;
		});
		return result;
	}
}
