// Debug-only: read back the lens's final render target and print it to the log as hex rows,
// so tools/decode_capture.py can rebuild a PNG of exactly what the Lens rendered.
// Only staged into debug builds (tools/build_all.sh --debug).

exports.capture = function (tag, maxW) {
  try {
    var tgt = global.scene.captureTarget;
    var pt = ProceduralTextureProvider.createFromTexture(tgt);
    var w = pt.getWidth(), h = pt.getHeight();
    var step = Math.max(1, Math.ceil(w / (maxW || 180)));
    var ow = Math.floor(w / step), oh = Math.floor(h / step);
    var row = new Uint8Array(w * 4);
    var hex = "0123456789abcdef";
    print("[capture] begin " + tag + " " + ow + "x" + oh + " src " + w + "x" + h);
    for (var y = 0; y < oh; y++) {
      pt.control.getPixels(0, h - 1 - y * step, w, 1, row);   // rows go upward: read top-down
      var s = "";
      for (var x = 0; x < ow; x++) {
        var i = x * step * 4;
        for (var c = 0; c < 3; c++) { var v = row[i + c]; s += hex[v >> 4] + hex[v & 15]; }
      }
      print("[capture] " + tag + " " + y + " " + s);
    }
    print("[capture] end " + tag);
  } catch (e) {
    print("[capture] failed " + e);
  }
};
