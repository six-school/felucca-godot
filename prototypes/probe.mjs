import fs from "fs";
const bytes = fs.readFileSync(process.argv[2]);
const mod = await WebAssembly.compile(bytes);
console.log("imports:", WebAssembly.Module.imports(mod).length);
console.log("exports:", WebAssembly.Module.exports(mod).map(e => e.name).filter(n => n.startsWith("web_")).join(" "));
async function boot() {
  const { exports: ex } = await WebAssembly.instantiate(mod, {});
  ex._initialize(); ex.web_nor_erase(); ex.web_master(700); ex.web_boot();
  return ex;
}
const a = await boot(), b = await boot();          // two independent instances in one process
const run = (ex, ms, f) => { let peak = 0; for (let k = 0; k < ms * 44.1; k += 128) { ex.web_render(128);
  const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), 128); for (const v of l) peak = Math.max(peak, Math.abs(v)); } return peak; };
const t0 = performance.now();
run(a, 1500); run(b, 1500);                        // past the splash
a.web_midi(0x90, 60, 100);                          // note on to instance A only
const pa = run(a, 500), pb = run(b, 500);
const dt = performance.now() - t0;
console.log(`rate ${a.web_sample_rate()}  peakA ${pa.toFixed(3)}  peakB ${pb.toFixed(3)}  screenDrawsA ${a.web_screen_draws()}`);
console.log(`4.0 s of device time (2 instances x 2 s) rendered in ${dt.toFixed(0)} ms`);
