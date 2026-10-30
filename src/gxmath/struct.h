/**********************************************************************************************
*
*   gxmath/struct.h
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

//lanttu: original gxmath struct type definitions

#ifndef GXMATH_STRUCT_H
#define GXMATH_STRUCT_H

//array vector
typedef struct gX_F32a2  { gX_F32 v[2]; } gX_F32a2;
typedef struct gX_F32a3  { gX_F32 v[3]; } gX_F32a3;
typedef struct gX_F32a4  { gX_F32 v[4]; } gX_F32a4;
typedef struct gX_F32a6  { gX_F32 v[6]; } gX_F32a6;
typedef struct gX_F32a8  { gX_F32 v[8]; } gX_F32a8;
typedef struct gX_F32a9  { gX_F32 v[9]; } gX_F32a9;
typedef struct gX_F32a12 { gX_F32 v[12];} gX_F32a12;
typedef struct gX_F32a16 { gX_F32 v[16];} gX_F32a16;

//array
typedef gX_F32 gX_F32_2[2];
typedef gX_F32 gX_F32_4[4];
typedef gX_F32 gX_F32_8[8];
typedef gX_F32 gX_F32_16[16];
typedef gX_F32 gX_F32_32[32];
typedef gX_F32 gX_F32_64[64];
typedef gX_F32 gX_F32_128[128];
typedef gX_F32 gX_F32_256[256];
typedef gX_F32 gX_F32_512[512];
typedef gX_F32 gX_F32_1024[1024];
typedef gX_F32*gX_F32_n;
typedef gX_U32 gX_F32_n_size;

//complex
typedef gX_F32 _Complex gX_F32_complex;

typedef gX_F32_complex gX_F32_complex_2[2];
typedef gX_F32_complex gX_F32_complex_4[4];
typedef gX_F32_complex gX_F32_complex_8[8];
typedef gX_F32_complex gX_F32_complex_16[16];
typedef gX_F32_complex gX_F32_complex_32[32];
typedef gX_F32_complex gX_F32_complex_64[64];
typedef gX_F32_complex gX_F32_complex_128[128];
typedef gX_F32_complex gX_F32_complex_256[256];
typedef gX_F32_complex gX_F32_complex_512[512];
typedef gX_F32_complex gX_F32_complex_1024[1024];
typedef gX_F32_complex*gX_F32_complex_n;
typedef gX_U32         gX_F32_complex_n_size;

#endif //GXMATH_STRUCT_H
