import { readFileSync } from "node:fs";
import { WASI } from "node:wasi";

const [modulePath, ...arguments_] = process.argv.slice(2);
if (!modulePath) {
  throw new Error("usage: node run_wasi.mjs MODULE.wasm [ARG ...]");
}

const wasi = new WASI({
  version: "preview1",
  args: [modulePath, ...arguments_],
  env: {},
  preopens: {},
  returnOnExit: true,
});
const module = await WebAssembly.compile(readFileSync(modulePath));
const instance = await WebAssembly.instantiate(module, {
  wasi_snapshot_preview1: wasi.wasiImport,
});
process.exitCode = wasi.start(instance);
