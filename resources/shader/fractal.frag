#version 330

//modified from bg.frag
//opencode: refactored to use fractal_math.glsl include for DD48 precision and algorithm dispatch

uniform float time;
uniform vec2 res;

uniform int iterations;
uniform float exponent;
uniform float escape;
uniform vec2 seed;

uniform float zoom; 
uniform float angle;
uniform float scale;
uniform vec2 pos;
uniform vec2 tilt;

uniform int fractalType;

uniform vec2 y_offset;

uniform int paletteMode;

uniform vec2 morph;

uniform int trapMode;
uniform vec2 trapPos;
uniform float trapRadius;

uniform int paletteAmount;
uniform vec3 paletteFactor;
uniform vec3 paletteSpeed;
uniform vec3 palettePhase;
uniform vec3 paletteAmplitude;

out vec4 finalColor;

//----------MATH LIBRARY----------//
#include "fractal_math.glsl"

void main() {
    float asp = res.x / res.y;
    vec2 screencenter = vec2(0.5, 0.5);
    vec2 coord = vec2(asp, 1.0) * (gl_FragCoord.xy / res - screencenter);
    
    // Camera transform
    mat2 rot = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    float tiltexpression = zoom * (1.0 + coord.x * tilt.x + coord.y * tilt.y);
    coord /= tiltexpression;
    coord *= rot;
    coord += pos;
    
    vec3 color = vec3(0.0);
    
    //opencode: check if initial point is trivially divergent (outside escape radius)
    if (length(coord) > escape) {
        color = vec3(0.0, 0.0, 0.0);
    } else {
        //opencode: orbit trap distance accumulator
        float trap_dist = 1.0e10;
        
        //opencode: dispatch to math library - handles precision, algorithm, and trap tracking
        float iters = iterate_fractal(coord, seed, iterations, exponent, escape, fractalType, y_offset, trap_dist);
        int i = int(iters);
        
        if (i < iterations) {
            // Escaping point - color with palette using fractional iteration count
            color = palette_color(iters, paletteMode, iterations, paletteAmount, paletteFactor, paletteSpeed, palettePhase, paletteAmplitude, time);
        }
        // else: non-escaping (inside set) - stays black
        
        //opencode: blend orbit trap coloring on top
        if (trapMode > 0 && trap_dist < trapRadius) {
            vec3 trapCol = trap_color(trap_dist, trapRadius);
            color = mix(color, trapCol, 0.5);
        }
    }
    
    //opencode: negative tilt region indicator (blue)
    if (tiltexpression < 0.0) {
        color = vec3(0.2, 0.5, 1.0);
    }
    
    finalColor = vec4(color, 1.0);
}
