// A handful of numbers everyone else needs, worked out on the GPU so nothing
// has to be read back. Rendered into a 4 x 1 target:
//   texel 0: r sunlight left after the clouds, at the aircraft
//            g cloud density at the camera (flying inside a cloud)
//            b rain or snow falling at the camera
//   texel 1: sky light from above (irradiance / pi)
//   texel 2: light from below: the ground and the cloud tops under us
//   texel 3: the key light at the camera's height, after the atmosphere
out vec4 frag;

uniform sampler2D uSkyView;
uniform sampler2D uCloudShadow;
uniform vec3  uAcRel;          // aircraft, camera-relative
uniform vec3  uLightDir;
uniform vec3  uLightIllum;
uniform float uCamRkm;
uniform vec3  uGroundAlbedo;
uniform float uBelowCloud;     // how much of the view down is cloud top (0..1)

vec3 sky(vec3 d) { return textureLod(uSkyView, skyview_uv(d, uCamRkm), 2.0).rgb; }

void main() {
    int i = int(gl_FragCoord.x);
    vec3 key = uLightIllum * transmittance(uCamRkm, uLightDir.y);
    if (i == 0) {
        float od = 0.0, hf;
        float t = 0.0;
        for (int k = 0; k < 28; ++k) {
            float dt = 25.0 + t * 0.12;
            vec3 p = uAcRel + uLightDir * (t + dt * 0.5);
            od += cloud_density(p, k < 8, hf) * CLOUD_EXT * dt;
            t += dt;
            if (t > 25000.0) break;
        }
        float inside = cloud_density(vec3(0.0), true, hf);
        vec2 air = uCamWorld.xz - uAirOffset;
        float rain = weather_at(air).a;
        rain *= smoothstep(uLow0.y, uLow0.y * 0.6 + uLow0.x * 0.4, uCamWorld.y);
        rain = max(rain, uRain * step(0.3, inside) * step(uCamWorld.y, uLow0.y));
        frag = vec4(exp(-od), inside, rain, 1.0);
    } else if (i == 1) {
        // cosine-weighted sum over the upper hemisphere
        vec3 s = vec3(0.0);
        float wsum = 0.0;
        for (int a = 0; a < 8; ++a)
        for (int e = 0; e < 3; ++e) {
            float el = (float(e) + 0.5) / 3.0 * PI * 0.5;
            float az = (float(a) + 0.5) / 8.0 * 2.0 * PI;
            vec3 d = vec3(cos(el) * sin(az), sin(el), -cos(el) * cos(az));
            float w = sin(el) * cos(el);
            s += sky(d) * w;
            wsum += w;
        }
        frag = vec4(s / wsum, 1.0);
    } else if (i == 2) {
        vec3 s = vec3(0.0);
        for (int a = 0; a < 8; ++a) {
            float az = (float(a) + 0.5) / 8.0 * 2.0 * PI;
            s += sky(vec3(cos(0.9) * sin(az), -sin(0.9), -cos(0.9) * cos(az)));
        }
        s /= 8.0;
        // the ground: lit by the sun through the clouds, plus the sky
        float shadow = texture(uCloudShadow, vec2(0.5)).r;
        vec3 sun_g = uLightIllum * transmittance(ATM_BOTTOM + 0.3, uLightDir.y) *
                     saturate(uLightDir.y) * shadow;
        vec3 ground = uGroundAlbedo * (sun_g + vec3(0.0)) / PI;
        // cloud tops below are bright
        vec3 tops = key * saturate(uLightDir.y + 0.1) * 0.9 / PI;
        frag = vec4(s + mix(ground, tops, uBelowCloud), 1.0);
    } else {
        frag = vec4(key, 1.0);
    }
}
