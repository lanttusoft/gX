//opencode: water surface with Fresnel, specular, foam, sky reflection, and underwater
#version 330

uniform float time, cfov;
uniform vec2  res;
uniform vec3  cpos, ctarget, cup;

uniform samplerCube skyBox;
uniform sampler2D  skyTex2D;
uniform int        skyUse2D;
uniform int        underwater;   // 1 = camera below water surface
uniform vec3  sunDir;
uniform vec3  sunColor;
uniform float sunBrightness;
uniform int velocityMode = 0;

uniform sampler2D normalMap;
uniform float normalMapStrength;
uniform vec2  normalMapScale;
//ai: editor-controlled strength and frequency of the shader-generated normal detail.
uniform float proceduralNormalStrength;
uniform float proceduralNormalScale;
//ai: legacy diagonal drift is preserved when authored rates are omitted.
uniform vec2  normalMapScroll = vec2(0.01);
//opencode: authoring tint multiplies the shallow/deep base water colors.
uniform vec3 waterTint = vec3(1.0);

in vec2  fragTexCoord;
in vec3  fragNormal;
in vec3  fragPosition;
in float fragWaveHeight;
in float fragEdgeDist;
in vec2   vVel;

//opencode: v0.1.20 - wave uniforms mirrored from water.vert so the fragment
//   stage can recompute the surface normal analytically PER-PIXEL. The old
//   path shaded with the interpolated per-VERTEX normal: at the demo's
//   camera angle the 0.31-unit mesh cells showed as a diamond/checker
//   lattice (triangle-facet shading) that no amplitude/steepness tune could
//   remove, because the facets were interpolation, not wave energy.
uniform int   waveCount;
uniform vec2  waveDirs[6];
uniform float waveAmps[6];
uniform float waveFreqs[6];
uniform float waveSpeeds[6];
uniform float waveSteeps[6];
uniform int   rippleCount;
uniform vec2  ripplePos[16];
uniform float rippleAmp[16];
uniform float rippleRadius[16];
uniform float ripplePhase[16];

out vec4 finalColor;

#ifdef GX_WATER_REFRACTION_EXPERIMENT
//ai: compile-isolated screen-space transmission; coordinates match the raw pre-water blit.
uniform sampler2D waterSceneColor;
uniform sampler2D waterSceneDepth;
uniform mat4 waterInverseVP;
uniform mat4 matView;
uniform int waterSceneValid;
uniform int waterSceneDiagnostic;
//ai: independent transient gate for the experiment's shallow contact foam.
uniform int waterDepthFoamEnabled;
uniform float waterPlaneHeight;
uniform float waterOffsetPixels;

vec3 waterSceneTransmission(vec3 legacy, float alpha, vec3 normal)
{
    int reason = 2;
    vec3 transmitted = legacy;
    if (waterSceneValid == 1 && underwater == 0 && alpha > 0.0001) {
        ivec2 size = textureSize(waterSceneDepth, 0);
        vec2 uv = gl_FragCoord.xy / vec2(size);
        vec2 direction = (matView * vec4(normal, 0.0)).xy;
        float magnitude = length(direction);
        if (magnitude > 0.0001) direction /= magnitude;
        else direction = vec2(0.0);
        vec2 candidateUV = uv + direction * waterOffsetPixels / vec2(size);
        reason = 3;
        if (!any(isnan(candidateUV)) && !any(isinf(candidateUV))
            && all(greaterThanEqual(candidateUV, vec2(0.0)))
            && all(lessThan(candidateUV, vec2(1.0)))) {
            ivec2 pixel = ivec2(candidateUV * vec2(size));
            float depth = texelFetch(waterSceneDepth, pixel, 0).r;
            reason = 4;
            if (depth >= 0.0 && depth < 1.0) {
                vec2 centerUV = (vec2(pixel) + 0.5) / vec2(size);
                vec4 point = waterInverseVP * vec4(centerUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
                reason = 7;
                if (abs(point.w) > 0.000001 && !any(isnan(point)) && !any(isinf(point))) {
                    vec3 world = point.xyz / point.w;
                    float waterDepth = -(matView * vec4(fragPosition, 1.0)).z;
                    float candidateDepth = -(matView * vec4(world, 1.0)).z;
                    if (!any(isnan(world)) && !any(isinf(world))
                        && !isnan(waterDepth) && !isinf(waterDepth) && waterDepth > 0.0
                        && !isnan(candidateDepth) && !isinf(candidateDepth)) {
                        reason = 5;
                        if (candidateDepth > waterDepth + max(0.01, 0.001 * waterDepth)) {
                            reason = 6;
                            if (world.y < waterPlaneHeight - 0.01) {
                                vec3 displaced = texelFetch(waterSceneColor, pixel, 0).rgb;
                                vec3 original = texelFetch(waterSceneColor, ivec2(gl_FragCoord.xy), 0).rgb;
                                if (!any(isnan(displaced)) && !any(isinf(displaced))
                                    && !any(isnan(original)) && !any(isinf(original))) {
                                    transmitted = legacy + ((1.0 - alpha) / alpha) * (displaced - original);
                                    reason = 1;
                                } else reason = 7;
                            }
                        }
                    }
                }
            }
        }
    }
    if (waterSceneDiagnostic == 1) {
        if (reason == 1) return vec3(0, 1, 0);
        if (reason == 2) return vec3(0.5, 0, 1);
        if (reason == 3) return vec3(1, 1, 0);
        if (reason == 4) return vec3(0, 1, 1);
        if (reason == 5) return vec3(1, 0, 0);
        if (reason == 6) return vec3(1, 0.5, 0);
        return vec3(1, 0, 1);
    }
    return transmitted;
}

//ai: opt-in contact foam uses only current-frame geometry just behind shallow water.
float waterDepthContactFoam()
{
    if (waterDepthFoamEnabled != 1 || waterSceneValid != 1 || underwater == 1)
        return 0.0;
    ivec2 size = textureSize(waterSceneDepth, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(waterSceneDepth, pixel, 0).r;
    float depthEdge = smoothstep(0.0005, 0.008, fwidth(depth));
    if (!(depth >= 0.0 && depth < 1.0)) return 0.0;
    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
    vec4 point = waterInverseVP * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    if (abs(point.w) <= 0.000001 || any(isnan(point)) || any(isinf(point)))
        return 0.0;
    vec3 world = point.xyz / point.w;
    float waterDepth = -(matView * vec4(fragPosition, 1.0)).z;
    float sceneDepth = -(matView * vec4(world, 1.0)).z;
    float viewSeparation = sceneDepth - waterDepth;
    float shallowDepth = waterPlaneHeight - world.y;
    if (any(isnan(world)) || any(isinf(world)) || isnan(waterDepth) || isinf(waterDepth)
        || isnan(sceneDepth) || isinf(sceneDepth) || isnan(shallowDepth) || isinf(shallowDepth)
        || waterDepth <= 0.0
        || viewSeparation <= max(0.01, 0.001 * waterDepth) || shallowDepth <= 0.01)
        return 0.0;
    float viewContact = 1.0 - smoothstep(0.05, 1.5, viewSeparation);
    float shallowContact = 1.0 - smoothstep(0.01, 1.25, shallowDepth);
    return 0.6 * viewContact * shallowContact * depthEdge;
}
#endif

//ai: periodic value-noise derivatives add micro-ripple slopes without texture assets.
float waterNoiseHash(vec2 cell)
{
    cell = mod(cell, 289.0);
    vec3 p = fract(vec3(cell.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

//ai: cubic value noise returns the scalar field and its analytic local gradient.
vec3 waterValueNoiseDerivative(vec2 position)
{
    vec2 cell = floor(position);
    vec2 local = fract(position);
    vec2 blend = local * local * (3.0 - 2.0 * local);
    vec2 blendDerivative = 6.0 * local * (1.0 - local);
    float value00 = waterNoiseHash(cell);
    float value10 = waterNoiseHash(cell + vec2(1.0, 0.0));
    float value01 = waterNoiseHash(cell + vec2(0.0, 1.0));
    float value11 = waterNoiseHash(cell + vec2(1.0, 1.0));
    float row0 = mix(value00, value10, blend.x);
    float row1 = mix(value01, value11, blend.x);
    float value = mix(row0, row1, blend.y);
    float derivativeX = mix((value10 - value00) * blendDerivative.x,
                            (value11 - value01) * blendDerivative.x, blend.y);
    float derivativeY = (row1 - row0) * blendDerivative.y;
    return vec3(value, derivativeX, derivativeY);
}

//ai: fade detail once a noise footprint approaches subpixel scale.
float waterNoiseScreenFade(vec2 position)
{
    float footprint = max(length(dFdx(position)), length(dFdy(position)));
    return 1.0 - smoothstep(0.35, 0.8, footprint);
}

//ai: two world-anchored, counter-scrolling noise bands return a band-limited height gradient.
vec2 waterProceduralSlope(vec2 worldXZ)
{
    float detailScale = max(proceduralNormalScale, 0.0);
    vec2 coarsePosition = (worldXZ * 1.25 + time * vec2(0.018, -0.013)) * detailScale;
    vec2 finePosition = (worldXZ * 2.8 + time * vec2(-0.027, 0.021)
        + vec2(17.0, 41.0)) * detailScale;
    vec3 coarseNoise = waterValueNoiseDerivative(coarsePosition);
    vec3 fineNoise = waterValueNoiseDerivative(finePosition);
    vec2 coarseSlope = coarseNoise.yz * (1.25 * detailScale) * 0.025
        * waterNoiseScreenFade(coarsePosition);
    vec2 fineSlope = fineNoise.yz * (2.8 * detailScale) * 0.008
        * waterNoiseScreenFade(finePosition);
    return (coarseSlope + fineSlope) * max(proceduralNormalStrength, 0.0);
}

vec3 waveNormalAt(vec2 pos)
{
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    vec3 bitangent = vec3(0.0, 0.0, 1.0);
    for (int i = 0; i < waveCount && i < 6; i++)
    {
        float theta = waveFreqs[i] * dot(waveDirs[i], pos) + time * waveSpeeds[i];
        float c = cos(theta);
        float s = sin(theta);
        float Qi = max(waveSteeps[i] / max(waveFreqs[i] * waveAmps[i] * float(waveCount), 0.001), 0.0);
        Qi = min(Qi, 0.85);
        float WA = waveFreqs[i] * waveAmps[i];

        tangent.x   -= Qi * waveDirs[i].x * waveDirs[i].x * WA * s;
        tangent.y   += waveDirs[i].x * WA * c;
        tangent.z   -= Qi * waveDirs[i].x * waveDirs[i].y * WA * s;

        bitangent.x -= Qi * waveDirs[i].x * waveDirs[i].y * WA * s;
        bitangent.y += waveDirs[i].y * WA * c;
        bitangent.z -= Qi * waveDirs[i].y * waveDirs[i].y * WA * s;
    }
    for (int i = 0; i < rippleCount && i < 16; i++)
    {
        vec2 delta = pos - ripplePos[i];
        float dist = length(delta);
        if (dist < rippleRadius[i])
        {
            float r = dist / max(rippleRadius[i], 0.001);
            float envelope = 1.0 - r * r;
            float dr = 1.0 / max(rippleRadius[i], 0.001);
            float dEnvelope = -2.0 * r * dr;
            float wave = sin(dist * 3.0 - ripplePhase[i]);
            float dWave = cos(dist * 3.0 - ripplePhase[i]) * 3.0;
            float dRipple_dDist = rippleAmp[i] * (dWave * envelope + wave * dEnvelope);
            vec2 rippleDir = delta / max(dist, 0.001);

            tangent.y   += dRipple_dDist * rippleDir.x;
            bitangent.y += dRipple_dDist * rippleDir.y;
        }
    }
    return normalize(cross(tangent, bitangent));
}

//ai: combine procedural normals, world-space breakup, and opt-in depth foam with water shading.
void main()
{
    if (velocityMode == 1)
    {
        //opencode: water velocity uses B=1 (written) and A=1 (opaque).
        finalColor = vec4(vVel, 1.0, 1.0);
        return;
    }
    vec3 N = waveNormalAt(fragPosition.xz);
    vec3 V = normalize(cpos - fragPosition);

    // Normal map detail blend
    //opencode: judging - distance fade: minified normals alias into moiré
    //   at grazing incidence; melt detail with view distance (near keeps
    //   full strength, far tends to the analytic wave normal).
    float detailFade = exp(-length(cpos - fragPosition) / 30.0);
    vec2 proceduralSlope = waterProceduralSlope(fragPosition.xz) * detailFade;
    vec3 T = normalize(cross(N, abs(N.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0)));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);
    vec2 tangentSlope = vec2(dot(proceduralSlope, T.xz),
                             dot(proceduralSlope, B.xz));
    vec2 normalXY = -tangentSlope;
    if (normalMapStrength * detailFade > 0.001) {
        vec2 uv = fragTexCoord * normalMapScale + time * normalMapScroll;
        vec3 sampledNormal = texture(normalMap, uv).rgb * 2.0 - 1.0;
        normalXY += sampledNormal.xy * normalMapStrength * detailFade;
    }
    vec3 combinedNormal = vec3(normalXY,
        sqrt(max(1.0 - dot(normalXY, normalXY), 0.0)));
    N = normalize(TBN * combinedNormal);

    // Shared water colors and foam
    vec3 shallowColor = vec3(0.05, 0.25, 0.20) * waterTint;
    vec3 deepColor    = vec3(0.00, 0.03, 0.06) * waterTint;
    float depthFactor = clamp((fragPosition.y + 2.0) * 0.5, 0.0, 1.0);
    vec3 waterColor = mix(deepColor, shallowColor, depthFactor);

    float NdotV = abs(dot(N, V));
    float fresnel = pow(1.0 - NdotV, 4.0);
    fresnel = clamp(fresnel, 0.0, 1.0);

    float crestFoam = smoothstep(0.3, 0.7, fragWaveHeight) * 0.4;
    float shoreFoam = 0.0;
    if (fragEdgeDist < 1.5) {
        shoreFoam = (1.0 - fragEdgeDist / 1.5) * 0.6;
        shoreFoam *= smoothstep(0.05, 0.25, abs(fragWaveHeight));
    }
    float foam = max(crestFoam, shoreFoam);
#ifdef GX_WATER_REFRACTION_EXPERIMENT
    foam = max(foam, waterDepthContactFoam());
#endif
    vec2 foamPosition = fragPosition.xz * 0.72 + time * vec2(-0.008, 0.011);
    vec3 foamNoise = waterValueNoiseDerivative(foamPosition);
    float foamDetailFade = waterNoiseScreenFade(foamPosition);
    float foamPattern = smoothstep(0.22, 0.78, foamNoise.x);
    foam *= mix(1.0, mix(0.22, 1.0, foamPattern), foamDetailFade);

    if (underwater == 1)
    {
        // === UNDERWATER: camera below surface ===
        float transmit = 1.0 - fresnel;

        // Sky refraction through surface (Snell's window)
        vec3 Rrefr = refract(-V, N, 1.333);
        vec3 skyAbove;
        if (skyUse2D == 1) {
            vec2 uvRefr = vec2(atan(Rrefr.z, Rrefr.x) / 6.2832 + 0.5, acos(Rrefr.y) / 3.1416);
            skyAbove = texture(skyTex2D, uvRefr).rgb;
        } else {
            skyAbove = texture(skyBox, Rrefr).rgb;
        }

        // Exponential fog
        float dist = length(cpos - fragPosition);
        float fogDensity = 0.15;
        float fog = 1.0 - exp(-dist * fogDensity);
        vec3 fogColor = vec3(0.02, 0.12, 0.10);

        // Sun shafts and specular
        vec3 Luw = normalize(sunDir);
        float sunShaft = pow(max(dot(N, -V), 0.0), 8.0) * 0.3;
        vec3 Huw = normalize(-V + Luw);
        float specUw = pow(max(dot(N, Huw), 0.0), 64.0);

        vec3 uwColor = mix(waterColor, skyAbove, transmit * 0.4);
        uwColor += fogColor * fog;
        uwColor += sunColor * sunBrightness * sunShaft * 0.5;
        uwColor += sunColor * sunBrightness * specUw * 0.8;
        uwColor += foam * vec3(1.0, 0.95, 0.85) * 0.3;

        finalColor = vec4(uwColor, clamp(1.0 - fog * 0.5, 0.3, 1.0));
    }
    else
    {
        // === ABOVE WATER: reflection + specular ===
        vec3 R = reflect(-V, N);
        vec3 skyRefl;
        if (skyUse2D == 1) {
            vec2 skyUV = vec2(atan(R.z, R.x) / 6.2832 + 0.5, acos(R.y) / 3.1416);
            skyRefl = texture(skyTex2D, skyUV).rgb;
        } else {
            skyRefl = texture(skyBox, R).rgb;
        }

        vec3 L = normalize(sunDir);
        vec3 H = normalize(V + L);
        //opencode: v0.1.20 - spec softened (256->96, x1.5->x0.9): the tight
        //     lobe turned every wave-facet into a hard bright streak.
        float spec = pow(max(dot(N, H), 0.0), 96.0);
        vec3 specColor = sunColor * sunBrightness * spec * 0.9;

        vec3 color = mix(waterColor, skyRefl, fresnel * 0.7);
        color += specColor;
        color += foam * vec3(1.0, 0.95, 0.85);

        float edgeFade = 1.0;
        float halfSize = 20.0;
        float margin = 2.0;
        float dx = abs(fragPosition.x);
        float dz = abs(fragPosition.z);
        if (dx > halfSize - margin)
            edgeFade = min(edgeFade, (halfSize - dx + margin) / margin);
        if (dz > halfSize - margin)
            edgeFade = min(edgeFade, (halfSize - dz + margin) / margin);
        edgeFade = clamp(edgeFade, 0.0, 1.0);

        //opencode: v0.1.16 - deep-water opacity (was mix(0.6, 0.95)): the
        //   0.6 floor blended the water body with whatever renders BEHIND it -
        //   in the GFX scene that is the pale sky.color clear, washing the
        //   teal out to gray-white. The standalone scene looked right only
        //   because its background is black.
        float alpha = mix(0.88, 0.96, fresnel) * edgeFade;
#ifdef GX_WATER_REFRACTION_EXPERIMENT
        color = waterSceneTransmission(color, alpha, N);
        if (waterSceneDiagnostic == 1) alpha = 1.0;
#endif
        finalColor = vec4(color, alpha);
    }
}
