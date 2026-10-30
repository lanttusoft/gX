/**********************************************************************************************
*
*   [gX: src/physics/elements.h]
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


#ifndef GX_PHYSICS_ELEMENTS_H
#define GX_PHYSICS_ELEMENTS_H

//ai: element data for the physics sandbox — the X-macro keeps the id list
//   and the data table in sync; `gX_Elements_String` feeds logs and UI.
//   Values are measured (CRC handbook / IUPAC): density in kg/m3 at STP
//   (gases) or at 20 C (solids), molar mass in g/mol.

#include "../common/types.h"

#define gX_ELEMENTS \
    X(HYDROGEN) \
    X(HELIUM) \
    X(CARBON) \
    X(ALUMINUM) \
    X(TITANIUM) \
    X(IRON) \
    X(COPPER) \
    X(ZINC) \
    X(SILVER) \
    X(TIN) \
    X(GOLD) \
    X(LEAD)

typedef enum {
    gX_ELEMENT_NONE = -1,
#define X(element) gX_ELEMENT_##element,
    gX_ELEMENTS
#undef X
} gX_Elements;

const gX_I8 *gX_Elements_String(gX_I32 element);

typedef struct gX_Element_Info {
    gX_I8 symbol[4];
    gX_I8 name[24];
    gX_I32 atomicNumber;
    gX_F32 molarMass;     //ai: g/mol
    gX_F32 density;       //ai: kg/m3
    gX_F32 specificHeat;  //ai: J/(kg*K)
    gX_F32 meltingPoint;  //ai: K
    gX_F32 emissivity;    //ai: nominal 0..1 (surface-dependent)
    gX_F32 latentHeat;    //ai: J/kg heat of fusion (0 = no plateau modelled)
    //ai: contact surface parameters for the wrapper's simplified contact
    //   rule (min friction, max restitution, rolling resistances add).
    //   Real pairs are tabulated per material combination and vary widely
    //   with surface finish, so these are nominal demo ballparks, not
    //   engineering data.
    gX_F32 friction;      //ai: nominal dynamic friction 0..1
    gX_F32 restitution;   //ai: nominal restitution 0..1 (bounciness)
    //ai: nominal rolling + spin resistance (dimensionless, ~0.001 steel to
    //   0.05 soft/rough); both surfaces contribute additively.
    gX_F32 rollingFriction;
    //ai: nominal appearance for the sandbox props (polished-metal ballparks,
    //   graphite for carbon; real finishes vary) and the magnetic
    //   susceptibility mu_r - 1 (SI: iron carries the effective
    //   ferromagnetic value, the rest are weak paramagnets/diamagnets).
    gX_Color albedo;
    gX_F32 metalness;     //ai: 0..1 (1 = polished metal)
    gX_F32 roughness;     //ai: 0..1 (0 = mirror)
    gX_F32 susceptibility;
} gX_Element_Info;

//ai: table row for an element id (NULL for out-of-range/NONE).
const gX_Element_Info *gX_Elements_Get(gX_I32 element);

//ai: number of entries in the X-macro list.
gX_I32 gX_Elements_Count(gX_I0);

#endif //GX_PHYSICS_ELEMENTS_H
