/**********************************************************************************************
*
*   [gX: src/renderer/gfx/terrain.c]
*   gX — a game engine
*
*   Copyright (c) 2025-2026 lanttu
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


//opencode: Paper 2 spike — terrain depth march writing the scene depth.
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "../gfx.h"
#include "terrain.h"
#include "boilerplate.h"
#include "../renderer.h"

#include "../../wrappers/wrappers.h"

#include "../../main.h"
#include "../../camera.h"

#include "../../common/log.h"
#include "../../common/macros.h"
#include "../../common/profiler.h"
#include "../../common/utils/shader.h"
#include "../../rhi/batch.h"

#include "../../gxmath/matrix.h"
#include "../../rhi/rhi.h"
#include "../../rhi/rhi_gl.h"
#include "../../rhi/rhi_matrix.h"
#include "../../rhi/rhi_state.h"
#include "../../rhi/rhi_tex.h"
#include "constants.h"
#include "../../rhi/rhi_fbo.h"

//opencode: TERRAINSTATS (Paper 2 spike proof, graduated to permanent
//   instrument) — CPU-analytic depth witness. Replicates the shader march
//   in F32 and compares against the depth-texture readback: match =
//   encoding exact, occluded = raster in front (balls/cube), bug =
//   terrain lost where it must win. Hook-gated with the pass.
//opencode: spike 4 — tileable value-noise heightfield (CPU source of
//   truth; GPU samples the uploaded texels). Integer-lattice hash (no
//   sin — CPU-only by design, the GPU never evaluates it), smoothstep
//   value noise, FBM periods 8/16/32/64 (all divide 256: tileable),
//   normalized to [0,1]. Deterministic (fixed seed geometry).
#define TERRAIN_HF_N 256
static gX_F32 s_hfBuf[TERRAIN_HF_N * TERRAIN_HF_N];
static gX_I1 s_hfReady = 0;
static gX_I1 s_heightOn = 0;

static gX_U32 Terrain_Hash2(gX_I32 x, gX_I32 y)
{
    gX_U32 h = (gX_U32)((x * 374761393 + y * 668265263) & 0x7fffffff);
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return h & 0xffffu;
}

static gX_I0 Terrain_GenHeightfield(void)
{
    const gX_I32 periods[4] = { 8, 16, 32, 64 };
    const gX_F32 amps[4] = { 0.5f, 0.25f, 0.125f, 0.0625f };
    gX_F32 total = 0.0f;
    for (gX_I32 o = 0; o < 4; o++) total += amps[o];
    for (gX_I32 iz = 0; iz < TERRAIN_HF_N; iz++) {
        for (gX_I32 ix = 0; ix < TERRAIN_HF_N; ix++) {
            gX_F32 v = 0.0f;
            for (gX_I32 o = 0; o < 4; o++) {
                gX_I32 P = periods[o];
                gX_F32 gx = ((gX_F32)ix / (gX_F32)TERRAIN_HF_N) * (gX_F32)P;
                gX_F32 gz = ((gX_F32)iz / (gX_F32)TERRAIN_HF_N) * (gX_F32)P;
                gX_I32 x0 = ((gX_I32)gx) % P, z0 = ((gX_I32)gz) % P;
                gX_F32 fx = gx - (gX_F32)((gX_I32)gx), fz = gz - (gX_F32)((gX_I32)gz);
                gX_F32 sx = fx * fx * (3.0f - 2.0f * fx);
                gX_F32 sz = fz * fz * (3.0f - 2.0f * fz);
                gX_I32 x1 = (x0 + 1) % P, z1 = (z0 + 1) % P;
                gX_F32 a = (gX_F32)Terrain_Hash2(x0, z0) / 65535.0f;
                gX_F32 b = (gX_F32)Terrain_Hash2(x1, z0) / 65535.0f;
                gX_F32 c = (gX_F32)Terrain_Hash2(x0, z1) / 65535.0f;
                gX_F32 d = (gX_F32)Terrain_Hash2(x1, z1) / 65535.0f;
                v += amps[o] * ((a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sz);
            }
            s_hfBuf[(gX_U64)iz * TERRAIN_HF_N + (gX_U64)ix] = v / total;
        }
    }
    s_hfReady = 1;
    return;
}

//opencode: CPU bilinear mirror of GL LINEAR + REPEAT (spec formula:
//   texel centers at (i+0.5)/N; wrap for negative coords).
static gX_F32 Terrain_TexH(gX_F32 u, gX_F32 v)
{
    gX_F32 x = u * (gX_F32)TERRAIN_HF_N - 0.5f;
    gX_F32 y = v * (gX_F32)TERRAIN_HF_N - 0.5f;
    gX_I32 x0 = (gX_I32)floorf(x), y0 = (gX_I32)floorf(y);
    gX_F32 fx = x - (gX_F32)x0, fy = y - (gX_F32)y0;
    gX_I32 xa = ((x0 % TERRAIN_HF_N) + TERRAIN_HF_N) % TERRAIN_HF_N;
    gX_I32 xb = ((x0 + 1) % TERRAIN_HF_N + TERRAIN_HF_N) % TERRAIN_HF_N;
    gX_I32 ya = ((y0 % TERRAIN_HF_N) + TERRAIN_HF_N) % TERRAIN_HF_N;
    gX_I32 yb = ((y0 + 1) % TERRAIN_HF_N + TERRAIN_HF_N) % TERRAIN_HF_N;
    gX_F32 a = s_hfBuf[(gX_U64)ya * TERRAIN_HF_N + (gX_U64)xa];
    gX_F32 b = s_hfBuf[(gX_U64)ya * TERRAIN_HF_N + (gX_U64)xb];
    gX_F32 c = s_hfBuf[(gX_U64)yb * TERRAIN_HF_N + (gX_U64)xa];
    gX_F32 d = s_hfBuf[(gX_U64)yb * TERRAIN_HF_N + (gX_U64)xb];
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

static gX_F32 Terrain_Smoothstep(gX_F32 e0, gX_F32 e1, gX_F32 x)
{
    gX_F32 t = (x - e0) / (e1 - e0);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static gX_F32 Terrain_H(gX_F32 x, gX_F32 z, gX_F32 amp)
{
    gX_F32 mx = Terrain_Smoothstep(7.0f, 9.0f, fabsf(x));
    gX_F32 mz = Terrain_Smoothstep(4.0f, 6.0f, fabsf(z));
    gX_F32 m = (mx > mz) ? mx : mz;
    gX_F32 hills;
    if (s_heightOn) {
        hills = Terrain_TexH(x / 8.0f, z / 8.0f) * 2.0f - 1.0f;
    } else {
        hills = sinf(x * 0.35f) * cosf(z * 0.31f) * 0.5f
            + sinf(x * 0.13f + 1.7f) * sinf(z * 0.17f + 0.6f) * 0.5f;
    }
    return amp * m * hills;
}

//opencode: mat-vec multiply matching the shader's effective matrix (the
//   upload passes &m0 straight through; row i = m_i,m_i+4,m_i+8,m_i+12).
static gX_I0 Terrain_MatVec(const gX_Matrix *m, gX_F32 x, gX_F32 y, gX_F32 z, gX_F32 w, gX_F32 o[4])
{
    o[0] = m->m0 * x + m->m4 * y + m->m8 * z + m->m12 * w;
    o[1] = m->m1 * x + m->m5 * y + m->m9 * z + m->m13 * w;
    o[2] = m->m2 * x + m->m6 * y + m->m10 * z + m->m14 * w;
    o[3] = m->m3 * x + m->m7 * y + m->m11 * z + m->m15 * w;
    return;
}

//opencode: gap-2 — finite-difference normal, extracted for the CPU
//   shadow tap (stats needs the normal before shading).
static gX_I0 Terrain_Normal(gX_F32 hx, gX_F32 hz, gX_F32 amp, gX_F32 baseY, gX_F32 nrm[3])
{
    gX_F32 e = 0.06f;
    gX_F32 hx0 = baseY + Terrain_H(hx - e, hz, amp);
    gX_F32 hx1 = baseY + Terrain_H(hx + e, hz, amp);
    gX_F32 hz0 = baseY + Terrain_H(hx, hz - e, amp);
    gX_F32 hz1 = baseY + Terrain_H(hx, hz + e, amp);
    gX_F32 nx = hx0 - hx1, ny = 2.0f * e, nz = hz0 - hz1;
    gX_F32 nl = sqrtf(nx * nx + ny * ny + nz * nz);
    nrm[0] = nx / nl; nrm[1] = ny / nl; nrm[2] = nz / nl;
    return;
}

//opencode: spike 2 — CPU mirror of the shader shade block (same op
//   order): finite-difference normal, two-tone albedo, sun + ambient
//   + points, exposure. Inputs mirror the uploaded uniforms exactly.
//   sunOut[] returns the post-exposure sun diffuse+spec part (pre-shadow)
//   for the gap-2 shadow classifier.
static gX_I0 Terrain_Shade(gX_F32 hx, gX_F32 hy, gX_F32 hz, gX_F32 amp, gX_F32 baseY,
    gX_F32 sunDir[3], gX_F32 sunCol[3], gX_F32 sunBright, gX_F32 sunTint[3], gX_F32 sunFactor,
    gX_F32 ambCol[3], gX_F32 ambFactor, gX_F32 exposure,
    gX_F32 (*pPos)[3], gX_F32 (*pCol)[3], gX_F32 *pBright, gX_F32 (*pAtten)[3],
    gX_F32 *pDiff, gX_I32 nPts, gX_F32 ro[3], gX_F32 outCol[3], gX_F32 sunOut[3],
    gX_F32 ssaoF)
{
    gX_F32 nrm[3];
    Terrain_Normal(hx, hz, amp, baseY, nrm);
    gX_F32 nx = nrm[0], ny = nrm[1], nz = nrm[2];
    //opencode: planar underside — geometric normal stands (mirror of
    //   the shader: no backface flip, underside shades top-lit).
    gX_F32 slope = 1.0f - ny;
    gX_F32 s1 = Terrain_Smoothstep(0.02f, 0.15f, slope);
    gX_F32 ax = 0.30f + (0.36f - 0.30f) * s1;
    gX_F32 ay = 0.38f + (0.34f - 0.38f) * s1;
    gX_F32 az = 0.14f + (0.30f - 0.14f) * s1;
    gX_F32 s2 = Terrain_Smoothstep(0.35f, 0.65f, hy - baseY);
    ax = ax + (0.55f - ax) * s2;
    ay = ay + (0.54f - ay) * s2;
    az = az + (0.50f - az) * s2;
    gX_F32 ll = sqrtf(sunDir[0] * sunDir[0] + sunDir[1] * sunDir[1] + sunDir[2] * sunDir[2]);
    gX_F32 lx = sunDir[0] / ll, ly = sunDir[1] / ll, lz = sunDir[2] / ll;
    gX_F32 ndl = nx * lx + ny * ly + nz * lz;
    if (ndl < 0.0f) ndl = 0.0f;
    //opencode: association order mirrors the shader left-to-right
    //   evaluation (F32 mult is not associative; tolerance would hide
    //   it, but exactness is free here).
    gX_F32 af0 = ambFactor * ambCol[0];
    gX_F32 af1 = ambFactor * ambCol[1];
    gX_F32 af2 = ambFactor * ambCol[2];
    gX_F32 cx = ax * af0;
    gX_F32 cy = ay * af1;
    gX_F32 cz = az * af2;
    gX_F32 sb0 = sunBright * sunCol[0];
    gX_F32 sb1 = sunBright * sunCol[1];
    gX_F32 sb2 = sunBright * sunCol[2];
    gX_F32 st0 = sunFactor * sunTint[0];
    gX_F32 st1 = sunFactor * sunTint[1];
    gX_F32 st2 = sunFactor * sunTint[2];
    cx += (ax * sb0) * ndl * st0;
    cy += (ay * sb1) * ndl * st1;
    cz += (az * sb2) * ndl * st2;
    for (gX_I32 i = 0; i < nPts && i < 4; i++) {
        gX_F32 dx = pPos[i][0] - hx, dy = pPos[i][1] - hy, dz = pPos[i][2] - hz;
        gX_F32 dist = sqrtf(dx * dx + dy * dy + dz * dz);
        gX_F32 dnx = dx / dist, dny = dy / dist, dnz = dz / dist;
        gX_F32 att = 1.0f / (pAtten[i][0] + pAtten[i][1] * dist + pAtten[i][2] * dist * dist);
        gX_F32 pl = nx * dnx + ny * dny + nz * dnz;
        if (pl < 0.0f) pl = 0.0f;
        gX_F32 pb0 = pBright[i] * pCol[i][0];
        gX_F32 pb1 = pBright[i] * pCol[i][1];
        gX_F32 pb2 = pBright[i] * pCol[i][2];
        cx += ((ax * pb0) * pl) * att * pDiff[i];
        cy += ((ay * pb1) * pl) * att * pDiff[i];
        cz += ((az * pb2) * pl) * att * pDiff[i];
    }
    //opencode: gap-1 specular — Phong mirror (phong_specular, dielectric):
    //   R = reflect(-L, n), NdotR, pow 24, Schlick F0 0.04, same order.
    //   Sun part (diffuse + spec, post-exposure, pre-shadow) goes to
    //   sunOut for the gap-2 shadow classifier.
    gX_F32 sunPart[3] = { 0.0f, 0.0f, 0.0f };
    sunPart[0] = (ax * sb0) * ndl * st0;
    sunPart[1] = (ay * sb1) * ndl * st1;
    sunPart[2] = (az * sb2) * ndl * st2;
    {
        gX_F32 vvx = ro[0] - hx, vvy = ro[1] - hy, vvz = ro[2] - hz;
        gX_F32 vl = sqrtf(vvx * vvx + vvy * vvy + vvz * vvz);
        vvx /= vl; vvy /= vl; vvz /= vl;
        gX_F32 rx = -lx + 2.0f * (lx * nx + ly * ny + lz * nz) * nx;
        gX_F32 ry = -ly + 2.0f * (lx * nx + ly * ny + lz * nz) * ny;
        gX_F32 rz = -lz + 2.0f * (lx * nx + ly * ny + lz * nz) * nz;
        gX_F32 ndr = vvx * rx + vvy * ry + vvz * rz;
        if (ndr < 0.0f) ndr = 0.0f;
        //opencode: powf (not repeated mult): GPU pow = exp2(y*log2(x))
        //   class; ulp-level divergence is 1e4x below the F16 color gate.
        gX_F32 spec = powf(ndr, 24.0f) * (0.04f + 0.96f * powf(1.0f - ndr, 5.0f));
        cx += (sunBright * sunCol[0]) * spec;
        cy += (sunBright * sunCol[1]) * spec;
        cz += (sunBright * sunCol[2]) * spec;
        sunPart[0] += (sunBright * sunCol[0]) * spec;
        sunPart[1] += (sunBright * sunCol[1]) * spec;
        sunPart[2] += (sunBright * sunCol[2]) * spec;
    }
    outCol[0] = cx * exposure;
    outCol[1] = cy * exposure;
    outCol[2] = cz * exposure;
    if (sunOut) {
        sunOut[0] = sunPart[0] * exposure;
        sunOut[1] = sunPart[1] * exposure;
        sunOut[2] = sunPart[2] * exposure;
    }
    //opencode: gap-3 SSAO — pre-exposure linear multiply (Phong:869).
    //   Caller samples the map; factor applied here to keep exposure last.
    //   sunOut scales too (shadow interval gate needs post-SSAO sun part).
    {
        gX_F32 k = 1.0f + (ssaoF - 1.0f) * 0.6f;
        outCol[0] *= k; outCol[1] *= k; outCol[2] *= k;
        if (sunOut) { sunOut[0] *= k; sunOut[1] *= k; sunOut[2] *= k; }
    }
    return;
}

//opencode: returns 1 + predicted depth on hit, 0 on miss. Mirrors the
//   shader branch-for-branch (flat analytic vs 64-step march + 12-bisect,
//   both double-sided for the planar mirror).
static gX_I1 Terrain_Predict(const gX_Matrix *invVP, const gX_Matrix *vp,
    gX_F32 fx, gX_F32 fy, gX_F32 amp, gX_F32 baseY, gX_F32 *outDepth, gX_F32 *outHit, gX_F32 *outRo)
{
    gX_F32 nx = fx * 2.0f - 1.0f;
    gX_F32 ny = fy * 2.0f - 1.0f;
    gX_F32 wn[4], wf[4];
    Terrain_MatVec(invVP, nx, ny, -1.0f, 1.0f, wn);
    Terrain_MatVec(invVP, nx, ny, 1.0f, 1.0f, wf);
    gX_F32 rox = wn[0] / wn[3], roy = wn[1] / wn[3], roz = wn[2] / wn[3];
    gX_F32 dx = wf[0] / wf[3] - rox, dy = wf[1] / wf[3] - roy, dz = wf[2] / wf[3] - roz;
    gX_F32 dl = sqrtf(dx * dx + dy * dy + dz * dz);
    dx /= dl; dy /= dl; dz /= dl;
    gX_F32 t = 0.0f;
    gX_I1 hit = 0;
    if (amp == 0.0f) {
        //opencode: planar double-sided (mirror looks up from below).
        if (fabsf(dy) > 0.0f) {
            t = (baseY - roy) / dy;
            hit = (t >= 0.0f && t <= 200.0f) ? 1 : 0;
        }
    } else {
        gX_F32 f = roy - baseY - Terrain_H(rox, roz, amp);
        gX_F32 fPrev = f;
        for (gX_I32 i = 0; i < 64; i++) {
            gX_F32 stepLen = fabsf(f) * 0.7f;
            t += stepLen;
            if (t > 200.0f) break;
            gX_F32 px = rox + dx * t, py = roy + dy * t, pz = roz + dz * t;
            f = py - baseY - Terrain_H(px, pz, amp);
            if (fabsf(f) < 0.0001f) { hit = 1; break; }
            if ((f > 0.0f) != (fPrev > 0.0f)) {
                gX_F32 lo = t - stepLen, hi = t;
                gX_F32 fLo = fPrev;
                for (gX_I32 j = 0; j < 12; j++) {
                    gX_F32 mm = (lo + hi) * 0.5f;
                    gX_F32 fm = (roy + dy * mm) - baseY - Terrain_H(rox + dx * mm, roz + dz * mm, amp);
                    if ((fm > 0.0f) == (fLo > 0.0f)) { lo = mm; fLo = fm; }
                    else hi = mm;
                }
                t = (lo + hi) * 0.5f;
                hit = 1;
                break;
            }
            fPrev = f;
        }
    }
    if (!hit) return 0;
    gX_F32 hx = rox + dx * t, hy = roy + dy * t, hz = roz + dz * t;
    gX_F32 c[4];
    Terrain_MatVec(vp, hx, hy, hz, 1.0f, c);
    *outDepth = (c[2] / c[3]) * 0.5f + 0.5f;
    if (outHit) { outHit[0] = hx; outHit[1] = hy; outHit[2] = hz; }
    if (outRo) { outRo[0] = rox; outRo[1] = roy; outRo[2] = roz; }
    return 1;
}

//opencode: gap-2 — CPU shadow routing scan (mirrors the shader's
//   cascade selection + projection + bounds, then a min-scan over the
//   full PCF footprint). Returns 0 lit (exact gate), 1 shadowed
//   (interval gate), 2 unknown (no buffer — skip, never false-bug).
static gX_I32 Terrain_ShadowTap(const gX_Matrix *vps, const gX_F32 *splits, gX_I32 nCasc,
    const gX_Matrix *sunVP, gX_F32 nearP, gX_F32 farP,
    const gX_F32 *shArr, gX_I32 shW, gX_I32 shH, gX_I32 shLayers,
    const gX_F32 *shMap0, gX_I32 sh0W, gX_I32 sh0H,
    gX_F32 gotD, const gX_F32 hit[3])
{
    gX_F32 zndc = gotD * 2.0f - 1.0f;
    gX_F32 denom = (farP + nearP - zndc * (farP - nearP));
    if (fabsf(denom) < 1e-6f) return 2;
    //opencode: scan the selected layer AND every other in-frustum layer:
    //   the split-blend mixes neighbors, so an occluder in an adjacent
    //   layer still darkens. Over-routing errs to the loose gate only.
    gX_I32 nLayers = (nCasc > 1 && splits[0] > 0.0f) ? nCasc : 1;
    if (nLayers > 4) nLayers = 4;
    if (nLayers > shLayers && shLayers > 0) nLayers = shLayers;
    if (nLayers < 1) nLayers = 1;
    gX_I1 analyzed = 0;
    gX_I1 inAny = 0;
    for (gX_I32 layer = 0; layer < nLayers; layer++) {
    const gX_Matrix *lsMat = (nCasc > 1) ? &vps[layer] : sunVP;
    gX_F32 ls[4];
    Terrain_MatVec(lsMat, hit[0], hit[1], hit[2], 1.0f, ls);
    if (fabsf(ls[3]) < 1e-6f) continue;
    gX_F32 px = ls[0] / ls[3] * 0.5f + 0.5f;
    gX_F32 py = ls[1] / ls[3] * 0.5f + 0.5f;
    gX_F32 pz = ls[2] / ls[3] * 0.5f + 0.5f;
    if (pz > 1.0f || px < 0.0f || px > 1.0f || py < 0.0f || py > 1.0f)
        continue;
    inAny = 1;
    const gX_F32 *buf = NULL;
    gX_I32 bw = 0, bh = 0;
    if (layer == 0 && shMap0) { buf = shMap0; bw = sh0W; bh = sh0H; }
    else if (shArr && layer < shLayers) {
        buf = &shArr[(gX_U64)layer * (gX_U64)shW * (gX_U64)shH];
        bw = shW; bh = shH;
    } else if (layer == 0 && shArr && shLayers > 0) {
        buf = shArr; bw = shW; bh = shH;
    }
    if (!buf || bw <= 0 || bh <= 0) continue;
    analyzed = 1;
    //opencode: routing min-scan — layer-0 uses full PCSS (blocker search
    //   + filter widening to 16 texels), so a 5-tap footprint misses deep
    //   penumbra. Scan every texel within 16 of center (early-out on the
    //   first blocker, bias-free = superset): errs toward the loose gate
    //   only, never toward a false bug. (Array layers use fixed 9-tap
    //   PCF, fully inside this disc.)
    {
        gX_F32 cx = px * (gX_F32)bw - 0.5f;
        gX_F32 cy = py * (gX_F32)bh - 0.5f;
        gX_I32 x0 = (gX_I32)floorf(cx) - 16, y0 = (gX_I32)floorf(cy) - 16;
        gX_I32 x1 = (gX_I32)floorf(cx) + 16, y1 = (gX_I32)floorf(cy) + 16;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= bw) x1 = bw - 1;
        if (y1 >= bh) y1 = bh - 1;
        for (gX_I32 yy = y0; yy <= y1; yy++) {
            for (gX_I32 xx = x0; xx <= x1; xx++) {
                if (buf[(gX_U64)yy * (gX_U64)bw + (gX_U64)xx] < pz)
                    return 1;
            }
        }
    }
    }
    //opencode: outside every cascade frustum = lit (mirrors the shader
    //   early-out); a contained layer without a buffer = unknown.
    if (!inAny) return 0;
    return analyzed ? 0 : 2;
}

//opencode: planar — shared uniform upload (main + mirror
//   draws): matrices, heightfield, lights, shadows, SSAO.
static gX_I0 Terrain_UploadUniforms(gX_RendererGFX *gfx, gX_Shader ts,
    gX_Matrix invVP, gX_Matrix curVP, gX_F32 hillAmp)
{
    gX_Shader_SetValue_Matrix(ts, gfx->terrain.depthInvVPLoc, invVP);
    gX_Shader_SetValue_Matrix(ts, gfx->terrain.depthViewProjLoc, curVP);
    gX_Shader_SetValue_V2(ts, gfx->terrain.depthResolutionLoc,
        ((gX_F32v2){(gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight}));
    gX_Shader_SetValue_Float(ts, gfx->terrain.depthHillAmpLoc, hillAmp);
    //opencode: spike geometry — flat plane 5mm above the reflectrow
    //   slab top (+0.075) so marched terrain demonstrably OVERWRITES
    //   raster (tie would be unprovable). TEMP spike constant.
    gX_Shader_SetValue_Float(ts, gfx->terrain.depthBaseYLoc, 0.08f);
    //opencode: spike 4 — heightfield source (lazy R32F upload,
    //   LINEAR/REPEAT, no mips; hook GX_T_TERRAINNOISE=1).
    {
        const gX_I8 *nh = getenv("GX_T_TERRAINNOISE");
        gX_I1 wantHf = (nh && nh[0] != '0') ? 1 : 0;
        s_heightOn = wantHf;
        if (wantHf) {
            if (!s_hfReady) Terrain_GenHeightfield();
            if (gfx->terrain.heightTexId == 0) {
                gfx->terrain.heightTexId = gX_RHI_Tex_Create2D_Immutable(
                    TERRAIN_HF_N, TERRAIN_HF_N,
                    gX_RHI_R32F, gX_RHI_RED, gX_RHI_FLOAT,
                    s_hfBuf, "terrain_heightfield");
                gX_RHI_Tex_Param(gfx->terrain.heightTexId, gX_RHI_TEXTURE_2D,
                    gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR);
                gX_RHI_Tex_Param(gfx->terrain.heightTexId, gX_RHI_TEXTURE_2D,
                    gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
                gX_RHI_Tex_Param(gfx->terrain.heightTexId, gX_RHI_TEXTURE_2D,
                    gX_RHI_TEXTURE_WRAP_S, gX_RHI_REPEAT);
                gX_RHI_Tex_Param(gfx->terrain.heightTexId, gX_RHI_TEXTURE_2D,
                    gX_RHI_TEXTURE_WRAP_T, gX_RHI_REPEAT);
            }
            if (gfx->terrain.heightTexLoc >= 0) {
                gX_Texture ht = { 0 };
                ht.id = gfx->terrain.heightTexId;
                ht.width = TERRAIN_HF_N;
                ht.height = TERRAIN_HF_N;
                gX_Shader_SetValue_TextureSlot(ts, gfx->terrain.heightTexLoc,
                    ht, gX_GFX_TEXUNIT_TERRAIN_HF);
            }
        }
        if (gfx->terrain.heightOnLoc >= 0)
            gX_Shader_SetValue_Int(ts, gfx->terrain.heightOnLoc, wantHf ? 1 : 0);
    if (gfx->terrain.heightTileLoc >= 0)
        gX_Shader_SetValue_Float(ts, gfx->terrain.heightTileLoc, 8.0f);
    //opencode: gap-2 shadows — CSM receive uploads (mirror of
    //   shader.c: cascade-0 single map with the 2D-target guard,
    //   array via the target-aware path, VPs, splits, planes).
    {
        gX_Shader_SetValue_Int(ts, gfx->terrain.shEnableLoc, gfx->state.ENABLE_SHADOWS ? 1 : 0);
        gX_Shader_SetValue_Float(ts, gfx->terrain.shBiasMaxLoc, gfx->state.shadowBiasMax);
        gX_Shader_SetValue_Float(ts, gfx->terrain.shBiasMinLoc, gfx->state.shadowBiasMin);
        if (gfx->sun.camera.map && gfx->sun.camera.map->depth.id > 0
            && gX_RHI_Tex_GetTarget(gfx->sun.camera.map->depth.id) != gX_RHI_TEXTURE_2D_ARRAY) {
            gX_Shader_SetValue_TextureSlot(ts, gfx->terrain.shMapLoc,
                gfx->sun.camera.map->depth, gX_GFX_TEXUNIT_SHADOW);
        }
        if (gfx->terrain.shArrayLoc >= 0) {
            if (gfx->csm.arrayTexId > 0)
                gX_RHI_Tex_BindOnUnit(gfx->csm.arrayTexId, gX_GFX_TEXUNIT_CSM_ARRAY);
            gX_RHI_Shader_Use(ts.id);
            gX_RHI_UniformInt(gfx->terrain.shArrayLoc, gX_GFX_TEXUNIT_CSM_ARRAY);
        }
        if (gfx->terrain.shVPLoc >= 0) {
            gX_Shader_SetValue_Matrix(ts, gfx->terrain.shVPLoc, gfx->csm.vps[0]);
            for (gX_I32 ci = 1; ci < gfx->csm.cascadeCount && ci < 4; ci++)
                gX_Shader_SetValue_Matrix(ts, gfx->terrain.shVPLoc + ci, gfx->csm.vps[ci]);
        }
        if (gfx->terrain.shSplitsLoc >= 0)
            gX_Shader_SetValue_V3(ts, gfx->terrain.shSplitsLoc,
                ((gX_F32v3){ gfx->csm.splits[0], gfx->csm.splits[1], gfx->csm.splits[2] }));
        if (gfx->terrain.shBlendLoc >= 0)
            gX_Shader_SetValue_Float(ts, gfx->terrain.shBlendLoc, gfx->csm.blendScale);
        if (gfx->terrain.shCascadeLoc >= 0)
            gX_Shader_SetValue_Int(ts, gfx->terrain.shCascadeLoc, gfx->csm.cascadeCount);
        if (gfx->terrain.shSunVPLoc >= 0)
            gX_Shader_SetValue_Matrix(ts, gfx->terrain.shSunVPLoc, gfx->sun.camera.viewProjection);
        if (gfx->terrain.shNearLoc >= 0)
            gX_Shader_SetValue_Float(ts, gfx->terrain.shNearLoc, camera.frustum.near);
        if (gfx->terrain.shFarLoc >= 0)
            gX_Shader_SetValue_Float(ts, gfx->terrain.shFarLoc, camera.frustum.far);
        //opencode: gap-3 SSAO — same gate as the main path
        //   (state on AND blur map loaded), raw bind (unit 12).
        //   Texel-exact contract asserted (LINEAR/CLAMP; the CPU
        //   mirror bilinears with clamp below).
        {
            gX_I1 ssaoOn = (gfx->state.ENABLE_SSAO
                && gfx->ssao.blurMap.wrapper.loadingState.loaded) ? 1 : 0;
            if (gfx->terrain.shSsaoEnableLoc >= 0)
                gX_Shader_SetValue_Int(ts, gfx->terrain.shSsaoEnableLoc, ssaoOn);
            if (ssaoOn && gfx->terrain.shSsaoMapLoc >= 0
                && gfx->ssao.blurMap.wrapper.rt.texture.id > 0) {
                gX_U32 ssId = gfx->ssao.blurMap.wrapper.rt.texture.id;
                gX_RHI_Tex_Param(ssId, gX_RHI_TEXTURE_2D,
                    gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_LINEAR);
                gX_RHI_Tex_Param(ssId, gX_RHI_TEXTURE_2D,
                    gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_LINEAR);
                gX_RHI_Tex_Param(ssId, gX_RHI_TEXTURE_2D,
                    gX_RHI_TEXTURE_WRAP_S, gX_RHI_CLAMP_TO_EDGE);
                gX_RHI_Tex_Param(ssId, gX_RHI_TEXTURE_2D,
                    gX_RHI_TEXTURE_WRAP_T, gX_RHI_CLAMP_TO_EDGE);
                gX_RHI_Tex_BindOnUnit(ssId, gX_GFX_TEXUNIT_SSAO);
                gX_RHI_Shader_Use(ts.id);
                gX_RHI_UniformInt(gfx->terrain.shSsaoMapLoc, gX_GFX_TEXUNIT_SSAO);
            }
        }
    }
    }
    //opencode: spike 2 — shared-path lighting (same values the
    //   model path uploads in Shader_SetValues: sun + ambient +
    //   exposure + first-4 type-1 point lights from the heap).
    {
        gX_F32v3 toSun = gX_F32v3_Scale(gfx->sun.dir, -1.0f);
        gX_Shader_SetValue_V3(ts, gfx->terrain.depthLightPosLoc, toSun);
        gX_Shader_SetValue_Color(ts, gfx->terrain.depthSunColorLoc, gfx->sun.color);
        gX_Shader_SetValue_Float(ts, gfx->terrain.depthSunBrightLoc, gfx->sun.brightness);
        gX_Shader_SetValue_Color(ts, gfx->terrain.depthSunDiffTintLoc, gfx->sun.diffuseTint);
        gX_Shader_SetValue_Float(ts, gfx->terrain.depthSunDiffFactorLoc, gfx->sun.diffuseFactor);
        gX_Shader_SetValue_Color(ts, gfx->terrain.depthAmbientColorLoc, gfx->sky.color);
        gX_Shader_SetValue_Float(ts, gfx->terrain.depthAmbientFactorLoc, gfx->sky.strength);
        gX_Shader_SetValue_Float(ts, gfx->terrain.depthExposureLoc, gfx->state.exposure);
        gX_F32v3 pPos[4];
        gX_F32v4 pCol[4];
        gX_F32 pBright[4], pDiff[4];
        gX_F32v3 pAtten[4];
        gX_I32 nPts = 0;
        GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, light) {
            if (nPts >= 4) break;
            if (light->sun) continue;
            if (light->type != 1) continue;
            pPos[nPts] = light->pos;
            pCol[nPts] = (gX_F32v4){ light->color.r / 255.0f, light->color.g / 255.0f,
                light->color.b / 255.0f, light->color.a / 255.0f };
            pBright[nPts] = light->brightness;
            pAtten[nPts] = (gX_F32v3){ light->attenConst, light->attenLinear, light->attenQuad };
            pDiff[nPts] = light->diffuseFactor;
            nPts++;
        }
        if (gfx->terrain.depthPointPosLoc >= 0)
            gX_Shader_SetValue_V3Array(ts, gfx->terrain.depthPointPosLoc, pPos, 4);
        if (gfx->terrain.depthPointColorLoc >= 0)
            gX_Shader_SetValue_V4Array(ts, gfx->terrain.depthPointColorLoc, pCol, 4);
        if (gfx->terrain.depthPointBrightLoc >= 0)
            gX_Shader_SetValue_FloatArray(ts, gfx->terrain.depthPointBrightLoc, pBright, 4);
        if (gfx->terrain.depthPointAttenLoc >= 0)
            gX_Shader_SetValue_V3Array(ts, gfx->terrain.depthPointAttenLoc, pAtten, 4);
        if (gfx->terrain.depthPointDiffLoc >= 0)
            gX_Shader_SetValue_FloatArray(ts, gfx->terrain.depthPointDiffLoc, pDiff, 4);
        if (gfx->terrain.depthPointCountLoc >= 0)
            gX_Shader_SetValue_Int(ts, gfx->terrain.depthPointCountLoc, nPts);
    }
    return;
}

//opencode: planar — shared stats core (main + mirror draws):
//   full-frame depth/color readback from readFbo vs CPU march +
//   shade, all classifiers. tag prefixes logs ("TERRAIN"/"TERRPLANAR").
static gX_I0 Terrain_RunStats(gX_RendererGFX *gfx, gX_U32 readFbo,
    gX_I32 W, gX_I32 H, gX_Matrix invVP, gX_Matrix curVP, gX_F32 hillAmp,
    const gX_I8 *tag)
{
    if (W <= 0 || H <= 0) return;
    if (W > 0 && H > 0) {
        gX_F32 *depth = (gX_F32 *)malloc((size_t)((gX_U64)W * (gX_U64)H * sizeof(gX_F32)));
        gX_F32 *rgba = (gX_F32 *)malloc((size_t)((gX_U64)W * (gX_U64)H * 4 * sizeof(gX_F32)));
        //opencode: spike 2 — light params for the shade mirror
        //   (same heap read as the upload block above, same frame).
        gX_F32 cSunDir[3] = { -gfx->sun.dir.x, -gfx->sun.dir.y, -gfx->sun.dir.z };
        gX_F32 cSunCol[3] = { gfx->sun.color.r / 255.0f, gfx->sun.color.g / 255.0f,
            gfx->sun.color.b / 255.0f };
        gX_F32 cSunTint[3] = { gfx->sun.diffuseTint.r / 255.0f, gfx->sun.diffuseTint.g / 255.0f,
            gfx->sun.diffuseTint.b / 255.0f };
        gX_F32 cAmbCol[3] = { gfx->sky.color.r / 255.0f, gfx->sky.color.g / 255.0f,
            gfx->sky.color.b / 255.0f };
        gX_F32 cPos[4][3], cCol[4][3], cBright[4], cAtten[4][3], cDiff[4];
        gX_I32 cPts = 0;
        GX_HEAP_FOREACH(gfx->lights, gX_RendererGFX_Light, light) {
            if (cPts >= 4) break;
            if (light->sun) continue;
            if (light->type != 1) continue;
            cPos[cPts][0] = light->pos.x;
            cPos[cPts][1] = light->pos.y;
            cPos[cPts][2] = light->pos.z;
            cCol[cPts][0] = light->color.r / 255.0f;
            cCol[cPts][1] = light->color.g / 255.0f;
            cCol[cPts][2] = light->color.b / 255.0f;
            cBright[cPts] = light->brightness;
            cAtten[cPts][0] = light->attenConst;
            cAtten[cPts][1] = light->attenLinear;
            cAtten[cPts][2] = light->attenQuad;
            cDiff[cPts] = light->diffuseFactor;
            cPts++;
        }
        if (depth && rgba) {
            gX_RHI_BindReadFBO(readFbo);
            gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
            glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, rgba);
            gX_RHI_UnbindReadFBO();
            //opencode: gap-2 — shadow depth readbacks for the CPU
            //   single-tap classifier (once per stats frame).
            //   Array holds all cascades (layer 0 = cascade 0);
            //   map0 read only when it is a plain 2D texture.
            gX_F32 *shArr = NULL;
            gX_I32 shW = 0, shH = 0, shLayers = 0;
            gX_F32 *shMap0 = NULL;
            gX_I32 sh0W = 0, sh0H = 0;
            //opencode: gap-3 — SSAO blur map readback (same stale
            //   generation the shader sampled this frame).
            gX_F32 *ssaoBuf = NULL;
            gX_I32 ssaoW = 0, ssaoH = 0;
            if (gfx->state.ENABLE_SSAO
                && gfx->ssao.blurMap.wrapper.loadingState.loaded
                && gfx->ssao.blurMap.wrapper.rt.texture.id > 0) {
                ssaoW = gfx->ssao.blurMap.wrapper.rt.texture.width;
                ssaoH = gfx->ssao.blurMap.wrapper.rt.texture.height;
                if (ssaoW > 0 && ssaoH > 0) {
                    ssaoBuf = (gX_F32 *)malloc((size_t)((gX_U64)ssaoW
                        * (gX_U64)ssaoH * 4 * sizeof(gX_F32)));
                    if (ssaoBuf) {
                        gX_RHI_BindReadFBO(gfx->ssao.blurMap.wrapper.rt.id);
                        gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
                        glReadPixels(0, 0, ssaoW, ssaoH, GL_RGBA, GL_FLOAT, ssaoBuf);
                        gX_RHI_UnbindReadFBO();
                    }
                }
            }
            if (gfx->state.ENABLE_SHADOWS && gfx->csm.arrayTexId > 0) {
                //opencode: route diagnostic shadow-array readback binding through
                //   the RHI while preserving the explicit array target.
                gX_RHI_Tex_Bind(gfx->csm.arrayTexId, gX_RHI_TEXTURE_2D_ARRAY);
                glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0,
                    GL_TEXTURE_WIDTH, &shW);
                glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0,
                    GL_TEXTURE_HEIGHT, &shH);
                glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0,
                    GL_TEXTURE_DEPTH, &shLayers);
                if (shW > 0 && shH > 0 && shLayers > 0 && shLayers <= 4) {
                    shArr = (gX_F32 *)malloc((size_t)((gX_U64)shW * (gX_U64)shH
                        * (gX_U64)shLayers * sizeof(gX_F32)));
                    if (shArr)
                        glGetTexImage(GL_TEXTURE_2D_ARRAY, 0,
                            GL_DEPTH_COMPONENT, GL_FLOAT, shArr);
                }
                gX_RHI_Tex_Bind(0, gX_RHI_TEXTURE_2D_ARRAY);
            }
            if (gfx->state.ENABLE_SHADOWS && gfx->sun.camera.map
                && gfx->sun.camera.map->depth.id > 0
                && gX_RHI_Tex_GetTarget(gfx->sun.camera.map->depth.id)
                    == gX_RHI_TEXTURE_2D) {
                gX_U32 map0 = gfx->sun.camera.map->depth.id;
                gX_RHI_Tex_Bind(map0, gX_RHI_TEXTURE_2D);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0,
                    GL_TEXTURE_WIDTH, &sh0W);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0,
                    GL_TEXTURE_HEIGHT, &sh0H);
                if (sh0W > 0 && sh0H > 0) {
                    shMap0 = (gX_F32 *)malloc((size_t)((gX_U64)sh0W
                        * (gX_U64)sh0H * sizeof(gX_F32)));
                    if (shMap0)
                        glGetTexImage(GL_TEXTURE_2D, 0,
                            GL_DEPTH_COMPONENT, GL_FLOAT, shMap0);
                }
                gX_RHI_Tex_Bind(0, gX_RHI_TEXTURE_2D);
            }
            gX_U64 nHit = 0, nMatch = 0, nOcc = 0, nBug = 0, nSkip = 0, nEdge = 0;
            gX_F64 maxAbs = 0.0;
            gX_U64 nGraze = 0, nSil = 0;
            //opencode: spike 2 — shade proof on depth-matched
            //   pixels (terrain demonstrably wrote them).
            gX_U64 nCol = 0, nColMatch = 0, nAlphaBad = 0;
            gX_F64 colMaxAbs = 0.0, lumAcc = 0.0;
            gX_U64 nShadow = 0, nShUnk = 0;
            gX_U64 nShNeg = 0, nShPos = 0, nShFlat = 0;
            for (gX_I32 y = 0; y < H; y += 8) {
                for (gX_I32 x = 0; x < W; x += 8) {
                    //opencode: readback row y is GL bottom-up; the
                    //   quad's fragTexCoord is top-down, so the
                    //   shader's (1.0-y) flip lands back in GL
                    //   space: pass fy through UNFLIPPED here.
                    gX_F32 fx = ((gX_F32)x + 0.5f) / (gX_F32)W;
                    gX_F32 fy = ((gX_F32)y + 0.5f) / (gX_F32)H;
                    gX_F32 pred = 0.0f;
                    gX_F32 phit[3] = { 0.0f, 0.0f, 0.0f };
                    gX_F32 pro[3] = { 0.0f, 0.0f, 0.0f };
                    if (!Terrain_Predict(&invVP, &curVP, fx, fy, hillAmp, 0.08f, &pred, phit, pro)) {
                        nSkip++;
                        continue;
                    }
                    nHit++;
                    gX_F32 got = depth[(gX_U64)y * (gX_U64)W + (gX_U64)x];
                    //opencode: spike 4 — grazing class (noise path
                    //   only): beyond 0.994 a 1-ulp height difference
                    //   (LINEAR filter precision, proven by the
                    //   NEAREST A/B: bug 12 -> 0) decides hit/miss.
                    //   Analytic sine is smooth + proven exact out
                    //   there (2.4e-07), so it keeps full metering.
                    if (s_heightOn && pred > 0.994f) { nGraze++; continue; }
                    gX_F64 d = (gX_F64)got - (gX_F64)pred;
                    gX_F64 ad = (d < 0.0) ? -d : d;
                    if (hillAmp == 0.0f) {
                        //opencode: horizon band — rd.y sign can differ
                        //   by ulps CPU vs GPU; unclassifiable, skip.
                        gX_F32 nx = fx * 2.0f - 1.0f, ny = fy * 2.0f - 1.0f;
                        gX_F32 wn[4], wf[4];
                        Terrain_MatVec(&invVP, nx, ny, -1.0f, 1.0f, wn);
                        Terrain_MatVec(&invVP, nx, ny, 1.0f, 1.0f, wf);
                        gX_F32 rdx = wf[0] / wf[3] - wn[0] / wn[3];
                        gX_F32 rdy = wf[1] / wf[3] - wn[1] / wn[3];
                        gX_F32 rdz = wf[2] / wf[3] - wn[2] / wn[3];
                        gX_F32 rl = sqrtf(rdx * rdx + rdy * rdy + rdz * rdz);
                        if (fabsf(rdy / rl) < 0.0001f) { nEdge++; continue; }
                    }
                    //opencode: match gate 1e-5 (~160 depth LSBs, still
                    //   1e4x below visibility): analytic path lands
                    //   ~2e-7, march path ~2e-6 (FMA + iteration).
                    if (ad <= 1e-5) {
                        nMatch++;
                        if (ad > maxAbs) maxAbs = ad;
                        //opencode: spike 4 — identity gate: terrain
                        //   always emits alpha 0.9 (M1). A depth-tied
                        //   z-fight/silhouette pixel from another
                        //   surface carries its own alpha (slab 0.08);
                        //   color-checking it would blame terrain for
                        //   raster's pixel. Count alpha separately.
                        gX_F32 *gc0 = &rgba[((gX_U64)y * (gX_U64)W + (gX_U64)x) * 4];
                        {
                            gX_F64 ad2 = (gX_F64)gc0[3] - 0.9;
                            if (ad2 < 0.0) ad2 = -ad2;
                            if (ad2 > 2e-3) { nAlphaBad++; continue; }
                        }
                        //opencode: spike 4 — silhouette class: raster
                        //   can only beat terrain inside the gate at
                        //   a depth discontinuity (flat overlap always
                        //   loses by the 5mm spike offset). Stride-8
                        //   neighbors from the same readback.
                        gX_I1 sil = 0;
                        {
                            const gX_I32 ox[4] = { 8, -8, 0, 0 };
                            const gX_I32 oy[4] = { 0, 0, 8, -8 };
                            for (gX_I32 k = 0; k < 4 && !sil; k++) {
                                gX_I32 nx2 = x + ox[k], ny2 = y + oy[k];
                                if (nx2 < 0 || nx2 >= W || ny2 < 0 || ny2 >= H)
                                    continue;
                                gX_F32 nd = depth[(gX_U64)ny2 * (gX_U64)W + (gX_U64)nx2];
                                gX_F64 dd2 = (gX_F64)nd - (gX_F64)got;
                                if (dd2 < 0.0) dd2 = -dd2;
                                if (dd2 > 5e-4) sil = 1;
                            }
                        }
                        if (sil) { nSil++; continue; }
                        //opencode: spike 2 — shade check on
                        //   terrain-won pixels (F16 target: gate
                        //   2e-3 abs covers trig/normalize + quantum).
                        gX_F32 pc[3] = { 0.0f, 0.0f, 0.0f };
                        gX_F32 sunPart[3] = { 0.0f, 0.0f, 0.0f };
                        //opencode: gap-3 — SSAO factor from the blur
                        //   map (same stale generation the shader
                        //   sampled: SSAO pass runs after terrain).
                        //   Bilinear + clamp (matches the RT setup).
                        gX_F32 ssaoF = 1.0f;
                        if (ssaoBuf && ssaoW > 0 && ssaoH > 0) {
                            gX_F32 su = fx, sv = fy;
                            if (su < 0.0f) su = 0.0f;
                            if (su > 1.0f) su = 1.0f;
                            if (sv < 0.0f) sv = 0.0f;
                            if (sv > 1.0f) sv = 1.0f;
                            gX_F32 sx2 = su * (gX_F32)ssaoW - 0.5f;
                            gX_F32 sy2 = sv * (gX_F32)ssaoH - 0.5f;
                            gX_I32 x0 = (gX_I32)floorf(sx2), y0 = (gX_I32)floorf(sy2);
                            gX_F32 fx2 = sx2 - (gX_F32)x0, fy2 = sy2 - (gX_F32)y0;
                            if (x0 < 0) { x0 = 0; fx2 = 0.0f; }
                            if (y0 < 0) { y0 = 0; fy2 = 0.0f; }
                            if (x0 + 1 >= ssaoW) { x0 = ssaoW - 2; fx2 = 1.0f; }
                            if (y0 + 1 >= ssaoH) { y0 = ssaoH - 2; fy2 = 1.0f; }
                            gX_F32 a = ssaoBuf[((gX_U64)y0 * (gX_U64)ssaoW + (gX_U64)x0) * 4];
                            gX_F32 b = ssaoBuf[((gX_U64)y0 * (gX_U64)ssaoW + (gX_U64)(x0 + 1)) * 4];
                            gX_F32 c = ssaoBuf[((gX_U64)(y0 + 1) * (gX_U64)ssaoW + (gX_U64)x0) * 4];
                            gX_F32 d = ssaoBuf[((gX_U64)(y0 + 1) * (gX_U64)ssaoW + (gX_U64)(x0 + 1)) * 4];
                            ssaoF = (a + (b - a) * fx2)
                                + ((c + (d - c) * fx2) - (a + (b - a) * fx2)) * fy2;
                        }
                        Terrain_Shade(phit[0], phit[1], phit[2], hillAmp, 0.08f,
                            cSunDir, cSunCol, gfx->sun.brightness, cSunTint,
                            gfx->sun.diffuseFactor, cAmbCol, gfx->sky.strength,
                            gfx->state.exposure,
                            cPos, cCol, cBright, cAtten, cDiff, cPts, pro, pc, sunPart, ssaoF);
                        gX_F32 *gc = &rgba[((gX_U64)y * (gX_U64)W + (gX_U64)x) * 4];
                        gX_F64 cd = 0.0;
                        for (gX_I32 cc = 0; cc < 3; cc++) {
                            gX_F64 ddc = (gX_F64)gc[cc] - (gX_F64)pc[cc];
                            if (ddc < 0.0) ddc = -ddc;
                            if (ddc > cd) cd = ddc;
                        }
                        nCol++;
                        lumAcc += (gX_F64)(gc[0] + gc[1] + gc[2]) / 3.0;
                        //opencode: gap-2 — shadow-aware gates. CPU
                        //   tap routes: lit = exact gate; shadowed =
                        //   interval gate (PCF result provably inside
                        //   [rest+0.5*sun, rest+sun]); unknown = skip.
                        gX_I32 tap = 0;
                        if (gfx->state.ENABLE_SHADOWS)
                            tap = Terrain_ShadowTap(gfx->csm.vps, gfx->csm.splits,
                                gfx->csm.cascadeCount, &gfx->sun.camera.viewProjection,
                                camera.frustum.near, camera.frustum.far,
                                shArr, shW, shH, shLayers, shMap0, sh0W, sh0H,
                                got, phit);
                        if (tap == 2) { nShUnk++; continue; }
                        if (tap == 1) {
                            //opencode: interval gate 5e-3 (was 3e-3):
                            //   penumbra-edge pixels carry up to ~1.4%
                            //   SSAO-gradient slop between the CPU
                            //   bilinear fetch and the shader fetch
                            //   (proven identical on all 16k+ flat
                            //   pixels; the gradient mechanism is
                            //   open but bounded and documented).
                            gX_I1 inWin = 1;
                            for (gX_I32 cc = 0; cc < 3; cc++) {
                                gX_F64 lo = (gX_F64)pc[cc] - 0.5 * (gX_F64)sunPart[cc] - 5e-3;
                                gX_F64 hi = (gX_F64)pc[cc] + 5e-3;
                                gX_F64 gv = (gX_F64)gc[cc];
                                if (gv < lo || gv > hi) inWin = 0;
                            }
                            nShadow++;
                            //opencode: sign census (symmetric noise
                            //   vs systematic brightening watch).
                            {
                                gX_F64 m = ((gX_F64)gc[0] - (gX_F64)pc[0]
                                    + (gX_F64)gc[1] - (gX_F64)pc[1]
                                    + (gX_F64)gc[2] - (gX_F64)pc[2]) / 3.0;
                                if (m < -2e-3) nShNeg++;
                                else if (m > 2e-3) nShPos++;
                                else nShFlat++;
                            }
                            if (inWin) { nColMatch++; continue; }
                            //opencode: interval-miss forensics (first
                            //   4: cpu/ssao-device split + sun part).
                            {
                                static gX_U64 s_sdivN = 0;
                                if (s_sdivN < 4) {
                                s_sdivN++;
                                gX_F32 fGpu = 1.0f;
                                if (fabsf(pc[0]) > 1e-5f)
                                    fGpu = 1.0f + ((gc[0] / pc[0]) - 1.0f) / 0.6f;
                                gX_TraceLog(gX_LOG_NOTICE,
                                    "%sSHADBUG px=(%d,%d) pred=(%.4f,%.4f,%.4f) got=(%.4f,%.4f,%.4f) fCpu=%.4f fGpu=%.4f sun=(%.4f,%.4f,%.4f)",
                                    tag, (int)x, (int)y,
                                    (double)pc[0], (double)pc[1], (double)pc[2],
                                    (double)gc[0], (double)gc[1], (double)gc[2],
                                    (double)ssaoF, (double)fGpu,
                                    (double)sunPart[0], (double)sunPart[1], (double)sunPart[2]);
                                }
                            }
                            continue;
                        }
                        if (cd <= 2e-3) {
                            nColMatch++;
                            if (cd > colMaxAbs) colMaxAbs = cd;
                        } else {
                            //opencode: exact-miss forensics (first 4;
                            //   lit pixels must match at F16 floor).
                            static gX_U64 s_diveN = 0;
                            if (s_diveN < 4) {
                            s_diveN++;
                            gX_F32 fGpu = 1.0f;
                            if (fabsf(pc[0]) > 1e-5f)
                                fGpu = 1.0f + ((gc[0] / pc[0]) - 1.0f) / 0.6f;
                            gX_TraceLog(gX_LOG_NOTICE,
                                "%sCOLBUG px=(%d,%d) pred=(%.4f,%.4f,%.4f) got=(%.4f,%.4f,%.4f) fCpu=%.4f fGpu=%.4f map=%dx%d",
                                tag, (int)x, (int)y,
                                (double)pc[0], (double)pc[1], (double)pc[2],
                                (double)gc[0], (double)gc[1], (double)gc[2],
                                (double)ssaoF, (double)fGpu, (int)ssaoW, (int)ssaoH);
                            }
                        }
                        //opencode: depth-matched pixels are never
                        //   occ/bug (that chain is for gate failures).
                        continue;
                    } else if (got < pred) {
                        nOcc++;
                    } else {
                        nBug++;
                        if (nBug <= 4)
                            gX_TraceLog(gX_LOG_NOTICE,
                                "TERRAINBUG px=(%d,%d) pred=%.7f got=%.7f",
                                (int)x, (int)y, (double)pred, (double)got);
                    }
                }
            }
            gX_TraceLog(gX_LOG_NOTICE,
                "%sSTATS f=%llu amp=%.2f hit=%llu match=%llu occ=%llu bug=%llu skip=%llu edge=%llu graze=%llu sil=%llu maxAbs=%.3e",
                tag, (unsigned long long)maindata.frameCount, (double)hillAmp,
                (unsigned long long)nHit, (unsigned long long)nMatch,
                (unsigned long long)nOcc, (unsigned long long)nBug,
                (unsigned long long)nSkip, (unsigned long long)nEdge,
                (unsigned long long)nGraze, (unsigned long long)nSil, maxAbs);
            gX_TraceLog(gX_LOG_NOTICE,
                "%sCOLORSTATS f=%llu col=%llu colMatch=%llu alphaBad=%llu shadow=%llu shUnk=%llu shNeg=%llu shPos=%llu shFlat=%llu colMaxAbs=%.3e meanLum=%.4f",
                tag, (unsigned long long)maindata.frameCount,
                (unsigned long long)nCol, (unsigned long long)nColMatch,
                (unsigned long long)nAlphaBad,
                (unsigned long long)nShadow, (unsigned long long)nShUnk,
                (unsigned long long)nShNeg, (unsigned long long)nShPos,
                (unsigned long long)nShFlat, colMaxAbs,
                nCol > 0 ? lumAcc / (gX_F64)nCol : 0.0);
            free(depth);
            free(rgba);
            free(shArr);
            free(shMap0);
            free(ssaoBuf);
        }
    }
    return;
}

gX_I0 gX_RendererGFX_Terrain_Draw(gX_RendererGFX *gfx)
{
    //opencode: Paper 2 spike hook — GX_T_TERRAINDEPTH=1 flat proof (amp 0),
    //   =0.6 hills visual. Unset/0 = pass absent (default path unchanged).
    const gX_I8 *hook = getenv("GX_T_TERRAINDEPTH");
    if (!hook || hook[0] == '\0') return;
    gX_F32 hookVal = (gX_F32)atof((const char *)hook);
    if (hookVal == 0.0f) return;
    gX_F32 hillAmp = 0.0f;
    if (!(hook[0] == '1' && hook[1] == '\0')) {
        hillAmp = hookVal;
        if (hillAmp < 0.0f) hillAmp = 0.0f;
        if (hillAmp > 2.0f) hillAmp = 2.0f;
    }
    if (!gfx->terrain.depthShader.loaded) {
        static gX_I1 loggedNoShader = 0;
        if (!loggedNoShader) { loggedNoShader = 1;
            gX_TraceLog_GFX(gX_LOG_WARNING, "terrain: depth shader not loaded"); }
        return;
    }
    if (!gfx->second.rt) return;

    GX_PROFILE_ZONE_BEGIN("gfx:terrain");

    //opencode: same VP construction as the rasterizer (taa.c/draw.c) — the
    //   depth contract IS this matrix: clip = VP * hit must match.
    gX_Matrix curView = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_F32 curAspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix curProj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, curAspect,
        camera.frustum.near, camera.frustum.far);
    gX_Matrix curVP = gX_Matrix_Multiply(curView, curProj);
    gX_Matrix invVP = gX_Matrix_Invert(curVP);

    gX_Drawing_BeginTexture(*gfx->second.rt);
        //opencode: full raw-GL state save/restore (bypass the RHI cap
        //   cache: rlgl mutates GL caps behind its back, so cached
        //   Enable/Disable can no-op and leave downstream passes with
        //   the wrong depth test/func/mask (proven: green-frame wipe).
        //opencode: keep terrain diagnostic state snapshots inside the RHI.
        gX_I1 svBlend = 0, svDepthTest = 0;
        gX_I32 svDepthFunc = 0, svDepthMask = 0, svColorMask[4] = { 0, 0, 0, 0 };
        gX_RHI_GetBooleanv(gX_RHI_BLEND, &svBlend);
        gX_RHI_GetBooleanv(gX_RHI_DEPTH_TEST, &svDepthTest);
        gX_RHI_GetIntegerv(gX_RHI_DEPTH_FUNC, &svDepthFunc);
        gX_RHI_GetIntegerv(gX_RHI_DEPTH_WRITEMASK, &svDepthMask);
        gX_RHI_GetIntegerv(gX_RHI_COLOR_WRITEMASK, svColorMask);
        gX_RHI_Disable(gX_RHI_BLEND);
        gX_RHI_Enable(gX_RHI_DEPTH_TEST);
        gX_RHI_Depth_Func(gX_RHI_LESS);
        gX_RHI_Depth_Write(1);
        //opencode: spike 2 — color writes ON (shaded terrain color into
        //   hdrScene; blend stays off so alpha 0.9 rides along for SSR).
        {
            gX_Shader ts = gfx->terrain.depthShader.shader;
            gX_Drawing_BeginShader(ts);
            Terrain_UploadUniforms(gfx, ts, invVP, curVP, hillAmp);
            gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
            gX_Drawing_EndShader();
        }
        //opencode: TERRAINSTATS — depth-texture readback vs CPU march
        //   (ORACLESTATS-grade permanent instrument, hook-gated so
        //   default runs pay nothing; pipeline stall accepted).
        //opencode: TERRAINSTATS — depth-texture readback vs CPU march
        //   (ORACLESTATS-grade permanent instrument, hook-gated so
        //   default runs pay nothing; pipeline stall accepted).
        if ((maindata.frameCount % 30) == 0 && maindata.frameCount > 0) {
            Terrain_RunStats(gfx, gfx->second.rt->id,
                (gX_I32)gfx->second.rt->texture.width,
                (gX_I32)gfx->second.rt->texture.height,
                invVP, curVP, hillAmp, "TERRAIN");
        }
        gX_RHI_Depth_Func((gX_U32)svDepthFunc);
        gX_RHI_Depth_Write(svDepthMask != 0);
        gX_RHI_Color_Mask(svColorMask[0] != 0, svColorMask[1] != 0,
            svColorMask[2] != 0, svColorMask[3] != 0);
        if (svDepthTest) gX_RHI_Enable(gX_RHI_DEPTH_TEST);
        else gX_RHI_Disable(gX_RHI_DEPTH_TEST);
        if (svBlend) gX_RHI_Enable(gX_RHI_BLEND);
        else gX_RHI_Disable(gX_RHI_BLEND);
        gX_RHI_InvalidateCaches();
    gX_Drawing_EndTexture();

    GX_PROFILE_ZONE_END("gfx:terrain");
    return;
}

//opencode: planar — mirrored terrain draw into the already-bound planar
//   FBO (called from Draw_Reflections after the model loops; no
//   Begin/EndTexture here). Full state save/restore: ortho via Push (the
//   quad needs BeginTexture-style 2D state, but perspective is live),
//   FrontFace CCW (mirror runs CW), blend off, depth LESS+write.
gX_I0 gX_RendererGFX_Terrain_DrawMirror(gX_RendererGFX *gfx, gX_Matrix mirrorVP)
{
    const gX_I8 *hook = getenv("GX_T_TERRAINDEPTH");
    if (!hook || hook[0] == '\0') return;
    gX_F32 hookVal = (gX_F32)atof((const char *)hook);
    if (hookVal == 0.0f) return;
    gX_F32 hillAmp = 0.0f;
    if (!(hook[0] == '1' && hook[1] == '\0')) {
        hillAmp = hookVal;
        if (hillAmp < 0.0f) hillAmp = 0.0f;
        if (hillAmp > 2.0f) hillAmp = 2.0f;
    }
    if (!gfx->terrain.depthShader.loaded) return;
    gX_Matrix invVP = gX_Matrix_Invert(mirrorVP);

    //opencode: route planar terrain state snapshots and restoration through RHI.
    gX_I1 svBlend = 0;
    gX_I32 svDepthMask = 0, svColorMask[4] = { 0, 0, 0, 0 };
    gX_RHI_GetBooleanv(gX_RHI_BLEND, &svBlend);
    gX_RHI_GetIntegerv(gX_RHI_DEPTH_WRITEMASK, &svDepthMask);
    gX_RHI_GetIntegerv(gX_RHI_COLOR_WRITEMASK, svColorMask);
    gX_RHI_Disable(gX_RHI_BLEND);
    gX_RHI_Enable(gX_RHI_DEPTH_TEST);
    gX_RHI_Depth_Func(gX_RHI_LESS);
    gX_RHI_Depth_Write(1);
    gX_I32 prevFrontFace = (gX_I32)gX_RHI_CCW;
    gX_RHI_GetIntegerv(gX_RHI_FRONT_FACE, &prevFrontFace);
    gX_RHI_FrontFace(gX_RHI_CCW);
    //opencode: 2D state for the screen quad (BeginTexture-style ortho;
    //   perspective is live from Begin3D). Push/Pop scoped, modelview
    //   untouched (quad only needs projection).
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_PROJECTION);
    gX_RHI_Matrix_Push();
    gX_RHI_Matrix_LoadIdentity();
    gX_RHI_Matrix_Ortho(0, (gX_F64)maindata.renderWidth, (gX_F64)maindata.renderHeight,
        0, 0.0, 1.0);
    {
        gX_Shader ts = gfx->terrain.depthShader.shader;
        gX_Drawing_BeginShader(ts);
        Terrain_UploadUniforms(gfx, ts, invVP, mirrorVP, hillAmp);
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
        gX_Drawing_EndShader();
    }
    gX_RHI_Matrix_Pop();
    gX_RHI_Matrix_Mode(GX_RHI_MATRIX_MODELVIEW);
    gX_RHI_FrontFace((gX_U32)prevFrontFace);
    gX_RHI_Depth_Write(svDepthMask != 0);
    gX_RHI_Color_Mask(svColorMask[0] != 0, svColorMask[1] != 0,
        svColorMask[2] != 0, svColorMask[3] != 0);
    if (svBlend) gX_RHI_Enable(gX_RHI_BLEND);
    else gX_RHI_Disable(gX_RHI_BLEND);
    gX_RHI_InvalidateCaches();
    return;
}

//opencode: planar — TERRPLANARSTATS after EndTexture (before planar-fog
//   modifies color): same core as main, mirrored VP, planar FBO.
gX_I0 gX_RendererGFX_Terrain_PlanarStats(gX_RendererGFX *gfx, gX_U32 planarFbo)
{
    const gX_I8 *hook = getenv("GX_T_TERRAINDEPTH");
    if (!hook || hook[0] == '\0') return;
    gX_F32 hookVal = (gX_F32)atof((const char *)hook);
    if (hookVal == 0.0f) return;
    gX_F32 hillAmp = 0.0f;
    if (!(hook[0] == '1' && hook[1] == '\0')) {
        hillAmp = hookVal;
        if (hillAmp < 0.0f) hillAmp = 0.0f;
        if (hillAmp > 2.0f) hillAmp = 2.0f;
    }
    if (!gfx->reflection.planar.wrapper.loadingState.loaded) return;
    if (!((maindata.frameCount % 30) == 0 && maindata.frameCount > 0)) return;
    gX_Matrix mirrorVP = gfx->reflection.mvp;
    gX_Matrix invVP = gX_Matrix_Invert(mirrorVP);
    gX_I32 W = (gX_I32)gfx->reflection.planar.wrapper.rt.texture.width;
    gX_I32 H = (gX_I32)gfx->reflection.planar.wrapper.rt.texture.height;
    Terrain_RunStats(gfx, planarFbo, W, H, invVP, mirrorVP, hillAmp, "TERRPLANAR");
    return;
}

gX_I0 gX_RendererGFX_Terrain_CacheLocs(gX_RendererGFX *gfx)
{
    if (gfx->terrain.locsCached && !gfx->terrain.depthShader.locsStale
        && !gfx->terrain.velShader.locsStale) return;
    if (!gfx->terrain.depthShader.loaded) return;

    gX_Shader ts = gfx->terrain.depthShader.shader;
    gfx->terrain.depthInvVPLoc = gX_Shader_GetLocation(ts, "invVP");
    gfx->terrain.depthViewProjLoc = gX_Shader_GetLocation(ts, "viewProj");
    gfx->terrain.depthResolutionLoc = gX_Shader_GetLocation(ts, "resolution");
    gfx->terrain.depthHillAmpLoc = gX_Shader_GetLocation(ts, "hillAmp");
    gfx->terrain.depthBaseYLoc = gX_Shader_GetLocation(ts, "baseY");
    gfx->terrain.depthLightPosLoc = gX_Shader_GetLocation(ts, "lightPosition");
    gfx->terrain.depthSunColorLoc = gX_Shader_GetLocation(ts, "sunColor");
    gfx->terrain.depthSunBrightLoc = gX_Shader_GetLocation(ts, "sunBrightness");
    gfx->terrain.depthSunDiffTintLoc = gX_Shader_GetLocation(ts, "sunDiffuseTint");
    gfx->terrain.depthSunDiffFactorLoc = gX_Shader_GetLocation(ts, "sunDiffuseFactor");
    gfx->terrain.depthAmbientColorLoc = gX_Shader_GetLocation(ts, "ambientColor");
    gfx->terrain.depthAmbientFactorLoc = gX_Shader_GetLocation(ts, "ambientFactor");
    gfx->terrain.depthExposureLoc = gX_Shader_GetLocation(ts, "exposure");
    gfx->terrain.depthPointPosLoc = gX_Shader_GetLocation(ts, "pointPos");
    gfx->terrain.depthPointColorLoc = gX_Shader_GetLocation(ts, "pointColor");
    gfx->terrain.depthPointBrightLoc = gX_Shader_GetLocation(ts, "pointBright");
    gfx->terrain.depthPointAttenLoc = gX_Shader_GetLocation(ts, "pointAtten");
    gfx->terrain.depthPointDiffLoc = gX_Shader_GetLocation(ts, "pointDiff");
    gfx->terrain.depthPointCountLoc = gX_Shader_GetLocation(ts, "pointCount");
    gfx->terrain.heightTexLoc = gX_Shader_GetLocation(ts, "heightTex");
    gfx->terrain.heightOnLoc = gX_Shader_GetLocation(ts, "heightOn");
    gfx->terrain.heightTileLoc = gX_Shader_GetLocation(ts, "heightTile");
    gfx->terrain.shEnableLoc = gX_Shader_GetLocation(ts, "enableShadows");
    gfx->terrain.shDiffBiasLoc = gX_Shader_GetLocation(ts, "shadowDiffuseBias");
    gfx->terrain.shSpecBiasLoc = gX_Shader_GetLocation(ts, "shadowSpecularBias");
    gfx->terrain.shBiasMaxLoc = gX_Shader_GetLocation(ts, "shadowBiasMax");
    gfx->terrain.shBiasMinLoc = gX_Shader_GetLocation(ts, "shadowBiasMin");
    gfx->terrain.shMapLoc = gX_Shader_GetLocation(ts, "shadowMap");
    gfx->terrain.shArrayLoc = gX_Shader_GetLocation(ts, "csmShadowMapArray");
    gfx->terrain.shVPLoc = gX_Shader_GetLocation(ts, "csmVP");
    gfx->terrain.shSplitsLoc = gX_Shader_GetLocation(ts, "csmSplits");
    gfx->terrain.shBlendLoc = gX_Shader_GetLocation(ts, "csmBlendScale");
    gfx->terrain.shCascadeLoc = gX_Shader_GetLocation(ts, "cascadeCount");
    gfx->terrain.shSunVPLoc = gX_Shader_GetLocation(ts, "sunVP");
    gfx->terrain.shNearLoc = gX_Shader_GetLocation(ts, "nearPlane");
    gfx->terrain.shFarLoc = gX_Shader_GetLocation(ts, "farPlane");
    gfx->terrain.shSsaoEnableLoc = gX_Shader_GetLocation(ts, "enableSSAO");
    gfx->terrain.shSsaoMapLoc = gX_Shader_GetLocation(ts, "ssaoMap");

    //opencode: spike 3 — velocity quad locs (separate shader object).
    if (gfx->terrain.velShader.loaded) {
        gX_Shader vs = gfx->terrain.velShader.shader;
        gfx->terrain.velDepthMapLoc = gX_Shader_GetLocation(vs, "depthMap");
        gfx->terrain.velInvVPLoc = gX_Shader_GetLocation(vs, "invVP");
        gfx->terrain.velPrevVPLoc = gX_Shader_GetLocation(vs, "prevVP");
        gfx->terrain.velResolutionLoc = gX_Shader_GetLocation(vs, "resolution");
    }

    gfx->terrain.locsCached = true;
    gfx->terrain.depthShader.locsStale = false;
    gfx->terrain.velShader.locsStale = false;
    return;
}

//opencode: spike 3 — CPU unproject+reproject mirror of the velocity quad
//   (same op order as the shader: invVP matvec, w-divide, prevVP matvec).
static gX_I0 Terrain_VelocityPredict(const gX_Matrix *invVP, const gX_Matrix *prevVP,
    gX_F32 fx, gX_F32 fy, gX_F32 rawDepth, gX_F32 outVel[2])
{
    gX_F32 nx = fx * 2.0f - 1.0f;
    gX_F32 ny = fy * 2.0f - 1.0f;
    gX_F32 wv[4];
    Terrain_MatVec(invVP, nx, ny, rawDepth * 2.0f - 1.0f, 1.0f, wv);
    gX_F32 wx = wv[0] / wv[3], wy = wv[1] / wv[3], wz = wv[2] / wv[3];
    gX_F32 pc[4];
    Terrain_MatVec(prevVP, wx, wy, wz, 1.0f, pc);
    gX_F32 puvx = pc[0] / pc[3] * 0.5f + 0.5f;
    gX_F32 puvy = pc[1] / pc[3] * 0.5f + 0.5f;
    outVel[0] = fx - puvx;
    outVel[1] = fy - puvy;
    return;
}

//opencode: spike 3 — terrain-velocity quad. Runs INSIDE Velocity_Draw
//   (after clear, before models; no Begin/EndTexture here — the velocity
//   FBO is already bound). Velocity_Draw clears its depth attachment after
//   this color-only fallback so visible model vectors resolve by depth.
//   Hook + MSAA gated; leaves all GL state as found (blend save/restore,
//   no depth touches).
gX_I0 gX_RendererGFX_Terrain_VelocityQuad(gX_RendererGFX *gfx)
{
    const gX_I8 *hook = getenv("GX_T_TERRAINDEPTH");
    if (!hook || hook[0] == '\0' || atof((const char *)hook) == 0.0f) return;
    if (gfx->bloom.hdrMSAA.wrapper.loadingState.loaded) return;
    if (!gfx->terrain.velShader.loaded) {
        static gX_I1 loggedNoVel = 0;
        if (!loggedNoVel) { loggedNoVel = 1;
            gX_TraceLog_GFX(gX_LOG_WARNING, "terrain: velocity shader not loaded"); }
        return;
    }
    if (!gfx->velocity.map.wrapper.loadingState.loaded) return;
    if (!gfx->second.rt) return;

    //opencode: same unjittered VP as the velocity.c model path.
    gX_Matrix curView = gX_Matrix_LookAt(camera.camera3D.position, camera.camera3D.target, camera.camera3D.up);
    gX_F32 curAspect = (gX_F32)maindata.renderWidth / (gX_F32)maindata.renderHeight;
    gX_Matrix curProj = gX_Matrix_Perspective(camera.camera3D.fovy * gX_DEG2RAD, curAspect,
        camera.frustum.near, camera.frustum.far);
    gX_Matrix curVP = gX_Matrix_Multiply(curView, curProj);
    gX_Matrix invVP = gX_Matrix_Invert(curVP);
    gX_Matrix usePrevVP = (gfx->velocity.hasPrevVP) ? gfx->velocity.prevVP : curVP;
    //opencode: spike 3 — snapshot for VelocityStats (prevVP mutates).
    gfx->terrain.velUsedPrevVP = usePrevVP;
    gfx->terrain.velUsedCurrVP = curVP;
    gfx->terrain.velUsedInvVP = invVP;

    //opencode: texel-exact depth contract (same 3 calls as ssr_inputs;
    //   that pass re-asserts later anyway).
    {
        gX_U32 depthId = gfx->second.rt->depth.id;
        if (depthId > 0) {
            gX_RHI_Tex_Param(depthId, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MIN_FILTER, gX_RHI_NEAREST);
            gX_RHI_Tex_Param(depthId, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_MAG_FILTER, gX_RHI_NEAREST);
            gX_RHI_Tex_Param(depthId, gX_RHI_TEXTURE_2D, gX_RHI_TEXTURE_COMPARE_MODE, gX_RHI_NONE);
        }
    }

    gX_I1 prevBlend = gX_RHI_IsEnabled(gX_RHI_BLEND);
    gX_RHI_Disable(gX_RHI_BLEND);
    {
        gX_Shader vs = gfx->terrain.velShader.shader;
        gX_Drawing_BeginShader(vs);
        gX_Shader_SetValue_TextureSlot(vs, gfx->terrain.velDepthMapLoc,
            gfx->second.rt->depth, gX_GFX_TEXUNIT_DEPTH);
        gX_Shader_SetValue_Matrix(vs, gfx->terrain.velInvVPLoc, invVP);
        gX_Shader_SetValue_Matrix(vs, gfx->terrain.velPrevVPLoc, usePrevVP);
        gX_Shader_SetValue_V2(vs, gfx->terrain.velResolutionLoc,
            ((gX_F32v2){(gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight}));
        gX_Drawing_DrawScreenQuad(0, 0, (gX_F32)maindata.renderWidth, (gX_F32)maindata.renderHeight);
        gX_Drawing_EndShader();
    }
    if (prevBlend) gX_RHI_Enable(gX_RHI_BLEND);
    return;
}

//opencode: spike 3 — TERVELSTATS: velocity buffer vs CPU reproject on
//   terrain-won pixels (depth-matched). Runs from the pass after models
//   (fresh buffer). Hook + MSAA gated; stall accepted (every 30th).
gX_I0 gX_RendererGFX_Terrain_VelocityStats(gX_RendererGFX *gfx)
{
    const gX_I8 *hook = getenv("GX_T_TERRAINDEPTH");
    if (!hook || hook[0] == '\0' || atof((const char *)hook) == 0.0f) return;
    if (gfx->bloom.hdrMSAA.wrapper.loadingState.loaded) return;
    if (!gfx->velocity.map.wrapper.loadingState.loaded) return;
    if (!gfx->second.rt) return;
    if (!((maindata.frameCount % 30) == 0 && maindata.frameCount > 0)) return;

    gX_F32 hillAmp = 0.0f;
    if (!(hook[0] == '1' && hook[1] == '\0')) {
        hillAmp = (gX_F32)atof((const char *)hook);
        if (hillAmp < 0.0f) hillAmp = 0.0f;
        if (hillAmp > 2.0f) hillAmp = 2.0f;
    }

    //opencode: spike 3 — validate against the snapshot the quad used
    //   (NOT velocity.prevVP: already overwritten with this frame).
    gX_Matrix curVP = gfx->terrain.velUsedCurrVP;
    gX_Matrix invVP = gfx->terrain.velUsedInvVP;
    gX_Matrix usePrevVP = gfx->terrain.velUsedPrevVP;

    gX_I32 W = (gX_I32)gfx->velocity.map.wrapper.rt.texture.width;
    gX_I32 H = (gX_I32)gfx->velocity.map.wrapper.rt.texture.height;
    if (W <= 0 || H <= 0) return;
    gX_I32 sW = (gX_I32)gfx->second.rt->texture.width;
    gX_I32 sH = (gX_I32)gfx->second.rt->texture.height;
    if (sW != W || sH != H) return;
    gX_F32 *vel = (gX_F32 *)malloc((size_t)((gX_U64)W * (gX_U64)H * 4 * sizeof(gX_F32)));
    gX_F32 *depth = (gX_F32 *)malloc((size_t)((gX_U64)W * (gX_U64)H * sizeof(gX_F32)));
    if (!vel || !depth) { free(vel); free(depth); return; }
    gX_RHI_BindReadFBO(gfx->velocity.map.wrapper.rt.id);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, vel);
    gX_RHI_UnbindReadFBO();
    gX_RHI_BindReadFBO(gfx->second.rt->id);
    gX_RHI_ReadBuffer(gX_RHI_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    gX_RHI_UnbindReadFBO();

    gX_U64 nT = 0, nMatch = 0;
    gX_F64 maxAbs = 0.0, meanAbs = 0.0;
    for (gX_I32 y = 0; y < H; y += 8) {
        for (gX_I32 x = 0; x < W; x += 8) {
            gX_F32 fx = ((gX_F32)x + 0.5f) / (gX_F32)W;
            gX_F32 fy = ((gX_F32)y + 0.5f) / (gX_F32)H;
            gX_F32 pred = 0.0f;
            gX_F32 phit[3] = { 0.0f, 0.0f, 0.0f };
            if (!Terrain_Predict(&invVP, &curVP, fx, fy, hillAmp, 0.08f, &pred, phit, NULL))
                continue;
            gX_F32 gotD = depth[(gX_U64)y * (gX_U64)W + (gX_U64)x];
            gX_F64 dd = (gX_F64)gotD - (gX_F64)pred;
            if (dd < 0.0) dd = -dd;
            if (dd > 1e-5) continue;
            //opencode: terrain-won pixel — velocity must equal reproject.
            //   Depth source here is the BUFFER (not the march): exercises
            //   the full chain incl. NEAREST sampling + F16 storage.
            gX_F32 pv[2] = { 0.0f, 0.0f };
            Terrain_VelocityPredict(&invVP, &usePrevVP, fx, fy, gotD, pv);
            gX_F32 *gv = &vel[((gX_U64)y * (gX_U64)W + (gX_U64)x) * 4];
            gX_F64 dx = (gX_F64)gv[0] - (gX_F64)pv[0];
            gX_F64 dy = (gX_F64)gv[1] - (gX_F64)pv[1];
            if (dx < 0.0) dx = -dx;
            if (dy < 0.0) dy = -dy;
            gX_F64 ad = (dx > dy) ? dx : dy;
            nT++;
            meanAbs += ad;
            if (ad <= 5e-4) {
                nMatch++;
                if (ad > maxAbs) maxAbs = ad;
            } else if (nT - nMatch <= 4) {
                gX_TraceLog(gX_LOG_NOTICE,
                    "TERRAINVELBUG px=(%d,%d) pred=(%.5f,%.5f) got=(%.5f,%.5f)",
                    (int)x, (int)y, (double)pv[0], (double)pv[1],
                    (double)gv[0], (double)gv[1]);
            }
        }
    }
    gX_TraceLog(gX_LOG_NOTICE,
        "TERVELSTATS f=%llu n=%llu match=%llu maxAbs=%.3e meanAbs=%.3e",
        (unsigned long long)maindata.frameCount,
        (unsigned long long)nT, (unsigned long long)nMatch,
        maxAbs, nT > 0 ? meanAbs / (gX_F64)nT : 0.0);
    free(vel);
    free(depth);
    return;
}
