"""The typed API (docs/DESIGN.md D10, §8) from the firmware's own tables: params.json (core/tools/dump_params.c) and the
hand-kept names (names.json) -> C++ tables for extension/src/felucca_api.cpp and Godot class-reference XML.

    gen_api.py params.json names.json OUT_DIR [GODOT_CPP]   -> OUT_DIR/felucca_api.gen.h, felucca_api.gen.cpp,
                                                               doc_classes/*.xml (and with godot-cpp, doc_data.gen.cpp)

Rules (§8): one property per parameter id, named after the page it is first on (page + label) or, on no page, its
label; names.json renames one by id, checked against the label it must still have (a Felucca update that relabels or
moves it fails here, not silently in a game); the generated name stays as a hidden fallback. FeluccaPart: the track
parameters below the engine's eight; FeluccaSong: the globals the core lets code write; one view an engine (its eight,
from edit[]), the retired slot "-" left out. Enum values: the enum's name + the label, as Godot's own (a class's
constants share one namespace, whatever their enum): ArpRate 1/16 -> ARP_RATE_1_16, LfoWave S&H -> LFO_WAVE_S_AND_H.
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


def prop(name, generated, q, enum_index, where):
    if q["enum"]:
        hint = ",".join(f"{label.replace(',', ' ').replace(':', ' ')}:{q['min'] + k}" for k, label in enumerate(q["names"]))
    else:
        hint = f"{q['min']},{q['max']}"
    doc = f"{where}: {q['label']}, {q['min']} .. {q['max']}, {q['def']} by default (firmware id {q['id']})."
    return {"name": name, "fallback": generated if generated != name else None, "id": q["id"], "min": q["min"],
            "max": q["max"], "def": q["def"], "enum": enum_index, "hint": hint, "doc": doc}


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
            gen[i], where[i] = snake(p["page"] + " " + q["label"]), f"page {p['page']}"
    for q in d["track"]:
        i = q["id"]
        if i < e0 and i not in skip and i not in gen and q["label"] and q["min"] < q["max"]:
            gen[i], where[i] = snake(q["label"]), "on no page"
    for key in names["part"]:
        if int(key) not in gen:
            raise Fail(f"names.json: part {key} is no parameter now")
    part = {"class": "FeluccaPart", "props": [], "enums": []}
    for i in sorted(gen):
        q = d["track"][i]
        name = renamed(names["part"], str(i), q["label"], gen[i])
        ei = -1
        if q["enum"]:
            ei = len(part["enums"])
            part["enums"].append(enum_of(camel(name), q))
        part["props"].append(prop(name, gen[i], q, ei, where[i]))
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
        ei = -1
        if q["enum"]:
            ei = len(song["enums"])
            song["enums"].append(enum_of(camel(name), q))
        song["props"].append(prop(name, g, q, ei, f"page {gpage[i]}" if i in gpage else "on no page"))
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
            ei = -1
            if q["enum"]:
                ei = len(v["enums"])
                v["enums"].append(enum_of(camel(name), q))
            v["props"].append(prop(name, g, q, ei, f"{e['name']} E{k}"))
        views.append(v)
    for c in [part, song] + views:
        primary = [p["name"] for p in c["props"]]
        fb = [p["fallback"] for p in c["props"] if p["fallback"]]
        for p in c["props"]:                    # (a generated name several share, or another's name: no fallback)
            if p["fallback"] and (fb.count(p["fallback"]) > 1 or p["fallback"] in primary):
                p["fallback"] = None
        n = [p["name"] for p in c["props"]] + [p["fallback"] for p in c["props"] if p["fallback"]]
        dup = {x for x in n if n.count(x) > 1}
        if dup:
            raise Fail(f"{c['class']}: two properties named {sorted(dup)}: rename them in names.json")
        k = [v[0] for e in c["enums"] for v in e["values"]]
        dup = {x for x in k if k.count(x) > 1}
        if dup:
            raise Fail(f"{c['class']}: two constants named {sorted(dup)} (one namespace for all its enums)")
    return part, song, views


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
        out.append(f"    {{{cstr(p['name'])}, {cstr(p['fallback']) if p['fallback'] else 'nullptr'}, {p['id']}, {p['min']}, "
                   f"{p['max']}, {p['def']}, {p['enum']}, {cstr(p['hint'])}}},")
    out.append("};")
    return (f"{{{cstr(c['class'])}, {sym}_PROPS, {len(c['props'])}, {sym + '_ENUMS' if c['enums'] else 'nullptr'}, "
            f"{len(c['enums'])}, {c.get('engine', -1)}}}")


def xml(c, inherits, brief, desc):
    o = ['<?xml version="1.0" encoding="UTF-8" ?>',
         f'<class name="{c["class"]}" inherits="{inherits}" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">',
         f"\t<brief_description>\n\t\t{escape(brief)}\n\t</brief_description>",
         f"\t<description>\n\t\t{escape(desc)}\n\t</description>", "\t<tutorials>\n\t</tutorials>", "\t<members>"]
    for p in c["props"]:
        enum = f' enum="{c["class"]}.{c["enums"][p["enum"]]["name"]}"' if p["enum"] >= 0 else ""
        o.append(f'\t\t<member name="{p["name"]}" type="int"{enum} default="{p["def"]}">\n\t\t\t{escape(p["doc"])}'
                 + (f" Also [code]{p['fallback']}[/code] (the generated name)." if p["fallback"] else "") + "\n\t\t</member>")
    for x in c.get("extra_members", []):
        o.append(x)
    o.append("\t</members>")
    o.append("\t<constants>")
    for e in c["enums"]:
        for n, v, label in e["values"]:
            o.append(f'\t\t<constant name="{n}" value="{v}" enum="{e["name"]}">\n\t\t\t{escape(label)}\n\t\t</constant>')
    for x in c.get("extra_constants", []):
        o.append(x)
    o.append("\t</constants>\n</class>\n")
    return "\n".join(o)


def main(argv):
    if len(argv) not in (4, 5):
        sys.stderr.write(__doc__)
        return 2
    d, names, out_dir = json.load(open(argv[1])), json.load(open(argv[2])), argv[3]
    try:
        part, song, views = build(d, names)
    except Fail as e:
        sys.stderr.write(f"gen_api: {e}\n")
        return 1
    # the part's engine: an enum of the engines (but the retired slot), and a view property each
    part["engine_enum"] = [(ident(v["label"], "ENGINE"), v["engine"], v["label"]) for v in views]
    os.makedirs(os.path.join(out_dir, "doc_classes"), exist_ok=True)
    cpp = [f"// generated by extension/codegen/gen_api.py from Felucca {d['version']}'s tables: do not edit",
           '#include "felucca_api.h"', "", "namespace felucca {", ""]
    pc = emit_class(cpp, "PART", part)
    sc = emit_class(cpp, "SONG", song)
    vcs = [emit_class(cpp, f"V{k}", v) for k, v in enumerate(views)]
    cpp.append(f"static const ApiConst ENGINES_E[] = {{" + ", ".join(f"{{{cstr(n)}, {v}}}" for n, v, _ in part["engine_enum"]) + "};")
    # the part's parameters by name, typed (FeluccaOverride.param, M6's actions): PARAM_SEQ_LENGTH = its id
    part["param_enum"] = [("PARAM_" + p["name"].upper(), p["id"], p["name"]) for p in part["props"]]
    cpp.append(f"static const ApiConst PARAMS_E[] = {{" + ", ".join(f"{{{cstr(n)}, {v}}}" for n, v, _ in part["param_enum"]) + "};")
    # the DRUM grid's lanes (FeluccaAction.hit): named as the LANES pages name their levels (KICK .. BELL), in order
    labels = {q["id"]: q["label"] for q in d["track"]}
    levels = sorted(i for pg in d["pages"] if pg["page"] in ("LANES", "LANES 2") for i in pg["ids"])
    if len(levels) != 8 or [i - levels[0] for i in levels] != list(range(8)):
        raise SystemExit("gen_api: the LANES pages do not hold the 8 lane levels in order")
    part["lane_enum"] = [(ident(labels[i], "LANE"), k, labels[i]) for k, i in enumerate(levels)]
    cpp.append(f"static const ApiConst LANES_E[] = {{" + ", ".join(f"{{{cstr(n)}, {v}}}" for n, v, _ in part["lane_enum"]) + "};")
    cpp += [f"const ApiClass API_PART = {pc};", f"const ApiClass API_SONG = {sc};",
            f"const ApiClass API_VIEWS[{len(views)}] = {{" + ", ".join(vcs) + "};",
            f"const int API_VIEW_COUNT = {len(views)};",
            f"const ApiEnum API_ENGINE_ENUM = {{\"Engine\", ENGINES_E, {len(views)}}};",
            f"const ApiEnum API_PARAM_ENUM = {{\"Param\", PARAMS_E, {len(part['param_enum'])}}};",
            f"const ApiEnum API_LANE_ENUM = {{\"Lane\", LANES_E, {len(part['lane_enum'])}}};",
            f"const char *const API_FELUCCA = {cstr(d['version'])};", "", "} // namespace felucca", ""]
    h = [f"// generated by extension/codegen/gen_api.py from Felucca {d['version']}'s tables: do not edit",
         "#pragma once", "", "// the engine views: X(class, view index, engine index, the part's property)",
         "#define FELUCCA_ENGINE_VIEWS(X) \\"]
    h += [f"\tX({v['class']}, {k}, {v['engine']}, {v['access']}) \\" for k, v in enumerate(views)]
    h += ["", ""]
    open(os.path.join(out_dir, "felucca_api.gen.cpp"), "w").write("\n".join(cpp))
    open(os.path.join(out_dir, "felucca_api.gen.h"), "w").write("\n".join(h))
    # ---- the class reference
    dc = os.path.join(out_dir, "doc_classes")
    part["extra_members"] = [
        '\t\t<member name="engine" type="int" enum="FeluccaPart.Engine" default="0">\n\t\t\tThe part\'s engine; setting '
        "it loads the engine's first preset, as the device's ENGINE page does, then the values this source's scripts set "
        "again.\n\t\t</member>"] + [
        f'\t\t<member name="{v["access"]}" type="{v["class"]}">\n\t\t\tThe part\'s {v["label"]} parameters, or '
        f"[code]null[/code] while its engine is another.\n\t\t</member>" for v in views]
    part["extra_constants"] = [f'\t\t<constant name="{n}" value="{v}" enum="Engine">\n\t\t\t{escape(l)}\n\t\t</constant>'
                               for n, v, l in part["engine_enum"]] + [
        f'\t\t<constant name="{n}" value="{v}" enum="Param">\n\t\t\tThe parameter [member {l}] (FeluccaOverride.param).'
        "\n\t\t</constant>" for n, v, l in part["param_enum"]] + [
        f'\t\t<constant name="{n}" value="{v}" enum="Lane">\n\t\t\tThe DRUM grid\'s {escape(l)} lane (FeluccaAction.hit).'
        "\n\t\t</constant>" for n, v, l in part["lane_enum"]]
    open(os.path.join(dc, "FeluccaPart.xml"), "w").write(xml(part, "RefCounted", "One of a Felucca source's four parts.",
        "Every parameter of a part of the FM-1, typed, from the firmware's own tables (Felucca " + d["version"] + "). A "
        "value set here is kept on the source: applied now when an instance plays it, on binding when not. Values the "
        "firmware moves itself (recorded automation, parameter locks) read back as they sound."))
    open(os.path.join(dc, "FeluccaSong.xml"), "w").write(xml(song, "RefCounted", "A Felucca source's song-wide sound.",
        "The globals of the FM-1 that shape the sound: swing, tune, delay, reverb, chorus. Tempo is the conductor's "
        "(FeluccaConductor.tempo); clock, MIDI routing and the project actions are not the game's."))
    for v in views:
        open(os.path.join(dc, v["class"] + ".xml"), "w").write(xml(v, "FeluccaEngineView", f"The {v['label']} engine of a part.",
            f"The eight parameters of {v['label']} (its EDIT pages), on the part that has it as its engine (FeluccaPart."
            f"{v['access']}). The ranges are its editor's; some engines name a value differently by mode, the range stays."))
    if len(argv) == 5:                          # the class reference compiled in (godot-cpp's own generator)
        sys.path.insert(0, argv[4])
        from doc_source_generator import generate_doc_source_from_directory
        generate_doc_source_from_directory(os.path.join(out_dir, "doc_data.gen.cpp"), dc)
    n = len(part["props"]) + len(song["props"]) + sum(len(v["props"]) for v in views)
    ne = len(part["enums"]) + len(song["enums"]) + sum(len(v["enums"]) for v in views) + 1
    print(f"gen_api: Felucca {d['version']}: FeluccaPart {len(part['props'])} properties, FeluccaSong "
          f"{len(song['props'])}, {len(views)} engine views; {n} properties, {ne} enums")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
