/**********************************************************************************************
*
*   gxmath/config.h
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

#ifndef GXMATH_CONFIG_H
#define GXMATH_CONFIG_H

//opencode: Ported physics constants from gamemath to gxmath

#ifndef gX_PHYSICS_ALPHA
    #define gX_PHYSICS_ALPHA 7.2973525664e-3
#endif

#ifndef gX_PHYSICS_AU
    #define gX_PHYSICS_AU 149597870691.0
#endif

#ifndef gX_PHYSICS_AVOGADRO
    #define gX_PHYSICS_AVOGADRO 6.022140857e23
#endif

#ifndef gX_PHYSICS_BOLTZMANN
    #define gX_PHYSICS_BOLTZMANN 1.380649e-23
#endif

#ifndef gX_PHYSICS_C
    #define gX_PHYSICS_C 299792458.0
    #define gX_PHYSICS_LIGHTSPEED gX_PHYSICS_C
#endif

//ai: vacuum permittivity and the Coulomb constant (k_e = 1/(4*pi*eps0)).
#ifndef gX_PHYSICS_EPSILON0
    #define gX_PHYSICS_EPSILON0 8.8541878128e-12
#endif

#ifndef gX_PHYSICS_COULOMB
    #define gX_PHYSICS_COULOMB 8.9875517923e9
#endif

#ifndef gX_PHYSICS_DIRAC
    #define gX_PHYSICS_DIRAC 1.054571800e-34
    #define gX_PHYSICS_HBAR gX_PHYSICS_DIRAC
#endif

//ai: elementary charge (not Euler's number).
#ifndef gX_PHYSICS_E
    #define gX_PHYSICS_E 1.6021766208e-19
#endif

//ai: vacuum permeability (mu0 = 4*pi*1e-7). The old misnamed PHYSICS_G
//   constant was this value; nothing used it.
#ifndef gX_PHYSICS_MU0
    #define gX_PHYSICS_MU0 12.566370614e-7
#endif

//ai: the magnetic constant mu0/(4*pi) = 1e-7 exactly (SI-2019 definition);
//   the dipole field/force laws use this factor.
#ifndef gX_PHYSICS_MAGNETIC
    #define gX_PHYSICS_MAGNETIC 1.0e-7
#endif

//ai: Stefan-Boltzmann constant and Wien's displacement constant.
#ifndef gX_PHYSICS_STEFAN_BOLTZMANN
    #define gX_PHYSICS_STEFAN_BOLTZMANN 5.670374419e-8
#endif

#ifndef gX_PHYSICS_WIEN
    #define gX_PHYSICS_WIEN 2.897771955e-3
#endif

#ifndef gX_PHYSICS_GAS
    #define gX_PHYSICS_GAS 8.3144598
#endif

#ifndef gX_PHYSICS_H
    #define gX_PHYSICS_H 6.626070040e-34
    #define gX_PHYSICS_PLANCK gX_PHYSICS_H
#endif

#ifndef gX_PHYSICS_LIGHTYEAR
    #define gX_PHYSICS_LIGHTYEAR 9.4607304725808e15
#endif

//ai: the simulation-scale clamp for the relativity helpers — |beta| stays
//   below this so gamma is finite (<= ~70.7). The SI-exact
//   `gX_F32_Physics_LorentzLength` keeps using the real gX_PHYSICS_C.
#ifndef gX_PHYSICS_RELATIVITY_BETA_MAX
    #define gX_PHYSICS_RELATIVITY_BETA_MAX 0.9999f
#endif

#ifndef gX_PHYSICS_PARSEC
    #define gX_PHYSICS_PARSEC 3.08567802e16
#endif

#endif //GXMATH_CONFIG_H
