# Mriya - design notes

How the screensaver draws and flies. The engine (C11, statically linked SDL3,
OpenGL 3.3 core, one `.scr`, the X-macro settings table, the Win32 shell and
dialog, the build scripts) comes from The Black Hole; everything on screen is
new.

## The frame

One frame, in order (`src/render.c`):

1. **The atmosphere** (Hillaire 2020). A transmittance table and a
   multiple-scattering table, rebuilt only when the air changes; a sky-view
   table each frame; aerial perspective as a 32^3 froxel volume. Distances in
   kilometres. The sun, the moon (lit by the sun at the scenario's phase) and
   the stars go through the same transmittance, so a low sun reddens and the
   moon dims in haze without any special cases.
2. **Weather map and cloud shadows.** A 512^2 map of cloud coverage, type,
   height and rain, anchored to the drifting air mass; from it a shadow map
   70 km across and a small probe of what the aircraft itself sees (sun
   through cloud, whether it is inside one, the sky and ground light).
3. **The ground.** Nested grid levels round the camera, each twice the spacing
   of the last, snapped to their own grid so vertices never slide. Height is
   one function shared by C (the flight model) and GLSL (the renderer):
   continents, eroded ridged mountains, hills, dunes, rivers. Land cover -
   fields, towns and their night lights, forest, snow, water with sun glint -
   is worked out per pixel.
4. **The aircraft**, 4x multisampled so its edges are smooth while its paint
   stays sharp, then folded (colour averaged, distance the nearest sample).
5. **Clouds.** Raymarched at a fraction of the resolution through two layers
   and a cirrus shell: Perlin-Worley base noise, Worley detail eroding the
   edges, a six-tap light march, multiple scattering by Wrenninge's octaves,
   the powder effect, lightning and the aircraft's own lights inside them.
   Accumulated over frames with reprojection, then upsampled against depth.
6. **Composite**, bloom, automatic exposure, a filmic curve, the eye's night
   vision, lens effects, grain.

Automatic quality measures the frame time on the first run and settles the
render scale, cloud resolution and step count against the budget.

## The aircraft

`tools/meshpack.c` runs at build time on `res/models/Mriya.glb`: welds,
computes crease-aware smooth normals, reorients to the body frame (x right,
y up, z aft, metres), labels every vertex with its part (fuselage, wing,
engine, fin, tailplane, and glass), and packs positions into 16 bits. It also
repairs two things in the model: each engine's core, closed by a flat disk,
becomes a tube a metre deep with an annulus and a small plug cone at its
bottom; and the windscreen panes - patches of skin sunk behind the frames,
bounded by creases - are found and marked as glass.

The livery is drawn, not textured:

* **The cheatline and the grey belly** come from a table of their edges along
  the fuselage (`src/bands_table.h`), measured by `tools/make_bands.py` from
  another model's texture and smoothed. The shader draws each edge from the
  table, anti-aliased by how fast the edge moves across the screen, so it is
  sharp at any distance. Under the nose the grey's edge is one line: an arc
  round the chin joined to the line under the band by a rounded corner, a
  white line following it all the way; forward of it the blue wraps the chin.
* **Titles, fin and nacelle swooshes, the roundel, the flag, the chin's
  lettering and the registrations** are decals from one atlas
  (`tools/make_decals.py`), projected from the side, from below or wrapped
  round the part they belong to. The lettering is set in type.
* **Moving parts** are done in the shaders: the fans' blade pattern turning
  (the geometry stays put), ailerons, elevators and rudders rotating about
  their measured hinge lines, the wings flexing with the load factor.

## Flight

`src/flight.c`: a point mass with the forces that decide how a heavy freighter
moves - lift through the load factor, a drag polar, thrust that lapses with
air density and Mach, gravity. The ceiling is not a number: it is wherever
the thrust left over after drag runs out.

* **The pilot** asks for a pitch rate (3 deg/s slow, 10 deg/s from 540 km/h
  indicated, limited by the wing's lift) and a roll rate, reached after a
  short lag; released, the controls hold the flight path and the bank.
* **The autopilot** flies the racetrack with lookahead guidance, picks a new
  altitude every 300 km, schedules 280 kt / Mach 0.74, and resumes after 20
  seconds untouched (never, with a joystick connected).
* **The floor** follows the terrain 50 m up, raised ahead of every rise by
  what the aircraft could climb in the distance left. Coming down onto it, a
  predicted pull-out height decides when it takes the pitch over, gradually;
  climbing is always the pilot's.
* **Turbulence** is only felt in rough weather: slow heavy swells, a shudder
  only in real storms.

## Weather

`src/weather.c`: 27 scenarios, each a complete description of the sky, the
clouds, the air, the precipitation and the ground. A shuffled deck deals
them. A change takes a minute: the sky's parameters blend, and the ground
blends by height and land cover between the old terrain and the new (never
their parameters - a feature 500 km from the origin would slide past at
kilometres a second as its scale changed). The ground never grows to within
600 m of the aircraft; the autopilot climbs first.

**Real time** (`src/realtime.c`): the sun and moon from low-precision
astronomy and the clock, and the current conditions from Open-Meteo, fetched
on a background thread every fifteen minutes and turned into a scenario - the
weather code picks the kind, cloud cover by level sets the layers, and the
visibility, precipitation, wind and temperature set the rest.
