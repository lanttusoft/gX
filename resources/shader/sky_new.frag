//opencode: physically-based procedural sky with Rayleigh/Mie scattering, altitude, turbidity
#version 330

uniform float time, cfov;
uniform vec2 res;
uniform vec3 cpos, ctarget, cup;
uniform vec3 sunDir;

//opencode: atmospheric state - x=altitude km (0-5), y=turbidity (1-10)
uniform vec2  atmosphereState;
uniform float skyDensity;       // color intensity (0.1-3.0)
uniform float skyHue;           // additional hue shift (0-360)
uniform float skyContrast;      // color contrast boost (0.5-2.0, default 1.2)
uniform float horizonGlow;      // horizon glow intensity (0-2)
uniform float sunDiskSize;      // sun disk size (500-5000)
uniform float sunDiskIntensity; // sun disk brightness (0-10)

// Starfield controls
uniform float starDensity;      // star density multiplier (0.5-3.0, default 1.0)
uniform float starBrightness;   // star brightness multiplier (0.0-3.0, default 1.0)
uniform float starTwinkleSpeed; // star twinkle speed (0.0-5.0, default 1.0)

// Moon
uniform vec3  moonDir;          // direction TO moon center
uniform float moonSize;         // moon angular size (500-5000, default 700)
uniform float moonBrightness;   // moon brightness (0-10, default 2.0)

// Planets (up to 4)
uniform int   planetCount;      // number of active planets (0-4)
uniform vec3  planetDir[4];     // directions TO each planet
uniform float planetSize[4];    // angular size of each planet
uniform vec3  planetColor[4];   // color of each planet
uniform float planetBrightness[4]; // brightness of each planet

//opencode: SSR sky-LUT mode - 1 = ray from equirect (world-anchored, Y-up)
//   texture coords instead of the camera basis; the SSR shader samples the
//   LUT by reflected-ray direction with the mirror mapping (see gfx_ssr.frag):
//     v = 0 at BOTTOM (up), pitch = v*pi from +Y; u = yaw/(2pi), yaw from +X
uniform int skyEquirect;
//ai: visible image backgrounds never select the reflection/LUT source implicitly.
uniform int visibleImageSky = 0;
uniform samplerCube visibleSkyCube;

in vec2 fragTexCoord;
out vec4 finalColor;
//ai: isolated capture experiment; main sky retains alpha one.
uniform int gXProbeCoverageCapture = 0;
//ai: isolated LUT direction correspondence control, reset before scene/cube sky draws.
uniform int gXProbeSkyDirectionCapture = 0;

const float pi = 3.14159265;

// HSV to RGB conversion for hue shift
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Simple hash for stars
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

// Safe normalize for vec2 (avoids NaN at zero length)
vec2 safeNormalize(vec2 v) {
    float len = length(v);
    return len > 0.001 ? v / len : vec2(0.0, 0.0);
}

//ai: editable celestial directions and polar camera frames must remain finite.
vec3 skyDirection(vec3 value, vec3 fallback) {
    float squared = dot(value,value);
    return squared > 1e-12 ? value * inversesqrt(squared) : fallback;
}

//opencode: optical airmass (relative path length through atmosphere)
//   Uses the Kasten-Young formula which accounts for atmospheric refraction
//   near the horizon. For elevation angle ? (0=horizon, ?/2=zenith):
//   X = 1 / sin(? + 0.025? ? exp(?? / 11.6?))
float airmass(float sinElev) {
    //ai: Kasten-Young in degrees is finite at the horizon (~38), not ~600.
    float elev = asin(clamp(sinElev, 0.0, 1.0));
    float elevDeg = elev * 180.0 / pi;
    return 1.0 / (sin(elev) + 0.50572 * pow(elevDeg + 6.07995, -1.6364));
}

//opencode: Rayleigh scattering phase function P(?) = 3/(16?) ? (1 + cos?(?))
float rayleighPhase(float cosTheta) {
    return (3.0 / (16.0 * pi)) * (1.0 + cosTheta * cosTheta);
}

//opencode: Mie scattering phase function (Henyey-Greenstein)
//   g = asymmetry factor (?0.85 for atmospheric aerosols)
float miePhase(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / max(1e-6, 4.0 * pi * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
}

void main() {
    //opencode: use the quad's interpolated texcoord, NOT gl_FragCoord.
    //   gl_FragCoord y=0 is the BOTTOM on GL but the TOP on Vulkan - the sky
    //   rendered vertically mirrored on Vulkan (the ground branch at the top
    //   -> green sky). fragTexCoord (0,0) is the quad's top-left on every
    //   backend, so 1.0 - y restores the GL convention the shader was
    //   written against (works for both APIs, independent of viewport).
    vec2 uv = (vec2(fragTexCoord.x, 1.0 - fragTexCoord.y) / 1.0) * 2.0 - 1.0;
    uv.x *= res.x / res.y;

    // Build ray direction from camera
    vec3 camForward = skyDirection(ctarget - cpos, vec3(0.0,0.0,1.0));
    vec3 cameraReference = abs(camForward.y) < 0.9 ? vec3(0.0,1.0,0.0) : vec3(0.0,0.0,-1.0);
    vec3 camRight = skyDirection(cross(camForward, cup), normalize(cross(camForward,cameraReference)));
    vec3 camUp = cross(camRight, camForward);
    float fovRad = cfov * pi / 180.0;
    float planeDist = 1.0 / tan(fovRad * 0.5);
    vec3 rd;
    if (skyEquirect != 0)
    {
        //opencode: equirect ray - world-anchored, independent of camera.
        //   Convention (mirrored in gfx_ssr.frag LUT sampling):
        //   v=0 at bottom = straight up, v=1 = down; u=0.5 = +Z, u wraps 2pi
        //   rd = (cos(yaw)*sin(pitch), cos(pitch), sin(yaw)*sin(pitch))
        vec2 equ = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);   // y=0 at bottom (GL coords)
        float pitch = equ.y * pi;
        float yaw = equ.x * 2.0 * pi;
        rd = normalize(vec3(cos(yaw) * sin(pitch), cos(pitch), sin(yaw) * sin(pitch)));
    }
    else
    {
        rd = normalize(uv.x * camRight + uv.y * camUp + camForward * planeDist);
    }

    //ai: sample the existing cubemap loader's world-direction contract without additional tone conversion.
    if (visibleImageSky == 1)
    {
        finalColor = vec4(texture(visibleSkyCube, rd).rgb, 1.0);
        return;
    }

    // Sun direction (normalized)
    vec3 sunDirN = skyDirection(sunDir,vec3(0.0,1.0,0.0));
    float cosTheta = dot(rd, sunDirN);
    float viewSinElev = rd.y;      // rd is normalized, so rd.y = sin(elevation angle)
    float sunSinElev = sunDirN.y;  // sun height -1..1
    float sunElev = asin(clamp(sunSinElev, -1.0, 1.0));
    float sunY = sunSinElev;       // -1 (below) to 1 (above)

    // Hue shift as color tint
    vec3 hueColor = hsv2rgb(vec3(skyHue / 360.0, 0.3, 1.0));
    float dayFactor = clamp(sunY * 0.55 + 0.55, 0.0, 1.0);

    // ================================================================
    // Physical atmosphere parameters
    // ================================================================

    float altitudeKm = max(atmosphereState.x, 0.0);
    float turbidity  = clamp(atmosphereState.y, 1.0, 10.0);

    // Atmospheric density decays exponentially with altitude (scale height ? 8.5 km)
    float density = exp(-altitudeKm / 8.5);

    // Wavelengths for RGB approximation (in ?m)
    //   Red=0.650, Green=0.570, Blue=0.475
    const vec3 lambda = vec3(0.650, 0.570, 0.475);
    const vec3 lambda4 = lambda * lambda * lambda * lambda;

    // Rayleigh scattering coefficient ? 1/?? (sea level reference)
    //   At 550nm (green): ? ? 0.01 km?? -> zenith optical depth ? 0.1
    //   This gives ~10% green scattering at zenith, ~33% blue (correct for clear sky)
    vec3 betaRayleigh = vec3(0.01) / lambda4;

    // Mie scattering coefficient (scales with turbidity)
    vec3 betaMie = vec3(0.00002) * turbidity;

    // Total extinction cross-section
    vec3 betaTotal = betaRayleigh + betaMie;

    // ================================================================
    // Sky dome via optical path length (airmass)
    // ================================================================

    // Airmass for view direction and sun direction (only for above-horizon rays)
    //ai: continue horizon scattering through the narrow ground blend, avoiding a discontinuity at y=0.
    float viewAirmass = airmass(max(viewSinElev, 0.0));

    // Sun extinction through the atmosphere
    float sunAirmass = airmass(max(sunSinElev,0.0));

    // Scattering optical depth along the view path and the sun path
    vec3 viewOpticalDepth = betaTotal * density * viewAirmass;
    vec3 sunOpticalDepth  = betaTotal * density * sunAirmass;

    // Transmittance along each path
    vec3 transmittanceView = exp(-viewOpticalDepth);
    vec3 transmittanceSun  = exp(-sunOpticalDepth);

    // Sun color after atmospheric extinction (sun turns red at low elevation)
    vec3 sunColor = vec3(1.0) * transmittanceSun;
    // Clamp so sun isn't black at the horizon - still visible through haze
    sunColor = max(sunColor, vec3(0.01));

    // Rayleigh-scattered skylight
    //   L_R(?) ? (1 ? T_v ? T_s) ? P_R(cos(?))
    vec3 rayleighScatter = (1.0 - transmittanceView * transmittanceSun)
                         * rayleighPhase(cosTheta);

    // Mie-scattered skylight (aerosol glow around sun)
    vec3 mieScatter = (1.0 - exp(-betaMie * density * (viewAirmass + sunAirmass) * 2.0))
                    * miePhase(cosTheta, 0.85);

    // Combine scattering with multiple-scattering approximation (?8 boost)
    //   Modulate by a gentle skyBrightness derived from sunColor so the sky is dimmer
    //   at sunrise/sunset but doesn't go black (scattering in upper atmosphere).
    float skyBrightness = max(sunColor.r * 0.3 + sunColor.g * 0.6 + sunColor.b * 0.1, 0.02);
    vec3 scatteredLight = (rayleighScatter * 8.0 + mieScatter * 3.0) * sqrt(skyBrightness);

    // ================================================================
    // Sky dome color
    // ================================================================

    // Night sky base (with tiny ambient so never pure black)
    vec3 nightSky = vec3(0.02, 0.02, 0.04);

    // Day sky color: scattered sunlight + small ambient floor
    vec3 daySky = scatteredLight + vec3(0.005);

    // Apply hue tint to day sky
    daySky += hueColor * 0.05;

    // Mix day/night based on sun altitude
    //   Transition from 8? below horizon (sunY=-0.14) to 8? above (sunY=0.14)
    float nightBlend = clamp((sunY + 0.14) / 0.28, 0.0, 1.0);
    vec3 skyColor = mix(nightSky, daySky, max(nightBlend, 0.05));

    // ================================================================
    // Horizon glow (Earth's limb brightening)
    // ================================================================

    vec2 rdXZ = safeNormalize(rd.xz);
    vec2 sunXZ = safeNormalize(sunDirN.xz);
    float viewSunDot = dot(rdXZ, sunXZ);  // -1 (away) to 1 (toward sun)

    // Horizon brightening: the horizon has a longer path through the atmosphere
    float horizonBand = exp(-abs(viewSinElev) * 4.0);

    // Twilight glow: peaks when sun is slightly below the horizon, decays to both sides
    //   Fixed: was using exp(-max(sunY+0.03,0)*20) which gave constant 1 below horizon
    float twilight = exp(-abs(sunY + 0.03) * 14.0);

    // Sunset horizon colors: orange/red toward sun, purple away
    float sunsetSide = 0.5 + 0.5 * viewSunDot;
    vec3 sunsetColor = mix(vec3(0.5, 0.12, 0.25), vec3(1.0, 0.5, 0.2), sunsetSide);

    // Also tint the horizon with the sun's color (redder at sunset)
    vec3 horizonTint = sunColor * 0.5 + vec3(0.5) * 0.5;

    float sunsetBlend = clamp(twilight * horizonBand * horizonGlow * 0.6, 0.0, 1.0);
    skyColor = mix(skyColor, sunsetColor * horizonTint, sunsetBlend);


    // Upper sky sunset dome (purple/pink from Belt of Venus) - only when twilight active
    float sunsetDome = exp(-abs(viewSinElev - 0.3) * 6.0) * twilight * 0.25;
    skyColor += vec3(0.5, 0.15, 0.3) * sunsetDome;

    // ================================================================
    // Sun disk and glow
    // ================================================================

    // Mie-scattered glow around the sun
    float sunGlow = miePhase(cosTheta, 0.9) * 0.002 * transmittanceSun.g;
    skyColor += sunGlow * sunColor;

    // Sun disk (fades at horizon via atmospheric extinction and smoothstep)
    float sunDisk = exp(-(1.0 - cosTheta) * sunDiskSize);
    float sunVisible = smoothstep(-0.05, 0.03, sunY);
    // Sun disk inherits the reddened sun color at low elevations
    skyColor += sunDisk * sunColor * sunDiskIntensity * sunVisible;

    // Moon direction (computed early for eclipse check)
    vec3 moonDirN = skyDirection(moonDir,vec3(0.0,-1.0,0.0));

    // Solar eclipse: if the moon blocks the sun, dim the sky
    float eclipseFactor = 1.0;
    {
        float cosMoonSun = dot(moonDirN, sunDirN);
        float moonRadiusRad = moonSize * 0.00006;
        float sunRadius = sunDiskSize * 0.00006;  // same proportional formula as moon
        float eclipseRadius = (moonRadiusRad + sunRadius) * 2.0;
        float eclipseAngle = acos(clamp(cosMoonSun, -1.0, 1.0));
        float eclipseDepth = 1.0 - smoothstep(0.0, eclipseRadius, eclipseAngle);
        float sunAbove = step(0.0, sunY);
        eclipseFactor = 1.0 - eclipseDepth * 0.7 * sunAbove;
    }
    skyColor *= eclipseFactor;

    // Moon rendering (solid disk with curved terminator - crescent/gibbous arc)
    float cosMoonAngle = dot(rd, moonDirN);
    float moonAboveHorizon = step(0.0, dot(moonDirN, vec3(0.0, 1.0, 0.0)));
    if (moonAboveHorizon > 0.0) {
        float angularDist = acos(clamp(cosMoonAngle, -1.0, 1.0));
        float moonRadius = max(moonSize * 0.00006,0.00001);
        //ai: reversed smoothstep edges are undefined by GLSL.
        float moonEdge = 1.0-smoothstep(moonRadius * 0.98, moonRadius * 1.02, angularDist);
        if (moonEdge > 0.0) {
            // Curved terminator: u = toward sun, v = perpendicular
            // lit where u > t * sqrt(1 - v?)  (t = -dot(sunDir, moonDir))
            float dotSM = dot(sunDirN, moonDirN);
            vec3 moonU;
            vec3 moonV;
            if (abs(dotSM) < 0.999) {
                moonU = normalize(sunDirN - moonDirN * dotSM);
                moonV = normalize(cross(moonU, moonDirN));
            } else {
                // Near conjunction/opposition: use arbitrary perpendicular
                //ai: use a nonparallel reference even when the moon sits at zenith.
                moonU = normalize(cross(moonDirN, abs(moonDirN.y) < 0.9
                    ? vec3(0.0,1.0,0.0) : vec3(1.0,0.0,0.0)));
                moonV = normalize(cross(moonU, moonDirN));
            }
            //ai: disk coordinates retain radius; normalizing the offset erased radial phase information.
            float u = dot(rd, moonU) / max(sin(moonRadius),0.00001);
            float v = dot(rd, moonV) / max(sin(moonRadius),0.00001);
            float t = -dot(sunDirN, moonDirN);  // -1=new, 0=half, 1=full
            float v2 = v * v;
            // lit where u > -t * sqrt(1-v?): full moon (t=1) -> u > -sqrt -> full disk
            float terminatorEdge = u + t * sqrt(max(0.0, 1.0 - v2));
            float moonLit = smoothstep(-0.04, 0.04, terminatorEdge);
            // Earthshine on dark side
            float moonPhaseFactor = (1.0 - dotSM) * 0.5;
            float earthshine = (1.0 - moonPhaseFactor) * 0.08;
            vec3 moonColor = mix(vec3(0.02 + earthshine, 0.02 + earthshine, 0.03 + earthshine),
                                 vec3(0.60, 0.60, 0.68), moonLit);
            // Daytime: additive (moon brighter than sky); Nighttime: mix (solid disk)
            float horizonFade = smoothstep(0.0, 0.1, cosMoonAngle);
            float moonVis = min(moonEdge * moonBrightness * horizonFade, 1.0);
            float isNight = 1.0 - clamp(dayFactor * 2.0, 0.0, 1.0);  // 1 at night, 0 at day
            vec3 moonDayAdd = moonColor * moonVis * 0.2;              // subtle daytime glow
            vec3 moonNightMix = mix(skyColor, moonColor, moonVis);    // solid disk at night
            skyColor = mix(moonNightMix, skyColor + moonDayAdd, 1.0 - isNight);
        }
    }

    // Planets (solid disks with curved terminator)
    for (int i = 0; i < 4; i++) {
        if (i >= planetCount) break;
        vec3 pDir = skyDirection(planetDir[i],vec3(0.0,-1.0,0.0));
        float cosPlanet = dot(rd, pDir);
        float planetAboveHorizon = step(0.0, dot(pDir, vec3(0.0, 1.0, 0.0)));
        if (planetAboveHorizon > 0.0) {
            float angDist = acos(clamp(cosPlanet, -1.0, 1.0));
            float pRadius = max(planetSize[i] * 0.00003,0.00001);
            float pEdge = 1.0-smoothstep(pRadius * 0.98, pRadius * 1.02, angDist);
            if (pEdge > 0.0) {
                float dotPS = dot(sunDirN, pDir);
                vec3 pU;
                vec3 pV;
                if (abs(dotPS) < 0.999) {
                    pU = normalize(sunDirN - pDir * dotPS);
                    pV = normalize(cross(pU, pDir));
                } else {
                    pU = normalize(cross(pDir, abs(pDir.y) < 0.9
                        ? vec3(0.0,1.0,0.0) : vec3(1.0,0.0,0.0)));
                    pV = normalize(cross(pU, pDir));
                }
                float pUcoord = dot(rd, pU) / max(sin(pRadius),0.00001);
                float pVcoord = dot(rd, pV) / max(sin(pRadius),0.00001);
                float pt = -dotPS;
                float pV2 = pVcoord * pVcoord;
                float pTerminator = pUcoord + pt * sqrt(max(0.0, 1.0 - pV2));
                float pLit = smoothstep(-0.04, 0.04, pTerminator);
                vec3 pCol = mix(planetColor[i] * 0.03, planetColor[i], pLit);
                float pHorizonFade = smoothstep(0.0, 0.1, cosPlanet);
                float pVis = min(pEdge * planetBrightness[i] * pHorizonFade, 1.0);
                float pNight = 1.0 - clamp(dayFactor * 2.0, 0.0, 1.0);
                vec3 pDayAdd = pCol * pVis * 0.2;
                vec3 pNightMix = mix(skyColor, pCol, pVis);
                skyColor = mix(pNightMix, skyColor + pDayAdd, 1.0 - pNight);
            }
        }
    }

    // Stars (night, upper hemisphere, safe UV)
    //opencode: star-field visibility fix - three compounded killers found
    //   during the hunt, all proven live (TEMP red/+5.0 paints, C-side
    //   glGetUniformfv readback of every star uniform, gain echoes):
    //   (1) the gnomonic projection (rd.xz/rd.y) is singular at the
    //   horizon: thin near-horizon slivers (rd.y small, e.g. pitched-down
    //   cameras) get 1-4px cells with subpixel exp cores -> yaw/pitch
    //   cells are uniform-angle (~0.5deg, resolving ~1-2px cores).
    //   (2) twinkle phase used star*100, but kept stars share a 1.5-rad
    //   window: the field pulsed in unison -> independent per-cell seed.
    //   (3) gain staging (density 2.2 / contrast 1.5 crush <0.17 HDR):
    //   threshold 0.95 + wider cores (falloff 30) + twinkle floor 0.3 so
    //   resolved cores survive to the screen. starDensity/brightness
    //   uniforms still scale count/gain (brightness default 1.0).
    if (viewSinElev > 0.01 && dayFactor < 0.3) {
        float starYaw = atan(rd.z, rd.x);
        float starPitch = asin(clamp(rd.y, -1.0, 1.0));
        vec2 starCell = floor(vec2(starYaw * 114.59156, starPitch * 114.59156)
                    * (1.0 * starDensity));
        float star = hash(starCell);
        float starVis = smoothstep(0.95, 1.0, star) * (1.0 - dayFactor * 3.0)
                      * smoothstep(0.005, 0.03, viewSinElev);
        if (starVis > 0.0) {
            vec2 starFrac = fract(vec2(starYaw * 114.59156, starPitch * 114.59156)
                    * (1.0 * starDensity)) - 0.5;
            float twinkleSeed = hash(starCell + 17.0);
            float twinkle = 0.3 + 0.7 * (sin(time * starTwinkleSpeed * 3.0 + twinkleSeed * 6.2831853) * 0.5 + 0.5);
            float starShape = exp(-dot(starFrac, starFrac) * 30.0);
            skyColor += vec3(1.0) * starVis * twinkle * starShape * starBrightness;
        }
    }

    //opencode: keep low-valued ground colors out of the atmospheric contrast
    //   transform, which otherwise clamps the entire ground palette to black.
    float groundBlend = 1.0 - smoothstep(-0.02, 0.02, rd.y);
    vec3 groundColor = vec3(0.0);
    if (groundBlend > 0.0) {
        vec3 groundDay = vec3(0.12, 0.18, 0.08);
        vec3 groundNight = vec3(0.01, 0.01, 0.02);
        vec3 groundBase = mix(groundNight, groundDay, dayFactor);
        float groundDepth = clamp(-rd.y * 4.0, 0.0, 1.0);
        //opencode: night fix — the horizon tan is daylight soil; at night
        //   (dayFactor 0) it glowed salmon through AgX on every downward
        //   camera (ledrow void read [147,108,102]). Scale it with daylight;
        //   day scenes (dayFactor 1) are bit-identical.
        vec3 groundTan = vec3(0.42, 0.40, 0.36) * (0.05 + 0.95 * dayFactor);
        groundColor = mix(groundTan, groundBase, groundDepth);
    }

    // Apply density
    skyColor *= skyDensity;

    //ai: luminance-anchored contrast preserves hue and dim sky instead of clipping night/eclipses to black.
    float skyLuminance = max(dot(skyColor,vec3(0.2126,0.7152,0.0722)),0.00001);
    skyColor *= pow(skyLuminance / 0.18, max(skyContrast,0.01)-1.0);

    //opencode: remove negative below-horizon contrast values before blending the ground.
    skyColor = max(skyColor, vec3(0.0));

    //opencode: blend the ground after sky contrast so its low luminance remains visible.
    skyColor = mix(skyColor, groundColor * skyDensity, groundBlend);

    //ai: preserve solar radiance in HDR paths; LDR targets quantize/clamp at storage instead.
    finalColor = vec4(max(skyColor, vec3(0.0)),
        (gXProbeCoverageCapture > 0) ? 0.0 : 1.0);
    if (gXProbeSkyDirectionCapture == 1) finalColor = vec4(rd, 1.0);
}
