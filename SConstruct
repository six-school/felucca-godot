#!/usr/bin/env python
# The GDExtension (docs/DESIGN.md D4, §4): godot-cpp at the Godot API version below, the C core's pool of renamed
# Felucca copies (core/build.sh), and extension/src, into demo/bin.
#   python -m SCons [target=template_debug|template_release] [pool=8]   (tools/setup_felucca.sh first)
#   python -m SCons platform=web [..]     (M7, DESIGN.md §9: tools/setup_web.sh, then emsdk_env.sh sourced; threaded)
import os
from SCons.Util import WhereIs

GODOT_API = "4.7"                       # the version seam (§4.1): the oldest Godot this build loads in
POOL = int(ARGUMENTS.get("pool", 8))    # pool slots: copies of the core compiled in (D3, §13.7)

env = SConscript("third_party/godot-cpp/SConstruct", {"api_version": GODOT_API})

web = env["platform"] == "web"
core_dir = "build/core/%spool%d" % ("web-" if web else "", POOL)
core_with = ""
if web:  # (the side module's flags, as godot-cpp's tools/web.py builds its own objects: shared memory, relocatable)
    core_with = "CC=emcc AR=emar NM=%s CFLAGS_EXTRA=\"-sUSE_PTHREADS=1 -sSIDE_MODULE=1 -sSUPPORT_LONGJMP=wasm\" " % (
        os.path.join(os.path.dirname(os.path.realpath(WhereIs("emcc"))), "..", "bin", "llvm-nm"))
core_lib = env.Command(
    os.path.join(core_dir, "libfelucca_core.a"),
    ["core/board.c", "core/fel_core.h", "core/fel_pool.h", "core/renames.py", "core/build.sh"] + Glob("third_party/felucca/firmware/src/*.c") +
    Glob("third_party/felucca/firmware/src/*.h") + Glob("third_party/felucca/web/emu/*.c"),
    "%sOUT=%s core/build.sh %d" % (core_with, core_dir, POOL),
)

# the typed API (D10, §8): the firmware's tables (core/tools/dump_params.c) -> extension/codegen/gen_api.py -> C++ tables
# and the class reference
felucca = "third_party/felucca"
version = open(felucca + "/firmware/src/felucca.c").read().split('#define FELUCCA_VERSION "', 1)[1].split('"', 1)[0]
dump = env.Command(
    "build/gen_api/dump_params",
    ["core/tools/dump_params.c", "core/board.c", "core/fel_core.h"] + Glob(felucca + "/firmware/src/*.c") +
    Glob(felucca + "/firmware/src/*.h"),
    "clang -O1 -ffp-contract=off -std=gnu99 -w '-DFELUCCA_VERSION=\"%s\"' -I%s -I%s/build/gen -I%s/firmware/src -Icore "
    "-o $TARGET core/tools/dump_params.c -lm" % (version, felucca, felucca, felucca),
)
params = env.Command("build/gen_api/params.json", dump, "$SOURCE > $TARGET")
gen = env.Command(
    ["build/gen_api/felucca_api.gen.cpp", "build/gen_api/felucca_api.gen.h", "build/gen_api/doc_data.gen.cpp"],
    [params, "extension/codegen/gen_api.py", "extension/codegen/names.json"],
    "python3 -I extension/codegen/gen_api.py ${SOURCES[0]} extension/codegen/names.json build/gen_api third_party/godot-cpp",
)

env.Append(CPPPATH=["extension/src", "core", "build/gen_api"])
env.Append(CPPDEFINES=[("FEL_POOL_SIZE", POOL)])
sources = Glob("extension/src/*.cpp") + [gen[0]]
if env["target"] in ["editor", "template_debug"]:
    sources.append(gen[2])
# only the GDExtension entry is exported: the core copies' own exports (felucca_web.c's web_*) stay inside (the web's
# side module exports all it defines: the copies' names are their own, fel<n>_*)
if not web:
    env.Append(LINKFLAGS=["-Wl,--version-script=extension/felucca.version", "-Wl,--no-undefined"])
env.Prepend(LIBS=[core_lib])
library = env.SharedLibrary("demo/bin/libfelucca{}{}".format(env["suffix"], env["SHLIBSUFFIX"]), source=sources)
env.Depends(library, [core_lib, "extension/felucca.version"])
env.NoCache(library)
Default(library)
