// A project made on the FM-1, as the Felucca web app exports it (D13): its store object, checked (core/project_json.c:
// format, version, size, CRC-32; then the firmware's own proj_import), and what it holds. The importer makes one of a
// .felucca file (the web app's .json, renamed); load_json reads one at run time.
#pragma once

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace godot {

class FeluccaProject : public Resource {
	GDCLASS(FeluccaProject, Resource);

	PackedByteArray data;
	String firmware, project_name;
	int64_t slot = -1, bpm = 0;
	PackedStringArray engines;

protected:
	static void _bind_methods();

public:
	// the export's text -> OK, or an error with r_error saying why
	Error parse_json(const PackedByteArray &p_json, String &r_error);
	static Ref<FeluccaProject> from_json(const String &p_text);
	static Ref<FeluccaProject> load_json(const String &p_path);

	void set_data(const PackedByteArray &p_data) { data = p_data; }
	PackedByteArray get_data() const { return data; }
	void set_firmware(const String &p_v) { firmware = p_v; }
	String get_firmware() const { return firmware; }
	void set_project_name(const String &p_v) { project_name = p_v; }
	String get_project_name() const { return project_name; }
	void set_slot(int64_t p_v) { slot = p_v; }
	int64_t get_slot() const { return slot; }
	void set_bpm(int64_t p_v) { bpm = p_v; }
	int64_t get_bpm() const { return bpm; }
	void set_engines(const PackedStringArray &p_v) { engines = p_v; }
	PackedStringArray get_engines() const { return engines; }
};

} // namespace godot
