// The extension's entry (felucca.gdextension: felucca_library_init) and its classes.
#include "felucca_clock.h"
#include "felucca_conductor.h"
#include "felucca_importer.h"
#include "felucca_project.h"
#include "felucca_source.h"
#include "felucca_stream.h"

#include <gdextension_interface.h>
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

static FeluccaConductor *conductor = nullptr;

static void initialize_felucca(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(FeluccaProject);
		GDREGISTER_INTERNAL_CLASS(FeluccaClockEffectInstance);
		GDREGISTER_INTERNAL_CLASS(FeluccaClockEffect);
		GDREGISTER_INTERNAL_CLASS(FeluccaStream);
		GDREGISTER_INTERNAL_CLASS(FeluccaPlayback);
		GDREGISTER_ABSTRACT_CLASS(FeluccaConductor); // (the engine singleton only)
		GDREGISTER_CLASS(FeluccaConductorNode); // (the autoload's: addons/felucca/felucca.tscn)
		GDREGISTER_CLASS(FeluccaSource);
		GDREGISTER_CLASS(FeluccaSource2D);
		GDREGISTER_CLASS(FeluccaSource3D);
		conductor = memnew(FeluccaConductor);
		Engine::get_singleton()->register_singleton("FeluccaConductor", conductor);
	} else if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(FeluccaProjectImporter);
		GDREGISTER_INTERNAL_CLASS(FeluccaEditorPlugin);
		EditorPlugins::add_by_type<FeluccaEditorPlugin>();
	}
}

static void uninitialize_felucca(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE && conductor) {
		Engine::get_singleton()->unregister_singleton("FeluccaConductor");
		memdelete(conductor);
		conductor = nullptr;
	}
}

extern "C" {
GDExtensionBool GDE_EXPORT felucca_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_felucca);
	init_obj.register_terminator(uninitialize_felucca);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}
