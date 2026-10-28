#pragma once

#include "mdpanel.h"

#include <vector>

// Timed front-panel key sequences, shared by the plug-in and firmware tests so
// the tests prove the exact sequence the plug-in plays.
namespace md
{
	struct PanelStep
	{
		uint8_t row = 0;
		uint8_t mask = 0;
		uint32_t waitFrames = 0;   // emulated frames to let pass after this event
	};

	namespace panelScript
	{
		constexpr uint32_t g_pressFrames = 2048;
		constexpr uint32_t g_settleFrames = 6400;

		inline void tap(std::vector<PanelStep>& _script, const MachineModel _model, const PanelControl _control, const unsigned _times = 1)
		{
			const auto packet = panelPacket(_model, _control);
			for(unsigned i = 0; packet && i < _times; ++i)
			{
				_script.push_back({packet->row, packet->mask, g_pressFrames});
				_script.push_back({packet->row, 0, g_settleFrames});
			}
		}

		inline void functionChord(std::vector<PanelStep>& _script, const MachineModel _model, const PanelControl _control)
		{
			const auto function = panelPacket(_model, PanelControl::Function);
			const auto target = panelPacket(_model, _control);
			if(!function || !target) return;
			PanelRowState rows;
			for(const auto packet : {rows.press(*function), rows.press(*target), rows.release(*target), rows.release(*function)})
				_script.push_back({packet.row, packet.mask, g_pressFrames});
			_script.back().waitFrames += g_settleFrames;
		}
	}

	// Leaves any menu: the machine ends on its main screen.
	inline std::vector<PanelStep> exitMenusScript(const MachineModel _model)
	{
		std::vector<PanelStep> script;
		panelScript::tap(script, _model, PanelControl::Exit, 8);
		return script;
	}

	// Monomachine GLOBAL > FILE > SYSEX RECEIVE, from the main screen: the only
	// place where the firmware accepts kit and pattern dumps.
	inline std::vector<PanelStep> mmSysexReceiveScript()
	{
		constexpr auto mm = MachineModel::Monomachine;
		auto script = exitMenusScript(mm);
		panelScript::functionChord(script, mm, PanelControl::Kit);
		panelScript::tap(script, mm, PanelControl::Enter);
		panelScript::tap(script, mm, PanelControl::Left, 4);
		panelScript::tap(script, mm, PanelControl::Up, 8);
		panelScript::tap(script, mm, PanelControl::Down, 2);
		panelScript::tap(script, mm, PanelControl::Right);
		panelScript::tap(script, mm, PanelControl::Up, 8);
		panelScript::tap(script, mm, PanelControl::Down);
		panelScript::tap(script, mm, PanelControl::Enter);
		panelScript::tap(script, mm, PanelControl::Right);
		panelScript::tap(script, mm, PanelControl::Enter);
		return script;
	}
}
