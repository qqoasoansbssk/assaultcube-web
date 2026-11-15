// Local server for the browser build of AssaultCube.
//
//  - Serves web/dist with the cross-origin isolation headers that WebAssembly threads
//    (SharedArrayBuffer) require.
//  - /relay  WebSocket <-> UDP/TCP bridge. Browsers cannot open raw sockets; every socket peer of
//            the game arrives here as ws://<this host>/relay?to=<host:port>&proto=udp|tcp
//            (see web/pre.js) and is forwarded to the real game server / master server.
//  - /master returns the public server list (the game's own master query blocks the page).
//  - /profile receives results of the ?profile sampling mode (saved to web/profiles/).
//
// Usage: node serve.mjs [port]   (or: npm start, or start.bat on Windows)
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import net from 'node:net';
import dgram from 'node:dgram';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, 'public');
const profilesDir = path.join(here, 'profiles');
const port = Number(process.argv[2] || process.env.PORT || 8080);
const types = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript',
  '.wasm': 'application/wasm', '.data': 'application/octet-stream', '.json': 'application/json',
  '.png': 'image/png', '.ico': 'image/x-icon',
};

const MASTER_HOST = 'ms.cubers.net';
const MASTER_PORT = 28760;

// ---- relay -----------------------------------------------------------------------
// Only plain host:port targets; never broadcast, multicast or unspecified addresses
// (ENet's LAN discovery broadcasts to 255.255.255.255 and is rejected here).
function parseTarget(to) {
  const m = /^([A-Za-z0-9.-]{1,253}):(\d{1,5})$/.exec(to || '');
  if (!m) return null;
  const host = m[1], p = Number(m[2]);
  if (p < 1 || p > 65535) return null;
  if (/^(0\.|255\.255\.255\.255$|22[4-9]\.|23\d\.)/.test(host)) return null;
  return { host, port: p };
}

// Emscripten announces a bound UDP socket's local port with this message; it is not game data.
function isPortMessage(buf) {
  return buf.length === 10 && buf[0] === 255 && buf[1] === 255 && buf[2] === 255 && buf[3] === 255 &&
    buf.toString('latin1', 4, 8) === 'port';
}

function relayUdp(ws, target) {
  const sock = dgram.createSocket('udp4');
  let closed = false;
  const close = () => {
    if (closed) return;
    closed = true;
    try { sock.close(); } catch {}
    try { ws.close(); } catch {}
  };
  sock.on('message', (msg, rinfo) => {
    if (rinfo.port !== target.port || ws.readyState !== ws.OPEN) return;
    ws.send(msg);
  });
  sock.on('error', close);
  ws.on('message', (data) => {
    const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
    if (isPortMessage(buf)) return;
    sock.send(buf, target.port, target.host, (err) => { if (err) close(); });
  });
  ws.on('close', close);
  ws.on('error', close);
}

function relayTcp(ws, target) {
  const queue = [];
  let connected = false;
  const sock = net.connect(target.port, target.host, () => {
    connected = true;
    for (const b of queue) sock.write(b);
    queue.length = 0;
  });
  const close = () => {
    try { sock.destroy(); } catch {}
    try { ws.close(); } catch {}
  };
  sock.on('data', (d) => { if (ws.readyState === ws.OPEN) ws.send(d); });
  sock.on('end', close);
  sock.on('error', close);
  sock.setTimeout(120000, close);
  ws.on('message', (data) => {
    const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
    if (connected) sock.write(buf); else queue.push(buf);
  });
  ws.on('close', close);
  ws.on('error', close);
}

// ---- master server list ----------------------------------------------------------
// The game asks for the list with a blocking request, so the page freezes until it is answered.
// The list is therefore kept cached and refreshed in the background, and a fetch stops as soon
// as the master server goes quiet (it does not always close the connection promptly).
function fetchMasterList(name, version, build) {
  return new Promise((resolve) => {
    let data = '';
    let finished = false;
    let idle = null;
    const sock = net.connect(MASTER_PORT, MASTER_HOST, () => {
      sock.write(`list ${name} ${version} ${build}\n`);
    });
    const done = () => {
      if (finished) return;
      finished = true;
      clearTimeout(idle);
      sock.destroy();
      resolve(data);
    };
    sock.setEncoding('latin1');
    sock.on('data', (c) => {
      data += c;
      if (data.length > 1 << 20) return done();
      clearTimeout(idle);
      idle = setTimeout(done, 300);
    });
    sock.on('end', done);
    sock.on('error', done);
    sock.setTimeout(6000, done);
  });
}

const MASTER_TTL = 60000;
const masterCache = new Map(); // "version build" -> { list, time, pending }

function refreshMaster(version, build) {
  const key = `${version} ${build}`;
  const entry = masterCache.get(key) || { list: '', time: 0, pending: null };
  if (!entry.pending) {
    entry.pending = fetchMasterList('webclient', version, build).then((list) => {
      if (list.includes('addserver')) { entry.list = list; entry.time = Date.now(); }
      entry.pending = null;
      return entry.list;
    });
  }
  masterCache.set(key, entry);
  return entry;
}

// answers from cache when possible; only the very first request waits for the master server
async function getMasterList(version, build) {
  const entry = masterCache.get(`${version} ${build}`);
  if (entry && entry.list) {
    if (Date.now() - entry.time > MASTER_TTL) refreshMaster(version, build);
    return entry.list;
  }
  return refreshMaster(version, build).pending || '';
}

// ---- http ------------------------------------------------------------------------
const server = http.createServer(async (req, res) => {
  const u = new URL(req.url, 'http://x');

  if (req.method === 'POST' && u.pathname === '/profile') {
    const chunks = [];
    req.on('data', (c) => chunks.push(c));
    req.on('end', () => {
      fs.mkdirSync(profilesDir, { recursive: true });
      const out = path.join(profilesDir, `profile-${new Date().toISOString().replace(/[:.]/g, '-')}.json`);
      fs.writeFileSync(out, Buffer.concat(chunks));
      console.log('saved', out);
      res.writeHead(200).end('ok');
    });
    return;
  }

  if (u.pathname === '/master') {
    const version = String(parseInt(u.searchParams.get('version'), 10) || 0);
    const build = String(parseInt(u.searchParams.get('build'), 10) || 0);
    const list = await getMasterList(version, build);
    console.log(`master request version=${version} build=${build}`);
    console.log(`master list: ${list.split('\n').filter((l) => l.startsWith('addserver')).length} servers`);
    res.writeHead(200, {
      'Content-Type': 'text/plain; charset=latin1',
      'Cache-Control': 'no-store',
      'Cross-Origin-Resource-Policy': 'same-origin',
    });
    res.end(list);
    return;
  }

  const url = decodeURIComponent(u.pathname);
  let file = path.normalize(path.join(root, url));
  if (!file.startsWith(root)) { res.writeHead(403).end(); return; }
  if (fs.existsSync(file) && fs.statSync(file).isDirectory()) file = path.join(file, 'index.html');
  fs.stat(file, (err, st) => {
    if (err) { res.writeHead(404).end('not found'); return; }
    res.writeHead(200, {
      'Content-Type': types[path.extname(file)] || 'application/octet-stream',
      'Content-Length': st.size,
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
      'Cross-Origin-Resource-Policy': 'same-origin',
      'Cache-Control': 'no-cache',
      'Document-Policy': 'js-profiling', // allows the in-page sampling profiler (?profile)
    });
    fs.createReadStream(file).pipe(res);
  });
});

const wss = new WebSocketServer({
  noServer: true,
  // Emscripten asks for the "binary" subprotocol
  handleProtocols: (protocols) => (protocols.has('binary') ? 'binary' : false),
});

server.on('upgrade', (req, socket, head) => {
  const u = new URL(req.url, 'http://x');
  const target = u.pathname === '/relay' ? parseTarget(u.searchParams.get('to')) : null;
  const proto = u.searchParams.get('proto');
  if (!target || (proto !== 'udp' && proto !== 'tcp')) { socket.destroy(); return; }
  wss.handleUpgrade(req, socket, head, (ws) => {
    if (proto === 'udp') relayUdp(ws, target); else relayTcp(ws, target);
  });
});

server.listen(port, () => console.log(`AssaultCube web: http://localhost:${port}/`));

// warm the server-list cache for the release client (AC 1.3.0.2) so the first refresh is instant
refreshMaster('1302', '0');
