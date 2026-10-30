/**********************************************************************************************
*
*   [gX: src/renderer/sky_tests.c]
*   Sky preset tests — look parameters + the ephemeris clock, guard
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


//opencode: headless pins for the sky preset IO (no GPU/window): look-field
//   round trip with the ephemeris clock, a missing-file failure, the shipped
//   sample preset loading, and the shipped-path harness guard.
#include "sky.h"

#include "../game/game.h"
#include "../common/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SKY_TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { gX_TraceLog(gX_LOG_ERROR, "SKY TEST FAIL: %s", msg); errors++; } \
} while (0)

gX_I32 gX_Tests_Sky(gX_I0);
gX_I32 gX_Tests_Sky(gX_I0) {
    gX_I32 errors = 0;

    //opencode: static — the renderer struct is large and the test only touches
    //   data fields (preset IO has no GL dependency).
    static gX_RendererSky sky;
    memset(&sky, 0, sizeof(sky));
    gX_Ephemeris_SetupDefaults(&gamedata.ephemeris);

    gX_I8 path[128];
    snprintf((char *)path, sizeof(path), "/tmp/opencode/gx_sky_preset_%ld.json",
             (long)getpid());

    sky.skyDensity = 1.7f;
    sky.turbidity = 6.5f;
    sky.moonBrightness = 0.25f;
    sky.showHelpers = 1;
    gamedata.ephemeris.timeSec = 12345.0;
    SKY_TEST_ASSERT(gX_RendererSky_Preset_Save(&sky, path) == 0,
        "sky preset saved");

    sky.skyDensity = 0.0f;
    sky.turbidity = 0.0f;
    sky.moonBrightness = 0.0f;
    sky.showHelpers = 0;
    gamedata.ephemeris.timeSec = 0.0;
    SKY_TEST_ASSERT(gX_RendererSky_Preset_Load(&sky, path) == 0,
        "sky preset loaded");
    SKY_TEST_ASSERT(sky.skyDensity == 1.7f && sky.turbidity == 6.5f
        && sky.moonBrightness == 0.25f && sky.showHelpers == 1,
        "look fields restored");
    SKY_TEST_ASSERT(gamedata.ephemeris.timeSec == 12345.0, "clock restored");

    SKY_TEST_ASSERT(gX_RendererSky_Preset_Load(&sky,
        "/tmp/opencode/gx_sky_preset_missing.json") == 1, "missing preset fails");

    //opencode: the shipped sample must stay loadable (the editor's fixture
    //   list ships it).
    SKY_TEST_ASSERT(gX_RendererSky_Preset_Load(&sky,
        "resources/tools/sky/sky_sample.json") == 0, "shipped sample loads");
    SKY_TEST_ASSERT(gamedata.ephemeris.timeSec == 9000.0
        && sky.skyHue > 0.34f && sky.skyHue < 0.36f, "sample fields applied");

    //opencode: harness guard — the probe path is assembled at runtime (the
    //   deps audit scans literals; this deliberate non-file must not read as
    //   a missing reference) and the target dir exists (a broken guard would
    //   actually write there, caught by the not-created assert). The prior
    //   env value is saved and restored: an inherited harness guard must
    //   survive the test (an unconditional unsetenv disabled the guard for
    //   the rest of the process and the preset gate caught it).
    gX_I8 guardPath[128];
    snprintf((char *)guardPath, sizeof(guardPath),
             "%s/tools/sky/should_not_write.json", "resources");
    const gX_I8 *guardPrev = (const gX_I8 *)getenv("GX_NO_SETTINGS_SAVE");
    gX_I8 *guardSaved = guardPrev ? (gX_I8 *)strdup((const char *)guardPrev) : NULL;
    SKY_TEST_ASSERT(!guardPrev || guardSaved, "harness guard snapshot allocated");
    if (!guardPrev || guardSaved)
    {
        gX_I32 guardSet = setenv("GX_NO_SETTINGS_SAVE", "1", 1);
        SKY_TEST_ASSERT(guardSet == 0, "harness guard set");
        if (guardSet == 0)
        {
            SKY_TEST_ASSERT(gX_RendererSky_Preset_Save(&sky, guardPath) == 1,
                "shipped preset refused under the harness guard");
        }
        gX_I32 guardRestored = guardSaved
            ? setenv("GX_NO_SETTINGS_SAVE", (const char *)guardSaved, 1)
            : unsetenv("GX_NO_SETTINGS_SAVE");
        SKY_TEST_ASSERT(guardRestored == 0, "inherited harness guard restored");
        free(guardSaved);
    }
    FILE *check = fopen((const char *)guardPath, "rb");
    SKY_TEST_ASSERT(check == NULL, "refusal wrote nothing");
    if (check) fclose(check);

    remove((const char *)path);
    if (errors == 0) {
        gX_TraceLog(gX_LOG_INFO, "GX_TESTS: sky preset — 0 errors");
    }
    return errors;
}
