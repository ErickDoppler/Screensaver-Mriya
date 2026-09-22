// The aircraft. Positions arrive as 16-bit fractions of the bounding box with
// the part number in w (see tools/meshpack.c), normals as signed bytes.
layout(location = 0) in vec4 aPos;
layout(location = 1) in vec4 aNrm;

uniform vec3  uCenter, uHalf;
uniform mat4  uModel;         // body -> camera-relative world
uniform mat4  uViewProj;
uniform float uFlex;          // wingtip deflection, metres
uniform float uFanAngle;      // how far the fans have turned, radians

out vec3 vBody;               // body frame, before flex (the livery is painted here)
out vec3 vNBody;
out vec3 vRel;                // camera-relative world
out vec3 vN;                  // world normal
flat out float vPart;
out float vFan;               // 1 on the fan blades, 2 on the spinner
out vec2 vFanUV;              // position on the fan, turning with it

// Control surfaces. The model has none of its own - each wing half is one
// piece - so the band behind each hinge line is turned about that line. The
// hinges were measured off the mesh: straight lines along the span (or up
// the fin), given here in the right-hand half; the left is its mirror.
uniform vec3 uSurf;   // aileron (+ rolls right), elevator (+ trailing edge down), rudder (+ trailing edge right)

vec3 rodrigues(vec3 v, vec3 k, float a) {
    float c = cos(a), s = sin(a);
    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

void control_surfaces(inout vec3 b, inout vec3 n, float part) {
    float side = b.x < 0.0 ? -1.0 : 1.0;
    vec3 m = vec3(abs(b.x), b.y, b.z);            // mirrored into the right half
    vec3 mn = vec3(n.x * side, n.y, n.z);
    vec3 H, k;
    float th = 0.0;
    if (part > 0.5 && part < 1.5) {
        // ailerons: the outer wing, hinge at 72% of the chord
        if (m.x > 30.8 && m.x < 43.4) {
            float zh = 6.16 + (m.x - 32.0) * 0.366;
            if (m.z > zh) {
                H = vec3(m.x, 0.78 - (m.x - 32.0) * 0.092, zh);
                k = normalize(vec3(1.0, -0.092, 0.366));
                th = uSurf.x * -side * smoothstep(30.8, 31.6, m.x) * (1.0 - smoothstep(42.6, 43.4, m.x));
            }
        }
    } else if (part > 3.5 && part < 4.5) {
        // elevators: the tailplane, hinge at 66%
        if (m.x > 1.6 && m.x < 14.4) {
            float zh = 33.74 + (m.x - 6.5) * 0.519;
            if (m.z > zh) {
                H = vec3(m.x, 4.19 + (m.x - 6.5) * 0.111, zh);
                k = normalize(vec3(1.0, 0.111, 0.519));
                th = uSurf.y * smoothstep(1.6, 2.4, m.x) * (1.0 - smoothstep(13.6, 14.4, m.x));
            }
        }
    } else if (part > 2.5) {
        // rudders: both fins, hinge at 60%; both trailing edges swing the
        // same way, which in the mirrored half means opposite signs
        if (m.y > 5.4 && m.y < 12.6 && m.x > 13.4) {
            float zh = 37.93 + (m.y - 7.0) * 0.505;
            if (m.z > zh) {
                H = vec3(14.84 - (m.y - 7.0) * 0.1, m.y, zh);
                k = normalize(vec3(-0.1, 1.0, 0.505));
                th = uSurf.z * side * smoothstep(5.4, 6.0, m.y);
            }
        }
    }
    if (th != 0.0) {
        m = H + rodrigues(m - H, k, th);
        mn = rodrigues(mn, k, th);
        b = vec3(m.x * side, m.y, m.z);
        n = vec3(mn.x * side, mn.y, mn.z);
    }
}

void main() {
    vec3 b = uCenter + aPos.xyz * (1.0 / 32767.0) * uHalf;
    vec3 n = normalize(aNrm.xyz);
    float part = aPos.w;
    vFan = 0.0;
    vFanUV = vec2(0.0);
    // The fans: a disc of blades about 0.6 m inside each intake, turning
    // about the engine's axis. Axis x, y and the front of each nacelle come
    // from the mesh (see the notes in docs/DESIGN.md).
    if (part > 1.5 && part < 2.5) {
        float ax = abs(b.x);
        vec3 eng = ax < 13.6 ? vec3(9.895, -0.296, -17.24) : (ax < 20.9 ? vec3(17.275, -0.56, -12.435) : vec3(24.595, -0.835, -7.59));
        vec2 axis = vec2(sign(b.x) * eng.x, eng.y);
        vec2 d = b.xy - axis;
        float r = length(d);
        float dz = b.z - eng.z;
        // The blades sit 0.55-0.8 m in and reach r 1.13; the spinner is the
        // cone inside r 0.4. The geometry stays put: turning it as a rigid
        // body tears the model's blades, and a real fan at 3000 rpm is a blur
        // anyway - the fragment shader turns the pattern instead.
        if ((r < 0.42 && dz > 0.12 && dz < 0.9) || (r < 1.16 && dz > 0.42 && dz < 0.9)) {
            vFanUV = d;
            vFan = r < 0.42 ? 2.0 : 1.0;   // 2: the spinner
        }
    }
    control_surfaces(b, n, part);
    vBody = b;
    vNBody = n;
    // The wing bends like a cantilever, and the engines ride on it.
    if (part > 0.5 && part < 2.5) {
        float s = (abs(b.x) - 4.5) / 39.8;
        if (s > 0.0) {
            b.y += uFlex * s * s;
            float slope = 2.0 * uFlex * s / 39.8 * sign(b.x);
            n = normalize(vec3(n.x - slope * n.y, n.y + slope * n.x, n.z));
        }
    }
    vec4 w = uModel * vec4(b, 1.0);
    vRel = w.xyz;
    vN = normalize(mat3(uModel) * n);
    vPart = part;
    gl_Position = uViewProj * w;
}
