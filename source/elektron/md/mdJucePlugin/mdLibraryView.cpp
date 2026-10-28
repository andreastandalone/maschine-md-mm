#include "mdLibraryView.h"

#include "mdCombinedProcessor.h"
#include "mdLibrarySession.h"

namespace mdJucePlugin
{
	namespace
	{
		const juce::Colour g_background(0xff0d0f0d);
		const juce::Colour g_text(0xffd8e6c8);
		const juce::Colour g_divider(0xff3c423c);
		const juce::Colour g_dim = g_text.withAlpha(0.4f);

		juce::Font font(const int _row)
		{
			return juce::Font(juce::Font::getDefaultMonospacedFontName(), float(_row) * 0.62f, juce::Font::bold);
		}

		juce::String twoDigits(const int _value) { return juce::String(_value).paddedLeft('0', 2); }
	}

	LibraryView::LibraryView(CombinedProcessor& _processor)
		: m_processor(_processor)
		, m_folder(library::libraryFolder(juce::File(_processor.machinedrum().getDataFolder())))
		, m_packs(library::scan(m_folder, md::MachineModel::Machinedrum))
	{
		setWantsKeyboardFocus(true);
		if(itemCount()) m_marked = {0};
		// Reading the machine's free slots waits on one reply per tick.
		startTimer(10);
	}

	LibraryView::Layout LibraryView::layout() const
	{
		Layout l;
		auto area = getLocalBounds().reduced(getWidth() / 40, getHeight() / 40);
		l.row = std::max(22, getHeight() / 26);
		l.tabs = area.removeFromTop(l.row + l.row / 2);
		auto buttons = area.removeFromBottom(l.row * 2);
		l.status = area.removeFromBottom(l.row + l.row / 3);
		l.hint = area.removeFromBottom(l.row + l.row / 3);
		l.packs = area.removeFromLeft(area.getWidth() * 2 / 5);
		l.items = area.withTrimmedLeft(l.row / 2);
		buttons.removeFromTop(l.row / 3);
		const auto third = buttons.getWidth() / 3;
		l.playKit = buttons.removeFromLeft(third).withTrimmedRight(l.row / 4);
		l.playPattern = buttons.removeFromLeft(third).reduced(l.row / 8, 0);
		l.import = buttons.withTrimmedLeft(l.row / 4);
		return l;
	}

	int LibraryView::itemCount() const
	{
		return m_packs.empty() ? 0 : int(m_packs[size_t(m_columns[0].selected)].items.size());
	}

	const md::packlib::Item* LibraryView::selectedItem() const
	{
		if(itemCount() == 0) return nullptr;
		return &m_packs[size_t(m_columns[0].selected)].items[size_t(m_columns[1].selected)];
	}

	juce::String LibraryView::itemLabel(const md::packlib::Item& _item) const
	{
		if(_item.kind == md::packlib::ItemKind::Pattern)
			return juce::String(_item.name.substr(8)) + "  PATTERN    KIT " + twoDigits(_item.kitSlot + 1);
		auto label = "K" + twoDigits(_item.slot + 1) + "  " + juce::String(_item.name).toUpperCase();
		if(!_item.sampleSlots.empty())
			label << "  +" << int(_item.sampleSlots.size()) << " CAMPIONI";
		return label;
	}

	void LibraryView::paintColumn(juce::Graphics& _g, const juce::Rectangle<int> _area, const int _row,
		const juce::StringArray& _rows, const Column& _column, const bool _focused,
		const std::set<int>& _marked) const
	{
		_g.setColour(g_divider);
		_g.drawRect(_area);
		const auto inner = _area.reduced(2);
		const auto visible = inner.getHeight() / _row;
		for(int i = 0; i < visible && _column.first + i < _rows.size(); ++i)
		{
			const auto index = _column.first + i;
			const juce::Rectangle<int> r(inner.getX(), inner.getY() + i * _row, inner.getWidth(), _row);
			const bool selected = index == _column.selected || _marked.count(index);
			if(selected)
			{
				_g.setColour(_focused ? g_text : g_dim);
				_g.fillRect(r);
			}
			_g.setColour(selected ? g_background : g_text);
			_g.drawText(_rows[index], r.reduced(_row / 3, 0), juce::Justification::centredLeft, true);
		}
	}

	void LibraryView::paint(juce::Graphics& _g)
	{
		_g.fillAll(g_background);
		const auto l = layout();
		_g.setFont(font(l.row));

		// Tabs: MD active, MM disabled until passo 2.
		const auto tabWidth = l.tabs.getWidth() / 4;
		const auto mdTab = l.tabs.withWidth(tabWidth).withTrimmedBottom(l.row / 3);
		_g.setColour(g_text);
		_g.fillRect(mdTab);
		_g.setColour(g_background);
		_g.drawText("MACHINEDRUM", mdTab, juce::Justification::centred);
		_g.setColour(g_dim);
		_g.drawText("MM: PROSSIMAMENTE", mdTab.translated(tabWidth + l.row / 2, 0), juce::Justification::centred);
		_g.setColour(g_divider);
		_g.fillRect(l.tabs.getX(), l.tabs.getBottom() - 2, l.tabs.getWidth(), 1);

		if(m_packs.empty())
		{
			_g.setColour(g_text);
			auto area = l.packs.getUnion(l.items);
			_g.drawText("NESSUN PACK IN LIBRARY", area.removeFromTop(l.row * 2), juce::Justification::centredLeft);
			_g.setColour(g_dim);
			_g.drawFittedText("COPIA I PACK IN:\n" + m_folder.getFullPathName(), area.removeFromTop(l.row * 3),
				juce::Justification::topLeft, 3);
			return;
		}

		juce::StringArray packs, items;
		for(const auto& pack : m_packs) packs.add(pack.name);
		for(const auto& item : m_packs[size_t(m_columns[0].selected)].items) items.add(itemLabel(item));
		paintColumn(_g, l.packs, l.row, packs, m_columns[0], m_focus == 0, {});
		paintColumn(_g, l.items, l.row, items, m_columns[1], m_focus == 1, m_marked);

		_g.setColour(g_dim);
		_g.drawText(importHint(), l.hint, juce::Justification::centredLeft, true);
		const auto* session = m_processor.librarySession();
		_g.setColour(g_text);
		_g.drawText(session ? session->status() : juce::String(), l.status, juce::Justification::centredLeft, true);

		const auto* item = selectedItem();
		const auto button = [&](const juce::Rectangle<int> _r, const juce::String& _text, const bool _enabled)
		{
			_g.setColour(_enabled ? g_text : g_divider);
			_g.drawRect(_r, 2);
			_g.drawText(_text, _r, juce::Justification::centred);
		};
		button(l.playKit, "SUONA KIT", item != nullptr);
		button(l.playPattern, "SUONA PATTERN", item && item->kind == md::packlib::ItemKind::Pattern);
		button(l.import, "IMPORTA", item != nullptr);
	}

	void LibraryView::select(const int _column, const int _index)
	{
		const auto count = _column == 0 ? int(m_packs.size()) : itemCount();
		if(count == 0) return;
		auto& c = m_columns[_column];
		c.selected = juce::jlimit(0, count - 1, _index);
		const auto l = layout();
		const auto visible = std::max(1, ((_column == 0 ? l.packs : l.items).getHeight() - 4) / l.row);
		if(c.selected < c.first) c.first = c.selected;
		if(c.selected >= c.first + visible) c.first = c.selected - visible + 1;
		if(_column == 0) m_columns[1] = {};
		m_marked = {m_columns[1].selected};
		repaint();
	}

	std::vector<size_t> LibraryView::markedItems() const
	{
		std::vector<size_t> items;
		for(const auto index : m_marked)
			if(index < itemCount()) items.push_back(size_t(index));
		return items;
	}

	// What IMPORTA will copy, and which sample slots it overwrites.
	juce::String LibraryView::importHint() const
	{
		if(m_packs.empty()) return {};
		const auto& pack = m_packs[size_t(m_columns[0].selected)];
		int kits = 0, patterns = 0;
		std::set<uint8_t> samples;
		for(const auto index : markedItems())
		{
			const auto& item = pack.items[index];
			(item.kind == md::packlib::ItemKind::Kit ? kits : patterns)++;
			for(const auto& kit : pack.items)
				if(kit.kind == md::packlib::ItemKind::Kit
					&& (&kit == &item || (item.kind == md::packlib::ItemKind::Pattern && kit.slot == item.kitSlot)))
					samples.insert(kit.sampleSlots.begin(), kit.sampleSlots.end());
		}
		juce::String hint = "SELEZIONATI: " + juce::String(kits) + " KIT, " + juce::String(patterns) + " PATTERN";
		if(!samples.empty())
			hint << "  -  IMPORTA SOSTITUISCE I CAMPIONI " << juce::String(*samples.begin() + 1)
				<< (samples.size() > 1 ? "-" + juce::String(*samples.rbegin() + 1) : juce::String());
		return hint;
	}

	void LibraryView::importMarked()
	{
		if(auto* session = m_processor.librarySession(); session && !m_packs.empty())
			session->importItems(m_packs[size_t(m_columns[0].selected)], markedItems());
	}

	void LibraryView::playKit()
	{
		const auto* item = selectedItem();
		auto* session = m_processor.librarySession();
		if(!item || !session) return;
		const auto& pack = m_packs[size_t(m_columns[0].selected)];
		if(item->kind == md::packlib::ItemKind::Kit)
		{
			session->playKit(pack, size_t(m_columns[1].selected));
			return;
		}
		for(size_t i = 0; i < pack.items.size(); ++i)
			if(pack.items[i].kind == md::packlib::ItemKind::Kit && pack.items[i].slot == item->kitSlot)
				return session->playKit(pack, i);
	}

	void LibraryView::playPattern()
	{
		const auto* item = selectedItem();
		auto* session = m_processor.librarySession();
		if(item && session && item->kind == md::packlib::ItemKind::Pattern)
			session->playPattern(m_packs[size_t(m_columns[0].selected)], size_t(m_columns[1].selected));
	}

	void LibraryView::play()
	{
		const auto* item = selectedItem();
		if(item && item->kind == md::packlib::ItemKind::Pattern) playPattern();
		else playKit();
	}

	void LibraryView::mouseDown(const juce::MouseEvent& _event)
	{
		grabKeyboardFocus();
		const auto l = layout();
		const auto p = _event.getPosition();
		if(l.playKit.contains(p)) return playKit();
		if(l.playPattern.contains(p)) return playPattern();
		if(l.import.contains(p)) return importMarked();
		for(int column = 0; column < 2; ++column)
		{
			const auto area = (column == 0 ? l.packs : l.items).reduced(2);
			if(!area.contains(p)) continue;
			m_focus = column;
			const auto index = m_columns[column].first + (p.y - area.getY()) / l.row;
			if(column == 0 || index >= itemCount())
			{
				if(column == 0 && index < int(m_packs.size()) && index != m_columns[0].selected)
					select(0, index);
				continue;
			}
			// Finder-style: Cmd toggles one item, Shift extends from the cursor.
			const auto mods = _event.mods;
			const auto anchor = m_columns[1].selected;
			if(mods.isCommandDown())
			{
				m_columns[1].selected = index;
				if(!m_marked.erase(index)) m_marked.insert(index);
				repaint();
			}
			else if(mods.isShiftDown())
			{
				for(int i = std::min(anchor, index); i <= std::max(anchor, index); ++i) m_marked.insert(i);
				m_columns[1].selected = index;
				repaint();
			}
			else
				select(1, index);
		}
	}

	void LibraryView::mouseDoubleClick(const juce::MouseEvent& _event)
	{
		if(layout().items.contains(_event.getPosition())) play();
	}

	void LibraryView::mouseWheelMove(const juce::MouseEvent& _event, const juce::MouseWheelDetails& _wheel)
	{
		const auto l = layout();
		const int column = l.items.contains(_event.getPosition()) ? 1 : 0;
		const auto count = column == 0 ? int(m_packs.size()) : itemCount();
		const auto visible = ((column == 0 ? l.packs : l.items).getHeight() - 4) / l.row;
		auto& c = m_columns[column];
		if(_wheel.deltaY == 0) return;
		const auto step = _wheel.deltaY > 0 ? -3 : 3;
		c.first = juce::jlimit(0, std::max(0, count - visible), c.first + step);
		repaint();
	}

	bool LibraryView::keyPressed(const juce::KeyPress& _key)
	{
		const auto code = _key.getKeyCode();
		if(code == juce::KeyPress::upKey) select(m_focus, m_columns[m_focus].selected - 1);
		else if(code == juce::KeyPress::downKey) select(m_focus, m_columns[m_focus].selected + 1);
		else if(code == juce::KeyPress::leftKey) { m_focus = 0; repaint(); }
		else if(code == juce::KeyPress::rightKey) { if(itemCount()) m_focus = 1; repaint(); }
		else if(code == juce::KeyPress::returnKey) play();
		else if(_key.getModifiers().isCommandDown() && (code == 'A' || code == 'a'))
		{
			for(int i = 0; i < itemCount(); ++i) m_marked.insert(i);
			m_focus = 1;
			repaint();
		}
		else return false;
		return true;
	}

	void LibraryView::timerCallback()
	{
		auto* session = m_processor.librarySession();
		if(!session) return;
		session->service();
		if(session->status() != m_shownStatus)
		{
			m_shownStatus = session->status();
			repaint();
		}
	}
}
