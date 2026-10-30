//opencode: shared fog blending helpers

#ifndef GX_COMMON_FOG_GLSL
#define GX_COMMON_FOG_GLSL

vec4 gX_Fog_Mix(vec4 color, float fragDistance,
    float fogNear, float fogFar, vec4 fogColor)
{
    float fogvalue = (fragDistance - fogNear) / max(fogFar - fogNear, 0.001);
    return mix(color, fogColor, clamp(fogvalue, 0.0, 1.0));
}

#endif //GX_COMMON_FOG_GLSL
