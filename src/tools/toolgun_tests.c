/**********************************************************************************************
*
*   [gX: src/tools/toolgun_tests.c]
*   Toolgun framework unit tests
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

//ai: toolgun framework tests — mode state, tool selection/cycling and the
//   fire accessors, all headless (no Step: input needs a window). The
//   sandbox-side tool effects are pinned by the scene tool tests.
#include <stdio.h>
#include <string.h>

#include "toolgun.h"
#include "../common/log.h"

gX_I32 gX_Tests_Toolgun(gX_I0);

#define CHECK(cond) do { if (!(cond)) { errors++; gX_TraceLog(gX_LOG_WARNING, "GX_TESTS: FAIL %s", #cond); } } while(0)

static const gX_Tools_ToolgunTool s_tools[3] = {
    {"ALPHA", "first tool", {10, 20, 30, 255}},
    {"BETA", "second tool", {40, 50, 60, 255}},
    {"GAMMA", "third tool", {70, 80, 90, 255}}
};

gX_I32 gX_Tests_Toolgun(gX_I0) {
    gX_I32 errors = 0;

    CHECK(gX_Tools_Toolgun_Active() == 0);
    CHECK(gX_Tools_Toolgun_ToolCount() == 0);
    gX_Tools_Toolgun_Select(1);
    gX_Tools_Toolgun_Cycle(1);
    gX_Tools_Toolgun_Close();
    CHECK(gX_Tools_Toolgun_Active() == 0);

    gX_Tools_Toolgun_Open(NULL, 3);
    CHECK(gX_Tools_Toolgun_Active() == 0);
    gX_Tools_Toolgun_Open(s_tools, 0);
    CHECK(gX_Tools_Toolgun_Active() == 0);

    gX_Tools_Toolgun_Open(s_tools, 3);
    CHECK(gX_Tools_Toolgun_Active() == 1);
    CHECK(gX_Tools_Toolgun_ToolCount() == 3);
    CHECK(gX_Tools_Toolgun_Tool() == 0);
    CHECK(strcmp((const char *)gX_Tools_Toolgun_ToolName(0), "ALPHA") == 0);
    CHECK(strcmp((const char *)gX_Tools_Toolgun_ToolName(2), "GAMMA") == 0);
    CHECK(gX_Tools_Toolgun_ToolName(-1)[0] == '\0');
    CHECK(gX_Tools_Toolgun_ToolName(3)[0] == '\0');

    gX_Tools_Toolgun_Select(2);
    CHECK(gX_Tools_Toolgun_Tool() == 2);
    gX_Tools_Toolgun_Select(-1);
    CHECK(gX_Tools_Toolgun_Tool() == 2);
    gX_Tools_Toolgun_Select(3);
    CHECK(gX_Tools_Toolgun_Tool() == 2);
    gX_Tools_Toolgun_Cycle(1);
    CHECK(gX_Tools_Toolgun_Tool() == 0);
    gX_Tools_Toolgun_Cycle(0);
    CHECK(gX_Tools_Toolgun_Tool() == 0);
    gX_Tools_Toolgun_Cycle(-1);
    CHECK(gX_Tools_Toolgun_Tool() == 2);
    gX_Tools_Toolgun_Cycle(-1);
    CHECK(gX_Tools_Toolgun_Tool() == 1);

    CHECK(gX_Tools_Toolgun_Fire(0) == 0);
    CHECK(gX_Tools_Toolgun_Fire(1) == 0);
    CHECK(gX_Tools_Toolgun_FirePressed(0) == 0);
    CHECK(gX_Tools_Toolgun_FirePressed(1) == 0);
    gX_Tools_Toolgun_SetAim(1);
    gX_Tools_Toolgun_SetAim(0);

    gX_Tools_Toolgun_Close();
    CHECK(gX_Tools_Toolgun_Active() == 0);
    CHECK(gX_Tools_Toolgun_ToolCount() == 0);
    CHECK(gX_Tools_Toolgun_ToolName(0)[0] == '\0');
    gX_Tools_Toolgun_Select(0);
    CHECK(gX_Tools_Toolgun_Tool() == 0);

    gX_Tools_Toolgun_Open(s_tools, 2);
    CHECK(gX_Tools_Toolgun_ToolCount() == 2);
    gX_Tools_Toolgun_Select(1);
    gX_Tools_Toolgun_Cycle(1);
    CHECK(gX_Tools_Toolgun_Tool() == 0);
    gX_Tools_Toolgun_Close();

    if (errors == 0) gX_TraceLog(gX_LOG_INFO, "TOOLGUN TESTS: all passed");
    return errors;
}
