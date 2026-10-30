/**********************************************************************************************
*
*   [gX: src/tools/tools.h]
*   gX — a game engine
*
*   Copyright (c) 2026 lanttu, opencode
*
*
*   SPDX-License-Identifier: GPL-3.0-or-later
*
*   This file is part of gX.
*   gX is free software: you can redistribute it and/or modify it under the
*   terms of the GNU General Public License as published by the Free Software
*   Foundation, either version 3 of the License, or (at your option) any later
*   version.
*   gX is distributed in the hope that it will be useful, but WITHOUT ANY
*   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*   FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*   details.
*   You should have received a copy of the GNU General Public License along
*   with gX. If not, see <https://www.gnu.org/licenses/>.
*
*
**********************************************************************************************/


#ifndef GX_TOOLS_H
#define GX_TOOLS_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

//opencode: v0.6.5 — runtime tool open/close (EXTRAS menu). CLI flags init at
//   load; these work from in-app buttons at any time.
gX_I1 gX_Tools_RuntimeActive(gX_I0);
gX_I0 gX_Tools_OpenEditor_Runtime(gX_I0);
gX_I0 gX_Tools_OpenTheme_Runtime(gX_I0);
gX_I0 gX_Tools_OpenThemLab_Runtime(gX_I0);
gX_I0 gX_Tools_OpenMaterials_Runtime(gX_I0);
//opencode: shader experiment gallery runtime opener.
gX_I0 gX_Tools_OpenShaderLab_Runtime(gX_I0);
gX_I0 gX_Tools_OpenGfx_Runtime(gX_I0);
//opencode: wavetable editor — synth wavetable authoring + .f32 fixtures.
gX_I0 gX_Tools_OpenWavetable_Runtime(gX_I0);
//opencode: textwriter editor — line/tag/choice authoring with scene preview.
gX_I0 gX_Tools_OpenTextwriter_Runtime(gX_I0);
//opencode: camera rail editor — module-drawn (gx_shell) rail panel.
gX_I0 gX_Tools_OpenRail_Runtime(gX_I0);
//opencode: background editor — focused background authoring + presets.
gX_I0 gX_Tools_OpenBackground_Runtime(gX_I0);
//opencode: sky editor — look parameters + the ephemeris clock.
gX_I0 gX_Tools_OpenSky_Runtime(gX_I0);
//ai: scripting debugger panel opener (opt-in scripting builds).
gX_I0 gX_Tools_OpenScript_Runtime(gX_I0);
gX_I0 gX_Tools_Close_Runtime(gX_I0);
//opencode: edit lock — editor target canvas inert while edited.
gX_I0 gX_Tools_Editor_ClearEditLock(gX_I0);
gX_I0 gX_Tools_Editor_SoloRestore(gX_I0);
//opencode: window-X close writes back through these (ImGui Begin p_open).
gX_I1 *gX_Tools_RuntimeEditorFlag(gX_I0);
gX_I1 *gX_Tools_RuntimeThemeFlag(gX_I0);
gX_I1 *gX_Tools_RuntimeGfxFlag(gX_I0);
gX_I1 *gX_Tools_RuntimeScriptFlag(gX_I0);
gX_I1 *gX_Tools_RuntimeWavetableFlag(gX_I0);
gX_I1 *gX_Tools_RuntimeTextwriterFlag(gX_I0);
gX_I1 *gX_Tools_RuntimeRailFlag(gX_I0);
gX_I1 *gX_Tools_RuntimeBackgroundFlag(gX_I0);
gX_I1 *gX_Tools_RuntimeSkyFlag(gX_I0);

#ifdef __cplusplus
}
#endif

#endif
