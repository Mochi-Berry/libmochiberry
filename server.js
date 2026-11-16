#!/usr/bin/env node
/*
 * MochiBerry (libmochiberry) preview server.
 *
 * Zero-dependency Node server that powers the Freebuff preview for this
 * repository. It serves a small landing page and lets you run JavaScript
 * live through the freshly built `mbjs` engine (build/mbjs).
 *
 * Usage:
 *   node server.js            # listens on PORT (default 3000), bound to 0.0.0.0
 */
"use strict";

const http = require("http");
const path = require("path");
const fs = require("fs");
const { execFile } = require("child_process");

const PORT = Number(process.env.PORT || 3000);
const HOST = process.env.HOST || "0.0.0.0";
const MBJS = path.join(__dirname, "build", "mbjs");
const REPO = path.basename(__dirname);

function engineVersion() {
  try {
    const out = execFileSyncSafe(MBJS, ["--version"]);
    return out.split("\n")[0] || "MochiBerry";
  } catch {
    return "MochiBerry (not built yet)";
  }
}

function execFileSyncSafe(file, args) {
  const { execFileSync } = require("child_process");
  return execFileSync(file, args, { encoding: "utf8", timeout: 5000 });
}

const LANDING = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>MochiBerry · Live Preview</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    min-height: 100vh;
    font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    background: #0d1117;
    color: #e6edf3;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 48px 20px 64px;
  }
  main { width: 100%; max-width: 860px; }
  .badge {
    display: inline-block;
    padding: 4px 12px;
    border-radius: 999px;
    background: #1f6feb22;
    border: 1px solid #1f6feb55;
    color: #79b8ff;
    font-size: 12px;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    margin-bottom: 16px;
  }
  h1 { font-size: 40px; margin: 0 0 8px; letter-spacing: -0.02em; }
  h1 .bolt { color: #f0c674; }
  .version { color: #8b949e; margin: 0 0 28px; font-size: 14px; }
  p.lede { color: #c9d1d9; line-height: 1.65; max-width: 640px; margin: 0 0 32px; }
  p.lede code { background: #161b22; padding: 2px 6px; border-radius: 6px; color: #7ee787; }
  .panel {
    background: #161b22;
    border: 1px solid #30363d;
    border-radius: 12px;
    padding: 16px;
    margin-bottom: 24px;
  }
  .panel h2 { margin: 0 0 12px; font-size: 15px; color: #8b949e; font-weight: 600; }
  textarea {
    width: 100%;
    min-height: 130px;
    background: #0d1117;
    color: #e6edf3;
    border: 1px solid #30363d;
    border-radius: 8px;
    padding: 12px;
    font-family: inherit;
    font-size: 13px;
    line-height: 1.5;
    resize: vertical;
  }
  textarea:focus { outline: none; border-color: #1f6feb; }
  .row { display: flex; justify-content: space-between; align-items: center; margin-top: 12px; gap: 12px; flex-wrap: wrap; }
  button {
    background: #238636;
    color: #fff;
    border: none;
    border-radius: 8px;
    padding: 10px 22px;
    font-family: inherit;
    font-size: 14px;
    font-weight: 600;
    cursor: pointer;
  }
  button:hover { background: #2ea043; }
  button:disabled { background: #30363d; cursor: wait; }
  .hint { color: #8b949e; font-size: 12px; }
  pre.output {
    background: #0d1117;
    border: 1px solid #30363d;
    border-radius: 8px;
    padding: 12px;
    min-height: 80px;
    max-height: 320px;
    overflow: auto;
    font-size: 13px;
    line-height: 1.5;
    margin: 0;
    white-space: pre-wrap;
    word-break: break-word;
  }
  pre.output.error { border-color: #da3633aa; color: #ffa198; }
  .footer { color: #8b949e; font-size: 12px; margin-top: 40px; text-align: center; line-height: 1.8; }
  .footer a { color: #79b8ff; text-decoration: none; }
</style>
</head>
<body>
<main>
  <span class="badge">MochiBerry · embeddable JavaScript engine</span>
  <h1><span class="bolt">⚡</span> MochiBerry</h1>
  <p class="version" id="version">Checking engine…</p>
  <p class="lede">
    A small, embeddable ECMAScript engine written in C. This preview runs the
    engine compiled from this repository (<code>build/mbjs</code>) — type any
    JavaScript below and it will be executed by mbjs itself, not by Node.
  </p>
  <div class="panel">
    <h2>Run JavaScript with mbjs</h2>
    <textarea id="code" spellcheck="false">const fib = (n) => (n < 2 ? n : fib(n - 1) + fib(n - 2));
console.log("First 12 Fibonacci numbers:", Array.from({ length: 12 }, (_, i) => fib(i)).join(", "));
console.log("Engine check — BigInt:", 2n ** 64n);</textarea>
    <div class="row">
      <span class="hint" id="hint"></span>
      <button id="run">Run with mbjs ▸</button>
    </div>
  </div>
  <div class="panel">
    <h2>Output</h2>
    <pre class="output" id="output">// stdout of build/mbjs --std -e &lt;your code&gt; appears here</pre>
  </div>
  <div class="footer">
    <p>
      <a href="https://github.com/gryzzlaycutzy121/libmochiberry">Documentation</a> ·
      <a href="https://github.com/mbjs-ng/mbjs">Source</a> ·
      Repository: <code>${REPO}</code>
    </p>
  </div>
</main>
<script>
  const $ = (id) => document.getElementById(id);
  fetch("/api/version")
    .then((r) => r.text())
    .then((v) => { $("version").textContent = v; })
    .catch(() => { $("version").textContent = "MochiBerry"; });

  $("run").addEventListener("click", async () => {
    const btn = $("run"), out = $("output"), hint = $("hint");
    btn.disabled = true;
    hint.textContent = "executing…";
    out.className = "output";
    try {
      const res = await fetch("/api/run", {
        method: "POST",
        headers: { "Content-Type": "text/plain" },
        body: $("code").value,
      });
      const text = await res.text();
      out.textContent = text || "(no output)";
      if (!res.ok) out.className = "output error";
    } catch (e) {
      out.textContent = "Request failed: " + e;
      out.className = "output error";
    } finally {
      btn.disabled = false;
      hint.textContent = "";
    }
  });
</script>
</body>
</html>`;

function runEngine(code) {
  return new Promise((resolve) => {
    execFile(
      MBJS,
      ["--std", "-e", code],
      { timeout: 5000, maxBuffer: 1024 * 1024 },
      (err, stdout, stderr) => {
        let body = "";
        let timedOut = false;
        if (err) {
          if (err.killed) timedOut = true;
          else body = String(stderr || err.message);
        }
        const text = timedOut
          ? "Execution timed out after 5 seconds (infinite loop?)."
          : (body || stdout).slice(0, 100_000);
        resolve({ text, ok: !err || timedOut === false ? !err : false });
      }
    );
  });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || "localhost"}`);

  if (req.method === "GET" && (url.pathname === "/" || url.pathname === "/index.html")) {
    res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
    res.end(LANDING);
    return;
  }

  if (req.method === "GET" && url.pathname === "/api/version") {
    res.writeHead(200, { "Content-Type": "text/plain; charset=utf-8" });
    res.end(engineVersion());
    return;
  }

  if (req.method === "GET" && url.pathname === "/healthz") {
    res.writeHead(200, { "Content-Type": "text/plain" });
    res.end("ok");
    return;
  }

  if (req.method === "POST" && url.pathname === "/api/run") {
    let body = "";
    req.on("data", (c) => { body += c; if (body.length > 64 * 1024) req.destroy(); });
    req.on("end", async () => {
      const { text, ok } = await runEngine(body.slice(0, 16 * 1024));
      res.writeHead(ok ? 200 : 400, { "Content-Type": "text/plain; charset=utf-8" });
      res.end(text);
    });
    return;
  }

  res.writeHead(404, { "Content-Type": "text/plain" });
  res.end("Not found");
});

server.listen(PORT, HOST, () => {
  console.log(`MochiBerry preview listening on http://${HOST}:${PORT}`);
  console.log(`Engine: ${MBJS} (${fs.existsSync(MBJS) ? "built" : "not built — run make first"})`);
});
