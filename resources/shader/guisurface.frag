#version 330

//opencode: v0.2.1 - SDF widget surface, BLUEPRINT style per
//   docs/frontend_design.md: flat fill + AA silhouette + hairline border.
//   Optional lattice is an explicit mode, never a default gradient or glow.
//   Drawn through the shared widget program; per-draw inputs:
//     box        element rect in RT pixels (same frame as gl_FragCoord)
//     col        fill color (element color x canvasHue, states applied)
//     res        current RT resolution
//     uniforms[0] = (cornerRadius px, borderWidth px, latticeAlpha, mode)
//     uniforms[1] = border color (normalized RGB)
//     uniforms[2] = lattice color (normalized RGB)
//     uniforms[3].x = lattice cell size in pixels
//   gradient modes (gradientMode):
//     0 none (flat col)         1 vertical (top -> bottom)
//     2 horizontal (left -> right) 3 four-point ([0]=TL [1]=BL [2]=BR [3]=TR)
//     4 horizon (depth profile over start/end colors)
//     5 metallic (depth profile over start/end colors)
//   surfaceDepth = (mode only read on the C side, horizonPos, horizonSoft,
//                   horizonGlow) as surface-height fractions
//   metalParams  = (bandPos, bandWidth, highlightStrength, rimStrength)

uniform vec4 col;
uniform vec2 res;
uniform vec4 box;
uniform vec4 uniforms[4];
uniform vec4 gradientTop;
uniform vec4 gradientBottom;
uniform vec4 gradientCornerA;   // four-point BR
uniform vec4 gradientCornerB;   // four-point TR
uniform float gradientMode;
uniform vec4 surfaceDepth;
uniform vec4 metalParams;
//opencode: cursor light — cursor.xy in the same space as box (top-down px),
//   cursor.z = radius px, cursor.w = strength (0 disables)
uniform vec4 cursor;
uniform vec4 cursorColor;

out vec4 finalColor;

float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.y, q.x), 0.0) - r;
}

void main() {
    if (col.a <= 0.002) discard;

    //opencode: v0.2.0-pre10 - CRITICAL Y-flip: box is in window coords
    //   (Y down, matching the ortho(0,w,h,0) used by the vertex stage) but
    //   gl_FragCoord is framebuffer coords (Y up). Without the flip every
    //   non-centered rect evaluated its SDF at a mirrored position and
    //   rasterized nothing; the fullscreen backdrop masked this for months
    //   because it is symmetric about the screen center.
    vec2 fragPos = vec2(gl_FragCoord.x, res.y - gl_FragCoord.y);
    vec2 center = box.xy + box.zw * 0.5;
    vec2 p = fragPos - center;

    float maxR = min(box.z, box.w) * 0.5;
    float radius = clamp(uniforms[0].x, 0.0, maxR);
    float borderWidth = uniforms[0].y;
    float latticeAlpha = clamp(uniforms[0].z, 0.0, 1.0);
    float latticeMode = uniforms[0].w;

    //opencode: half-size inset ~1px so the AA edge never clips the quad
    vec2 half_ = max(box.zw * 0.5 - 1.0, vec2(1.0));
    float d = sdRoundBox(p, half_, radius);

    //opencode: ~1px analytic anti-aliasing
    float aa = max(fwidth(d), 0.0001);
    float alpha = 1.0 - smoothstep(-aa, aa, d);

    vec3 fill = col.rgb;

    //opencode: gradient modes — authored vertical/horizontal/four-point
    //   gradients (restored for the SDF path) plus the themed depth profiles.
    int gradientId = int(gradientMode + 0.5);
    float gradientT = clamp((fragPos.y - box.y) / max(box.w, 1.0), 0.0, 1.0);
    float gradientX = clamp((fragPos.x - box.x) / max(box.z, 1.0), 0.0, 1.0);
    if (gradientId == 1) {
        fill = mix(gradientTop.rgb, gradientBottom.rgb, gradientT);
    } else if (gradientId == 2) {
        fill = mix(gradientTop.rgb, gradientBottom.rgb, gradientX);
    } else if (gradientId == 3) {
        vec3 topRow = mix(gradientTop.rgb, gradientCornerB.rgb, gradientX);
        vec3 bottomRow = mix(gradientBottom.rgb, gradientCornerA.rgb, gradientX);
        fill = mix(topRow, bottomRow, gradientT);
    } else if (gradientId == 4) {
        //opencode: horizon — sky/ground ramp with a soft reflected band so a
        //   panel reads as a glossy surface instead of a flat ramp. The band
        //   is a hue-preserving partial lift (full white additive read as a
        //   neon stripe on saturated themes).
        vec3 base = mix(gradientTop.rgb, gradientBottom.rgb, gradientT);
        float softness = max(surfaceDepth.z, 0.001);
        float band = exp(-pow((gradientT - surfaceDepth.y) / softness, 2.0));
        vec3 lifted = mix(base, vec3(1.0), 0.5);
        fill = mix(base, lifted, clamp(band * surfaceDepth.w, 0.0, 1.0));
    } else if (gradientId == 5) {
        //opencode: metallic — compressed base ramp + anisotropic highlight
        //   band + a rim lift at the top edge.
        vec3 base = mix(gradientTop.rgb * 0.65, gradientBottom.rgb, gradientT);
        float width = max(metalParams.y, 0.001);
        float band = exp(-pow((gradientT - metalParams.x) / width, 2.0));
        float rim = 1.0 - smoothstep(0.0, 0.10, gradientT);
        fill = base + vec3(1.0) * band * metalParams.z + vec3(1.0) * rim * metalParams.w;
    } else if (gradientId == 6) {
        //opencode: metallic along X — the authored horizontal gradient keeps
        //   its orientation, so the band and rim follow the left edge.
        vec3 base = mix(gradientTop.rgb * 0.65, gradientBottom.rgb, gradientX);
        float width = max(metalParams.y, 0.001);
        float band = exp(-pow((gradientX - metalParams.x) / width, 2.0));
        float rim = 1.0 - smoothstep(0.0, 0.10, gradientX);
        fill = base + vec3(1.0) * band * metalParams.z + vec3(1.0) * rim * metalParams.w;
    }
    fill = clamp(fill, 0.0, 1.0);

    if (latticeMode > 0.5 && latticeAlpha > 0.0) {
        float cellSize = max(uniforms[3].x, 4.0);
        vec2 cell = fragPos / cellSize;
        vec2 cellFrac = fract(cell);
        vec2 edgeDist = min(cellFrac, 1.0 - cellFrac);
        float gridWidth = max(max(fwidth(cell.x), fwidth(cell.y)) * 1.5, 0.002);
        float gridLine = 1.0 - smoothstep(0.0, gridWidth, min(edgeDist.x, edgeDist.y));
        float diagonal = cell.x + cell.y;
        float diagonalDist = min(fract(diagonal), 1.0 - fract(diagonal));
        float diagonalLine = 1.0 - smoothstep(0.0, gridWidth * 0.8, diagonalDist);
        float lattice = max(gridLine, diagonalLine * 0.35) * latticeAlpha;
        fill = mix(fill, uniforms[2].rgb, lattice);
    }

    //opencode: cursor light — soft radial highlight following the pointer.
    //   Applied before the border so the hairline stays crisp; strength 0
    //   skips the block entirely (the uniform is uploaded as 0 then).
    if (cursor.w > 0.0 && cursor.z > 0.0) {
        vec2 dxy = fragPos - cursor.xy;
        float falloff = exp(-dot(dxy, dxy) / (2.0 * cursor.z * cursor.z));
        fill += cursorColor.rgb * (falloff * cursor.w);
        fill = clamp(fill, 0.0, 1.0);
    }

    // hairline stroke: ink rim inside the silhouette - lightened on dark
    // fills so the line reads against them (contrast note from vision audit)
    if (borderWidth > 0.25) {
        float ring = 1.0 - smoothstep(borderWidth - aa, borderWidth + aa, abs(d));
        vec3 stroke = uniforms[1].rgb;
        fill = mix(fill, stroke, ring * 0.9);
    }

    finalColor = vec4(fill, col.a * alpha);
}
