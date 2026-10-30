/**********************************************************************************************
*
*   [gX: src/rhi/renderdoc_hook.c]
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

//opencode: v0.2.0 — RenderDoc in-app trigger hook. When the target is launched
//   through `renderdoccmd capture`, RENDERDOC_GetAPI is present in the global
//   symbol table; GX_RD_CAPTURE=1 then marks the frame containing the SDF
//   flush for capture (capture written on frame end as .rdc).
#include <dlfcn.h>
#include <stdlib.h>

#include "renderdoc_hook.h"
#include "../common/log.h"

#include <renderdoc_app.h>

static RENDERDOC_DevicePointer  s_rdocDevice = 0;
static pRENDERDOC_TriggerCapture s_trigger = NULL;
static pRENDERDOC_SetCaptureOptionU32 s_optU32 = NULL;
static gX_I1 s_init = false;

gX_I0 gX_RenderDoc_Init(gX_I0)
{
    if (s_init) return;
    s_init = true;

    const gX_I8 *env = getenv("GX_RD_CAPTURE");
    if (!(env && env[0] == '1')) return;

    pRENDERDOC_GetAPI getAPI =
        (pRENDERDOC_GetAPI)dlsym(RTLD_DEFAULT, "RENDERDOC_GetAPI");
    if (!getAPI) {
        gX_TraceLog(gX_LOG_NOTICE,
            "RenderDoc: not injected (launch via renderdoccmd capture to enable)");
        return;
    }
    int ok = getAPI(eRENDERDOC_API_Version_1_0_0, (void **)&s_rdocDevice);
    if (!ok || !s_rdocDevice) {
        gX_TraceLog(gX_LOG_WARNING, "RenderDoc: GetAPI(v1.4.0) failed");
        return;
    }
    RENDERDOC_API_1_0_0 *api = (RENDERDOC_API_1_0_0 *)s_rdocDevice;
    s_trigger = api->TriggerCapture;
    (void)s_optU32;

    gX_TraceLog(gX_LOG_NOTICE,
        "RenderDoc: API v1.4 acquired — GX_RD_CAPTURE frames will be captured");
}

gX_I0 gX_RenderDoc_Trigger(gX_I0)
{
    static gX_I1 done = false;
    if (!s_trigger || done) return;
    const gX_I8 *env = getenv("GX_RD_CAPTURE");
    if (!(env && env[0] == '1')) return;
    done = true;
    s_trigger();
    gX_TraceLog(gX_LOG_NOTICE, "RenderDoc: capture triggered this frame");
}
