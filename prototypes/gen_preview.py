"""Naming rules a binding generator would apply to params.json (dumped from the firmware) -> GDScript-visible API."""
import json, re, sys
d = json.load(open(sys.argv[1]))

def ident(s):                       # an enum value label -> an UPPER_SNAKE identifier
    s = s.replace("&", " AND ").replace("+", " PLUS ").replace("/", "_")
    s = re.sub(r"[^A-Za-z0-9_]+", "_", s).strip("_").upper() or "NONE"
    return "R_" + s if s[0].isdigit() else s
def prop(page, label):              # page + label -> a snake_case property (the page keeps RATE apart from RATE)
    return re.sub(r"[^a-z0-9]+", "_", f"{page} {label}".lower()).strip("_")
def camel(s): return "".join(w.capitalize() for w in re.split(r"[^A-Za-z0-9]+", s) if w)

part, glob, seen = [], [], set()
for p in d["pages"]:
    for q in p["params"]:
        if not q["label"] or q["min"] == q["max"]: continue          # engine slots / fixed values: from the engines
        key = (p["scope"], q["id"])
        if key in seen: continue                                     # one property per parameter id
        seen.add(key)
        (glob if p["scope"] == "global" else part).append((p["page"], q))
def line(cls, page, q):
    name = prop(page, q["label"])
    if "names" in q:
        en = camel(f"{page} {q['label']}")
        return f"  {cls}.{name}: {cls}.{en}  = " + ", ".join(f"{cls}.{en}.{ident(n)}" for n in q["names"][:6]) + (" .." if len(q["names"]) > 6 else "")
    return f"  {cls}.{name}: int  # {q['min']}..{q['max']}"
print(f"Felucca {d['version']}: FeluccaPart {len(part)} properties, Felucca (song) {len(glob)} properties")
for page, q in part:
    if page in ("ARP", "ARP 2", "LFO", "ENV"): print(line("FeluccaPart", page, q))
for page, q in glob:
    if page in ("GLOBAL", "DLY"): print(line("Felucca", page, q))
engines = [e for e in d["engines"] if e["name"] != "-"]
print(f"{len(engines)} engine views: " + ", ".join("Felucca" + camel(e["name"].lower()) for e in engines))
a = next(e for e in engines if e["name"] == "ANALOG")
for q in a["params"]: print(line("FeluccaAnalog", "", q).replace(".__", ".").replace("._", "."))
