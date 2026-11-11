// minidoom - Freedoom E1M1 as a Snapchat Lens.
// Main script: boots the engine (C -> pure JS), drives tics from UpdateEvent,
// presents 3D view + HUD as one procedural texture and the player's face as another,
// maps touch (stick + drag-to-turn; fire/use are automatic engine assists) to Doom input,
// streams SFX through an AudioOutput, and records the run for an end-card replay.
//
// Attached to the "MiniDoom" object, a child of the Safe Render screen region.

var createDoom = require("./doomEngine");
var WAD_B64 = require("./wadData");
var LS = require("./LensState").LS;
var UI = require("./Ui");
var CONFIG = require("./BuildConfig");
var Debug = require("./DebugCapture");

var OUT_W = 320, OUT_H = 240;            // 3D view (200 rows) + HUD strip (40 rows)
var FACE_W = 56, FACE_H = 62;            // face canvas (= FaceRT resolution)
var TIC_MS = 1000 / 35;
var KEY = { UP: 0xad, DOWN: 0xaf, LEFT: 0xac, RIGHT: 0xae, STRAFE_L: 0xa0, STRAFE_R: 0xa1,
            USE: 0xa2, FIRE: 0xa3, RUN: 0x80 + 0x36 };
// Adaptive quality ladder: [detail (0 high, 1 low), render every Nth Lens frame]. Tics always run at 35 Hz.
var QUALITY = [[0, 1], [1, 1], [1, 2]];
var frameNo = 0;

var doom = null;
var state = null;          // Int32Array view of LensState
var mode = "loading";      // loading | title | playing | dead | done
var paused = false;
var acc = 0;
var ui = null;
var screenProv = null, hudProv = null;
var outView = null, viewView = null, hudView = null;
var quality = 0;
var deadSince = 0;
var faceProv = null, faceView = null, faceVersion = -1;
var overlayProv = null, overlayView = null, overlayVersion = -1, overlayText = null;
var heldKeys = {};

// perf counters (logged every 5 s, used for adaptive quality)
var perf = { updNs: 0, updates: 0, lastFrameT: 0, frameGapSum: 0, frames: 0, tics: 0, engineNs: 0, presentNs: 0, uploadNs: 0, faceNs: 0, audioNs: 0, t0: 0, slow: 0, fast: 0 };

function log(s) { print("[minidoom] " + s); }
function nowNs() { return getRealTimeNanos(); }

// ---------------------------------------------------------------- engine boot
function boot() {
  var t0 = nowNs();
  createDoom({ print: function () {}, printErr: function (s) { log("engine: " + s); } }).then(function (m) {
    doom = m;
    var wad = Base64.decode(WAD_B64.join(""));
    var ptr = doom._lens_wad_alloc(wad.length);
    doom.HEAPU8.set(wad, ptr);
    doom._lens_create();
    state = new Int32Array(doom.HEAP32.buffer, doom._lens_state_ptr(), LS_COUNT);
    var out = doom._lens_present();
    outView = new Uint8Array(doom.HEAPU8.buffer, out, OUT_W * OUT_H * 4);
    overlayView = new Uint8Array(doom.HEAPU8.buffer, doom._lens_overlay_ptr(), 320 * 200 * 4);
    viewView = outView.subarray(0, OUT_W * 200 * 4);           // rows 0..199: 3D view
    hudView = outView.subarray(OUT_W * 200 * 4);               // rows 200..239: HUD strip
    uploadScreen();
    faceView = new Uint8Array(doom.HEAPU8.buffer, doom._lens_face_ptr(), FACE_W * FACE_H * 4);
    uploadFace();
    ui.setBezel(function (w, h, b) {
      var p = doom._lens_bezel(w, h, b);
      if (!p) { log("bezel " + w + "x" + h + " too large"); return null; }
      var t = ProceduralTextureProvider.createWithFormat(w, h, TextureFormat.RGBA8Unorm);
      t.control.setPixels(0, 0, w, h, new Uint8Array(doom.HEAPU8.buffer, p, w * h * 4));
      return t;
    });
    ui.setBackground(function (w, h) {
      var p = doom._lens_background(w, h);
      if (!p) return null;
      var t = ProceduralTextureProvider.createWithFormat(w, h, TextureFormat.RGBA8Unorm);
      t.control.setPixels(0, 0, w, h, new Uint8Array(doom.HEAPU8.buffer, p, w * h * 4));
      return t;
    });
    audio.init();
    log("boot ms " + ((nowNs() - t0) / 1e6).toFixed(0) + " wad bytes " + wad.length);
    setMode("title");
  }, function (e) {
    log("BOOT FAILED " + e + " " + (e && e.stack));
    log("boot failed; the lens cannot start");
  });
}
var LS_COUNT = require("./LensState").LS_COUNT;

// Overlay text in Doom's HUD font (engine-drawn): '^' = 2x, '^^' = 3x, lines centred and wrapped
// to the columns of the view that are actually on screen.
function setOverlay(text, shade) {
  if (!doom || text === overlayText) return;
  overlayText = text;
  var n = text ? text.length * 4 + 1 : 1;
  var p = doom._malloc(n);
  doom.stringToUTF8(text || "", p, n);
  doom._lens_set_overlay(p, shade === undefined ? 150 : shade, ui.visibleCols);
  doom._free(p);
  uploadOverlay();
}
function uploadOverlay() {
  if (!overlayView) return;
  var v = state[LS.overlay_version];
  if (v === overlayVersion) return;
  overlayVersion = v;
  overlayProv.setPixels(0, 0, 320, 200, overlayView);
}

function mmss(secs) { return Math.floor(secs / 60) + ":" + ("0" + secs % 60).slice(-2); }

function setMode(m) {
  mode = m;
  if (m === "title") setOverlay("^^MINIDOOM\n\n^TAP TO START\n\nLEFT STICK MOVES. RIGHT STICK TURNS.\nSHOOTING AND DOORS ARE AUTOMATIC.\nTAP 2-7 FOR WEAPONS.");
  else if (m === "playing") { setOverlay(null); ui.showReplay(null); }
  else if (m === "dead") setOverlay(null);
  else if (m === "done") {
    if (recorder.on) recorder.finish(true); else recorder.show();
    var s = state;
    var secs = Math.floor(s[LS.leveltime] / 35);
    var pct = function (a, b) { return b ? Math.floor(100 * a / b) : 100; };
    var kills = pct(s[LS.kills], s[LS.totalkills]);
    var best = storage.recordRun(secs, kills);
    setOverlay("^^E1M1 COMPLETE\n\nTIME " + mmss(secs) +
      "\nKILLS " + kills + "%  ITEMS " + pct(s[LS.items], s[LS.totalitems]) + "%  SECRETS " + pct(s[LS.secrets], s[LS.totalsecret]) + "%" +
      "\nBEST TIME " + mmss(best.time) + "  BEST KILLS " + best.kills + "%" +
      "\n\n^TAP TO PLAY AGAIN", 120);
  }
}

// ---------------------------------------------------------------- storage
var storage = {
  store: null,
  recordRun: function (secs, kills) {
    var st = global.persistentStorageSystem.store;
    var bt = st.has("bestTime") ? st.getInt("bestTime") : 0;
    var bk = st.has("bestKills") ? st.getInt("bestKills") : 0;
    if (!bt || secs < bt) { st.putInt("bestTime", secs); bt = secs; }
    if (kills > bk) { st.putInt("bestKills", kills); bk = kills; }
    return { time: bt, kills: bk };
  }
};

// ---------------------------------------------------------------- input
function key(k, down) {
  if (!doom) return;
  if (!!heldKeys[k] === down) return;
  heldKeys[k] = down;
  doom._lens_post_key(down ? 1 : 0, k);
}
function releaseAll() {
  for (var k in heldKeys) if (heldKeys[k]) key(+k, false);
  if (doom) { analog.fwd = analog.side = analog.turn = 0; sendAnalog(); }
  if (ui) { ui.knobAt("move", 0, 0); ui.knobAt("turn", 0, 0); }
  touches = {};
}
function tapKey(k) { doom._lens_tap_key(k); }

// Twin sticks are analog: deflection from where the finger landed, normalised by the stick
// radius, goes to the engine as forward/strafe (left) and turn rate (right) via lens_set_analog.
var touches = {};          // id -> { zone, x0, y0, lx, ly }
var analog = { fwd: 0, side: 0, turn: 0 };
function sendAnalog() { doom._lens_set_analog(analog.fwd, analog.side, analog.turn); }
function stickValue(d, r) {
  var v = d / r;
  if (Math.abs(v) < 0.12) return 0;                 // dead zone
  return Math.round(Math.max(-1, Math.min(1, v)) * 1000);
}
function onTouch(kind, id, p) {
  // p: screen point (0..1, top-left origin)
  if (!doom) return;
  var l = ui.toLocal(p);
  if (kind === "start") {
    if (mode === "title") { setMode("playing"); return; }
    if (mode === "done") { doom._lens_restart(); releaseAll(); setMode("playing"); return; }
    if (mode === "dead" && getTime() - deadSince > 1.5) { doom._lens_restart(); releaseAll(); setMode("playing"); return; }
    var zone = ui.zoneAt(l);
    touches[id] = { zone: zone, x0: l.x, y0: l.y, lx: l.x, ly: l.y };
    if (zone === "hud") {
      var h = ui.hudPixel(l);
      var hit = doom._lens_hud_hit(h.x, h.y);
      if (hit >= 50 && hit <= 55) tapKey(hit);   // '2'..'7'
    }
  } else if (kind === "move") {
    var t = touches[id];
    if (!t) return;
    var r = ui.stickRadius, dx = l.x - t.x0, dy = l.y - t.y0;
    if (t.zone === "move") {
      analog.fwd = stickValue(dy, r);
      analog.side = stickValue(dx, r);
      sendAnalog();
      ui.knobAt("move", dx, dy);
      if (analog.fwd || analog.side) recorder.start();   // the run starts with the first move
    } else if (t.zone === "turnstick") {
      analog.turn = stickValue(dx, r);
      sendAnalog();
      ui.knobAt("turn", dx, 0);
    } else if (t.zone === "drag") {
      // free drag (3D view, face): full-width drag = 4096 Doom mouse units = 180 degrees
      var units = Math.round((l.x - t.lx) * 4096 / ui.width);
      if (units) doom._lens_post_mouse(units);
    }
    t.lx = l.x; t.ly = l.y;
  } else if (kind === "end") {
    var t2 = touches[id];
    if (!t2) return;
    if (t2.zone === "move") { analog.fwd = analog.side = 0; sendAnalog(); ui.knobAt("move", 0, 0); }
    if (t2.zone === "turnstick") { analog.turn = 0; sendAnalog(); ui.knobAt("turn", 0, 0); }
    delete touches[id];
  }
}

function uploadScreen() {
  screenProv.setPixels(0, 0, OUT_W, 200, viewView);
  hudProv.setPixels(0, 0, OUT_W, 40, hudView);
}

// Upload the face canvas only when the engine redrew it (camera frame, state change, palette flash).
function uploadFace() {
  if (!faceView) return;
  var v = state[LS.face_version];
  if (v === faceVersion) return;
  faceVersion = v;
  faceProv.setPixels(0, 0, FACE_W, FACE_H, faceView);
}

// ---------------------------------------------------------------- run recording
// Records the gameplay (VideoRecorder on GameRT: the 3D view only, no face, HUD or controls) from
// the player's first move; the end card plays it back over the 3D view as a replay the user can
// Snap. The Game Camera that fills GameRT runs only while recording. Capped so memory stays bounded.
var recorder = {
  rec: null, on: false, t0: 0, ok: true, MAX_S: 60,
  camera: function (on) { if (ui.gameCamera) ui.gameCamera.enabled = on; },
  start: function () {
    if (this.on || !this.ok || mode !== "playing") return;
    try {
      if (!this.rec) {
        var opt = new VideoRecorder.Options(requireAsset("../Textures/GameRT.renderTarget"));
        this.rec = VideoRecorder.create(opt);
      }
      this.camera(true);
      this.rec.startRecording();
      this.on = true;
      // Off by default (MINIDOOM_TRY_RECORD=1 to enable, for device experiments only; keep it off in
      // submitted builds): scene.forceRecordFrame() is an undocumented
      // runtime call that exists only on devices ("not available on the simulated platform").
      // Lenses have no documented way to start a Snap; this tests whether it does on a phone.
      if (CONFIG.TRY_FORCE_RECORD) {
        try { global.scene.forceRecordFrame(); log("forceRecordFrame called; recording=" + global.scene.isRecording()); }
        catch (e) { log("forceRecordFrame unavailable: " + e); }
      }
      this.t0 = getTime();
      log("run recording started");
    } catch (e) { this.ok = false; this.camera(false); log("video recorder unavailable: " + e); }
  },
  tick: function () {
    if (this.on && getTime() - this.t0 > this.MAX_S) this.finish(false, true);
  },
  // keep=true: show the replay (end card). keep=false: discard (death/restart), or keepTex for cap.
  finish: function (show, capped) {
    if (!this.on) return;
    this.on = false;
    var self = this;
    this.camera(false);
    try {
      if (!show && !capped) { this.rec.cancelRecording(); this.replay = null; return; }
      this.rec.stopRecording().then(function (tex) {
        self.replay = tex;
        log("run recording stopped (" + (getTime() - self.t0).toFixed(1) + " s)");
        if (mode === "done") self.show();
      }, function (e) { log("recording failed: " + e); });
    } catch (e) { log("recording stop failed: " + e); }
  },
  show: function () {
    if (!this.replay) return;
    try { this.replay.control.play(-1); } catch (e) { log("replay play failed: " + e); }
    ui.showReplay(this.replay);
  }
};

// ---------------------------------------------------------------- face
// The Face Camera renders "Face Source Image" (the whole device camera image) into FaceRT. On
// each landmark update the image is placed so a window around the face fills FaceRT. The window
// is clamped inside the camera image, so near the edges or very close up it slides/shrinks
// instead of running off-frame: the slot always shows real camera pixels.
var FACE_SLOT_ASPECT = FACE_W / (FACE_H * 1.2);   // displayed width / height of the face slot
var face = {
  rt: null, buf: new Uint8Array(FACE_W * FACE_H * 4), live: false, lastSeen: -10, frame: 0, ok: true,
  head: null, wasPresent: false, readbacks: 0, src: null, srcTex: null, lmAt: -10, win: null, logged: false,
  init: function () {
    try { this.rt = requireAsset("../Textures/FaceRT.renderTarget"); } catch (e) { log("FaceRT missing: " + e); this.rt = null; }
    var ht = UI.findByName("Head Tracker");
    this.head = ht ? ht.getComponent("Component.Head") : null;
    var so = UI.findByName("Face Source Image");
    if (so) {
      this.src = so.getComponent("Component.ScreenTransform");
      var img = so.getComponent("Component.Image");
      try { this.srcTex = img.mainMaterial.mainPass.baseTex; } catch (e) { this.srcTex = null; }
    }
    var self = this;
    if (this.head) {
      try { this.head.onLandmarksUpdate.add(function (pts) { self.onLandmarks(pts); }); }
      catch (e) { log("onLandmarksUpdate unavailable: " + e); }
    }
    log("face: render target " + (this.rt ? "ok" : "missing") + ", head tracker " + (this.head ? "ok" : "missing") +
        ", source image " + (this.src && this.srcTex ? "ok" : "missing"));
  },
  // Landmarks are screen points (0..1, top-left origin). The screen shows the camera image
  // aspect-filled, so map screen -> camera-image coords before framing.
  onLandmarks: function (pts) {
    if (!pts || !pts.length) return;
    this.lmAt = getTime();
    if (!this.src || !this.srcTex || !ui) return;
    var tw = this.srcTex.getWidth(), th = this.srcTex.getHeight();
    if (tw <= 1 || th <= 1) return;
    var ff = ui.fullFrame, as = (ff.x1 - ff.x0) / (ff.y1 - ff.y0), at = tw / th;
    var x0 = 1, x1 = 0, y0 = 1, y1 = 0;
    for (var i = 0; i < pts.length; i++) {
      var u = pts[i].x, v = pts[i].y;
      if (at > as) u = 0.5 + (u - 0.5) * as / at; else v = 0.5 + (v - 0.5) * at / as;
      if (u < x0) x0 = u; if (u > x1) x1 = u; if (v < y0) y0 = v; if (v > y1) y1 = v;
    }
    // In camera pixels: window 1.6x the landmark height (brows..chin -> hair..neck), centred a
    // little above the landmark centre, at the slot's aspect; at most the whole camera image.
    var fh = (y1 - y0) * th;
    var h = Math.max(fh * 1.6, 8), w = h * FACE_SLOT_ASPECT;
    var cx = (x0 + x1) / 2 * tw, cy = (y0 + y1) / 2 * th - 0.1 * fh;
    if (w > tw) { h *= tw / w; w = tw; }
    if (h > th) { h = th; }
    var W = this.win;
    if (!W) W = this.win = { cx: cx, cy: cy, h: h };
    else { W.cx += (cx - W.cx) * 0.5; W.cy += (cy - W.cy) * 0.5; W.h += (h - W.h) * 0.5; }
    var hh = W.h / 2, hw = W.h * FACE_SLOT_ASPECT / 2;
    W.cx = Math.max(hw, Math.min(tw - hw, W.cx));
    W.cy = Math.max(hh, Math.min(th - hh, W.cy));
    // Camera image placed so the window maps to the canvas's -1..1 (ScreenTransform y is up).
    var u0 = (W.cx - hw) / tw, du = 2 * hw / tw, v0 = (W.cy - hh) / th, dv = 2 * hh / th;
    var left = -1 - 2 * u0 / du, top = 1 + 2 * v0 / dv;
    this.src.anchors = Rect.create(left, left + 2 / du, top - 2 / dv, top);
    if (!this.logged) {
      this.logged = true;
      log("face window: camera " + tw + "x" + th + " screen aspect " + as.toFixed(3) + " landmarks " + pts.length +
          " bbox " + x0.toFixed(2) + "," + y0.toFixed(2) + ".." + x1.toFixed(2) + "," + y1.toFixed(2));
    }
  },
  // Face presence: landmark updates arrive only while a face is tracked (this replaces polling
  // the deprecated Head face count); FaceFound/Lost events remain as a backup.
  present: function () {
    var lm = getTime() - this.lmAt < 0.3;
    var p = lm || !!this.tracking;
    if (p !== this.wasPresent) { log("face " + (p ? "found" : "lost") + " (landmarks " + lm + ", event " + !!this.tracking + ")"); this.wasPresent = p; }
    return p;
  },
  update: function () {
    if (!doom || !this.rt) return;
    // Dead: keep the camera frame from the moment of death frozen until the player retries.
    if (state[LS.dead]) return;
    if (!this.ok) {                              // a failed readback is retried every 2 s
      if (getTime() < this.retryAt) return;
      this.ok = true;
    }
    if (this.present()) this.lastSeen = getTime();
    var tracked = getTime() - this.lastSeen < 0.5;
    if (!tracked) { if (this.live) { this.live = false; doom._lens_face_set_live(0); } return; }
    if ((this.frame++ & 1) !== 0) return;      // 15 Hz readback is plenty for a 24x29 face
    var t = nowNs();
    try {
      var tex = ProceduralTextureProvider.createFromTexture(this.rt);
      tex.control.getPixels(0, 0, FACE_W, FACE_H, this.buf);
      doom.HEAPU8.set(this.buf, doom._lens_face_input_ptr());
      doom._lens_face_frame();
      this.readbacks++;
      if (!this.live) { this.live = true; doom._lens_face_set_live(1); }
    } catch (e) {
      if (!this.failLogged) { log("face readback failed (retrying): " + e); this.failLogged = true; }
      this.ok = false;
      this.retryAt = getTime() + 2;
      if (this.live) { this.live = false; doom._lens_face_set_live(0); }
    }
    perf.faceNs += nowNs() - t;
  }
};

// ---------------------------------------------------------------- audio
var audio = {
  prov: null, buf: null, ok: false,
  init: function () {
    try {
      var out = requireAsset("../Audio/SfxOutput.audioOutput");
      this.prov = out.control;
      doom._lens_audio_set_rate(this.prov.sampleRate | 0);
      var so = script.getSceneObject();
      var ac = so.createComponent("Component.AudioComponent");
      ac.audioTrack = out;
      ac.play(-1);
      this.ok = true;
      log("audio rate " + this.prov.sampleRate + " max frame " + this.prov.maxFrameSize);
    } catch (e) { log("sfx output unavailable: " + e); }
    try {
      var music = requireAsset("../Audio/music_e1m1.mp3");
      var mo = global.scene.createSceneObject("Music");
      mo.setParent(script.getSceneObject());
      var mc = mo.createComponent("Component.AudioComponent");
      mc.audioTrack = music;
      mc.volume = 0.6;
      this.music = mc;
    } catch (e) { log("music unavailable: " + e); }
  },
  pump: function () {
    if (!this.ok) return;
    var t = nowNs();
    var n = this.prov.getPreferredFrameSize();
    if (n > 0) {
      if (n > 4096) n = 4096;
      var p = doom._lens_audio_mix(n, 1) >> 2;
      this.prov.enqueueAudioFrame(doom.HEAPF32.subarray(p, p + n), new vec3(n, 1, 1));
    }
    perf.audioNs += nowNs() - t;
  },
  playMusic: function (on) {
    if (!this.music) return;
    if (on && !this.music.isPlaying()) this.music.play(-1);
    if (!on && this.music.isPlaying()) this.music.stop(false);
  }
};

// ---------------------------------------------------------------- frame loop
// Debug builds only: an in-Lens autotest that drives the real touch handler (onTouch) with
// synthesized screen points, asserts on engine state, and captures the rendered output
// (decoded by tools/decode_capture.py). Release builds never call this.
var auto = { t0: -1, step: 0, mark: {}, pass: 0, fail: 0, tid: 900 };
function check(name, ok, detail) {
  if (ok) auto.pass++; else auto.fail++;
  log("AUTOTEST " + (ok ? "PASS " : "FAIL ") + name + (detail ? " (" + detail + ")" : ""));
}
function touchAt(kind, p, id) { onTouch(kind, id, ui.toScreen(p)); }
function zoneCenter(z) { var r = ui.zones[z]; return { x: (r.x0 + r.x1) / 2, y: (r.y0 + r.y1) / 2 }; }
function autopilot() {
  if (!CONFIG.DEBUG || !doom) return;
  if (auto.t0 < 0) auto.t0 = getTime();
  var t = getTime() - auto.t0, S = state, m = auto.mark;
  var stick = ui.moveHome, tstick = ui.turnHome, r = ui.stickRadius;
  var steps = [
    [0.5, function () { Debug.capture("title", 180); }],
    [1.0, function () { touchAt("start", zoneCenter("view"), 1); touchAt("end", zoneCenter("view"), 1); }],
    [1.2, function () { check("tap starts game", mode === "playing", mode); m.px = S[LS.px]; m.py = S[LS.py];
                        check("no FIRE/USE zones", !ui.zones.fire && !ui.zones.use, Object.keys(ui.zones).join(","));
                        check("left stick zone is move", ui.zoneAt(stick) === "move", ui.zoneAt(stick));
                        check("right stick zone is turn", ui.zoneAt(tstick) === "turnstick", ui.zoneAt(tstick)); }],
    [1.3, function () { touchAt("start", stick, 2); }],
    [1.4, function () { touchAt("move", { x: stick.x, y: stick.y + 0.8 * r }, 2); }],
    [1.6, function () { check("first move starts run recording", recorder.on || !recorder.ok, recorder.ok ? "recording" : "VideoRecorder unavailable"); }],
    [3.0, function () { touchAt("end", { x: stick.x, y: stick.y + 0.8 * r }, 2);
                        var d = Math.abs(S[LS.px] - m.px) + Math.abs(S[LS.py] - m.py);
                        check("stick moves player", d > 64, "moved " + d); m.ang = S[LS.angle]; }],
    // right stick: hold 80% right for 0.5 s -> turns right
    [3.1, function () { touchAt("start", tstick, 3); touchAt("move", { x: tstick.x + 0.8 * r, y: tstick.y }, 3); }],
    [3.6, function () { touchAt("end", { x: tstick.x + 0.8 * r, y: tstick.y }, 3); var da = (m.ang - S[LS.angle] + 360) % 360;
                        check("right stick turns right", da > 20 && da < 180, "turned " + da); m.ang = S[LS.angle]; }],
    [3.7, function () { m.fc = zoneCenter("face"); touchAt("start", m.fc, 9); }],
    [3.8, function () { touchAt("move", { x: m.fc.x + ui.width * 0.2, y: m.fc.y }, 9); touchAt("end", { x: m.fc.x + ui.width * 0.2, y: m.fc.y }, 9); }],
    [4.1, function () { var da = (m.ang - S[LS.angle] + 360) % 360; check("drag on face turns right", da > 20 && da < 180, "turned " + da); }],
    [4.15, function () { var fz = ui.zones.face, ff = ui.fullFrame, vw = ui.view.x1 - ui.view.x0, vh = ui.view.y1 - ui.view.y0;
                         check("face pushed down >= 50% of its height", ui.hud.y0 - fz.y1 >= 0.5 * (fz.y1 - fz.y0) - 1, "gap " + (ui.hud.y0 - fz.y1).toFixed(0) + " face h " + (fz.y1 - fz.y0).toFixed(0));
                         check("face stays on screen", fz.y0 >= ff.y0 - 1, "face bottom " + fz.y0.toFixed(0) + " screen bottom " + ff.y0.toFixed(0));
                         check("3D view taller than half, not squashed", vh > 500 && Math.abs(vw / vh - 4 / 3) < 0.01,
                               "view " + vw.toFixed(0) + "x" + vh.toFixed(0) + " crop " + (ui.crop * 100).toFixed(0) + "%");
                         check("sticks pinned to screen edges", ui.moveHome.x - ui.stickRadius - ff.x0 < ui.width * 0.03 && ff.x1 - ui.turnHome.x - ui.stickRadius < ui.width * 0.03,
                               "move " + ui.moveHome.x.toFixed(0) + " turn " + ui.turnHome.x.toFixed(0) + " frame " + ff.x0.toFixed(0) + ".." + ff.x1.toFixed(0)); }],
    // auto-fire: face a zombieman; nothing pressed
    // place at the start, facing east down the open corridor, then spawn a zombieman ahead
    [4.2, function () { doom._lens_test_place(-416, 256, 0); }],
    [4.25, function () { m.ammo = S[LS.ammo]; m.kills = S[LS.kills]; doom._lens_test_spawn_enemy(256); }],
    [6.2, function () { check("auto-fire kills enemy that appears in crosshair", S[LS.kills] > m.kills && S[LS.ammo] < m.ammo,
                              "kills " + m.kills + "->" + S[LS.kills] + " ammo " + m.ammo + "->" + S[LS.ammo]); Debug.capture("autofire", 180); }],
    // auto-use: face a closed door
    [6.3, function () { m.uses = S[LS.autouse_count]; doom._lens_test_place(504, 712, 0); }],
    [7.5, function () { check("auto-use opens door", S[LS.autouse_count] > m.uses && doom._lens_test_ceiling(552, 712) > -128,
                              "uses " + S[LS.autouse_count] + " ceiling " + doom._lens_test_ceiling(552, 712)); }],
    // pain, death, retry
    [7.6, function () { m.hp = S[LS.health]; doom._lens_test_damage(55); }],
    [8.0, function () { check("damage lowers health", S[LS.health] <= m.hp - 55, m.hp + " -> " + S[LS.health]); }],
    [9.1, function () { check("face shows pain tier", S[LS.faceindex] >= 8 && S[LS.faceindex] < 40, "faceindex " + S[LS.faceindex] + " hp " + S[LS.health]);
                        Debug.capture("hurt", 180); }],
    [9.2, function () { doom._lens_test_damage(200); }],
    [10.0, function () { m.rb = face.readbacks; }],
    [11.4, function () { check("death enters dead mode", mode === "dead" && S[LS.dead] === 1, mode);
                         check("dead face", S[LS.faceindex] === 41, "faceindex " + S[LS.faceindex]);
                         check("dead face is frozen (no camera readbacks while dead)", face.readbacks === m.rb, "readbacks " + m.rb + " -> " + face.readbacks);
                         check("TAP TO RETRY overlay shown", overlayText === "^^TAP TO RETRY", String(overlayText)); Debug.capture("dead", 180); }],
    [11.6, function () { touchAt("start", zoneCenter("view"), 7); touchAt("end", zoneCenter("view"), 7); }],
    [12.0, function () { check("tap retries", mode === "playing" && S[LS.health] === 100, mode + " hp " + S[LS.health]); }],
    // weapons via HUD strip
    [12.2, function () { typeCheat("idkfa"); }],
    [13.0, function () { // HUD cell "3" (shotgun): arms grid column 1, row 0 (lens_hud.c: x0 72, cw 32, y0 3, ch 17)
                         var p = ui.hudPoint(72 + 32 + 15, 3 + 8);
                         touchAt("start", p, 6); touchAt("end", p, 6); }],
    [14.2, function () { check("HUD tap selects weapon", S[LS.readyweapon] === 2, "weapon " + S[LS.readyweapon]); Debug.capture("armed", 180); }],
    // a new run: move (starts recording), exit -> end card with replay -> play again
    [14.3, function () { touchAt("start", stick, 10); touchAt("move", { x: stick.x, y: stick.y + 0.8 * r }, 10); }],
    [15.5, function () { touchAt("end", { x: stick.x, y: stick.y + 0.8 * r }, 10); doom._lens_test_exit(); }],
    [16.5, function () { check("exit shows end card", mode === "done", mode);
                         check("end card shows run replay", !!recorder.replay || !recorder.ok, recorder.ok ? (recorder.replay ? "replay" : "none") : "VideoRecorder unavailable");
                         Debug.capture("endcard", 180);
                         var st = global.persistentStorageSystem.store;
                         check("best time stored", st.has("bestTime"), "bestTime " + (st.has("bestTime") ? st.getInt("bestTime") : "none")); }],
    [17.0, function () { touchAt("start", zoneCenter("view"), 8); touchAt("end", zoneCenter("view"), 8); }],
    [17.4, function () { check("tap plays again", mode === "playing" && !S[LS.level_done], mode); }],
    [17.5, function () { log("AUTOTEST DONE pass " + auto.pass + " fail " + auto.fail); }],
  ];
  while (auto.step < steps.length && t >= steps[auto.step][0]) { steps[auto.step][1](); auto.step++; }
}
function typeCheat(s) { for (var i = 0; i < s.length; i++) tapKey(s.charCodeAt(i)); }

function update() {
  if (!doom || paused) return;
  var u0 = nowNs();
  try { updateInner(); } finally {
    perf.updNs += nowNs() - u0;
    perf.updates++;
    var now = getTime();
    if (perf.lastFrameT) perf.frameGapSum += now - perf.lastFrameT;
    perf.lastFrameT = now;
  }
}
function updateInner() {
  autopilot();
  face.update();
  if (mode === "playing" || mode === "dead") {
    acc = Math.min(acc + getDeltaTime() * 1000, TIC_MS * 4);
    var n = Math.min(Math.floor(acc / TIC_MS), 3);
    var render = (frameNo++ % QUALITY[quality][1]) === 0;
    var t = nowNs();
    for (var i = 0; i < n; i++) {
      doom._lens_tick(render && i === n - 1 ? 1 : 0);
    }
    acc -= n * TIC_MS;
    perf.engineNs += nowNs() - t;
    perf.tics += n;
    if (state[LS.level_done] && mode !== "done") { releaseAll(); setMode("done"); }
    if (state[LS.dead] && mode === "playing") { deadSince = getTime(); setMode("dead"); recorder.finish(false); }
    if (mode === "dead" && getTime() - deadSince > 1.5) setOverlay("^^TAP TO RETRY", 90);
    if (!state[LS.dead] && mode === "dead") setMode("playing");
    if (n === 0 || !render) { audio.pump(); return; }
  }
  var t1 = nowNs();
  doom._lens_present();
  var t2 = nowNs();
  uploadScreen();
  uploadFace();
  uploadOverlay();
  recorder.tick();
  var t3 = nowNs();
  perf.presentNs += t2 - t1;
  perf.uploadNs += t3 - t2;
  audio.pump();
  audio.playMusic(mode === "playing");
  perf.frames++;
  reportPerf();
}

function reportPerf() {
  var now = getTime();
  if (!perf.t0) perf.t0 = now;
  var dt = now - perf.t0;
  if (dt < 5) return;
  var f = Math.max(perf.frames, 1);
  var engMs = perf.engineNs / 1e6 / f;
  if (CONFIG.DEBUG) log("perf fps " + (perf.frames / dt).toFixed(1) + " tics/s " + (perf.tics / dt).toFixed(1) +
      " engine " + engMs.toFixed(2) + "ms present " + (perf.presentNs / 1e6 / f).toFixed(2) +
      "ms upload " + (perf.uploadNs / 1e6 / f).toFixed(2) + "ms face " + (perf.faceNs / 1e6 / f).toFixed(2) +
      "ms audio " + (perf.audioNs / 1e6 / f).toFixed(2) + "ms | lens frame " + (1000 * perf.frameGapSum / Math.max(perf.updates - 1, 1)).toFixed(1) +
      "ms script " + (perf.updNs / 1e6 / Math.max(perf.updates, 1)).toFixed(2) + "ms quality " + quality + " mode " + mode);
  // Adaptive quality: keep engine time per frame under ~22 ms (30 fps with headroom).
  if (mode === "playing") {
    if (engMs > 22 && quality < QUALITY.length - 1) setQuality(quality + 1);
    else if (engMs < 9 && quality > 0) setQuality(quality - 1);
  }
  perf.frames = perf.tics = perf.engineNs = perf.presentNs = perf.uploadNs = perf.faceNs = perf.audioNs = 0;
  perf.updNs = perf.updates = perf.frameGapSum = 0;
  perf.t0 = now;
}

function setQuality(q) {
  quality = q;
  doom._lens_set_view(11, QUALITY[q][0]);
  log("quality -> " + q + " (detail " + QUALITY[q][0] + ", render every " + QUALITY[q][1] + ")");
}

// ---------------------------------------------------------------- setup
function setup() {
  global.touchSystem.touchBlocking = true;
  var screenTex = ProceduralTextureProvider.createWithFormat(OUT_W, 200, TextureFormat.RGBA8Unorm);
  screenProv = screenTex.control;
  var hudTex = ProceduralTextureProvider.createWithFormat(OUT_W, 40, TextureFormat.RGBA8Unorm);
  hudProv = hudTex.control;
  var overlayTex = ProceduralTextureProvider.createWithFormat(320, 200, TextureFormat.RGBA8Unorm);
  overlayProv = overlayTex.control;
  var faceTex = ProceduralTextureProvider.createWithFormat(FACE_W, FACE_H, TextureFormat.RGBA8Unorm);
  faceProv = faceTex.control;
  ui = UI.build(script.getSceneObject(), screenTex, hudTex, faceTex, overlayTex);
  recorder.camera(false);
  log("game camera " + (ui.gameCamera ? "ok" : "missing (replay records nothing)"));
  face.init();
  script.createEvent("FaceFoundEvent").bind(function () { face.lastSeen = getTime(); });
  script.createEvent("UpdateEvent").bind(function () {
    // Keep face presence fresh while tracked (FaceFound fires once per acquisition).
    if (face.tracking) face.lastSeen = getTime();
    update();
  });
  script.createEvent("FaceFoundEvent").bind(function () { face.tracking = true; });
  script.createEvent("FaceLostEvent").bind(function () { face.tracking = false; });
  script.createEvent("TouchStartEvent").bind(function (e) { onTouch("start", e.getTouchId(), e.getTouchPosition()); });
  script.createEvent("TouchMoveEvent").bind(function (e) { onTouch("move", e.getTouchId(), e.getTouchPosition()); });
  script.createEvent("TouchEndEvent").bind(function (e) { onTouch("end", e.getTouchId(), e.getTouchPosition()); });
  script.createEvent("OnPauseEvent").bind(function () { paused = true; releaseAll(); });
  script.createEvent("OnResumeEvent").bind(function () { paused = false; acc = 0; });
  script.createEvent("CameraBackEvent").bind(function () { face.tracking = false; });
  // The lens can't press Snapchat's capture button, but when the user starts recording a Snap on
  // the title card, the run starts with it so the Snap captures the game from the first frame.
  script.createEvent("SnapRecordStartEvent").bind(function () {
    log("snap recording started");
    if (mode === "title") setMode("playing");
  });
  // Boot on the next frame so the loading card renders first.
  var d = script.createEvent("DelayedCallbackEvent");
  d.bind(boot);
  d.reset(0.05);
}

setup();
