// minidoom UI: runtime-built screen layout under the Safe Render region.
// Coordinates: "region px" with origin at the region centre, y up, region height = 1000.
//
//   [  3D view (cropped)  ]   320x200 at Doom's 1.2 pixel aspect; on a phone it is wider than
//   [                     ]   the screen, so the sides are cut off (never squashed)
//   [     HUD strip       ]   320x40, full width, never cropped
//   (MOVE) [ your face ] (TURN)  56x62 Doom-palette face canvas in a bevelled stone bezel, pushed
//                             down FACE_PUSH of its height (the view grows by the same amount);
//                             it may run past the safe region's bottom into the full frame.
//                             The sticks are pinned to the full frame's left/right edges.
// Firing and using are automatic (engine assists), so there are no buttons.
// Background is the HUD panel gray (PLAYPAL 109 = 47,47,47).

var VIEW_ASPECT = 320 / (200 * 1.2);   // 4:3
var HUD_ASPECT = 320 / (40 * 1.2);
var VIEW_FRAC = 0.5;                   // baseline 3D view height (fraction of the safe region)
var FACE_PUSH = 0.5;                   // face moves down this fraction of its height; view grows
var FACE_ASPECT = 56 / (62 * 1.2);
var BEZEL = 4;                         // bezel width in HUD (Doom) pixels

function makeCircleTex(size, r, g, b, a, ring) {
  var tex = ProceduralTextureProvider.createWithFormat(size, size, TextureFormat.RGBA8Unorm);
  var px = new Uint8Array(size * size * 4);
  var c = (size - 1) / 2;
  for (var y = 0; y < size; y++)
    for (var x = 0; x < size; x++) {
      var d = Math.sqrt((x - c) * (x - c) + (y - c) * (y - c)) / c;
      var inside = d <= 1 && (!ring || d >= 0.84);
      var edge = Math.max(0, Math.min(1, (1 - d) * c));     // 1px antialias
      var al = inside ? Math.min(1, edge) * a : 0;
      var i = (y * size + x) * 4;
      px[i] = r * al; px[i + 1] = g * al; px[i + 2] = b * al; px[i + 3] = 255 * al;   // premultiplied
    }
  tex.control.setPixels(0, 0, size, size, px);
  return tex;
}

// Rasterise a control icon: fn(u, v) -> [r, g, b, a] (0..255, 0..1), u/v in -1..1. 2x2 supersampled.
function iconTex(size, fn) {
  var tex = ProceduralTextureProvider.createWithFormat(size, size, TextureFormat.RGBA8Unorm);
  var px = new Uint8Array(size * size * 4);
  for (var y = 0; y < size; y++)
    for (var x = 0; x < size; x++) {
      var acc = [0, 0, 0, 0];
      for (var sy = 0; sy < 2; sy++)
        for (var sx = 0; sx < 2; sx++) {
          var c = fn(((x + 0.25 + sx * 0.5) / size) * 2 - 1, 1 - ((y + 0.25 + sy * 0.5) / size) * 2);
          if (!c) continue;
          acc[0] += c[0] * c[3]; acc[1] += c[1] * c[3]; acc[2] += c[2] * c[3]; acc[3] += c[3];
        }
      var i = (y * size + x) * 4;
      px[i] = acc[0] / 4; px[i + 1] = acc[1] / 4; px[i + 2] = acc[2] / 4; px[i + 3] = 255 * acc[3] / 4;   // premultiplied
    }
  tex.control.setPixels(0, 0, size, size, px);
  return tex;
}

// Triangle test: point (u, v) inside triangle pointing along (dx, dy) with tip at distance t from centre.
function inArrow(u, v, dx, dy, t, len, half) {
  var along = u * dx + v * dy, across = -u * dy + v * dx;
  var d = t - along;                       // distance back from the tip
  return d >= 0 && d <= len && Math.abs(across) <= half * d / len;
}

var STONE = [72, 72, 72], STONE_HI = [130, 130, 130], STONE_LO = [28, 28, 28], GOLD = [235, 190, 60];

// D-pad: a bevelled stone cross with four gold arrows. Reads as "move" without text.
function dpadTex(size) {
  var arm = 0.34;
  return iconTex(size, function (u, v) {
    var inX = Math.abs(v) <= arm && Math.abs(u) <= 0.98, inY = Math.abs(u) <= arm && Math.abs(v) <= 0.98;
    if (!inX && !inY) return null;
    for (var k = 0; k < 4; k++) {
      var dx = [1, -1, 0, 0][k], dy = [0, 0, 1, -1][k];
      if (inArrow(u, v, dx, dy, 0.9, 0.34, 0.24)) return GOLD.concat(1);
    }
    var edge = Math.min(inX ? arm - Math.abs(v) : 9, inY ? arm - Math.abs(u) : 9, 0.98 - Math.max(Math.abs(u), Math.abs(v)));
    if (edge < 0.06) return ((u + v) > 0 ? STONE_HI : STONE_LO).concat(0.95);
    return STONE.concat(0.9);
  });
}

// Turn control: a bevelled stone pill with left/right gold arrows.
function turnTex(size) {
  var hh = 0.36;
  return iconTex(size, function (u, v) {
    var cx = Math.max(Math.abs(u) - (0.98 - hh), 0);
    var d = Math.sqrt(cx * cx + v * v);
    if (d > hh) return null;
    if (inArrow(u, v, 1, 0, 0.9, 0.36, 0.26) || inArrow(u, v, -1, 0, 0.9, 0.36, 0.26)) return GOLD.concat(1);
    if (hh - d < 0.06) return (v > 0 ? STONE_HI : STONE_LO).concat(0.95);
    return STONE.concat(0.9);
  });
}

function solidTex(r, g, b, a) {
  var tex = ProceduralTextureProvider.createWithFormat(1, 1, TextureFormat.RGBA8Unorm);
  tex.control.setPixels(0, 0, 1, 1, new Uint8Array([r * a, g * a, b * a, 255 * a]));
  return tex;
}

function findByName(name) {
  function walk(o) {
    if (o.name === name) return o;
    for (var i = 0; i < o.getChildrenCount(); i++) {
      var r = walk(o.getChild(i));
      if (r) return r;
    }
    return null;
  }
  for (var i = 0; i < global.scene.getRootObjectsCount(); i++) {
    var r = walk(global.scene.getRootObject(i));
    if (r) return r;
  }
  return null;
}

function findCamera(o) {
  while (o) {
    var c = o.getComponent("Component.Camera");
    if (c) return c;
    o = o.getParent();
  }
  return null;
}

exports.findByName = findByName;

exports.build = function (root, screenTex, hudTex, faceTex, overlayTex) {
  var screenMat = requireAsset("../Materials/ScreenMat.mat");   // nearest filtering
  var uiMat = requireAsset("../Materials/UiMat.mat");           // bilinear
  var order = 10;
  var rootST = root.getComponent("Component.ScreenTransform");
  var cam = findCamera(root);

  function child(parent, name) {
    var o = global.scene.createSceneObject(name);
    o.setParent(parent);
    o.layer = parent.layer;
    var st = o.createComponent("Component.ScreenTransform");
    st.anchors = Rect.create(-1, 1, -1, 1);
    st.offsets = Rect.create(0, 0, 0, 0);
    return { obj: o, st: st };
  }
  function image(parent, name, tex, nearest) {
    var c = child(parent, name);
    var img = c.obj.createComponent("Component.Image");
    var mat = (nearest ? screenMat : uiMat).clone();
    img.clearMaterials();
    img.addMaterial(mat);
    mat.mainPass.baseTex = tex;
    img.stretchMode = StretchMode.Stretch;
    img.setRenderOrder(order++);
    c.img = img;
    return c;
  }
  function label(parent, name, size) {
    var c = child(parent, name);
    var t = c.obj.createComponent("Component.Text");
    t.size = size;
    t.sizeToFit = true;          // shrink to the box instead of overflowing
    t.textFill.color = new vec4(1, 0.85, 0.2, 1);
    t.setRenderOrder(order++);
    c.text = t;
    return c;
  }

  // Full-frame panel-gray backdrop (the camera feed only feeds the face).
  var bgRegion = findByName("Backdrop");
  var bg = bgRegion ? image(bgRegion, "PanelBG", solidTex(47, 47, 47, 1), true) : null;
  if (bg) bg.img.setRenderOrder(1);

  var game = image(root, "Game", screenTex, true);
  game.img.flipY = true;      // engine rows are top-down; setPixels row 0 is the texture bottom
  var hud = image(root, "Hud", hudTex, true);
  hud.img.flipY = true;
  var bezel = image(root, "FaceBezel", solidTex(0, 0, 0, 1), true);   // texture from the engine
  bezel.img.flipY = true;
  var face = image(root, "Face", faceTex, true);
  face.img.flipY = true;
  // Gameplay-only copy of the 3D view for the replay recording: rendered by "Game Camera" into
  // GameRT (see tools/ls_build_project.js), so the replay never contains the face or controls.
  var gameCanvas = findByName("Game Canvas");
  var gameCopy = gameCanvas ? image(gameCanvas, "GameCopy", screenTex, true) : null;
  if (gameCopy) gameCopy.img.flipY = true;
  var replay = image(root, "Replay", solidTex(0, 0, 0, 1), false);
  replay.obj.enabled = false;
  // Text overlay (title, end card, TAP TO RETRY): drawn by the engine in Doom's HUD font into a
  // transparent 320x200 layer, stacked exactly on the 3D view so it crops identically.
  var overlay = image(root, "Overlay", overlayTex, true);
  overlay.img.flipY = true;
  function stick(name, baseTex) {
    var base = image(root, name + "Base", baseTex, false);
    var knob = image(root, name + "Knob", makeCircleTex(64, 255, 255, 255, 0.45, false), false);
    base.img.setRenderOrder(1000);
    knob.img.setRenderOrder(1001);
    return { base: base, knob: knob, home: { x: 0, y: 0 } };
  }
  var sticks = { move: stick("Move", dpadTex(128)), turn: stick("Turn", turnTex(128)) };

  var L = { W: 0, H: 1000 };
  var api = { stickRadius: 60, zones: {}, width: 0, sticks: sticks, gameCamera: findByName("Game Camera") };

  // Region aspect from world-space corners: isotropic under the ortho camera on every device.
  // (Camera.aspect matched in Preview but not on a phone, which squashed the layout.)
  function regionPx() {
    var a = rootST.localPointToWorldPoint(new vec2(-1, -1));
    var b = rootST.localPointToWorldPoint(new vec2(1, 1));
    var w = Math.abs(b.x - a.x), h = Math.abs(b.y - a.y);
    return { w: 1000 * w / h, h: 1000 };
  }
  function place(st, x0, y0, x1, y1) {
    st.anchors = Rect.create(2 * x0 / L.W, 2 * x1 / L.W, 2 * y0 / L.H, 2 * y1 / L.H);
  }
  function rect(x0, y0, x1, y1) { return { x0: Math.min(x0, x1), y0: Math.min(y0, y1), x1: Math.max(x0, x1), y1: Math.max(y0, y1) }; }
  function inRect(p, r) { return p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1; }

  // Full frame (whole screen, including the areas under Snapchat's UI) in region px.
  function fullFrame() {
    var ffo = findByName("Backdrop");
    var st = ffo && ffo.getComponent("Component.ScreenTransform");
    if (!st) return { x0: -L.W / 2, x1: L.W / 2, y0: -L.H / 2, y1: L.H / 2 };
    var a = rootST.worldPointToLocalPoint(st.localPointToWorldPoint(new vec2(-1, -1)));
    var b = rootST.worldPointToLocalPoint(st.localPointToWorldPoint(new vec2(1, 1)));
    return rect(a.x * L.W / 2, a.y * L.H / 2, b.x * L.W / 2, b.y * L.H / 2);
  }

  function layout() {
    var R = regionPx();
    L.W = R.w; L.H = R.h;
    api.width = L.W;
    var halfW = L.W / 2, halfH = L.H / 2;
    var ff = fullFrame();
    api.fullFrame = ff;

    var hW = L.W * 0.96, hH = hW / HUD_ASPECT;
    var s = hW / 320;                       // region px per HUD pixel
    var bz = BEZEL * s, gap = 4 + bz;

    // Baseline: 3D view VIEW_FRAC tall at 4:3 (at least 92% wide), HUD under it, face under that
    // filling most of what's left of the safe region. This fixes the face size.
    var vH = VIEW_FRAC * L.H;
    if (vH * VIEW_ASPECT < L.W * 0.92) vH = L.W * 0.92 / VIEW_ASPECT;
    var fTop = halfH - vH - hH - gap;
    var fW = L.W * 0.6, fH = fW / FACE_ASPECT;
    var maxH = 0.82 * (fTop + halfH);
    if (fH > maxH) { fH = maxH; fW = fH * FACE_ASPECT; }

    // Push the face down FACE_PUSH of its height and give that space to the 3D view. The face may
    // leave the safe region but never the screen: if it would, it shrinks to fit.
    var push = FACE_PUSH * fH;
    vH += push; fTop -= push;
    var room = fTop - (ff.y0 + bz);
    if (fH > room) { fH = Math.max(room, 40); fW = fH * FACE_ASPECT; }
    api.facePush = push;

    // 3D view: top of the safe region. Wider than the region -> sides fall off the screen.
    var vW = vH * VIEW_ASPECT;
    var top = halfH, vBot = halfH - vH;
    place(game.st, -vW / 2, vBot, vW / 2, top);
    api.view = { x0: -vW / 2, x1: vW / 2, y0: vBot, y1: top };
    api.zones.view = rect(Math.max(-vW / 2, ff.x0), vBot, Math.min(vW / 2, ff.x1), top);
    api.crop = Math.max(0, (vW - (ff.x1 - ff.x0)) / vW);   // fraction of the view's width cut off

    // HUD strip: full width (unchanged look), directly under the view.
    var bottom = vBot - hH;
    place(hud.st, -hW / 2, bottom, hW / 2, vBot);
    api.hud = { x0: -hW / 2, x1: hW / 2, y0: bottom, y1: vBot };
    api.zones.hud = rect(-hW / 2, bottom, hW / 2, vBot);
    api.image = { x0: -hW / 2, x1: hW / 2, y0: bottom, y1: top };   // overall top block

    // Face in its bezel, centred.
    place(face.st, -fW / 2, fTop - fH, fW / 2, fTop);
    place(bezel.st, -fW / 2 - bz, fTop - fH - bz, fW / 2 + bz, fTop + bz);
    api.zones.face = rect(-fW / 2, fTop - fH, fW / 2, fTop);
    api.bezelPx = { w: Math.round(fW / s) + 2 * BEZEL, h: Math.round(fH / s) + 2 * BEZEL };

    // Twin sticks pinned to the screen's left/right edges (they may overlap the face), kept
    // vertically inside the safe region so Snapchat's bottom UI doesn't cover them.
    var r = Math.max(44, Math.min(L.W * 0.14, 90));
    api.stickRadius = r;
    var sy = Math.max(-halfH + r + 30, Math.min(bottom - r - 40, fTop - fH * 0.5));
    var margin = L.W * 0.02;
    var kr = r * 0.3;
    api.knobR = kr;
    function placeStick(stk, x) {
      stk.home = { x: x, y: sy };
      place(stk.base.st, x - r, sy - r, x + r, sy + r);
      place(stk.knob.st, x - kr, sy - kr, x + kr, sy + kr);
    }
    placeStick(sticks.move, ff.x0 + margin + r);
    placeStick(sticks.turn, ff.x1 - margin - r);
    api.moveHome = sticks.move.home;
    api.turnHome = sticks.turn.home;
    // Touch zones below the HUD: left part moves, right part turns, middle (face) drags to turn.
    api.zones.move = rect(ff.x0, ff.y0, -L.W * 0.17, bottom);
    api.zones.turnstick = rect(L.W * 0.17, ff.y0, ff.x1, bottom);

    // Replay (a recording of the 3D view only) and the text overlay both match the view exactly.
    var ox0 = Math.max(-vW / 2, ff.x0), ox1 = Math.min(vW / 2, ff.x1);
    place(replay.st, -vW / 2, vBot, vW / 2, top);
    place(overlay.st, -vW / 2, vBot, vW / 2, top);
    api.visibleCols = Math.round(320 * (ox1 - ox0) / vW);   // view columns actually on screen
  }

  api.toLocal = function (screenPt) {
    var l = rootST.screenPointToLocalPoint(screenPt);
    return { x: l.x * L.W / 2, y: l.y * L.H / 2 };
  };
  api.toScreen = function (p) {
    return rootST.localPointToScreenPoint(new vec2(p.x / (L.W / 2), p.y / (L.H / 2)));
  };
  // Touch zones: move stick, turn stick, HUD weapon taps; anything else drags to turn.
  api.zoneAt = function (p) {
    if (inRect(p, api.zones.hud)) return "hud";
    if (inRect(p, api.zones.move)) return "move";
    if (inRect(p, api.zones.turnstick)) return "turnstick";
    return "drag";
  };
  // Region px -> HUD pixel coords (0..320, 0..40 top-down)
  api.hudPixel = function (p) {
    var h = api.hud;
    return { x: Math.floor((p.x - h.x0) / (h.x1 - h.x0) * 320), y: Math.floor((h.y1 - p.y) / (h.y1 - h.y0) * 40) };
  };
  // HUD pixel -> region px (autotest taps)
  api.hudPoint = function (hx, hy) {
    var h = api.hud;
    return { x: h.x0 + (h.x1 - h.x0) * hx / 320, y: h.y1 - (h.y1 - h.y0) * hy / 40 };
  };
  // Show a stick's knob deflected by (dx, dy) region px (clamped to the radius); null resets.
  api.knobAt = function (which, dx, dy) {
    var s = sticks[which], kr = api.knobR, r = api.stickRadius;
    dx = dx || 0; dy = dy || 0;
    var d = Math.sqrt(dx * dx + dy * dy);
    if (d > r) { dx *= r / d; dy *= r / d; }
    var cx = s.home.x + dx, cy = s.home.y + dy;
    place(s.knob.st, cx - kr, cy - kr, cx + kr, cy + kr);
  };
  api.showReplay = function (tex) {
    if (tex) replay.img.mainPass.baseTex = tex;
    replay.obj.enabled = !!tex;
  };
  // Background texture from the engine (Doom status-bar stone), sized to the backdrop's aspect
  // at ~Doom pixel scale (192 px across).
  api.setBackground = function (makeBg) {
    if (!bg) return;
    var st = bg.obj.getParent().getComponent("Component.ScreenTransform");
    var a = st.localPointToWorldPoint(new vec2(-1, -1)), b = st.localPointToWorldPoint(new vec2(1, 1));
    var w = 192, h = Math.round(192 * Math.abs(b.y - a.y) / Math.abs(b.x - a.x));
    var tex = makeBg(w, h);
    if (tex) { bg.img.mainPass.baseTex = tex; bg.img.flipY = true; }
  };
  // Bezel around the face from the engine (stone panel + bevels at HUD pixel scale).
  api.setBezel = function (makeBezel) {
    var tex = makeBezel(api.bezelPx.w, api.bezelPx.h, BEZEL);
    if (tex) bezel.img.mainPass.baseTex = tex;
    else bezel.obj.enabled = false;
  };
  api.relayout = layout;
  layout();
  return api;
};
