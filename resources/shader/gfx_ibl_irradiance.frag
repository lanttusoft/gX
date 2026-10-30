//opencode: diffuse irradiance convolution — independent gX hemisphere
//   integration. The source cubemap is cosine-weighted and normalized.
//   No tutorial code is copied.
#version 330

in vec2 fragTexCoord;

uniform samplerCube sourceMap;
uniform float sampleDelta = 0.025; //opencode: hemisphere sampling step (radians)
//opencode: capture view matrix - rotates local +Z direction to face direction
uniform mat4 captureView;

out vec4 fragColor;

void main() {
    vec2 localFace = fragTexCoord * 2.0 - 1.0;
    vec3 normal = normalize(mat3(captureView)
        * normalize(vec3(localFace, 1.0)));

    vec3 basisUp = vec3(0.0, 1.0, 0.0);
    vec3 basisRight = normalize(cross(basisUp, normal));
    basisUp = cross(normal, basisRight);

    vec3 sum = vec3(0.0);
    int samples = 0;
    for (float polar = 0.0; polar < 1.57079632679; polar += sampleDelta) {
        float sinPolar = sin(polar);
        float cosPolar = cos(polar);
        for (float azimuth = 0.0; azimuth < 6.28318530718;
                azimuth += sampleDelta) {
            vec3 local = vec3(cos(azimuth) * sinPolar,
                sin(azimuth) * sinPolar, cosPolar);
            vec3 direction = basisRight * local.x
                + basisUp * local.y + normal * local.z;
            sum += texture(sourceMap, direction).rgb * cosPolar * sinPolar;
            ++samples;
        }
    }

    fragColor = vec4(3.14159265359 * sum / float(max(samples, 1)), 1.0);
}
