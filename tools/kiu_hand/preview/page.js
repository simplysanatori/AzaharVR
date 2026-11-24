// Copyright 2026 AzaharFrame contributors
// Licensed under GPLv3 or any later version
// Refer to the license.txt file included.

// Draws the dump of tools/kiu_hand/model_dump.cpp with the app's own shaders (the vertex shader
// of model_shaders.h and the fragment shader of pit_hand_shader.h, served as text by
// shots.mjs), the way controller_models.cpp's RecordPit sets them up: the same push constants
// (here a uniform block), the same two images with the same sampling (linear, clamped, no
// smaller levels), the eye, key light and up taken into model space. Only the declarations
// differ, as WebGL needs: see adapt().

const m4 = {
  mul(a, b) {
    const o = new Float32Array(16);
    for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
      let s = 0;
      for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
      o[c * 4 + r] = s;
    }
    return o;
  },
  perspective(fovDeg, near, far) {
    const f = 1 / Math.tan((fovDeg * Math.PI) / 360);
    // y turned over: the vertex shader turns it back (it writes Vulkan's clip space)
    return new Float32Array([f, 0, 0, 0, 0, -f, 0, 0, 0, 0, (far + near) / (near - far), -1, 0, 0, (2 * far * near) / (near - far), 0]);
  },
  look(eye, at, up) {
    const n = (v) => { const l = Math.hypot(...v); return v.map((x) => x / l); };
    const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
    const z = n([eye[0] - at[0], eye[1] - at[1], eye[2] - at[2]]);
    const x = n(cross(up, z));
    const y = cross(z, x);
    const d = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    return { view: new Float32Array([x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0, -d(x, eye), -d(y, eye), -d(z, eye), 1]), back: z };
  },
  // a placement with a uniform scale and perhaps a mirror: the inverse by cofactors
  invert(m) {
    const a = m, o = new Float32Array(16);
    const c = [
      a[5] * a[10] - a[6] * a[9], a[6] * a[8] - a[4] * a[10], a[4] * a[9] - a[5] * a[8],
      a[2] * a[9] - a[1] * a[10], a[0] * a[10] - a[2] * a[8], a[1] * a[8] - a[0] * a[9],
      a[1] * a[6] - a[2] * a[5], a[2] * a[4] - a[0] * a[6], a[0] * a[5] - a[1] * a[4]];
    const det = a[0] * c[0] + a[1] * c[1] + a[2] * c[2];
    for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) o[j * 4 + i] = c[i * 3 + j] / det;
    for (let i = 0; i < 3; i++) o[12 + i] = -(o[i] * a[12] + o[4 + i] * a[13] + o[8 + i] * a[14]);
    o[15] = 1;
    return o;
  },
  apply(m, v, w) {
    return [0, 1, 2].map((r) => m[r] * v[0] + m[4 + r] * v[1] + m[8 + r] * v[2] + m[12 + r] * w);
  },
};

/// The app's GLSL (Vulkan, version 450) with the declarations WebGL 2 wants; the bodies are
/// untouched.
function adapt(source, stage) {
  let s = source.replace('layout(push_constant) uniform PC', 'layout(std140) uniform PC');
  s = s.replace(/layout\(set = \d+, binding = \d+\) uniform/g, 'uniform');
  if (stage === 'vert') s = s.replace(/layout\(location = \d+\) out/g, 'out');
  else s = s.replace(/layout\(location = \d+\) in/g, 'in');
  return '#version 300 es\nprecision highp float;\nprecision highp int;\n#define OUT_LINEAR false\n' + s;
}

const CLAY = `
layout(push_constant) uniform PC { mat4 mvp; vec4 eye; vec4 key; vec4 up; vec4 params; } pc;
layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;
void main() {
    vec3 n = normalize(v_normal);
    bool front = mod(pc.params.w, 2.0) > 0.5 ? !gl_FrontFacing : gl_FrontFacing;
    if (!front) n = -n;
    vec3 L = normalize(pc.key.xyz), V = normalize(pc.eye.xyz - v_position), U = normalize(pc.up.xyz);
    float d = 0.25 + 0.6 * max(dot(n, L), 0.0) + 0.15 * (0.5 + 0.5 * dot(n, U)) + 0.12 * pow(1.0 - max(dot(n, V), 0.0), 3.0);
    fragColor = vec4(vec3(0.80, 0.79, 0.77) * d + v_uv.x * 0.0 + v_tangent.x * 0.0, 1.0);
}`;

async function bytes(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: ${r.status}`);
  return new Uint8Array(await r.arrayBuffer());
}

function program(gl, vert, frag) {
  const make = (type, src) => {
    const sh = gl.createShader(type);
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(sh));
    return sh;
  };
  const p = gl.createProgram();
  gl.attachShader(p, make(gl.VERTEX_SHADER, vert));
  gl.attachShader(p, make(gl.FRAGMENT_SHADER, frag));
  ['a_position', 'a_normal', 'a_tangent', 'a_uv'].forEach((n, i) => gl.bindAttribLocation(p, i, n));
  gl.linkProgram(p);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
  gl.uniformBlockBinding(p, gl.getUniformBlockIndex(p, 'PC'), 0);
  return p;
}

window.renderShots = async (spec) => {
  const canvas = document.getElementById('c');
  canvas.width = canvas.height = spec.size;
  const gl = canvas.getContext('webgl2', { antialias: true, alpha: true, premultipliedAlpha: true, preserveDrawingBuffer: true });
  const info = await (await fetch('/dump/info.json')).json();
  const vert = adapt(await (await fetch('/shader/vert')).text(), 'vert');
  const shaded = program(gl, vert, adapt(await (await fetch('/shader/frag')).text(), 'frag'));
  const clay = program(gl, vert, adapt(CLAY, 'frag'));
  for (const [p, names] of [[shaded, ['u_colour', 'u_surface']]]) {
    gl.useProgram(p);
    names.forEach((n, i) => gl.uniform1i(gl.getUniformLocation(p, n), i));
  }
  const [w, h] = info.texture;
  const texture = (unit, data, format) => {
    const t = gl.createTexture();
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.texImage2D(gl.TEXTURE_2D, 0, format, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, data);
    for (const [k, v] of [[gl.TEXTURE_MIN_FILTER, gl.LINEAR], [gl.TEXTURE_MAG_FILTER, gl.LINEAR],
      [gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE], [gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE]]) gl.texParameteri(gl.TEXTURE_2D, k, v);
  };
  texture(0, await bytes('/dump/colour.rgba'), gl.SRGB8_ALPHA8);
  texture(1, await bytes('/dump/surface.rgba'), gl.RGBA8);
  const ibo = gl.createBuffer();
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ibo);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint32Array((await bytes('/dump/indices.u32')).buffer), gl.STATIC_DRAW);
  const vbos = new Map();
  const vbo = async (name) => {
    if (!vbos.has(name)) {
      const b = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, b);
      gl.bufferData(gl.ARRAY_BUFFER, await bytes(`/dump/${name}`), gl.STATIC_DRAW);
      vbos.set(name, b);
    }
    return vbos.get(name);
  };
  const ubo = gl.createBuffer();
  gl.bindBufferBase(gl.UNIFORM_BUFFER, 0, ubo);
  gl.enable(gl.DEPTH_TEST);
  gl.depthFunc(gl.LEQUAL);
  gl.enable(gl.CULL_FACE);
  gl.frontFace(gl.CCW);
  gl.enable(gl.BLEND);
  gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
  const out = [];
  for (const shot of spec.shots) {
    const bg = shot.background ?? spec.background ?? [0.5, 0.52, 0.55];
    gl.viewport(0, 0, spec.size, spec.size);
    gl.clearColor(bg[0], bg[1], bg[2], 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    const { view, back } = m4.look(shot.eye, shot.target, shot.up ?? [0, 1, 0]);
    const vp = m4.mul(m4.perspective(shot.fov ?? 35, 0.01, 10), view);
    gl.useProgram(shot.look === 'clay' ? clay : shaded);
    for (const d of shot.draws) {
      const model = new Float32Array(d.model);
      const inv = m4.invert(model);
      const block = new Float32Array(32);
      block.set(m4.mul(vp, model), 0);
      block.set([...m4.apply(inv, shot.eye, 1), 0], 16);
      // the key light as RecordPit: above, and from behind the viewer's head
      const light = shot.key ?? [back[0] * 0.6, 1 + back[1] * 0.6, back[2] * 0.6];
      block.set([...m4.apply(inv, light, 0), 0], 20);
      block.set([...m4.apply(inv, [0, 1, 0], 0), 0], 24);
      block.set([0, 1, d.fade ?? 1, d.mirrored ? 1 : 0], 28);
      gl.bindBuffer(gl.UNIFORM_BUFFER, ubo);
      gl.bufferData(gl.UNIFORM_BUFFER, block, gl.DYNAMIC_DRAW);
      gl.bindBuffer(gl.ARRAY_BUFFER, await vbo(d.verts));
      [[0, 3, 0], [1, 3, 12], [2, 4, 24], [3, 2, 40]].forEach(([loc, n, off]) => {
        gl.enableVertexAttribArray(loc);
        gl.vertexAttribPointer(loc, n, gl.FLOAT, false, 48, off);
      });
      gl.cullFace(d.mirrored ? gl.FRONT : gl.BACK);
      const count = info.body_indices + (d.plate ? info.plate_indices : 0);
      gl.drawElements(gl.TRIANGLES, count, gl.UNSIGNED_INT, 0);
    }
    out.push({ name: shot.name, png: canvas.toDataURL('image/png') });
  }
  return out;
};
window.ready = true;
