// Runs before the game's main(): game arguments and persistent profile storage.
// pthread workers load this file too, so everything is guarded to the main page.
if (typeof window !== "undefined") {
// Chrome starts every AudioContext suspended until the user interacts with the page:
// remember the game's contexts (OpenAL creates them) and resume them on the first click/key.
(function () {
  var Native = window.AudioContext || window.webkitAudioContext;
  if (!Native || Native.__acTracked) return;
  var contexts = [];
  function Tracked(opts) { var c = new Native(opts); contexts.push(c); return c; }
  Tracked.prototype = Native.prototype;
  Tracked.__acTracked = true;
  window.AudioContext = Tracked;
  if (window.webkitAudioContext) window.webkitAudioContext = Tracked;
  function resumeAll() {
    contexts.forEach(function (c) { if (c.state === 'suspended') c.resume().catch(function () {}); });
  }
  ['pointerdown', 'keydown', 'touchstart'].forEach(function (ev) { window.addEventListener(ev, resumeAll, true); });
})();

// Networking: Emscripten turns each UDP/TCP peer of the game into a WebSocket to
// ws://host:port/?proto=udp|tcp (see web/tools/patch_emscripten.py). Browsers cannot open raw
// sockets, so those connections are sent to the relay of the page's own server (web/serve.mjs),
// which forwards them to the real host over UDP or TCP.
(function () {
  var Native = window.WebSocket;
  if (!Native || Native.__acRelay) return;
  function Relayed(url, protocols) {
    var m = /^wss?:\/\/([^\/?]+)\/?\?proto=(udp|tcp)$/.exec(String(url));
    if (m) {
      url = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host +
            '/relay?to=' + encodeURIComponent(m[1]) + '&proto=' + m[2];
    }
    return protocols === undefined ? new Native(url) : new Native(url, protocols);
  }
  Relayed.prototype = Native.prototype;
  ['CONNECTING', 'OPEN', 'CLOSING', 'CLOSED'].forEach(function (k) { Relayed[k] = Native[k]; });
  Relayed.__acRelay = true;
  window.WebSocket = Relayed;
})();

// SDL asks for pointer lock on its own; when the page is not focused (e.g. DevTools has focus)
// the browser rejects it. That is harmless, so swallow the rejection instead of logging it as uncaught.
(function () {
  var req = Element.prototype.requestPointerLock;
  if (!req || req.__acWrapped) return;
  Element.prototype.requestPointerLock = function () {
    try {
      var r = req.apply(this, arguments);
      if (r && typeof r.catch === 'function') r.catch(function () {});
      return r;
    } catch (e) { /* ignore */ }
  };
  Element.prototype.requestPointerLock.__acWrapped = true;
})();

// window size is passed on the command line so the game renders at the page's resolution
Module['arguments'] = Module['arguments'] || ['--home=/home/web', '--init', '-t0',
  '-w' + Math.max(640, window.innerWidth), '-h' + Math.max(480, window.innerHeight)];
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  FS.mkdirTree('/home/web');
  FS.mount(IDBFS, {}, '/home/web');
  addRunDependency('syncfs');
  FS.syncfs(true, function (err) {
    if (err) console.warn('profile load failed', err);
    removeRunDependency('syncfs');
  });
  // save the profile (configs, screenshots, demos) every few seconds and when leaving
  var saving = false;
  function save() {
    if (saving) return; saving = true;
    FS.syncfs(false, function (err) { saving = false; if (err) console.warn('profile save failed', err); });
  }
  Module['saveProfile'] = save;
  // write the game's config files, then flush them, whenever the tab is left
  function saveAll() {
    if (Module['_web_saveall'] && Module['calledRun']) { try { Module['_web_saveall'](); } catch (e) { console.warn(e); } }
    else save();
  }
  document.addEventListener('visibilitychange', function () { if (document.visibilityState === 'hidden') saveAll(); });
  window.addEventListener('pagehide', saveAll);
});
}
