// Wrap the emcc output for the Lens scripting runtime (QuickJS-family, ES2021, no browser globals).
// usage: node tools/wrap_for_lens.js <doom.raw.js> <out.js>
// Fails the build if the output is not valid ES2021 (acorn, ecmaVersion 2021).
"use strict";
const fs = require("fs");
const path = require("path");

const EMSDK_NODE = "/opt/homebrew/Cellar/emscripten/7.0.0/libexec/node_modules";
const acorn = require(path.join(EMSDK_NODE, "acorn"));

const [, , inPath, outPath] = process.argv;
let js = fs.readFileSync(inPath, "utf8");

// 1. ES2022 class fields -> constructor assignments. emcc emits `class X{name="X";constructor(a){...`.
let rewrites = 0;
js = js.replace(/class (\w+)\{((?:\w+=(?:"[^"]*"|\w+);)+)constructor\(([^)]*)\)\{/g, (m, name, fields, args) => {
  rewrites++;
  const assigns = fields.split(";").filter(Boolean).map((f) => "this." + f + ";").join("");
  return `class ${name}{constructor(${args}){${assigns}`;
});

// 2. Host shims. Lens has print() but may lack console/setTimeout.
const prelude = `
var __g = (typeof globalThis !== "undefined") ? globalThis : this;
if (typeof __g.console === "undefined") { __g.console = { log: function(s){ print(String(s)); }, warn: function(s){ print(String(s)); }, error: function(s){ print(String(s)); } }; }
if (typeof __g.setTimeout === "undefined") { __g.setTimeout = function(f){ Promise.resolve().then(f); return 0; }; __g.clearTimeout = function(){}; }
`;
const out = prelude + js + "\n;if (typeof module !== 'undefined') { module.exports = createDoom; }\n";

// 3. Gate: must parse as ES2021 script.
try {
  acorn.parse(out, { ecmaVersion: 2021, sourceType: "script" });
} catch (e) {
  const ctx = out.slice(Math.max(0, e.pos - 80), e.pos + 80);
  console.error(`wrap_for_lens: output is not ES2021: ${e.message}\n...${ctx}...`);
  process.exit(1);
}
fs.writeFileSync(outPath, out);
console.log(`wrap_for_lens: ${rewrites} class-field rewrite(s), ${out.length} bytes, ES2021 OK`);
