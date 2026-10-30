/**********************************************************************************************
*
*   [gX: src/gxmath/vector_macros.h]
*   gxmath/vector_macros.h
*   Consolidated macro generators for vector operations (v2, v3, v4).
*   Merged from: vector_unary.h, vector_func.h, vector_length.h, vector_field.h
*   //opencode: consolidated vector macro definitions
*
*   Copyright (c) 2025 lanttu
*
*
*   This software is provided "as-is", without any express or implied warranty.
*   In no event will the authors be held liable for any damages arising from
*   the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose,
*   including commercial applications, and to alter it and redistribute it
*   freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not
*     claim that you wrote the original software. If you use this software
*     in a product, an acknowledgment would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not
*     be misrepresented as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
*
*   gX addition to gxmath; distributed under the gxmath zlib/libpng terms.
*
**********************************************************************************************/

#ifndef GXMATH_VECTOR_MACROS_H
#define GXMATH_VECTOR_MACROS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "macros.h"
#include "../common/types/primary.h"
#include "scalar.h"

// ---- Unary operations ----
#define GXMATH_VECTOR_ZERO_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_Zero(gX_I0);
#define GXMATH_VECTOR_ONE_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_One(gX_I0);
#define GXMATH_VECTOR_NEGATE_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_Negate(gX_##dt##v##comps v);
#define GXMATH_VECTOR_INVERT_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_Invert(gX_##dt##v##comps v);
#define GXMATH_VECTOR_UNARY_DEC(dt,comps) \
    GXMATH_VECTOR_ZERO_DEC(dt,comps) \
    GXMATH_VECTOR_ONE_DEC(dt,comps) \
    GXMATH_VECTOR_NEGATE_DEC(dt,comps) \
    GXMATH_VECTOR_INVERT_DEC(dt,comps)

#define GXMATH_VECTOR_ZERO_DEF(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_Zero(gX_I0) { gX_##dt##v##comps r = {0}; return r; }
#define GXMATH_VECTOR_ONE_DEF(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_One(gX_I0) { gX_##dt##v##comps r = {0}; for (gX_I32 i = 0; i < comps; i++) *((gX_F32*)&r + i) = 1.0f; return r; }
#define GXMATH_VECTOR_NEGATE_DEF(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_Negate(gX_##dt##v##comps v) { gX_##dt##v##comps r = {0}; for (gX_I32 i = 0; i < comps; i++) *((gX_F32*)&r + i) = -(*((gX_F32*)&v + i)); return r; }
#define GXMATH_VECTOR_INVERT_DEF(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_Invert(gX_##dt##v##comps v) { gX_##dt##v##comps r = {0}; for (gX_I32 i = 0; i < comps; i++) *((gX_F32*)&r + i) = 1.0f/(*((gX_F32*)&v + i)); return r; }
#define GXMATH_VECTOR_UNARY_DEF(dt,comps) \
    GXMATH_VECTOR_ZERO_DEF(dt,comps) \
    GXMATH_VECTOR_ONE_DEF(dt,comps) \
    GXMATH_VECTOR_NEGATE_DEF(dt,comps) \
    GXMATH_VECTOR_INVERT_DEF(dt,comps)

// ---- Field operations ----
#define GXMATH_VECTOR_ADD_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_Add(gX_##dt##v##comps v1, gX_##dt##v##comps v2);
#define GXMATH_VECTOR_ADDSCALAR_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_AddScalar(gX_##dt##v##comps v, gX_F32 s);
#define GXMATH_VECTOR_SUB_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_Sub(gX_##dt##v##comps v1, gX_##dt##v##comps v2);
#define GXMATH_VECTOR_SUBSCALAR_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_SubScalar(gX_##dt##v##comps v, gX_F32 s);
#define GXMATH_VECTOR_MUL_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_Mul(gX_##dt##v##comps v1, gX_##dt##v##comps v2);
#define GXMATH_VECTOR_MULSCALAR_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_MulScalar(gX_##dt##v##comps v, gX_F32 s);
#define GXMATH_VECTOR_DIV_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_Div(gX_##dt##v##comps v1, gX_##dt##v##comps v2);
#define GXMATH_VECTOR_DIVSCALAR_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_DivScalar(gX_##dt##v##comps v, gX_F32 s);
#define GXMATH_VECTOR_FIELD_DEC(dt,comps) \
    GXMATH_VECTOR_ADD_DEC(dt,comps) GXMATH_VECTOR_ADDSCALAR_DEC(dt,comps) \
    GXMATH_VECTOR_SUB_DEC(dt,comps) GXMATH_VECTOR_SUBSCALAR_DEC(dt,comps) \
    GXMATH_VECTOR_MUL_DEC(dt,comps) GXMATH_VECTOR_MULSCALAR_DEC(dt,comps) \
    GXMATH_VECTOR_DIV_DEC(dt,comps) GXMATH_VECTOR_DIVSCALAR_DEC(dt,comps)

#define GXMATH_VECTOR_ADD_DEF(dt,comps) \
inline gX_##dt##v##comps gX_##dt##v##comps##_Add(gX_##dt##v##comps v1, gX_##dt##v##comps v2) { \
    return GXMATH_VECTOR##comps##_SUBSTITUTE_VOPV(dt,v1,v2,+); }
#define GXMATH_VECTOR_ADDSCALAR_DEF(dt,comps) \
inline gX_##dt##v##comps gX_##dt##v##comps##_AddScalar(gX_##dt##v##comps v, gX_F32 s) { \
    return GXMATH_VECTOR##comps##_SUBSTITUTE_VOPS(dt,v,s,+); }
#define GXMATH_VECTOR_SUB_DEF(dt,comps) \
inline gX_##dt##v##comps gX_##dt##v##comps##_Sub(gX_##dt##v##comps v1, gX_##dt##v##comps v2) { \
    return GXMATH_VECTOR##comps##_SUBSTITUTE_VOPV(dt,v1,v2,-); }
#define GXMATH_VECTOR_SUBSCALAR_DEF(dt,comps) \
inline gX_##dt##v##comps gX_##dt##v##comps##_SubScalar(gX_##dt##v##comps v, gX_F32 s) { \
    return GXMATH_VECTOR##comps##_SUBSTITUTE_VOPS(dt,v,s,-); }
#define GXMATH_VECTOR_MUL_DEF(dt,comps) \
inline gX_##dt##v##comps gX_##dt##v##comps##_Mul(gX_##dt##v##comps v1, gX_##dt##v##comps v2) { \
    return GXMATH_VECTOR##comps##_SUBSTITUTE_VOPV(dt,v1,v2,*); }
#define GXMATH_VECTOR_MULSCALAR_DEF(dt,comps) \
inline gX_##dt##v##comps gX_##dt##v##comps##_MulScalar(gX_##dt##v##comps v, gX_F32 s) { \
    return GXMATH_VECTOR##comps##_SUBSTITUTE_VOPS(dt,v,s,*); }
#define GXMATH_VECTOR_DIV_DEF(dt,comps) \
inline gX_##dt##v##comps gX_##dt##v##comps##_Div(gX_##dt##v##comps v1, gX_##dt##v##comps v2) { \
    return GXMATH_VECTOR##comps##_SUBSTITUTE_VOPV(dt,v1,v2,/); }
#define GXMATH_VECTOR_DIVSCALAR_DEF(dt,comps) \
inline gX_##dt##v##comps gX_##dt##v##comps##_DivScalar(gX_##dt##v##comps v, gX_F32 s) { \
    return GXMATH_VECTOR##comps##_SUBSTITUTE_VOPS(dt,v,s,/); }
#define GXMATH_VECTOR_FIELD_DEF(dt,comps) \
    GXMATH_VECTOR_ADD_DEF(dt,comps) GXMATH_VECTOR_ADDSCALAR_DEF(dt,comps) \
    GXMATH_VECTOR_SUB_DEF(dt,comps) GXMATH_VECTOR_SUBSCALAR_DEF(dt,comps) \
    GXMATH_VECTOR_MUL_DEF(dt,comps) GXMATH_VECTOR_MULSCALAR_DEF(dt,comps) \
    GXMATH_VECTOR_DIV_DEF(dt,comps) GXMATH_VECTOR_DIVSCALAR_DEF(dt,comps)

// ---- Length operations ----
#define GXMATH_VECTOR_LENGTH_DEC(dt,comps) \
gX_F32 gX_##dt##v##comps##_Length(gX_##dt##v##comps v);
#define GXMATH_VECTOR_LENGTHSQR_DEC(dt,comps) \
gX_F32 gX_##dt##v##comps##_LengthSqr(gX_##dt##v##comps v);
#define GXMATH_VECTOR_DISTANCE_DEC(dt,comps) \
gX_F32 gX_##dt##v##comps##_Distance(gX_##dt##v##comps v1, gX_##dt##v##comps v2);
#define GXMATH_VECTOR_DISTANCESQR_DEC(dt,comps) \
gX_F32 gX_##dt##v##comps##_DistanceSqr(gX_##dt##v##comps v1, gX_##dt##v##comps v2);
#define GXMATH_VECTOR_NORMALIZE_DEC(dt,comps) \
gX_##dt##v##comps gX_##dt##v##comps##_Normalize(gX_##dt##v##comps v);
#define GXMATH_VECTOR_LENGTHFUNC_DEC(dt,comps) \
    GXMATH_VECTOR_LENGTH_DEC(dt,comps) GXMATH_VECTOR_LENGTHSQR_DEC(dt,comps) \
    GXMATH_VECTOR_DISTANCE_DEC(dt,comps) GXMATH_VECTOR_DISTANCESQR_DEC(dt,comps) \
    GXMATH_VECTOR_NORMALIZE_DEC(dt,comps)

#define GXMATH_VECTOR_LENGTH_DEF(dt,comps) \
gX_F32 gX_##dt##v##comps##_Length(gX_##dt##v##comps v) { return sqrtf(gX_##dt##v##comps##_LengthSqr(v)); }
#define GXMATH_VECTOR_LENGTHSQR_DEF(dt,comps) \
gX_F32 gX_##dt##v##comps##_LengthSqr(gX_##dt##v##comps v) { gX_F32 r = 0; for (gX_I32 i = 0; i < comps; i++) r += *((gX_F32*)&v + i) * *((gX_F32*)&v + i); return r; }
#define GXMATH_VECTOR_LENGTHFUNC_DEF(dt,comps) \
    GXMATH_VECTOR_LENGTH_DEF(dt,comps) GXMATH_VECTOR_LENGTHSQR_DEF(dt,comps)

#ifdef __cplusplus
}
#endif

#endif //GXMATH_VECTOR_MACROS_H
