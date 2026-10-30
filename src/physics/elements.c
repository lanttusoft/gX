/**********************************************************************************************
*
*   [gX: src/physics/elements.c]
*   Element table — densities and molar masses for physics materials
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


#include "elements.h"

#include <stddef.h>

//ai: measured values; density kg/m3 (gases at STP, solids at 20 C), molar
//   mass g/mol, specific heat J/(kg*K), melting point K (sublimation/
//   pressure caveats noted for C) and nominal emissivity (polished/oxidized
//   surfaces vary widely). latentHeat is the heat of fusion in J/kg
//   (0 where the element does not melt as a prop: gases and carbon).
//   friction/restitution/rollingFriction are nominal contact ballparks for
//   the sandbox (rolling/spin resistance ~0.001 steel to 0.05 rough):
//   metals land in the 0.4..0.7 / 0.1..0.55 range with lead the dead soft
//   outlier and the hard metals springier; gases are inert placeholders.
//   albedo/metalness/roughness are polished-metal ballparks (graphite for
//   carbon) and susceptibility is mu_r - 1 (SI): iron 999 (effective soft
//   iron), titanium/aluminum weak paramagnets, the noble/soft metals
//   diamagnetic. Keep entries in the X-macro order.
static const gX_Element_Info g_elements_[] = {
    { "H",  "Hydrogen", 1,   1.008f,     0.08988f, 14300.0f,   13.99f, 0.05f,  58000.0f, 0.05f, 0.05f, 0.001f, {205, 225, 255, 255}, 0.00f, 1.00f, -2.2e-9f },
    { "He", "Helium",   2,   4.0026f,    0.1786f,   5193.0f,    0.95f, 0.05f,      0.0f, 0.05f, 0.05f, 0.001f, {215, 230, 255, 255}, 0.00f, 1.00f, -1.1e-9f },
    { "C",  "Carbon",   6,  12.011f,  2260.0f,      710.0f,  3823.0f,  0.85f,      0.0f, 0.30f, 0.45f, 0.05f, { 46,  46,  52, 255}, 0.05f, 0.75f, -4.0e-4f },
    { "Al", "Aluminum",13,  26.982f,  2700.0f,      897.0f,   933.47f, 0.10f, 397000.0f, 0.55f, 0.35f, 0.01f, {196, 202, 208, 255}, 0.90f, 0.28f,  2.2e-5f },
    { "Ti", "Titanium",22,  47.867f,  4507.0f,      523.0f,  1941.0f,  0.30f, 295000.0f, 0.50f, 0.45f, 0.006f, {151, 154, 160, 255}, 0.85f, 0.35f,  1.8e-4f },
    { "Fe", "Iron",    26,  55.845f,  7874.0f,      449.0f,  1811.0f,  0.70f, 247000.0f, 0.65f, 0.55f, 0.004f, {176, 180, 187, 255}, 0.90f, 0.30f,  999.0f },
    { "Cu", "Copper",  29,  63.546f,  8960.0f,      385.0f,  1357.77f, 0.60f, 205000.0f, 0.55f, 0.30f, 0.005f, {184, 115,  51, 255}, 0.95f, 0.22f, -9.6e-6f },
    { "Zn", "Zinc",    30,  65.38f,   7140.0f,      388.0f,   692.68f, 0.25f, 112000.0f, 0.50f, 0.25f, 0.007f, {166, 174, 180, 255}, 0.85f, 0.40f, -1.6e-5f },
    { "Ag", "Silver",  47, 107.8682f,10490.0f,      235.0f,  1234.93f, 0.05f, 105000.0f, 0.45f, 0.35f, 0.003f, {222, 224, 228, 255}, 0.98f, 0.15f, -2.4e-5f },
    { "Sn", "Tin",     50, 118.710f,  7310.0f,      228.0f,   505.08f, 0.10f,  59000.0f, 0.55f, 0.20f, 0.02f, {206, 208, 211, 255}, 0.85f, 0.35f,  2.2e-6f },
    { "Au", "Gold",    79, 196.96657f,19300.0f,     129.0f,  1337.33f, 0.03f,  64000.0f, 0.40f, 0.25f, 0.003f, {255, 205,  92, 255}, 0.98f, 0.18f, -3.4e-5f },
    { "Pb", "Lead",    82, 207.2f,   11340.0f,      129.0f,   600.61f, 0.40f,  23000.0f, 0.70f, 0.10f, 0.04f, {124, 132, 146, 255}, 0.70f, 0.48f, -1.7e-5f },
};

const gX_I8 *gX_Elements_String(gX_I32 element) {
    switch (element) {
#define X(name) case gX_ELEMENT_##name: return (const gX_I8 *)#name;
        gX_ELEMENTS
#undef X
        default: return (const gX_I8 *)"NONE";
    }
}

const gX_Element_Info *gX_Elements_Get(gX_I32 element) {
    if (element < 0) return NULL;
    if (element >= (gX_I32)(sizeof(g_elements_) / sizeof(g_elements_[0]))) return NULL;
    return &g_elements_[element];
}

gX_I32 gX_Elements_Count(gX_I0) {
    return (gX_I32)(sizeof(g_elements_) / sizeof(g_elements_[0]));
}