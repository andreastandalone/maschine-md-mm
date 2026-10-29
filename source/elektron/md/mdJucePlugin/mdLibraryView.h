#pragma once

#include "mdLibraryScanner.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <set>

namespace mdJucePlugin
{
	class CombinedProcessor;

	// Pack browser in the style of an Elektron menu: a tab per machine, packs on
	// the left, their kits and patterns on the right, audition and import buttons
	// below.
	class LibraryView final : public juce::Component, juce::Timer
	{
	public:
		explicit LibraryView(CombinedProcessor& _processor);

		void paint(juce::Graphics& _g) override;
		void mouseDown(const juce::MouseEvent& _event) override;
		void mouseDoubleClick(const juce::MouseEvent& _event) override;
		void mouseWheelMove(const juce::MouseEvent& _event, const juce::MouseWheelDetails& _wheel) override;
		bool keyPressed(const juce::KeyPress& _key) override;
		md::MachineModel model() const { return m_model; }

		std::function<void()> onMachineChanged;

	private:
		struct Column
		{
			int selected = 0;
			int first = 0;
		};
		struct Layout
		{
			juce::Rectangle<int> tabs, packs, items, hint, status, playKit, playPattern, import;
			int row = 0;
		};

		void timerCallback() override;
		void showMachine(md::MachineModel _model);
		Layout layout() const;
		juce::Rectangle<int> tabBounds(const Layout& _layout, md::MachineModel _model) const;
		const std::vector<library::Pack>& packs() const;
		int itemCount() const;
		juce::String itemLabel(const md::packlib::Item& _item) const;
		const md::packlib::Item* selectedItem() const;
		void select(int _column, int _index);
		std::vector<size_t> markedItems() const;
		void marksChanged();
		void importMarked();
		void playKit();
		void playPattern();
		void play();
		void paintColumn(juce::Graphics& _g, juce::Rectangle<int> _area, int _row,
			const juce::StringArray& _rows, const Column& _column, bool _focused,
			const std::set<int>& _marked, bool _marksItems) const;

		CombinedProcessor& m_processor;
		juce::File m_folder;
		md::MachineModel m_model = md::MachineModel::Machinedrum;
		std::map<md::MachineModel, std::vector<library::Pack>> m_scans;
		Column m_columns[2];
		int m_focus = 0;
		std::set<int> m_marked;                  // items picked for IMPORT
		juce::String m_shownStatus;              // both machines, to repaint on any change
		juce::String m_hint;
	};
}
