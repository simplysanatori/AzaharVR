# tools/kiu_hand: Pit's arm

Two generators live here. Both make everything from numbers typed in their own files: no game
file, no ripped model or texture and no picture is read by either. Python 3 with numpy only
(plus the standard library's zlib and struct for PNG writing). No code or data from the web is
used.

## The drawn arm (`make_arm.py`, the `arm_*.py` files)

A rigged left hand and forearm with two textures, drawn by the app from a model file
(`src/frame_xr/vis/pit_model.h`) with the shader of `pit_hand_shader.h`.

    python3 tools/kiu_hand/make_arm.py [--bin DIR]

writes `src/frame_xr/vis/pit_arm_data.inc` (mesh, rig, handle and controller fit as C++ arrays),
`pit_arm_colour.png` and `pit_arm_surface.png` beside it, and with `--bin` the same model as
`DIR/pit_hand.bin` ("PITHAND1", images raw). It prints a hash of each file: the same numbers give
the same bytes (no random generator, no clock; the noise is a hashed lattice with fixed seeds).
About 20 s.

| file | what |
| --- | --- |
| `arm_skel.py` | the proportions and the joints (the module's 28: OpenXR's 26, forearm, elbow), the bind pose, how the module's pose goes onto this arm |
| `arm_shape.py` | the hand's skin: a cage of quads, subdivided twice, laid on a smooth distance field; the glove's outline; the skin weights |
| `arm_gear.py` | the two gold bands, the leather barrel, the dark inside of each band, the fading forearm, the plate |
| `arm_build.py` | all of it as one vertex list with one 1024 x 1024 atlas, tangents, weights; the body's triangles first, the plate's last |
| `arm_paint.py` | the two images, painted per texel from the point of the arm it covers; ambient occlusion from the distance fields |
| `arm_pose.py` | the module's poses (keys from `poses.py`, blended as `PitHandPoseFromCurls`), the handle in the palm, the Steam Frame controller's fit |
| `arm_export.py` | the model file's writer, and a reader that checks every rule `pit_model.h` states |
| `arm_render.py` | a software rasteriser with the app's fragment shader ported line by line, and a clay look |
| `arm_sheets.py` | the pictures: the four judged views, the pose sheet, both arms in first person |

Left and right: the body (every triangle but the last `kPitArmPlateTriangleCount`) is drawn for
both arms, the right one mirrored; the plate only for the left. Nothing in the body's shape or in
the images is one-sided (the plate casts no baked shadow on the hand), so both arms can be drawn
in one frame in different poses from the one model.

Proportions (over the palm's length, wrist to middle knuckle, 80 mm): fingers 1.03, 1.05, 1.02,
0.81, the thumb 1.40 from its root; the hand as long as the forearm (164 mm). The finger
thicknesses asked for do not close as widths (four bases sum to 1.92 against a palm 1.60 wide),
so they are kept as ratios and scaled by 0.72; the wrist and the bands follow the reference
pictures (bands 122 mm across, 26 mm long, standing 16 mm off). See `arm_skel.py`.

Colours were sampled once (ImageMagick, by hand) from the owner's reference pictures and typed
into `arm_paint.py`; the tool itself reads no picture.

Pictures: `python3 tools/kiu_hand/arm_sheets.py OUT_DIR` (`--clay` for shape only). A sheet
beside a reference is made outside this tool, e.g. `magick ref.png OUT_DIR/shot_r1.png +append`.

Not checked by this tool: the app's own loader and its handle rule (`arm_pose.py` has its own
version of "a finger is opened until it lies on the handle"); the frame's row order and the
per-vertex joints/weights layout are read from `pit_model.h`'s comment (rows are the matrix's
rows, its columns the axes; four joint bytes then four weight bytes per vertex).

## The module's hand (`gen.py`, `hand.py`, `mesh.py`, `build.py`, `poses.py`, `laurel.py`)

The first hand, still compiled into `pit_hand.cpp` for its joints, key poses and fits, and the
laurel frame: `python3 tools/kiu_hand/gen.py` writes `pit_hand_data.inc` and
`laurel_frame_data.inc`. `sheets.py`, `render.py` and `dump.cpp` are its previews. The drawn arm
takes its poses from this module, so these stay until the module's pose tables move.
