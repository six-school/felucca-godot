// The editor side: .felucca files (a web app export, renamed from .json so Godot's own JSON loader keeps the
// project's other .json files) import as FeluccaProject resources.
#pragma once

#include <godot_cpp/classes/editor_import_plugin.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>

namespace godot {

class FeluccaProjectImporter : public EditorImportPlugin {
	GDCLASS(FeluccaProjectImporter, EditorImportPlugin);

protected:
	static void _bind_methods() {}

public:
	String _get_importer_name() const override { return "felucca.project"; }
	String _get_visible_name() const override { return "Felucca Project"; }
	PackedStringArray _get_recognized_extensions() const override;
	String _get_save_extension() const override { return "res"; }
	String _get_resource_type() const override { return "FeluccaProject"; } // (Quick Load and typed fields find it)
	int32_t _get_preset_count() const override { return 0; }
	String _get_preset_name(int32_t p_preset_index) const override { return String(); }
	TypedArray<Dictionary> _get_import_options(const String &p_path, int32_t p_preset_index) const override { return TypedArray<Dictionary>(); }
	bool _get_option_visibility(const String &p_path, const StringName &p_option_name, const Dictionary &p_options) const override { return true; }
	float _get_priority() const override { return 1.0f; }
	int32_t _get_import_order() const override { return 0; }
	int32_t _get_format_version() const override { return 2; } // (2: the project's values, for the typed API)
	bool _can_import_threaded() const override { return false; } // (project_info uses one copy's scratch)
	Error _import(const String &p_source_file, const String &p_save_path, const Dictionary &p_options,
			const TypedArray<String> &p_platform_variants, const TypedArray<String> &p_gen_files) const override;
};

class FeluccaEditorPlugin : public EditorPlugin {
	GDCLASS(FeluccaEditorPlugin, EditorPlugin);
	Ref<FeluccaProjectImporter> importer;

protected:
	static void _bind_methods() {}
	void _notification(int p_what);
};

} // namespace godot
