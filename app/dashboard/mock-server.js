const http = require("http");
const fs = require("fs");
const path = require("path");

const root = __dirname;
const port = Number(process.env.PORT || 8088);
const brain = process.env.BRAIN_HTTP || "mock";

const mime = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "application/javascript; charset=utf-8",
  ".md": "text/markdown; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".svg": "image/svg+xml",
};

function send(res, status, body, type = "text/plain; charset=utf-8") {
  res.writeHead(status, {
    "Content-Type": type,
    "Cache-Control": "no-store",
    "Access-Control-Allow-Origin": "*",
  });
  res.end(body);
}

function serveStatic(req, res) {
  const url = new URL(req.url, `http://localhost:${port}`);
  const pathname = url.pathname === "/" ? "/index.html" : url.pathname;
  const file = path.normalize(path.join(root, pathname));

  if (!file.startsWith(root)) {
    send(res, 403, "Forbidden");
    return;
  }

  fs.readFile(file, (error, data) => {
    if (error) {
      send(res, 404, "Not found");
      return;
    }
    send(res, 200, data, mime[path.extname(file)] || "application/octet-stream");
  });
}

function mockDetect(res) {
  const now = Date.now();
  const x = 80 + Math.round((Math.sin(now / 900) + 1) * 90);
  const infer = 132 + Math.round((Math.cos(now / 1300) + 1) * 24);
  send(
    res,
    200,
    JSON.stringify({
      w: 320,
      h: 240,
      infer_ms: infer,
      n: 1,
      boxes: [
        {
          name: "18650",
          s: 0.93,
          x1: x,
          y1: 78,
          x2: x + 112,
          y2: 136,
          a: -8.5,
          aniso: 0.82,
        },
      ],
    }),
    "application/json; charset=utf-8",
  );
}

function mockArmTarget(res) {
  send(
    res,
    200,
    JSON.stringify({
      valid: true,
      cx: 154.2,
      cy: 107.3,
      angle_deg: -8.5,
      score: 0.93,
      wrist_deg: null,
      w: 320,
      h: 240,
      frame_id: Math.floor(Date.now() / 600),
    }),
    "application/json; charset=utf-8",
  );
}

function mockStream(res) {
  const html = `
    <!doctype html>
    <html><body style="margin:0;display:grid;place-items:center;height:100vh;background:#f8fafc;font-family:Arial">
      <div style="width:100%;height:100%;display:grid;place-items:center;color:#2563eb">
        <div style="text-align:center">
          <div style="font-size:42px;font-weight:800">Mock Brain Stream</div>
          <div style="font-size:22px;color:#64748b;margin-top:10px">真实硬件时这里显示 :81/stream MJPEG</div>
        </div>
      </div>
    </body></html>`;
  send(res, 200, html, "text/html; charset=utf-8");
}

function proxyToBrain(req, res) {
  const incoming = new URL(req.url, `http://localhost:${port}`);
  const target = new URL(brain);
  const isStream = incoming.pathname === "/brain/stream";
  target.pathname = incoming.pathname.replace(/^\/brain/, "") || "/";
  target.search = incoming.search;
  if (isStream) target.port = "81";

  const proxyReq = http.request(target, { method: "GET" }, (proxyRes) => {
    res.writeHead(proxyRes.statusCode || 502, {
      ...proxyRes.headers,
      "Access-Control-Allow-Origin": "*",
      "Cache-Control": "no-store",
    });
    proxyRes.pipe(res);
  });

  proxyReq.on("error", (error) => {
    send(res, 502, `Brain proxy failed: ${error.message}`);
  });
  proxyReq.end();
}

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://localhost:${port}`);

  if (url.pathname.startsWith("/brain/")) {
    if (brain !== "mock") {
      proxyToBrain(req, res);
      return;
    }

    if (url.pathname === "/brain/detect") return mockDetect(res);
    if (url.pathname === "/brain/arm_target") return mockArmTarget(res);
    if (url.pathname === "/brain/stream") return mockStream(res);
    send(res, 404, "Unknown mock Brain endpoint");
    return;
  }

  serveStatic(req, res);
});

server.listen(port, () => {
  console.log(`ChargeReborn dashboard: http://localhost:${port}`);
  console.log(`Brain mode: ${brain === "mock" ? "mock" : `proxy ${brain}`}`);
});
