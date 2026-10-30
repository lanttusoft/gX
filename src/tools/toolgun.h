/**********************************************************************************************
*
*   [gX: src/tools/toolgun.h]
*   Toolgun — shared crosshair/tool mode for in-engine editors
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

//ai: a second interaction mode for editors (GMod-style): a crosshair at the
//   screen centre, a blocky procedural viewmodel drawn purely from the
//   camera (no assets), primary/secondary fire reported as aim actions and
//   the wheel cycling tools. The host editor owns its camera and its per-tool
//   effects; this module owns the mode state, the input routing and the
//   presentation, so an editor adopts it by Opening a tool table, Stepping
//   once per frame while active, and Drawing from its 2D overlay after the
//   world render.
#ifndef TOOLS_TOOLGUN_H
#define TOOLS_TOOLGUN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../common/config.h"
#include "../common/types.h"

#define GX_TOOLS_TOOLGUN_TOOLS_MAX 12

//ai: one entry of the host's tool table — the name/hint shown in the readout
//   and the accent used by the crosshair, the viewmodel tip and the readout.
typedef struct gX_Tools_ToolgunTool {
    const gX_I8 *name;
    const gX_I8 *hint;
    gX_Color accent;
} gX_Tools_ToolgunTool;

gX_I0 gX_Tools_Toolgun_Open(const gX_Tools_ToolgunTool *tools, gX_I32 count);
gX_I0 gX_Tools_Toolgun_Close(gX_I0);
gX_I1 gX_Tools_Toolgun_Active(gX_I0);
gX_I32 gX_Tools_Toolgun_ToolCount(gX_I0);
gX_I32 gX_Tools_Toolgun_Tool(gX_I0);
const gX_I8 *gX_Tools_Toolgun_ToolName(gX_I32 index);
gX_I0 gX_Tools_Toolgun_Select(gX_I32 index);
gX_I0 gX_Tools_Toolgun_Cycle(gX_I32 delta);
gX_I0 gX_Tools_Toolgun_SetAim(gX_I1 aimed);
//ai: per-frame poll — the wheel cycles tools and the mouse buttons become
//   this frame's down/edge states. Hosts call it from their frame while
//   active, then read the Fire accessors to apply the tool.
gX_I0 gX_Tools_Toolgun_Step(gX_I0);
gX_I1 gX_Tools_Toolgun_Fire(gX_I1 secondary);
gX_I1 gX_Tools_Toolgun_FirePressed(gX_I1 secondary);
//ai: presentation — the crosshair, the tool readout and the viewmodel, drawn
//   in the 2D overlay after the world render (the viewmodel projects through
//   the camera's own perspective, so it reads as attached to the view).
gX_I0 gX_Tools_Toolgun_Draw(gX_Camera3D camera3D);

#ifdef __cplusplus
}
#endif

#endif //TOOLS_TOOLGUN_H
