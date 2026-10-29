// A page in headless Chromium, its console printed (the page's and its workers': a threaded Godot's threads are
// workers) until a line matches --until or --timeout seconds pass (extension/run_web_tests.sh runs the tests with it).
// Node's own WebSocket speaks the DevTools protocol: no npm packages.
//   node tools/web_run.mjs URL [--until REGEX] [--timeout S] [--chromium PATH]
//   exit 0: --until matched (or, without it, the timeout reached); 1: a timeout before --until, or the page crashed
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const argv = process.argv.slice(2);
const opt = (name, def) => {
  const i = argv.indexOf(name);
  return i >= 0 ? argv.splice(i, 2)[1] : def;
};
const untilSource = opt("--until");
const until = untilSource ? new RegExp(untilSource) : null;
const timeout = Number(opt("--timeout", "60"));
const chromium = opt("--chromium", "chromium");
const url = argv[0];
if (!url) {
  console.error("usage: node tools/web_run.mjs URL [--until REGEX] [--timeout S] [--chromium PATH]");
  process.exit(2);
}

const profile = mkdtempSync(join(tmpdir(), "web_run-"));
const browser = spawn(chromium, [
  "--headless", "--no-first-run", "--no-default-browser-check", `--user-data-dir=${profile}`,
  "--remote-debugging-port=0", "--autoplay-policy=no-user-gesture-required",
  "--enable-unsafe-swiftshader", // (WebGL 2 without a GPU: Godot's web renderer)
  "about:blank",
], { stdio: ["ignore", "ignore", "pipe"] });

let done = false;
const finish = (code, why) => {
  if (done) return;
  done = true;
  if (why) console.log(`web_run: ${why}`);
  browser.kill("SIGKILL");
  rmSync(profile, { recursive: true, force: true });
  process.exit(code);
};
setTimeout(() => finish(until ? 1 : 0, `${timeout} s` + (until ? `, no line matched ${until}` : "")), timeout * 1000);

const endpoint = await new Promise((resolve, reject) => {
  let err = "";
  browser.stderr.on("data", (d) => {
    err += d;
    const m = err.match(/DevTools listening on (ws:\/\/\S+)/);
    if (m) resolve(m[1]);
  });
  browser.on("exit", (c) => reject(new Error(`chromium exited (${c}): ${err.trim()}`)));
}).catch((e) => finish(1, e.message));

const ws = new WebSocket(endpoint);
await new Promise((r) => ws.addEventListener("open", r, { once: true }));
let next = 1;
const waiting = new Map();
const send = (method, params = {}, sessionId) => new Promise((resolve, reject) => {
  const id = next++;
  waiting.set(id, { resolve, reject });
  ws.send(JSON.stringify({ id, method, params, sessionId }));
});
const text = (a) => a.value !== undefined ? String(a.value) : a.description ?? a.unserializableValue ?? a.type;
const line = (s) => {
  console.log(s);
  if (until && until.test(s)) finish(0);
};
ws.addEventListener("message", async (ev) => {
  const m = JSON.parse(ev.data);
  if (m.id && waiting.has(m.id)) {
    const w = waiting.get(m.id);
    waiting.delete(m.id);
    return m.error ? w.reject(new Error(m.error.message)) : w.resolve(m.result);
  }
  if (m.method === "Target.attachedToTarget") {     // (the page, then each worker as it starts)
    const s = m.params.sessionId;
    await send("Runtime.enable", {}, s);
    await send("Target.setAutoAttach", { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }, s).catch(() => {});
    await send("Runtime.runIfWaitingForDebugger", {}, s).catch(() => {});
  } else if (m.method === "Runtime.consoleAPICalled") {
    line(m.params.args.map(text).join(" "));
  } else if (m.method === "Runtime.exceptionThrown") {
    const d = m.params.exceptionDetails;
    line(`exception: ${d.exception?.description ?? d.text}`);
  } else if (m.method === "Inspector.targetCrashed") {
    finish(1, "the page crashed");
  }
});
const { targetId } = await send("Target.createTarget", { url: "about:blank" });
const { sessionId } = await send("Target.attachToTarget", { targetId, flatten: true });
await send("Runtime.enable", {}, sessionId);
await send("Inspector.enable", {}, sessionId).catch(() => {});
await send("Target.setAutoAttach", { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }, sessionId);
await send("Page.navigate", { url }, sessionId);
