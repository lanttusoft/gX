/**********************************************************************************************
*
*   gxmath/macros.h
*   Copyright (c) 2025 nauris
* 
*   Uses functions from: raymath v1.5
* 
* 
*   <raymath> LICENSE: zlib/libpng
* 
*   Copyright (c) 2015-2023 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

//lanttu: original gxmath macro definitions for vector generators

#ifndef GX_MATH_MACROS_H
#define GX_MATH_MACROS_H

#if !defined(gX_F32_EPSILON)
    #define gX_F32_EPSILON 0.000001f
#endif

#if !defined(gX_F64_EPSILON)
    #define gX_F64_EPSILON 0.000001
#endif

#define GXMATH_VECTOR2_SUBSTITUTE(dt,x) \
    (gX_##dt##v2){(x),(x)} \
    
#define GXMATH_VECTOR2_SUBSTITUTE_OP(dt,v,op) \
    (gX_##dt##v2){op(v.x),op(v.y)} \
    
#define GXMATH_VECTOR2_SUBSTITUTE_VOPS(dt,a,b,op) \
    (gX_##dt##v2){(a.x op b),(a.y op b)} \
    
#define GXMATH_VECTOR2_SUBSTITUTE_VOPV(dt,a,b,op) \
    (gX_##dt##v2){(a.x op b.x),(a.y op b.y)} \
    
#define GXMATH_VECTOR2_SUBSTITUTE_VOPFUNC(a,b,op1,op2,func) \
    func((a.x op1 b.x) op2 (a.y op1 b.y)) \

    
#define GXMATH_VECTOR3_SUBSTITUTE(dt,x) \
    (gX_##dt##v3){(x),(x),(x)} \
    
#define GXMATH_VECTOR3_SUBSTITUTE_OP(dt,v,op) \
    (gX_##dt##v3){op(v.x),op(v.y),op(v.z)} \
    
#define GXMATH_VECTOR3_SUBSTITUTE_VOPS(dt,a,b,op) \
    (gX_##dt##v3){(a.x op b),(a.y op b),(a.z op b)} \
    
#define GXMATH_VECTOR3_SUBSTITUTE_VOPV(dt,a,b,op) \
    (gX_##dt##v3){(a.x op b.x),(a.y op b.y),(a.z op b.z)} \
    
#define GXMATH_VECTOR3_SUBSTITUTE_VOPFUNC(a,b,op1,op2,func) \
    func((a.x op1 b.x) op2 (a.y op1 b.y) op2 (a.z op1 b.z)) \
    
    
#define GXMATH_VECTOR4_SUBSTITUTE(dt,x) \
    (gX_##dt##v4){(x),(x),(x),(x)} \
    
#define GXMATH_VECTOR4_SUBSTITUTE_OP(dt,v,op) \
    (gX_##dt##v4){op(v.x),op(v.y),op(v.z),op(v.w)} \
    
#define GXMATH_VECTOR4_SUBSTITUTE_VOPS(dt,a,b,op) \
    (gX_##dt##v4){(a.x op b),(a.y op b),(a.z op b),(a.w op b)} \
    
#define GXMATH_VECTOR4_SUBSTITUTE_VOPV(dt,a,b,op) \
    (gX_##dt##v4){(a.x op b.x),(a.y op b.y),(a.z op b.z),(a.w op b.w)} \
    
#define GXMATH_VECTOR4_SUBSTITUTE_VOPFUNC(a,b,op1,op2,func) \
    func((a.x op1 b.x) op2 (a.y op1 b.y) op2 (a.z op1 b.z) op2 (a.w op1 b.w)) \

//return void; parameter void
#define GXMATH_RET0_PAR0_DEF(dt,name) \
gX_I0   gX_##dt##_##name(gX_I0); \


//return scalar; parameter void
#define GXMATH_RETSCA_PAR0_DEF(dt,name) \
gX_##dt gX_##dt##_##name(gX_I0); \

//return scalar; parameter scalar
#define GXMATH_RETSCA_PARSCA_DEF(dt,name) \
gX_##dt gX_##dt##_##name(gX_##dt s); \


//return scalar; parameter vector
#define GXMATH_RETSCA_PARVEC_DEF(dt,name,n) \
gX_##dt gX_##dt##v##n##_##name(gX_##dt##v##n v); \

//return scalar; parameter vector, vector
#define GXMATH_RETSCA_PARVECVEC_DEF(dt,name,n) \
gX_##dt gX_##dt##v##n##_##name(gX_##dt##v##n v1, gX_##dt##v##n v2); \

//return scalar; parameter vector, vector, vector
#define GXMATH_RETSCA_PARVECVECVEC_DEF(dt,name,n) \
gX_##dt gX_##dt##v##n##_##name(gX_##dt##v##n v1, gX_##dt##v##n v2, gX_##dt##v##n v3); \


//return vector; parameter void
#define GXMATH_RETVEC_PAR0_DEF(dt,name,n) \
gX_##dt##v##n gX_##dt##v##n##_##name(gX_I0); \

//return vector; parameter scalar
#define GXMATH_RETVEC_PARSCA_DEF(dt,name,n) \
gX_##dt##v##n gX_##dt##v##n##_##name(gX_##dt s); \


//return vector; parameter vector
#define GXMATH_RETVEC_PARVEC_DEF(dt,name,n) \
gX_##dt##v##n gX_##dt##v##n##_##name(gX_##dt##v##n v); \

//return vector; parameter vector, scalar
#define GXMATH_RETVEC_PARVECSCA_DEF(dt,name,n) \
gX_##dt##v##n gX_##dt##v##n##_##name(gX_##dt##v##n v, gX_##dt s);

//return vector; parameter vector, vector
#define GXMATH_RETVEC_PARVECVEC_DEF(dt,name,n) \
gX_##dt##v##n gX_##dt##v##n##_##name(gX_##dt##v##n v1, gX_##dt##v##n v2); \

//return vector; parameter vector, vector, vector
#define GXMATH_RETVEC_PARVECVECVEC_DEF(dt,name,n) \
gX_##dt##v##n gX_##dt##v##n##_##name(gX_##dt##v##n v1, gX_##dt##v##n v2, gX_##dt##v##n v3); \



#define GXMATH_VECTOR2_DEF(dt,name,val) \
inline gX_##dt##v2 gX_##dt##v2_##name(gX_I0) { \
    return (gX_##dt##v2){val,val}; \
} \

#define GXMATH_VECTOR3_DEF(dt,name,val) \
inline gX_##dt##v3 gX_##dt##v3_##name(gX_I0) { \
    return (gX_##dt##v3){val,val,val}; \
} \

#define GXMATH_VECTOR4_DEF(dt,name,val) \
inline gX_##dt##v4 gX_##dt##v4_##name(gX_I0) { \
    return (gX_##dt##v4){val,val,val,val}; \
} \


#endif //GX_MATH_MACROS_H
