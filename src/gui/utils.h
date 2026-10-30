/**********************************************************************************************
*
*   [gX: src/gui/utils.h]
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


#ifndef GX_GUI_UTILS_H
#define GX_GUI_UTILS_H

#include "../common/types.h"

#include "element/element.h"

gX_I0 gX_GUI_Element_ExecuteState_Update(gX_GUI_Element_ExecuteState *es, gX_GUI_Element *element, gX_I1 *execute);

#endif //GX_GUI_UTILS_H
