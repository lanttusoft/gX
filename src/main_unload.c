/**********************************************************************************************
*
*   [gX: src/main_unload.c]
*   Unloading stage (unload time)
*
*   Copyright (c) 2026 lanttu
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


#include "main.h"
#include "input.h"
#include "wrappers/wrappers.h"

#include "audio.h"

#include "gui/gui.h"

#if gX_ENABLE_RENDERER == 1
    #include "renderer/renderer.h"
#endif
#if gX_ENABLE_PHYSICS == 1
    #include "physics/physics.h"
#endif
#if gX_ENABLE_SCENE == 1
    #include "scene/scene.h"
#endif

#include "resource/resource.h"
#include "settings.h"

#include "module/module.h"

#include "common/log.h"

static gX_I32 gX_Backend_DeInit(gX_I0) {
    gX_I32 errors = 0;
    
    if (maindata.backendUse || maindata.backendLoaded) {
        gX_Backend_Unload();
        maindata.backendLoaded = false;
    }
    if (maindata.backendAudioUse || maindata.backendAudioLoaded) {
        gX_BackendAudio_Unload();
        maindata.backendAudioLoaded = false;
    }
    
    return errors;
}

gX_I32 gX_Unload(gX_I0);
gX_I32 gX_Unload(gX_I0) {
    gX_I32 errors = 0;
    
    gX_TraceLog_MAIN(gX_LOG_INFO,"Unloading stuff");
    
    //opencode: modules shut down (shell console/scripting unload) before any
    //   engine module teardown; handles stay open per the module policy.
    gX_Module_PreUnload();
    gX_Module_Unload();
    
    gX_Settings_Save(); //opencode: gX — persist settings on exit
    errors = gX_Audio_Unload();
    
    errors = gX_GUI_Unload();

#if gX_ENABLE_RENDERER == 1
    errors = gX_Renderer_Unload();
#endif

#if gX_ENABLE_SCENE == 1
    errors = gX_Scene_Unload();
#endif

#if gX_ENABLE_PHYSICS == 1
    errors = gX_Physics_Shutdown();
#endif

    errors = gX_ResourceManager_Unload();
    
    errors = gX_Input_Unload();
    
    errors = gX_Backend_DeInit();
    
    gX_TraceLog_MAIN(gX_LOG_INFO,"Unloading done");
    
    if (errors > 0) {
        gX_TraceLog(gX_LOG_WARNING,"MAIN: errors: %i",errors);
    }
    
    return errors;
}
