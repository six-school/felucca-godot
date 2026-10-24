#!/usr/bin/env python
# The GDExtension (docs/DESIGN.md D4, §4): godot-cpp at the Godot API version below, the C core's pool of renamed
# Felucca copies (core/build.sh), and extension/src, into demo/bin.
#   python -m SCons [target=template_debug|template_release] [pool=8]   (tools/setup_felucca.sh first)
import os

GODOT_API = "4.7"                       # the version seam (§4.1): the oldest Godot this build loads in
POOL = int(ARGUMENTS.get("pool", 8))    # pool slots: copies of the core compiled in (D3, §13.7)

env = SConscript("third_party/godot-cpp/SConstruct", {"api_version": GODOT_API})

core_dir = "build/core/pool%d" % POOL
core_lib = env.Command(
    os.path.join(core_dir, "libfelucca_core.a"),
    ["core/board.c", "core/fel_core.h", "core/fel_pool.h", "core/project_json.c", "core/project_json.h",
     "core/renames.py", "core/build.sh"] + Glob("third_party/felucca/firmware/src/*.c") +
    Glob("third_party/felucca/firmware/src/*.h") + Glob("third_party/felucca/web/emu/*.c"),
    "OUT=%s core/build.sh %d" % (core_dir, POOL),
)

env.Append(CPPPATH=["extension/src", "core"])
env.Append(CPPDEFINES=[("FEL_POOL_SIZE", POOL)])
sources = Glob("extension/src/*.cpp")
# only the GDExtension entry is exported: the core copies' own exports (felucca_web.c's web_*) stay inside
env.Append(LINKFLAGS=["-Wl,--version-script=extension/felucca.version", "-Wl,--no-undefined"])
env.Prepend(LIBS=[core_lib])
library = env.SharedLibrary("demo/bin/libfelucca{}{}".format(env["suffix"], env["SHLIBSUFFIX"]), source=sources)
env.Depends(library, [core_lib, "extension/felucca.version"])
env.NoCache(library)
Default(library)
