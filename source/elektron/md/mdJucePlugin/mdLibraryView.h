#pragma once

#include "mdLibraryScanner.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mdJucePlugin
{
	class CombinedProcessor;

	// Full-area pack browser in the style of an Elektron menu: packs on the
	// left, their kits and patterns on the right, audition buttons below.
	class LibraryView final : public juce::Component, juce::Timer
	{
	public:
		explicit LibraryView(CombinedProcessor& _processor);

		void paint(juce::Graphics& _g) override;
		void mouseDown(const juce::MouseEvent& _event) override;
		void mouseDoubleClick(const juce::MouseEvent& _event) override;
		void mouseWheelMove(const juce::MouseEvent& _event, const juce::MouseWheelDetails& _wheel) override;
		bool keyPressed(const juce::KeyPress& _key) override;

	private:
		struct Column
		{
			int selected = 0;
			int first = 0;
		};
		struct Layout
		{
			juce::Rectangle<int> tabs, packs, items, status, playKit, playPattern;
			int row = 0;
		};

		void timerCallback() override;
		Layout layout() const;
		int itemCount() const;
		juce::String itemLabel(const md::packlib::Item& _item) const;
		const md::packlib::Item* selectedItem() const;
		void select(int _column, int _index);
		void playKit();
		void playPattern();
		void play();
		void paintColumn(juce::Graphics& _g, juce::Rectangle<int> _area, int _row,
			const juce::StringArray& _rows, const Column& _column, bool _focused) const;

		CombinedProcessor& m_processor;
		juce::File m_folder;
		std::vector<library::Pack> m_packs;
		Column m_columns[2];
		int m_focus = 0;
		juce::String m_shownStatus;
	};
}
