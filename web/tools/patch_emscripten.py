# Local fixes to the Emscripten SDK, applied by build.sh before linking. Idempotent.
#
# 1) libglemu.js (legacy GL emulation): the regex that finds texture loads in generated
#    fixed-function shaders stops at the first ')', so with a texture matrix active,
#    texture2D(u, (u_textureMatrix0 * v_texCoord0).xy) is cut in half, the shader fails to
#    compile and everything drawn with GL_COMBINE (AssaultCube's world) turns black.
#
# 2) libsockfs.js (sockets over WebSocket): tag every socket's WebSocket URL with its protocol
#    (?proto=udp|tcp) so the local relay (web/serve.mjs) knows whether to bridge it to UDP or
#    TCP. Emscripten only announces UDP sockets that were explicitly bound, and AssaultCube's
#    ping socket is not.
import sys

lib = sys.argv[1] + '/upstream/emscripten/src/lib/'

f = lib + 'libglemu.js'
s = open(f, encoding='utf-8').read()
if 'patched: nested parens' not in s:
    good = r'/(texture\w*\((?:[^()]|\([^()]*\))*\))/g; // patched: nested parens'
    a = r'var texLoadRegex = /(texture.*?\(.*?\))/g;'
    b = r'texLoadRegex = /(texture.*\(.*\))/g;'
    assert a in s and b in s, 'libglemu.js changed; review the patch'
    s = s.replace(a, 'var texLoadRegex = ' + good).replace(b, 'texLoadRegex = ' + good)
    open(f, 'w', encoding='utf-8').write(s)
    print('patched', f)

f = lib + 'libsockfs.js'
s = open(f, encoding='utf-8').read()
if 'patched: proto marker' not in s:
    anchor = "url = url + parts[0] + ':' + port + '/' + parts.slice(1).join('/');\n            }\n"
    assert anchor in s, 'libsockfs.js changed; review the patch'
    marker = ("            url += (url.indexOf('?') < 0 ? '?' : '&') + 'proto=' + "
              "(sock.type === {{{ cDefs.SOCK_DGRAM }}} ? 'udp' : 'tcp'); // patched: proto marker\n")
    s = s.replace(anchor, anchor + marker, 1)
    open(f, 'w', encoding='utf-8').write(s)
    print('patched', f)
