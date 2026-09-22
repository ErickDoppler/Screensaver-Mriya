// The aircraft's skin: the Antonov Airlines livery - the fuselage's paint
// texture and decals projected on - the details the mesh does not carry
// (the windscreen, seams, bare metal),
// and a physically based paint lit by everything around it.
in vec3 vBody;
in vec3 vNBody;
in vec3 vRel;
in vec3 vN;
flat in float vPart;
in float vFan;
in vec2 vFanUV;
layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 fragDist;

uniform sampler2D uDecals;
uniform sampler2D uBands;          // the cheatline's edges along the fuselage (tools/make_bands.py)
uniform vec2  uDecalSize;          // atlas texels
uniform vec4  uDecA[40];           // zc, yc, w, h
uniform vec4  uDecB[40];           // u0, v0, u1, v1
uniform vec4  uDecC[40];           // side, part, xc, reach
uniform vec4  uDecD[40];           // cos, sin of the rotation
uniform int   uDecCount;

uniform vec3  uLightDir;
uniform sampler2D uProbe;
uniform sampler2D uSkyView;
uniform float uCamRkm;
uniform sampler2D uShadowMap;
uniform mat4  uShadowMat;          // camera-relative -> light clip
uniform float uShadowOn;
uniform vec2  uRes;
uniform float uWet;
uniform float uFlash;
uniform vec3  uFlashRel;
uniform vec4  uPointPos[4];
uniform vec3  uPointCol[4];
uniform float uInCloud;
uniform float uFanBlur;           // 0 idle .. 1 a spinning blur
uniform float uFanAngle;
uniform float uEmit;              // physical radiance -> the frame's pre-exposed units

// --- the paint ------------------------------------------------------------
struct Surf { vec3 albedo; float rough; float metal; float glass; vec3 emit; float occ; };

float aa_line(float coord, float period, float width, float fw) {
    float d = abs(fract(coord / period + 0.5) - 0.5) * period;
    return 1.0 - smoothstep(width, width + fw * 1.5, d);
}

void apply_decals(inout vec3 col, vec3 b, vec3 nb) {
    vec3 dbx = dFdx(b), dby = dFdy(b);
    for (int i = 0; i < 40; ++i) {
        if (i >= uDecCount) break;
        vec4 C = uDecC[i];
        if (abs(vPart - C.y) > 0.5) continue;
        vec4 A = uDecA[i], B = uDecB[i], D = uDecD[i];
        if (D.z > 0.5) {
            // projected from below: x across, z along, onto what faces down
            float down = -nb.y;
            if (down < 0.15) continue;
            // turned by the decal's angle (0 or a half turn: D.x is its cosine)
            vec2 uvl = vec2(0.5 + D.x * (b.x - C.z) / A.z, 0.5 - D.x * (b.z - A.x) / A.w);
            if (any(lessThan(uvl, vec2(0.0))) || any(greaterThan(uvl, vec2(1.0)))) continue;
            vec2 span = B.zw - B.xy;
            vec2 uv = B.xy + uvl * span;
            vec2 gx = D.x * vec2(dbx.x / A.z, -dbx.z / A.w) * span;
            vec2 gy = D.x * vec2(dby.x / A.z, -dby.z / A.w) * span;
            vec4 t = textureGrad(uDecals, uv, gx, gy);
            float tpp = max(length(gx * uDecalSize), length(gy * uDecalSize));
            float w = clamp(tpp * 0.2 + 0.02, 0.02, 0.5);
            float a = smoothstep(0.5 - w, 0.5 + w, t.a) * smoothstep(0.15, 0.4, down);
            col = mix(col, t.rgb, a);
            continue;
        }
        float dx = b.x - C.z;
        // which face it is on is the normal's business: a canted fin crosses
        // its own centre plane, and a test on x would cut the decal in two
        if (abs(dx) > C.w) continue;
        float facing = nb.x * C.x;
        // a fin's decals only on the fin's upright faces, never the tailplane's top
        float fmin_ = abs(C.y - 3.0) < 0.5 ? 0.6 : 0.1;
        if (facing < fmin_) continue;
        vec2 q = vec2(b.z - A.x, b.y - A.y);
        q = vec2(q.x * D.x - q.y * D.y, q.x * D.y + q.y * D.x);
        float su = C.x > 0.0 ? -1.0 : 1.0;
        vec2 uvl = vec2(0.5 + su * q.x / A.z, 0.5 - q.y / A.w);
        if (any(lessThan(uvl, vec2(0.0))) || any(greaterThan(uvl, vec2(1.0)))) continue;
        vec2 span = B.zw - B.xy;
        vec2 uv = B.xy + uvl * span;
        vec2 gx = vec2(su * dbx.z / A.z, -dbx.y / A.w) * span;
        vec2 gy = vec2(su * dby.z / A.z, -dby.y / A.w) * span;
        vec4 t = textureGrad(uDecals, uv, gx, gy);
        // Sharpen the edge to the pixel, however close the camera gets: the
        // atlas edge ramps over ~3 texels, so this many pixels per texel make
        // the ramp this wide on screen.
        float tpp = max(length(gx * uDecalSize), length(gy * uDecalSize));
        float w = clamp(tpp * 0.2 + 0.02, 0.02, 0.5);
        float a = smoothstep(0.5 - w, 0.5 + w, t.a);
        // a side projection smears where the skin turns away: let it go
        a *= smoothstep(fmin_, fmin_ + 0.2, facing);
        col = mix(col, t.rgb, a);
    }
}

// Looking up the exhaust: the core's exit, a little over a metre aft of
// the cowl - the hot annulus round the plug, with the last turbine stage
// turning in it, a dull red heat and a trace of blue flame - and the plug
// itself, dark heat-stained metal glowing faintly at its root. Faint in
// daylight, a glow at night. The plug is joined to the pylon in the mesh, so
// it comes with the wing's parts as well as the engines'.
// Signed distance, metres, from point p to the line through a and b: > 0 on
// the left of a->b.
float side_of(vec2 p, vec2 a, vec2 b) {
    vec2 d = normalize(b - a);
    return (p - a).x * d.y * -1.0 + (p - a).y * d.x;
}

// The nacelle swoosh, drawn: a blue wedge from a point high on the cowl's
// front third, sweeping back and down and widening to a square end near the
// back; above it, past a white gap, a thin yellow stripe that starts later.
// Shaped off photographs of the aircraft, on both sides of every nacelle.
void swoosh(inout vec3 col, vec3 b, vec3 nb) {
    float ax = abs(b.x);
    float lipz = ax < 13.6 ? -17.24 : (ax < 20.9 ? -12.435 : -7.59);
    vec2 axis = vec2(sign(b.x) * (ax < 13.6 ? 9.895 : (ax < 20.9 ? 17.275 : 24.595)),
                     ax < 13.6 ? -0.296 : (ax < 20.9 ? -0.56 : -0.835));
    float outward = sign(b.x - axis.x) * nb.x;           // facing out of this side
    if (outward < 0.15) return;
    vec2 p = vec2(b.z - lipz, b.y - axis.y);             // along from the lip, up from the axis
    if (p.x < 1.5 || p.x > 5.2) return;
    // blue: a point at A, the upper edge to U, the lower edge to L, cut at CUT
    const vec2 A = vec2(1.82, 0.95), U = vec2(5.0, -0.09), L = vec2(5.04, -0.40);
    const float CUT = 5.02;
    // yellow: its top from Y0 to Y1; its bottom the blue's top, raised by GAP
    const vec2 Y0 = vec2(2.2, 0.85), Y1 = vec2(4.97, 0.13);
    const float GAP = 0.07;
    float du = side_of(p, A, U);                         // > 0 above the blue's top
    float dl = side_of(p, A, L);                         // > 0 above the blue's bottom
    float dc = CUT - p.x;                                // > 0 forward of the cut
    float dy = side_of(p, Y0, Y1);                       // > 0 above the yellow's top
    float w = max(fwidth(p.x) + fwidth(p.y), 0.004) * 0.6;
    float blu = smoothstep(-w, w, dl) * smoothstep(-w, w, -du) * smoothstep(-w, w, dc);
    float yel = smoothstep(-w, w, du - GAP) * smoothstep(-w, w, -dy) * smoothstep(-w, w, dc);
    float fade = smoothstep(0.15, 0.35, outward);
    col = mix(col, vec3(0.67, 0.55, 0.02), yel * fade);
    col = mix(col, vec3(0.054, 0.11, 0.33), blu * fade);
}

void exhaust(inout Surf s, vec3 b, vec3 nb) {
    float ax = abs(b.x);
    float cowl_end = ax < 13.6 ? -12.14 : (ax < 20.9 ? -7.335 : -2.49);
        vec2 ea = vec2(sign(b.x) * (ax < 13.6 ? 9.895 : (ax < 20.9 ? 17.275 : 24.595)),
                       ax < 13.6 ? -0.296 : (ax < 20.9 ? -0.56 : -0.835));
        vec2 rv = b.xy - ea;
        float rr = length(rv);
        float core = cowl_end + 1.14;               // the core's exit face
        float deep = core - 1.0;                    // the turbine, a metre up the nozzle
        // the annulus and the walls round it - not the plug in the middle
        float inside = step(rr, 0.585) * step(0.35, rr) * step(deep - 0.1, b.z) * step(b.z, core + 0.03);
        // only what faces out of the exhaust or in toward its axis: the
        // outside of the core's cowl is in that radius too, and stays metal
        float inward = -dot(nb.xy, rv) / max(rr, 1e-3);
        inside *= max(step(0.6, nb.z), step(0.3, inward));
        if (inside > 0.0) {
            float aft = saturate(nb.z);             // facing out of the exhaust
            float disk = smoothstep(0.6, 0.85, aft) * step(abs(b.z - deep), 0.15);
            // the turbine: 68 blades, turning faster than the fan, a
            // blur at speed with the pattern crawling round
            float ang = atan(rv.y, rv.x);
            float bl = fract((ang - uFanAngle * 1.7) / (2.0 * PI) * 68.0);
            float blade = smoothstep(0.0, 0.25, bl) * (1.0 - smoothstep(0.5, 0.85, bl));
            blade = mix(blade, 0.5, uFanBlur * 0.7);
            float hotr = 1.0 - smoothstep(0.15, 0.7, rr);
            // deep in the exhaust: dark and matte - a void round the plug,
            // the blades only just there in it
            s.albedo = mix(s.albedo, vec3(0.022, 0.02, 0.019) * (0.75 + 0.5 * blade), inside);
            s.metal = mix(s.metal, 0.15, inside);
            s.rough = mix(s.rough, 0.85, inside);
            // down the nozzle: it sees only a sliver of the sky, less the
            // deeper - at the bottom, next to nothing
            s.occ = mix(s.occ, mix(0.015, 0.25, saturate((b.z - deep) / (core - deep))), inside);
            float run = uFanBlur * 0.8 + 0.2;
            vec3 red = vec3(1.0, 0.28, 0.06) * (0.35 + 0.65 * blade * disk) * hotr * (0.4 + 0.6 * aft);
            vec3 blue = vec3(0.25, 0.4, 1.0) * (1.0 - smoothstep(0.05, 0.2, rr)) * disk;
            // the annulus between the plug and the core nozzle, and the plug
            // the flame's trace: a thin blue rim hugging the plug's root
            float ring = smoothstep(0.34, 0.36, rr) * (1.0 - smoothstep(0.37, 0.41, rr));
            vec3 flame = vec3(0.3, 0.45, 1.0) * ring * (0.6 + 0.4 * blade);
            s.emit = (red * 0.03 + blue * 0.03 + flame * 0.03) * run * inside;
        }
        // whatever else is inside the tube - a sliver of the pylon's fairing
        // pokes into it - lies in the same deep shadow
        // (and what the model had hidden behind its old disk, deeper still)
        float tube = step(rr, 0.585) * step(deep - 3.0, b.z) * step(b.z, core - 0.02);
        if (tube > 0.0) {
            s.albedo = min(s.albedo, vec3(0.05));
            s.occ = min(s.occ, mix(0.015, 0.25, saturate((b.z - deep) / (core - deep))));
            s.metal = 0.0;
        }
        // the plug: the short cone in the middle of the exhaust, inside the
        // nozzle (meshpack builds it; the model had a flat disk there)
        float plug = step(rr, 0.36) * step(deep - 0.05, b.z) * step(b.z, deep + 0.6);
        if (plug > 0.0) {
            float back = saturate((b.z - deep) / 0.5);
            s.albedo = mix(vec3(0.16, 0.15, 0.14), vec3(0.11, 0.11, 0.115), back);
            s.metal = 0.7;
            s.rough = 0.5;
            // the plug's root is deep in the nozzle, its tip nearly out
            s.occ = mix(0.015, 0.06, back);
            s.emit += vec3(1.0, 0.3, 0.07) * (1.0 - smoothstep(0.0, 0.5, back)) * 0.012 * (uFanBlur * 0.8 + 0.2);
        }
}

Surf skin() {
    vec3 b = vBody, nb = normalize(vNBody);
    // Some pieces of the model are wound inside out (the nacelles are), so
    // their normals point in. Seen from outside they are back faces: flip.
    if (!gl_FrontFacing) nb = -nb;
    Surf s;
    s.albedo = vec3(0.86, 0.87, 0.88);
    s.rough = 0.32;
    s.metal = 0.0;
    s.glass = 0.0;
    s.emit = vec3(0.0);
    s.occ = 1.0;
    int part = int(vPart + 0.5);
    bool pane = part == 5;             // PART_GLASS: fuselage, but glass
    if (pane) part = 0;
    float fw = max(length(fwidth(b)), 1e-4);

    if (part == 0) {
        // The fuselage's paint, drawn: the cheatline and the grey belly from a
        // table of their edges measured off the AN225.fbx model's texture
        // (tools/make_bands.py), so every edge is sharp to the pixel at any
        // distance. The titles, the flag and the chin's lettering are decals.
        {
            vec2 tc = vec2((b.z + 41.0) / 85.0 * (169.0 / 170.0) + 0.5 / 170.0, 0.25);
            vec4 band = texture(uBands, tc);                 // yellow top, bottom, blue top, bottom
            float gtop = texture(uBands, vec2(tc.x, 0.75)).r;  // the grey belly's top
            // Each edge is a line on the skin: how far above it a point is,
            // anti-aliased over exactly the pixel it falls in - sharp however
            // the edge curves round the nose.
            vec4 e = vec4(b.y) - band;
            vec4 ew = max(fwidth(e) * 0.75, vec4(0.002));
            float eg = b.y - gtop, egw = max(fwidth(eg) * 0.75, 0.002);
            float yel = smoothstep(-ew.y, ew.y, e.y) * (1.0 - smoothstep(-ew.x, ew.x, e.x)) * step(band.y + 0.01, band.x);
            float blu = smoothstep(-ew.w, ew.w, e.w) * (1.0 - smoothstep(-ew.z, ew.z, e.z)) * step(band.w + 0.01, band.z);
            float gry = 1.0 - smoothstep(-egw, egw, eg);
            // The grey belly's edge is one line: under the chin an arc round
            // the nose (measured off the texture), along the sides the line
            // under the blue band - joined by a rounded corner. A white line
            // follows it all the way round; forward of it, and below the
            // blue band, the nose's underside is blue, so the blue band wraps
            // round the chin and nowhere else.
            float x2 = b.x * b.x;
            float zaft = -37.84 + 0.21 * x2;
            float d_arc = (b.z - zaft) / sqrt(1.0 + 0.1764 * x2);  // aft of the arc, metres across it
            // below the grey's top line - which, toward the nose, climbs a
            // little into the blue band, so the grey trims the blue chin's sides
            float lift = 0.35 * smoothstep(-26.0, -34.0, b.z);
            float d_side = gtop + lift - b.y;
            float kk = 0.5;                                         // the corner's rounding
            float hh = max(kk - abs(d_arc - d_side), 0.0) / kk;
            float D = min(d_arc, d_side) - hh * hh * kk * 0.25;     // inside the grey where > 0
            float wline = clamp(gtop < band.w ? band.w - gtop : 0.12, 0.08, 0.2);  // the white line's width
            float dw = max(fwidth(D) * 0.75, 0.002);
            gry = smoothstep(-dw, dw, D);
            float below = 1.0 - smoothstep(-ew.w, ew.w, e.w);
            // blue: the band, and below it the cap - both only outside the
            // grey and its white line (added: the two share the band's lower
            // edge, and a max would leave a hairline there)
            float outside = 1.0 - smoothstep(-dw, dw, D + wline);
            blu = min(blu + below, 1.0) * outside;
            yel *= outside;
            // the paints, as the texture had them (sRGB made linear)
            vec3 white = vec3(0.90, 0.905, 0.90);
            // the belly: a cool, light blue-grey, as on the aircraft (sRGB 196, 205, 218)
            vec3 c = mix(white, vec3(0.55, 0.61, 0.70), gry);
            c = mix(c, vec3(0.67, 0.55, 0.02), yel);
            c = mix(c, vec3(0.054, 0.11, 0.33), blu);
            s.albedo = c;
        }
        apply_decals(s.albedo, b, nb);
        // the windscreen: the panes the model sinks behind its frames
        // (tools/meshpack.c marks them)
        if (pane) s.glass = 1.0;
    } else if (part == 1 || part == 4) {
        // wings and tailplane: light grey, bare metal leading edges
        s.albedo = part == 1 ? vec3(0.84, 0.85, 0.86) : vec3(0.86, 0.87, 0.88);
        s.rough = 0.42;
        float rib = aa_line(b.x, 0.62, 0.006, fw) * 0.5;
        s.albedo *= 1.0 - rib * 0.08 * saturate(1.0 - fw * 40.0);
        // the wingtips are painted red
        if (part == 1) {
            float tip = smoothstep(43.55, 43.65, abs(b.x));   // the tip cap, the last 1.6% of the span
            s.albedo = mix(s.albedo, vec3(0.55, 0.012, 0.008), tip);
        }
        float le = smoothstep(-0.55, -0.8, nb.z) * (1.0 - step(43.6, abs(b.x)) * float(part == 1));
        s.albedo = mix(s.albedo, vec3(0.78, 0.79, 0.8), le);
        s.metal = le;
        s.rough = mix(s.rough, 0.22, le);
        // exhaust soot streaks under the wing behind each engine
        if (part == 1 && nb.y < 0.0) {
            float ax = abs(b.x);
            float streak = 0.0;
            for (int e = 0; e < 3; ++e) {
                float ex = e == 0 ? 9.89 : (e == 1 ? 17.27 : 24.59);
                streak += exp(-pow((ax - ex) / 1.4, 2.0));
            }
            s.albedo *= 1.0 - 0.25 * saturate(streak);
        }
        // the registration under the left wing
        if (part == 1) apply_decals(s.albedo, b, nb);
        if (part == 1) exhaust(s, b, nb);
    } else if (part == 2) {
        // nacelles: white, bright metal lips, dark hot sections at the back
        apply_decals(s.albedo, b, nb);
        swoosh(s.albedo, b, nb);
        float lip = smoothstep(-0.6, -0.9, nb.z);
        s.albedo = mix(s.albedo, vec3(0.8, 0.8, 0.82), lip);
        s.metal = lip;
        s.rough = mix(s.rough, 0.2, lip);
        // a fan at speed is a blur, not a set of blades: flatten it toward
        // its average, darker metal
        if (vFan > 0.5) {
            float ang = atan(vFanUV.y, vFanUV.x);
            float rr = length(vFanUV);
            if (vFan > 1.5) {
                // the spinner, with the white spiral that shows it turning
                float t = fract((ang - uFanAngle) / (2.0 * PI) + rr * 2.6);
                float spiral = smoothstep(0.0, 0.03, t) * (1.0 - smoothstep(0.12, 0.15, t));
                s.albedo = mix(vec3(0.05, 0.05, 0.055), vec3(0.85), spiral);
                s.metal = 0.2;
                s.rough = 0.35;
            } else {
                // 33 blades turning: seen through a camera, the pattern
                // crawls round slowly while the blades themselves blur
                float bl = fract((ang - uFanAngle * 0.35) / (2.0 * PI) * 33.0);
                float blade = smoothstep(0.0, 0.18, bl) * (1.0 - smoothstep(0.55, 0.8, bl));
                vec3 lit = vec3(0.26, 0.26, 0.28), dark = vec3(0.03, 0.03, 0.035);
                vec3 c = mix(dark, lit, blade);
                s.albedo = mix(c, vec3(0.11, 0.11, 0.12), uFanBlur * 0.75);
                s.metal = 0.6;
                s.rough = mix(0.35, 0.6, uFanBlur);
            }
        }
        // The exhaust: everything aft of the cowl - the fan nozzle, the core
        // nozzle and the plug - is brushed titanium: dull, not a mirror, with
        // the fine rings the finishing left round it, straw and blue where the
        // heat has coloured it, sooty at the very end.
        float ax = abs(b.x);
        // the cowl ends 5.1 m behind the intake lip
        float cowl_end = ax < 13.6 ? -12.14 : (ax < 20.9 ? -7.335 : -2.49);
        float noz = smoothstep(cowl_end - 0.05, cowl_end + 0.08, b.z);
        // only the nozzle and plug on the engine's axis, not the pylon above
        vec2 eaxis = vec2(sign(b.x) * (ax < 13.6 ? 9.895 : (ax < 20.9 ? 17.275 : 24.595)),
                          ax < 13.6 ? -0.296 : (ax < 20.9 ? -0.56 : -0.835));
        noz *= step(length(b.xy - eaxis), 1.42) * step(b.z, cowl_end + 1.9);
        if (noz > 0.0) {
            float along = saturate((b.z - cowl_end) / 1.7);
            // the brushing: fine rings round the nozzle, each a little
            // brighter or duller, which is what breaks the mirror up
            float brush = hash1(uint(floor(b.z * 180.0 + 4000.0)) ^ uint(ax * 10.0)) - 0.5;
            vec3 ti = vec3(0.47, 0.45, 0.42) * (1.0 + 0.07 * brush + 0.03 * sin(b.z * 26.0 + ax));
            vec3 straw = vec3(0.62, 0.5, 0.32), blue = vec3(0.34, 0.37, 0.52);
            vec3 heat = mix(straw, blue, smoothstep(0.35, 0.8, along));
            ti = mix(ti, heat, smoothstep(0.15, 0.6, along) * 0.55);
            ti *= 1.0 - smoothstep(0.75, 1.0, along) * 0.6;
            s.albedo = mix(s.albedo, ti, noz);
            s.metal = mix(s.metal, 1.0, noz);
            s.rough = mix(s.rough, 0.5 + 0.1 * along + 0.08 * brush, noz);
        }
        exhaust(s, b, nb);
    } else {
        // fins
        apply_decals(s.albedo, b, nb);
    }
    // rain on the skin: darker, glossier
    s.rough = mix(s.rough, 0.1, uWet * 0.6);
    return s;
}

// --- light ------------------------------------------------------------------
float ggx(float nh, float a) {
    float a2 = a * a;
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float smith(float nv, float nl, float a) {
    float k = a * 0.5;
    return nl / (nl * (1.0 - k) + k) * nv / (nv * (1.0 - k) + k);
}

// Its own shadow. Where the sun grazes a surface the smooth normals say lit
// while the flat triangles under them shade one another in a sawtooth - so
// the lookup is pushed out along the normal and biased more the flatter the
// sun, filtered softly (16 taps on a disc, turned per pixel), and faded out
// where the surface is nearly edge-on to the sun and dark anyway.
float self_shadow(vec3 p, vec3 n, float nl) {
    if (uShadowOn <= 0.0) return 1.0;
    float graze = 1.0 - saturate(nl);
    vec4 c = uShadowMat * vec4(p + n * (0.06 + 0.45 * graze), 1.0);
    vec3 s = c.xyz / c.w * 0.5 + 0.5;
    if (any(lessThan(s, vec3(0.0))) || any(greaterThan(s, vec3(1.0)))) return 1.0;
    vec2 ts = 1.0 / vec2(textureSize(uShadowMap, 0));
    float bias = 0.0006 + 0.0025 * graze;
    float a = ign(gl_FragCoord.xy, 0.0) * 6.2832;
    mat2 rot = mat2(cos(a), sin(a), -sin(a), cos(a));
    const vec2 disc[16] = vec2[16](
        vec2(-0.613, 0.617), vec2(0.170, -0.040), vec2(-0.299, 0.791), vec2(0.645, 0.493),
        vec2(-0.651, 0.717), vec2(0.421, 0.027), vec2(-0.817, -0.271), vec2(-0.705, -0.668),
        vec2(0.977, -0.108), vec2(0.063, -0.992), vec2(0.346, -0.941), vec2(-0.110, 0.412),
        vec2(0.558, -0.558), vec2(-0.316, -0.316), vec2(0.180, 0.900), vec2(-0.950, 0.130));
    float radius = 1.6 + 1.6 * graze;
    float lit = 0.0;
    for (int i = 0; i < 16; ++i)
        lit += step(s.z - bias, texture(uShadowMap, s.xy + rot * disc[i] * ts * radius).r);
    lit /= 16.0;
    return mix(1.0, lit, smoothstep(0.02, 0.22, nl));
}

vec3 env(vec3 r, float rough) {
    vec3 below = texelFetch(uProbe, ivec2(2, 0), 0).rgb;
    float lod = rough * 5.0;
    vec3 up = textureLod(uSkyView, skyview_uv(vec3(r.x, max(r.y, 0.0), r.z), uCamRkm), lod).rgb;
    vec3 e = mix(below, up, smoothstep(-0.12, 0.05, r.y));
    // inside a cloud there is no sky to reflect, only the grey all round
    float inside = saturate(texelFetch(uProbe, ivec2(0, 0), 0).g * 1.5);
    vec3 fog = texelFetch(uProbe, ivec2(1, 0), 0).rgb * 0.55 + texelFetch(uProbe, ivec2(3, 0), 0).rgb * 0.09;
    return mix(e, fog, inside);
}

void main() {
    Surf s = skin();
    vec3 N = normalize(vN);
    if (!gl_FrontFacing) N = -N;
    float dist = length(vRel);
    vec3 V = -vRel / dist;
    vec3 L = uLightDir;

    vec4 p0 = texelFetch(uProbe, ivec2(0, 0), 0);
    vec3 key = texelFetch(uProbe, ivec2(3, 0), 0).rgb * p0.r;
    vec3 sky_up = texelFetch(uProbe, ivec2(1, 0), 0).rgb;
    vec3 sky_dn = texelFetch(uProbe, ivec2(2, 0), 0).rgb;

    // glass: nearly a mirror, a faint blue-green tint over the dark cockpit
    float rough = mix(s.rough, 0.025, s.glass);
    float a = rough * rough;
    vec3 albedo = mix(s.albedo, vec3(0.010, 0.016, 0.018), s.glass);
    vec3 f0 = mix(vec3(0.04), s.albedo, s.metal);
    // (a windscreen's heating coat gives its reflection a green cast)
    f0 = mix(f0, vec3(0.075, 0.095, 0.085), s.glass);

    float nl = saturate(dot(N, L));
    float nv = max(dot(N, V), 1e-3);
    vec3 H = normalize(L + V);
    float nh = saturate(dot(N, H));
    float vh = saturate(dot(V, H));
    vec3 F = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
    float sh = self_shadow(vRel, N, saturate(dot(N, L)));
    vec3 spec = F * ggx(nh, max(a, 0.002)) * smith(nv, nl, a) / (4.0 * nv * max(nl, 1e-3));
    vec3 direct = key * sh * nl * ((1.0 - F) * (1.0 - s.metal) * albedo / PI + spec);

    // the sky above and the world below, and their reflection
    float inside = saturate(p0.g * 1.5);
    vec3 fog = sky_up * 0.55 + texelFetch(uProbe, ivec2(3, 0), 0).rgb * 0.09;
    // light from the ground and the cloud tops below, as well as the sky
    vec3 below = max(sky_dn, sky_up * 0.35);
    vec3 amb = albedo * (1.0 - s.metal) * mix(mix(below, sky_up, N.y * 0.5 + 0.5), fog, inside);
    vec3 R = reflect(-V, N);
    vec3 Fe = f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(1.0 - nv, 5.0);
    vec3 refl = env(R, rough) * Fe;
    // occlusion from its own bulk: the belly and the wing roots are darker
    // (the belly is not buried in anything: it only faces away from the sky,
    // which the ambient term already knows - a light touch here, no more)
    float ao = mix(0.85, 1.0, saturate(N.y * 0.5 + 0.6));
    vec3 col = (direct + (amb + refl) * ao) * s.occ + s.emit * uEmit;
    // through the glass, the dim cockpit: a little of the daylight coming in,
    // darker toward the bottom of the pane where the panel is
    if (s.glass > 0.0) {
        float lower = saturate((3.3 - vBody.y) / 0.6);
        vec3 inside_light = sky_up * 0.035 * (1.0 - 0.6 * lower) + vec3(0.002, 0.0025, 0.003);
        col += inside_light * s.glass * (1.0 - Fe.g);
    }

    // its own lights and the lightning
    for (int l = 0; l < 4; ++l) {
        if (uPointPos[l].w <= 0.0) continue;
        vec3 d = uPointPos[l].xyz - vRel;
        float d2 = dot(d, d);
        vec3 ld = d * inversesqrt(d2);
        col += albedo * uPointCol[l] * saturate(dot(N, ld)) / (d2 + 1.0) / PI;
    }
    if (uFlash > 0.0) {
        vec3 fl = normalize(uFlashRel - vRel);
        col += albedo * vec3(0.7, 0.75, 1.0) * uFlash * 1.5 * (0.35 + saturate(dot(N, fl)));
    }

    vec3 S, T;
    aerial(gl_FragCoord.xy / uRes, dist, S, T);
    col = col * T + S;
    fragColor = vec4(col, 1.0);
    fragDist = vec4(dist, 0.0, 0.0, 1.0);
}
