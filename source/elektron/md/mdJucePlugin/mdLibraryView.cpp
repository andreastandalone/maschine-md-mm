#include "mdLibraryView.h"

#include "mdCombinedProcessor.h"
#include "mdLibrarySession.h"

namespace mdJucePlugin
{
	namespace
	{
		const juce::Colour g_background(0xff0d0f0d);
		const juce::Colour g_backdrop = g_background.withAlpha(0.85f);
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
	{
		setWantsKeyboardFocus(true);
		showMachine(md::MachineModel::Machinedrum);
		// Reading the machine's free slots waits on one reply per tick.
		startTimer(10);
	}

	void LibraryView::showMachine(const md::MachineModel _model)
	{
		m_model = _model;
		// Each tab reads the folder once per library opening.
		if(!m_scans.count(_model))
			m_scans[_model] = library::scan(m_folder, _model);
		m_columns[0] = {};
		m_columns[1] = {};
		m_focus = 0;
		m_marked.clear();
		if(itemCount()) m_marked = {0};
		marksChanged();
		if(onMachineChanged) onMachineChanged();
	}

	LibraryView::Layout LibraryView::layout() const
	{
		Layout l;
		auto area = getLocalBounds().reduced(getWidth() / 40, getHeight() / 40);
		l.row = std::max(18, getHeight() / 24);
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

	const std::vector<library::Pack>& LibraryView::packs() const
	{
		return m_scans.at(m_model);
	}

	int LibraryView::itemCount() const
	{
		return packs().empty() ? 0 : int(packs()[size_t(m_columns[0].selected)].items.size());
	}

	const md::packlib::Item* LibraryView::selectedItem() const
	{
		if(itemCount() == 0) return nullptr;
		return &packs()[size_t(m_columns[0].selected)].items[size_t(m_columns[1].selected)];
	}

	juce::String LibraryView::itemLabel(const md::packlib::Item& _item) const
	{
		if(_item.kind == md::packlib::ItemKind::Pattern)
			return juce::String(_item.name.substr(8)) + "  PATTERN    KIT " + twoDigits(_item.kitSlot + 1);
		auto label = "K" + twoDigits(_item.slot + 1) + "  " + juce::String(_item.name).toUpperCase();
		if(!_item.sampleSlots.empty())
			label << "  +" << int(_item.sampleSlots.size()) << " SAMPLES";
		return label;
	}

	void LibraryView::paintColumn(juce::Graphics& _g, const juce::Rectangle<int> _area, const int _row,
		const juce::StringArray& _rows, const Column& _column, const bool _focused,
		const std::set<int>& _marked, const bool _marksItems) const
	{
		_g.setColour(g_divider);
		_g.drawRect(_area);
		const auto inner = _area.reduced(2);
		const auto visible = inner.getHeight() / _row;
		for(int i = 0; i < visible && _column.first + i < _rows.size(); ++i)
		{
			const auto index = _column.first + i;
			const juce::Rectangle<int> r(inner.getX(), inner.getY() + i * _row, inner.getWidth(), _row);
			// The packs column shows its cursor; the items column shows what is
			// marked, with an outline for the cursor.
			const bool selected = _marksItems ? _marked.count(index) > 0 : index == _column.selected;
			if(selected)
			{
				_g.setColour(_focused ? g_text : g_dim);
				_g.fillRect(r);
			}
			else if(_marksItems && _focused && index == _column.selected)
			{
				_g.setColour(g_dim);
				_g.drawRect(r);
			}
			_g.setColour(selected ? g_background : g_text);
			_g.drawText(_rows[index], r.reduced(_row / 3, 0), juce::Justification::centredLeft, true);
		}
	}

	void LibraryView::paint(juce::Graphics& _g)
	{
		_g.fillAll(g_backdrop);
		const auto l = layout();
		_g.setFont(font(l.row));

		for(const auto model : {md::MachineModel::Machinedrum, md::MachineModel::Monomachine})
		{
			const auto tab = tabBounds(l, model);
			const bool active = model == m_model;
			_g.setColour(active ? g_text : g_dim);
			if(active) _g.fillRect(tab);
			else _g.drawRect(tab);
			_g.setColour(active ? g_background : g_text);
			const auto* session = m_processor.librarySession(model);
			const juce::String name = model == md::MachineModel::Machinedrum ? "MACHINEDRUM" : "MONOMACHINE";
			_g.drawText(name + (!active && session && session->busy() ? " ..." : ""), tab, juce::Justification::centred);
		}
		_g.setColour(g_divider);
		_g.fillRect(l.tabs.getX(), l.tabs.getBottom() - 2, l.tabs.getWidth(), 1);

		if(packs().empty())
		{
			_g.setColour(g_text);
			auto area = l.packs.getUnion(l.items);
			_g.drawText("NO PACKS IN LIBRARY", area.removeFromTop(l.row * 2), juce::Justification::centredLeft);
			_g.setColour(g_dim);
			_g.drawFittedText("COPY PACKS TO:\n" + m_folder.getFullPathName(), area.removeFromTop(l.row * 3),
				juce::Justification::topLeft, 3);
			return;
		}

		juce::StringArray names, items;
		for(const auto& pack : packs()) names.add(pack.name);
		for(const auto& item : packs()[size_t(m_columns[0].selected)].items) items.add(itemLabel(item));
		paintColumn(_g, l.packs, l.row, names, m_columns[0], m_focus == 0, {}, false);
		paintColumn(_g, l.items, l.row, items, m_columns[1], m_focus == 1, m_marked, true);

		_g.setColour(g_dim);
		_g.drawText(m_hint, l.hint, juce::Justification::centredLeft, true);
		const auto* session = m_processor.librarySession(m_model);
		_g.setColour(g_text);
		_g.drawText(session ? session->status() : juce::String(), l.status, juce::Justification::centredLeft, true);

		const auto* item = selectedItem();
		const auto button = [&](const juce::Rectangle<int> _r, const juce::String& _text, const bool _enabled)
		{
			_g.setColour(_enabled ? g_text : g_divider);
			_g.drawRect(_r, 2);
			_g.drawText(_text, _r, juce::Justification::centred);
		};
		button(l.playKit, "PLAY KIT", item != nullptr);
		button(l.playPattern, "PLAY PATTERN", item && item->kind == md::packlib::ItemKind::Pattern);
		button(l.import, "IMPORT", !m_marked.empty());
	}

	void LibraryView::select(const int _column, const int _index)
	{
		const auto count = _column == 0 ? int(packs().size()) : itemCount();
		if(count == 0) return;
		auto& c = m_columns[_column];
		const auto index = juce::jlimit(0, count - 1, _index);
		if(_column == 0 && index == c.selected)
			return;
		c.selected = index;
		const auto l = layout();
		const auto visible = std::max(1, ((_column == 0 ? l.packs : l.items).getHeight() - 4) / l.row);
		if(c.selected < c.first) c.first = c.selected;
		if(c.selected >= c.first + visible) c.first = c.selected - visible + 1;
		if(_column == 0) m_columns[1] = {};
		m_marked = {m_columns[1].selected};
		marksChanged();
	}

	std::vector<size_t> LibraryView::markedItems() const
	{
		std::vector<size_t> items;
		for(const auto index : m_marked)
			if(index < itemCount()) items.push_back(size_t(index));
		return items;
	}

	// What IMPORT will copy and which sample slots it overwrites, from the same
	// plan the import uses, as if every slot were free.
	void LibraryView::marksChanged()
	{
		m_hint.clear();
		if(!packs().empty() && !m_marked.empty())
		{
			const auto plan = md::packlib::planImport(packs()[size_t(m_columns[0].selected)].items, markedItems(),
				std::vector<bool>(128, true), std::vector<bool>(128, true));
			m_hint = "IMPORT: " + juce::String(int(plan.kitsNeeded)) + " KITS, "
				+ juce::String(int(plan.patternsNeeded)) + " PATTERNS";
			if(plan.skippedPatterns)
				m_hint << " (" << int(plan.skippedPatterns) << " WITHOUT KIT)";
			if(!plan.sampleSlots.empty())
			{
				m_hint << "  -  REPLACES SAMPLES";
				for(size_t i = 0; i < plan.sampleSlots.size();)
				{
					size_t j = i;
					while(j + 1 < plan.sampleSlots.size() && plan.sampleSlots[j + 1] == plan.sampleSlots[j] + 1) ++j;
					m_hint << " " << int(plan.sampleSlots[i] + 1);
					if(j > i) m_hint << "-" << int(plan.sampleSlots[j] + 1);
					i = j + 1;
				}
			}
		}
		repaint();
	}

	void LibraryView::importMarked()
	{
		if(auto* session = m_processor.librarySession(m_model); session && !packs().empty())
			session->importItems(packs()[size_t(m_columns[0].selected)], markedItems());
	}

	void LibraryView::playKit()
	{
		auto* session = m_processor.librarySession(m_model);
		if(selectedItem() && session)
			session->playKit(packs()[size_t(m_columns[0].selected)], size_t(m_columns[1].selected));
	}

	void LibraryView::playPattern()
	{
		const auto* item = selectedItem();
		auto* session = m_processor.librarySession(m_model);
		if(item && session && item->kind == md::packlib::ItemKind::Pattern)
			session->playPattern(packs()[size_t(m_columns[0].selected)], size_t(m_columns[1].selected));
	}

	void LibraryView::play()
	{
		const auto* item = selectedItem();
		if(item && item->kind == md::packlib::ItemKind::Pattern) playPattern();
		else playKit();
	}

	juce::Rectangle<int> LibraryView::tabBounds(const Layout& _layout, const md::MachineModel _model) const
	{
		const auto width = _layout.tabs.getWidth() / 4;
		const auto first = _layout.tabs.withWidth(width).withTrimmedBottom(_layout.row / 3);
		return _model == md::MachineModel::Machinedrum ? first : first.translated(width + _layout.row / 2, 0);
	}

	void LibraryView::mouseDown(const juce::MouseEvent& _event)
	{
		grabKeyboardFocus();
		const auto l = layout();
		const auto p = _event.getPosition();
		for(const auto model : {md::MachineModel::Machinedrum, md::MachineModel::Monomachine})
			if(model != m_model && tabBounds(l, model).contains(p))
				return showMachine(model);
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
				if(column == 0 && index < int(packs().size()) && index != m_columns[0].selected)
					select(0, index);
				continue;
			}
			// Finder-style: Cmd toggles one item, Shift extends from the cursor.
			const auto mods = _event.mods;
			if((mods.isCommandDown() || mods.isShiftDown()) && _event.getNumberOfClicks() > 1)
				continue;
			const auto anchor = m_columns[1].selected;
			if(mods.isCommandDown())
			{
				m_columns[1].selected = index;
				if(!m_marked.erase(index)) m_marked.insert(index);
				marksChanged();
			}
			else if(mods.isShiftDown())
			{
				for(int i = std::min(anchor, index); i <= std::max(anchor, index); ++i) m_marked.insert(i);
				m_columns[1].selected = index;
				marksChanged();
			}
			else
				select(1, index);
		}
	}

	void LibraryView::mouseDoubleClick(const juce::MouseEvent& _event)
	{
		if(layout().items.contains(_event.getPosition()) && !_event.mods.isCommandDown() && !_event.mods.isShiftDown())
			play();
	}

	void LibraryView::mouseWheelMove(const juce::MouseEvent& _event, const juce::MouseWheelDetails& _wheel)
	{
		const auto l = layout();
		const int column = l.items.contains(_event.getPosition()) ? 1 : 0;
		const auto count = column == 0 ? int(packs().size()) : itemCount();
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
		else if(code == juce::KeyPress::tabKey)
			showMachine(m_model == md::MachineModel::Machinedrum ? md::MachineModel::Monomachine : md::MachineModel::Machinedrum);
		else if(_key.getModifiers().isCommandDown() && (code == 'A' || code == 'a'))
		{
			if(!itemCount()) return true;
			for(int i = 0; i < itemCount(); ++i) m_marked.insert(i);
			m_focus = 1;
			marksChanged();
		}
		else return false;
		return true;
	}

	void LibraryView::timerCallback()
	{
		// Both machines keep working while the other tab is shown.
		for(const auto model : {md::MachineModel::Machinedrum, md::MachineModel::Monomachine})
			if(auto* session = m_processor.librarySession(model))
				session->service();
		juce::String shown;
		for(const auto model : {md::MachineModel::Machinedrum, md::MachineModel::Monomachine})
			if(const auto* session = m_processor.librarySession(model))
				shown << session->status() << (session->busy() ? "*" : "") << "\n";
		if(shown != m_shownStatus)
		{
			m_shownStatus = shown;
			repaint();
		}
	}
}
