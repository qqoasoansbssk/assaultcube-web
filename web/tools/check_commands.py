# Finds script commands whose C++ signature does not match how command.cpp calls them.
# On x86 such mismatches are harmless; in WebAssembly an indirect call with the wrong
# signature traps ("function signature mismatch").
import re, glob, os, sys

src = sys.argv[1]
files = glob.glob(os.path.join(src, '*.cpp')) + glob.glob(os.path.join(src, '*.h')) + glob.glob(os.path.join(src, 'bot', '*.cpp'))
text = {f: open(f, encoding='latin-1').read() for f in files}
alltext = '\n'.join(text.values())

def expected(sig):
    if 'v' in sig: return ['char**', 'int'], 'v'
    if 'c' in sig or 'w' in sig: return ['char*'], 'c'
    if 'd' in sig: return ['bool'], 'd'
    return ['ptr'] * len(sig), 'p'

def find_def(fun):
    # function definitions: return type, name, params, then '{'
    pat = re.compile(r'(^|\n)[ \t]*((?:static\s+|inline\s+|const\s+)*[\w:<>\*\s&]+?)\b' + re.escape(fun) + r'\s*\(([^()]*)\)\s*(?:const\s*)?\{')
    m = pat.search(alltext)
    if not m: return None
    return m.group(2).strip(), m.group(3).strip()

def parse_params(p):
    p = p.strip()
    if p in ('', 'void'): return []
    out = []
    for a in p.split(','):
        a = a.strip()
        a = re.sub(r'=.*$', '', a).strip()
        out.append(a)
    return out

def ok_param(a, kind):
    if kind == 'ptr': return '*' in a or '[' in a
    if kind == 'char**': return a.count('*') == 2
    if kind == 'char*': return '*' in a
    if kind == 'int': return re.match(r'(unsigned\s+)?int\b', a) is not None
    if kind == 'bool': return a.startswith('bool') or a.startswith('int')
    return False

bad = []
regs = []
for f, t in text.items():
    for m in re.finditer(r'\bCOMMANDN\(\s*(\w+)\s*,\s*(\w+)\s*,\s*"([^"]*)"\s*\)', t): regs.append((m.group(1), m.group(2), m.group(3), None, f))
    for m in re.finditer(r'\bCOMMAND\(\s*(\w+)\s*,\s*"([^"]*)"\s*\)', t): regs.append((m.group(1), m.group(1), m.group(2), None, f))
    for m in re.finditer(r'\bCOMMANDF\(\s*(\w+)\s*,\s*"([^"]*)"\s*,\s*\(([^)]*)\)', t): regs.append((m.group(1), None, m.group(2), ('void', m.group(3)), f))
    for m in re.finditer(r'addcommand\("([^"]+)",\s*\(void \(\*\)\(\)\)\s*(\w+),\s*"([^"]*)"\)', t): regs.append((m.group(1), m.group(2), m.group(3), None, f))

for name, fun, sig, inline, f in regs:
    d = inline or find_def(fun)
    if not d:
        print(f'?? {name} ({fun}) sig "{sig}" definition not found  [{os.path.basename(f)}]')
        continue
    ret, params = d
    ps = parse_params(params)
    exp, kind = expected(sig)
    problems = []
    if not re.search(r'\bvoid\s*$', ret) : problems.append(f'returns {ret!r}')
    if len(ps) != len(exp): problems.append(f'{len(ps)} params, called with {len(exp)}')
    else:
        for a, e in zip(ps, exp):
            if not ok_param(a, e): problems.append(f'param {a!r} vs {e}')
    if problems:
        bad.append(name)
        print(f'MISMATCH {name} (fun {fun}) sig "{sig}": ({params}) -> {ret}: ' + '; '.join(problems) + f'  [{os.path.basename(f)}]')
print(f'{len(regs)} commands checked, {len(bad)} mismatches', file=sys.stderr)
