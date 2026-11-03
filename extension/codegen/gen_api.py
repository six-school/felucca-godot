"""The typed API from the firmware's own tables: params.json (core/tools/dump_params.c) and the hand-kept names
(names.json) and descriptions (docs.json) -> C++ tables for extension/src/felucca_api.cpp and Godot class-reference XML.

    gen_api.py params.json names.json docs.json OUT_DIR [GODOT_CPP]
        -> OUT_DIR/felucca_api.gen.h, felucca_api.gen.cpp, doc_classes/*.xml (and with godot-cpp, doc_data.gen.cpp)

Rules: one property per parameter id, named after the page it is first on (page + label) or, on no page, its
label; names.json renames one by id, checked against the label it must still have (a Felucca update that relabels or
moves it fails here, not silently in a game). FeluccaPart: the track parameters below the engine's eight;
FeluccaSong: the globals the core lets code write; one view an engine (its eight, from edit[]), the retired slot "-"
left out. A parameter whose values are OFF and ON is a bool.

The part's and the song's enums are classes of their own, so a part's completion list holds its parameters: Felucca +
the property's name in camel case (names.json's enum_classes gives several properties one class; two classes of the
same values fail here), the labels their values, of the enum Value: ARP MODE's UP -> FeluccaArpMode.UP, a label
starting with a digit after R_ (1/16 -> FeluccaDivision.R_1_16). So are the parameters (FeluccaParam), the DRUM
lanes (FeluccaLane) and the engines (FeluccaEngine). An engine view keeps its enums, each value after its enum's name
as Godot's own (a class's constants share one namespace): FeluccaAnalog.Wave.WAVE_SAW.

docs.json describes every property, engine view and enum class (a value where its label says too little); a property
it leaves out, or a name of its that is no property, fails here. Each property's text ends with where it is on the
FM-1: its page and label, or docs.json's where for a parameter on no page.
"""
import json
import os
import re
import sys
from xml.sax.saxutils import escape


def snake(s):
    return re.sub(r"[^a-z0-9]+", "_", s.lower()).strip("_")


def camel(s):
    return "".join(w.capitalize() for w in re.split(r"[^A-Za-z0-9]+", s) if w)


def ident(s, prefix=""):                       # an enum value label -> UPPER_SNAKE, after the enum's name
    s = s.replace("&", " AND ").replace("+", " PLUS ").replace("/", "_").replace("#", " SHARP ")
    s = re.sub(r"[^A-Za-z0-9_]+", "_", s).strip("_").upper() or "NONE"
    return prefix + "_" + s if prefix else ("R_" + s if s[0].isdigit() else s)


class Fail(Exception):
    pass


def renamed(table, key, label, generated):
    """names.json's name for key, checked against the label the parameter must still have, else the generated one"""
    if key not in table:
        return generated
    name, want = table[key]
    if label != want:
        raise Fail(f"names.json: {key} is '{label}' now, not '{want}' (renamed to {name}): check what it is")
    return name


def enum_of(cls_enum_name, q):
    values, used = [], {}
    prefix = snake(re.sub(r"(?<=[a-z0-9])([A-Z])", r" \1", cls_enum_name)).upper()
    for k, label in enumerate(q["names"]):
        c = ident(label, prefix)
        used[c] = used.get(c, 0) + 1
        if used[c] > 1:
            c = f"{c}_{used[c]}"
        values.append((c, q["min"] + k, label))
    return {"name": cls_enum_name, "values": values}


def is_bool(q):
    return q["enum"] and q["names"] == ["OFF", "ON"] and q["min"] == 0


def bare_values(q):                             # an enum class's values: the labels alone (UP, R_1_16)
    values, used = [], {}
    for k, label in enumerate(q["names"]):
        c = ident(label)
        used[c] = used.get(c, 0) + 1
        if used[c] > 1:
            c = f"{c}_{used[c]}"
        values.append((c, q["min"] + k, label))
    return values


def enum_class(classes, names, name, values, user):
    """the class of an enum of the part's or the song's: names.json's enum_classes', else Felucca + its name; one
    class for every property naming it, all of the same values"""
    cls = names["enum_classes"].get(name, "Felucca" + name)
    if cls in classes and classes[cls]["values"] != values:
        raise Fail(f"names.json: {cls} is the class of enums of other values ({', '.join(classes[cls]['users'])}, {user})")
    classes.setdefault(cls, {"class": cls, "values": values, "users": []})["users"].append(user)
    return cls


def prop(name, q, enum_type, where):
    """a property: an int, a bool (OFF / ON) or of enum_type ("FeluccaArpMode.Value"); where: its page on the FM-1,
    None on no page"""
    kind = "bool" if is_bool(q) else "enum" if q["enum"] else "int"
    if kind == "enum":
        hint = ",".join(f"{label.replace(',', ' ').replace(':', ' ')}:{q['min'] + k}" for k, label in enumerate(q["names"]))
    else:
        hint = "" if kind == "bool" else f"{q['min']},{q['max']}"
    return {"name": name, "id": q["id"], "min": q["min"], "max": q["max"], "def": q["def"], "type": kind,
            "enum_type": enum_type, "hint": hint, "label": q["label"], "where": where}


def build(d, names):
    e0 = d["e0"]
    skip_pages, skip = set(names["skip_pages"]), set(names["skip"])
    # ---- the part: track parameters below P_E0
    gen, where = {}, {}
    for p in d["pages"]:
        if p["scope"] != "track" or p["page"] in skip_pages:
            continue
        for i in p["ids"]:
            q = d["track"][i]
            if i >= e0 or i in skip or i in gen or not q["label"] or q["min"] >= q["max"]:
                continue
            gen[i], where[i] = snake(p["page"] + " " + q["label"]), p["page"]
    for q in d["track"]:
        i = q["id"]
        if i < e0 and i not in skip and i not in gen and q["label"] and q["min"] < q["max"]:
            gen[i], where[i] = snake(q["label"]), None
    for key in names["part"]:
        if int(key) not in gen:
            raise Fail(f"names.json: part {key} is no parameter now")
    classes = {}                                # the enum classes, by name
    part = {"class": "FeluccaPart", "props": [], "enums": []}
    for i in sorted(gen):
        q = d["track"][i]
        name = renamed(names["part"], str(i), q["label"], gen[i])
        et = None
        if q["enum"] and not is_bool(q):
            et = enum_class(classes, names, camel(name), bare_values(q), "FeluccaPart." + name) + ".Value"
        part["props"].append(prop(name, q, et, where[i]))
    # ---- the song: the globals the core takes a write of
    song = {"class": "FeluccaSong", "props": [], "enums": []}
    gpage = {}
    for p in d["pages"]:
        if p["scope"] == "global":
            for i in p["ids"]:
                gpage.setdefault(i, p["page"])
    for i in d["global_writable"]:
        q = d["global"][i]
        g = snake((gpage[i] + " " if i in gpage else "") + q["label"])
        name = renamed(names["song"], str(i), q["label"], g)
        et = None
        if q["enum"] and not is_bool(q):
            et = enum_class(classes, names, camel(name), bare_values(q), "FeluccaSong." + name) + ".Value"
        song["props"].append(prop(name, q, et, gpage.get(i)))
    # ---- one view an engine
    views = []
    for e in d["engines"]:
        if e["name"] == "-":
            continue
        table = names["engines"].get(e["name"], {})
        v = {"class": "Felucca" + camel(e["name"].lower()), "engine": e["index"], "label": e["name"],
             "access": snake(e["name"]), "props": [], "enums": []}
        seen = {}
        for k, q in enumerate(e["params"]):
            if not q["label"] or q["label"] == "-" or q["min"] >= q["max"]:
                continue
            g = snake(q["label"])
            seen[g] = seen.get(g, 0) + 1
            if seen[g] > 1:
                g = f"{g}_{seen[g]}"
            name = renamed(table, str(k), q["label"], g)
            et = None
            if q["enum"] and not is_bool(q):
                et = f"{v['class']}.{camel(name)}"
                v["enums"].append(enum_of(camel(name), q))
            v["props"].append(prop(name, q, et, "EDIT › " + e["pages"][k // 4]))   # (E1 .. E4, E5 .. E8)
        # the engine's parameters by name (FeluccaAction.set_param), tagged with the engine, (engine + 1) x 256 + id:
        # every engine numbers its eight alike, and a part's own are below 256; a parameter of another engine is refused
        v["enums"].append({"name": "Param", "values": [
            ("PARAM_" + p["name"].upper(), (e["index"] + 1) * 256 + p["id"],
             f"The parameter [member {p['name']}], tagged with its engine (FeluccaAction.set_param).") for p in v["props"]]})
        views.append(v)
    seen = {}
    for c in classes.values():
        if tuple(c["values"]) in seen:
            raise Fail(f"{seen[tuple(c['values'])]} and {c['class']} have the same values: give them one class in "
                       "names.json's enum_classes")
        seen[tuple(c["values"])] = c["class"]
    # the part's parameters by name (FeluccaOverride.param, FeluccaAction.set_param): FeluccaParam.SEQ_LENGTH, its id
    special = [("FeluccaParam", [(p["name"].upper(), p["id"], f"The parameter [member FeluccaPart.{p['name']}].")
                                 for p in part["props"]],
                "A part's parameters, for [member FeluccaOverride.param] and [method FeluccaAction.set_param].")]
    # the DRUM grid's lanes (FeluccaAction.hit), in order, named after their LANES pages' levels: FeluccaLane.KICK
    lanes = sorted(i for pg in d["pages"] if pg["page"] in ("LANES", "LANES 2") for i in pg["ids"])
    if len(lanes) != 8 or [i - lanes[0] for i in lanes] != list(range(8)):
        raise Fail("the LANES pages do not hold the 8 lane levels in order")
    level = {p["id"]: p["name"] for p in part["props"]}
    special.append(("FeluccaLane", [(re.sub(r"^lane_", "", level[i]).upper(), k, d["track"][i]["label"])
                                    for k, i in enumerate(lanes)],
                    "The DRUM engine's eight lanes, for [method FeluccaAction.hit]."))
    special.append(("FeluccaEngine", [(ident(v["label"]), v["engine"], v["label"]) for v in views],
                    "The engines a part can play, for [member FeluccaPart.engine]."))
    for cls, values, brief in special:
        if cls in classes:
            raise Fail(f"{cls}: an enum's class and {cls}'s own: rename it in names.json's enum_classes")
        classes[cls] = {"class": cls, "values": values, "users": [], "brief": brief}
    for c in [part, song] + views:
        n = [p["name"] for p in c["props"]]
        dup = {x for x in n if n.count(x) > 1}
        if dup:
            raise Fail(f"{c['class']}: two properties named {sorted(dup)}: rename them in names.json")
        k = [v[0] for e in c["enums"] for v in e["values"]]
        dup = {x for x in k if k.count(x) > 1}
        if dup:
            raise Fail(f"{c['class']}: two constants named {sorted(dup)} (one namespace for all its enums)")
    return part, song, views, list(classes.values())


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit_class(out, sym, c):
    for k, e in enumerate(c["enums"]):
        out.append(f"static const ApiConst {sym}_E{k}[] = {{" + ", ".join(f"{{{cstr(n)}, {v}}}" for n, v, _ in e["values"]) + "};")
    if c["enums"]:
        out.append(f"static const ApiEnum {sym}_ENUMS[] = {{" +
                   ", ".join(f"{{{cstr(e['name'])}, {sym}_E{k}, {len(e['values'])}}}" for k, e in enumerate(c["enums"])) + "};")
    out.append(f"static const ApiProp {sym}_PROPS[] = {{")
    for p in c["props"]:
        out.append(f"    {{{cstr(p['name'])}, {p['id']}, {p['min']}, {p['max']}, {p['def']}, API_{p['type'].upper()}, "
                   f"{cstr(p['enum_type']) if p['enum_type'] else 'nullptr'}, {cstr(p['hint'])}}},")
    out.append("};")
    return (f"{{{cstr(c['class'])}, {sym}_PROPS, {len(c['props'])}, {sym + '_ENUMS' if c['enums'] else 'nullptr'}, "
            f"{len(c['enums'])}, {c.get('engine', -1)}}}")


def describe(docs, part, song, views, classes):
    """docs.json's text for every property, ended with where it is on the FM-1, and for every class; a property or
    class it leaves out, or a name of its that is none, fails"""
    for c in [part, song] + views:
        table, extra = docs.get(c["class"], {}), c.get("described", [])
        stale = sorted(set(table) - {p["name"] for p in c["props"]} - set(extra))
        if stale:
            raise Fail(f"docs.json: {c['class']} has no {', '.join(stale)}")
        for p in c["props"] + [{"name": x} for x in extra]:
            if not table.get(p["name"]):
                raise Fail(f"docs.json: {c['class']}.{p['name']} is not described")
        for p in c["props"]:
            where = f"{p['where']} › {p['label']}" if p["where"] else docs["where"].get(f"{c['class']}.{p['name']}")
            if not where:
                raise Fail(f"docs.json: {c['class']}.{p['name']} is on no page: say where it is (where)")
            p["doc"] = f"{table[p['name']]} On the FM-1: {where}."
        if len(docs["classes"].get(c["class"], [])) != 2:
            raise Fail(f"docs.json: classes has no brief and description of {c['class']}")
    named = {f"{c['class']}.{p['name']}" for c in [part, song] + views for p in c["props"] if not p["where"]}
    stale = sorted(set(docs["where"]) - named)
    if stale:
        raise Fail(f"docs.json: where names {', '.join(stale)}, on a page or no property")
    values = {c["class"]: {n for n, _, _ in c["values"]} for c in classes}
    values.update({f"{v['class']}.{e['name']}": {n for n, _, _ in e["values"]} for v in views for e in v["enums"]})
    for k, t in docs["enums"].items():
        stale = sorted(set(t) - values.get(k, set()) - {"_"})
        if k not in values or stale:
            raise Fail(f"docs.json: enums: {k} has no {', '.join(stale)}" if k in values else f"docs.json: no enum {k}")
    stale = sorted(set(docs["classes"]) - {c["class"] for c in [part, song] + views})
    if stale:
        raise Fail(f"docs.json: classes names {', '.join(stale)}, no part, song or engine view")


def constants(o, enum, values, text):
    for n, v, label in values:
        o.append(f'\t\t<constant name="{n}" value="{v}" enum="{enum}">\n\t\t\t{escape(text.get(n, label))}\n\t\t</constant>')


def xml(c, inherits, brief, desc, enum_docs):
    o = ['<?xml version="1.0" encoding="UTF-8" ?>',
         f'<class name="{c["class"]}" inherits="{inherits}" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">',
         f"\t<brief_description>\n\t\t{escape(brief)}\n\t</brief_description>",
         f"\t<description>\n\t\t{escape(desc)}\n\t</description>", "\t<tutorials>\n\t</tutorials>", "\t<members>"]
    for p in c["props"]:
        kind = "bool" if p["type"] == "bool" else "int"
        enum = f' enum="{p["enum_type"]}"' if p["enum_type"] else ""
        default = ("true" if p["def"] else "false") if p["type"] == "bool" else p["def"]
        o.append(f'\t\t<member name="{p["name"]}" type="{kind}"{enum} default="{default}">\n\t\t\t{escape(p["doc"])}'
                 "\n\t\t</member>")
    for x in c.get("extra_members", []):
        o.append(x)
    o.append("\t</members>")
    o.append("\t<constants>")
    for e in c["enums"]:
        constants(o, e["name"], e["values"], enum_docs.get(f"{c['class']}.{e['name']}", {}))
    o.append("\t</constants>\n</class>\n")
    return "\n".join(o)


def enum_xml(c, text):
    """an enum class's reference: what uses it (its brief), docs.json's description, its values"""
    users = [u if u.startswith("[") else f"[member {u}]" for u in c["users"]]
    brief = c.get("brief") or "The values of " + (", ".join(users[:-1]) + " and " if len(users) > 1 else "") + users[-1] + "."
    o = ['<?xml version="1.0" encoding="UTF-8" ?>',
         f'<class name="{c["class"]}" inherits="Object" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">',
         f"\t<brief_description>\n\t\t{escape(brief)}\n\t</brief_description>",
         f"\t<description>\n\t\t{escape(text.get('_', ''))}\n\t</description>", "\t<tutorials>\n\t</tutorials>",
         "\t<constants>"]
    constants(o, "Value", c["values"], text)
    o.append("\t</constants>\n</class>\n")
    return "\n".join(o)


def main(argv):
    if len(argv) not in (5, 6):
        sys.stderr.write(__doc__)
        return 2
    d, names, docs, out_dir = json.load(open(argv[1])), json.load(open(argv[2])), json.load(open(argv[3])), argv[4]
    try:
        part, song, views, classes = build(d, names)
        part["described"] = ["engine"]
        describe(docs, part, song, views, classes)
    except Fail as e:
        sys.stderr.write(f"gen_api: {e}\n")
        return 1
    os.makedirs(os.path.join(out_dir, "doc_classes"), exist_ok=True)
    cpp = [f"// generated by extension/codegen/gen_api.py from Felucca {d['version']}'s tables: do not edit",
           '#include "felucca_api.h"', "", "namespace felucca {", ""]
    pc = emit_class(cpp, "PART", part)
    sc = emit_class(cpp, "SONG", song)
    vcs = [emit_class(cpp, f"V{k}", v) for k, v in enumerate(views)]
    for k, c in enumerate(classes):
        cpp.append(f"static const ApiConst EC{k}[] = {{" + ", ".join(f"{{{cstr(n)}, {v}}}" for n, v, _ in c["values"]) + "};")
    engines = [k for k, c in enumerate(classes) if c["class"] == "FeluccaEngine"][0]
    cpp += [f"const ApiClass API_PART = {pc};", f"const ApiClass API_SONG = {sc};",
            f"const ApiClass API_VIEWS[{len(views)}] = {{" + ", ".join(vcs) + "};",
            f"const int API_VIEW_COUNT = {len(views)};",
            f"const ApiEnum API_ENUM_CLASSES[{len(classes)}] = {{" +
            ", ".join(f"{{{cstr(c['class'])}, EC{k}, {len(c['values'])}}}" for k, c in enumerate(classes)) + "};",
            f"const ApiEnum API_ENGINE_ENUM = {{\"FeluccaEngine\", EC{engines}, {len(views)}}};",
            f"const char *const API_FELUCCA = {cstr(d['version'])};", "", "} // namespace felucca", ""]
    h = [f"// generated by extension/codegen/gen_api.py from Felucca {d['version']}'s tables: do not edit",
         "#pragma once", "", "// the engine views: X(class, view index, engine index, the part's property)",
         "#define FELUCCA_ENGINE_VIEWS(X) \\"]
    h += [f"\tX({v['class']}, {k}, {v['engine']}, {v['access']}) \\" for k, v in enumerate(views)]
    h += ["", "// the enum classes: X(class, its index in API_ENUM_CLASSES)", "#define FELUCCA_ENUM_CLASSES(X) \\"]
    h += [f"\tX({c['class']}, {k}) \\" for k, c in enumerate(classes)]
    h += ["", ""]
    open(os.path.join(out_dir, "felucca_api.gen.cpp"), "w").write("\n".join(cpp))
    open(os.path.join(out_dir, "felucca_api.gen.h"), "w").write("\n".join(h))
    # ---- the class reference
    dc = os.path.join(out_dir, "doc_classes")
    part["extra_members"] = [
        f'\t\t<member name="engine" type="int" enum="FeluccaEngine.Value" default="0">\n\t\t\t'
        f'{escape(docs["FeluccaPart"]["engine"])}\n\t\t</member>'] + [
        f'\t\t<member name="{v["access"]}" type="{v["class"]}">\n\t\t\tThe part\'s {v["label"]} parameters, or '
        f"[code]null[/code] while it plays another engine.\n\t\t</member>" for v in views]
    for c, inherits in [(part, "RefCounted"), (song, "RefCounted")] + [(v, "FeluccaEngineView") for v in views]:
        brief, desc = docs["classes"][c["class"]]
        open(os.path.join(dc, c["class"] + ".xml"), "w").write(xml(c, inherits, brief, desc, docs["enums"]))
    for c in classes:
        open(os.path.join(dc, c["class"] + ".xml"), "w").write(enum_xml(c, docs["enums"].get(c["class"], {})))
    if len(argv) == 6:                          # the class reference compiled in (godot-cpp's own generator)
        sys.path.insert(0, argv[5])
        from doc_source_generator import generate_doc_source_from_directory
        generate_doc_source_from_directory(os.path.join(out_dir, "doc_data.gen.cpp"), dc)
    n = len(part["props"]) + len(song["props"]) + sum(len(v["props"]) for v in views)
    ne = len(classes) + sum(len(v["enums"]) for v in views)
    print(f"gen_api: Felucca {d['version']}: FeluccaPart {len(part['props'])} properties, FeluccaSong "
          f"{len(song['props'])}, {len(views)} engine views; {n} properties, {len(classes)} enum classes, {ne} enums")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
