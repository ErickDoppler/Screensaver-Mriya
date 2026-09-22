// Cloud shadows on the ground. Each texel is a point at sea level; the value
// is how much sunlight reaches it through the clouds. A receiver at height h
// looks itself up where its own sun ray meets sea level, so a mountain top
// pokes out of a shadow its valley is in.
in vec2 vUV;
out vec4 frag;

uniform vec2  uCSCenter;     // world xz of the map centre
uniform float uCSSize;       // metres
uniform vec3  uLightDir;
uniform float uHMin, uHMax;  // the cloud slab, metres

void main() {
    vec2 xz = uCSCenter + (vUV - 0.5) * uCSSize;
    vec3 prel = vec3(xz.x - uCamWorld.x, -uCamWorld.y, xz.y - uCamWorld.z);
    vec3 L = uLightDir;
    float sy = max(L.y, 0.06);
    float t0 = uHMin / sy, t1 = min(uHMax / sy, 60000.0);
    const int N = 18;
    float dt = (t1 - t0) / float(N);
    float od = 0.0;
    for (int i = 0; i < N; ++i) {
        float hf;
        vec3 p = prel + L * (t0 + (float(i) + 0.5) * dt);
        od += cloud_density(p, false, hf) * CLOUD_EXT * dt;
        od += rain_density(p) * dt;
    }
    // a real cloud's shadow is never pitch black: light scatters round it
    float T = exp(-od * 0.8);
    frag = vec4(mix(T, 1.0, 0.08), 0.0, 0.0, 1.0);
}
