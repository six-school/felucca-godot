#include "felucca_importer.h"

#include "felucca_project.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

PackedStringArray FeluccaProjectImporter::_get_recognized_extensions() const {
	PackedStringArray e;
	e.push_back("felucca");
	return e;
}

Error FeluccaProjectImporter::_import(const String &p_source_file, const String &p_save_path, const Dictionary &p_options,
		const TypedArray<String> &p_platform_variants, const TypedArray<String> &p_gen_files) const {
	Ref<FeluccaProject> p;
	String err;
	PackedByteArray b = FileAccess::get_file_as_bytes(p_source_file);
	if (b.is_empty()) {
		UtilityFunctions::push_error("Felucca import: cannot read ", p_source_file);
		return ERR_FILE_CANT_READ;
	}
	p.instantiate();
	if (p->parse_json(b, err) != OK) {
		UtilityFunctions::push_error("Felucca import: ", p_source_file, ": ", err);
		return ERR_INVALID_DATA;
	}
	return ResourceSaver::get_singleton()->save(p, p_save_path + String(".") + _get_save_extension());
}

void FeluccaEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		importer.instantiate();
		add_import_plugin(importer);
	} else if (p_what == NOTIFICATION_EXIT_TREE && importer.is_valid()) {
		remove_import_plugin(importer);
		importer.unref();
	}
}

} // namespace godot
