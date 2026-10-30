/**********************************************************************************************
*
*   [gX: src/rhi/rhi_buffer_audit.h]
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


//opencode: opt-in render-target lifecycle audit, parallel to the GL-state audit.
#ifndef RHI_BUFFER_AUDIT_H
#define RHI_BUFFER_AUDIT_H

#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GX_RHI_BUFFER_AUDIT_BIND_BOTH,
    GX_RHI_BUFFER_AUDIT_BIND_DRAW,
    GX_RHI_BUFFER_AUDIT_BIND_READ
} gX_RHI_BufferAuditBindMode;

gX_I1 gX_RHI_BufferAudit_IsEnabled(gX_I0);
gX_I0 gX_RHI_BufferAudit_BeginFrame(gX_U64 frame);
gX_I0 gX_RHI_BufferAudit_EndFrame(gX_I0);
gX_I0 gX_RHI_BufferAudit_BeginPass(const gX_I8 *name);
gX_I0 gX_RHI_BufferAudit_EndPass(gX_I0);
gX_I0 gX_RHI_BufferAudit_RegisterTarget(gX_U32 fbo, gX_U32 colorTexture,
    gX_U32 depthTexture, gX_U32 width, gX_U32 height, const gX_I8 *name);
gX_I0 gX_RHI_BufferAudit_UnregisterTarget(gX_U32 fbo);
gX_I0 gX_RHI_BufferAudit_TargetBegin(gX_U32 fbo);
gX_I0 gX_RHI_BufferAudit_TargetEnd(gX_I0);
gX_I0 gX_RHI_BufferAudit_FboCheck(gX_RHI_BufferAuditBindMode mode,
    gX_U32 requested, gX_U32 cachedDraw, gX_U32 cachedRead, gX_I1 issuedBind);
gX_I0 gX_RHI_BufferAudit_Clear(gX_U32 mask);
gX_I0 gX_RHI_BufferAudit_Draw(const gX_I8 *kind, gX_U32 count);
gX_I0 gX_RHI_BufferAudit_TextureBind(gX_U32 texture, gX_U32 unit,
    gX_U32 target);
//opencode: note known complete non-clear writes, such as full-target copies.
gX_I0 gX_RHI_BufferAudit_MarkFullWrite(gX_U32 fbo, gX_U32 mask);
gX_I0 gX_RHI_BufferAudit_Blit(gX_U32 source, gX_U32 destination,
    gX_U32 srcX, gX_U32 srcY, gX_U32 srcWidth, gX_U32 srcHeight,
    gX_U32 dstX, gX_U32 dstY, gX_U32 dstWidth, gX_U32 dstHeight,
    gX_U32 mask, gX_U32 filter);

#ifdef __cplusplus
}
#endif

#endif
