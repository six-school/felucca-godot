#include "felucca_project.h"

#include "fel_pool.h"
#include "project_json.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

namespace godot {

void FeluccaProject::_bind_methods() {
	ClassDB::bind_static_method("FeluccaProject", D_METHOD("from_json", "text"), &FeluccaProject::from_json);
	ClassDB::bind_static_method("FeluccaProject", D_METHOD("load_json", "path"), &FeluccaProject::load_json);
	ClassDB::bind_method(D_METHOD("set_data", "data"), &FeluccaProject::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &FeluccaProject::get_data);
	ClassDB::bind_method(D_METHOD("set_firmware", "firmware"), &FeluccaProject::set_firmware);
	ClassDB::bind_method(D_METHOD("get_firmware"), &FeluccaProject::get_firmware);
	ClassDB::bind_method(D_METHOD("set_project_name", "name"), &FeluccaProject::set_project_name);
	ClassDB::bind_method(D_METHOD("get_project_name"), &FeluccaProject::get_project_name);
	ClassDB::bind_method(D_METHOD("set_slot", "slot"), &FeluccaProject::set_slot);
	ClassDB::bind_method(D_METHOD("get_slot"), &FeluccaProject::get_slot);
	ClassDB::bind_method(D_METHOD("set_bpm", "bpm"), &FeluccaProject::set_bpm);
	ClassDB::bind_method(D_METHOD("get_bpm"), &FeluccaProject::get_bpm);
	ClassDB::bind_method(D_METHOD("set_engines", "engines"), &FeluccaProject::set_engines);
	ClassDB::bind_method(D_METHOD("get_engines"), &FeluccaProject::get_engines);
	ClassDB::bind_method(D_METHOD("set_engine_ids", "ids"), &FeluccaProject::set_engine_ids);
	ClassDB::bind_method(D_METHOD("get_engine_ids"), &FeluccaProject::get_engine_ids);
	ClassDB::bind_method(D_METHOD("set_track_values", "values"), &FeluccaProject::set_track_values);
	ClassDB::bind_method(D_METHOD("get_track_values"), &FeluccaProject::get_track_values);
	ClassDB::bind_method(D_METHOD("set_song_values", "values"), &FeluccaProject::set_song_values);
	ClassDB::bind_method(D_METHOD("get_song_values"), &FeluccaProject::get_song_values);
	// stored with the resource; read-only in the inspector (they are what the device made)
	const uint32_t ro = PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_READ_ONLY;
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_BYTE_ARRAY, "data", PROPERTY_HINT_NONE, "", ro), "set_data", "get_data");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "project_name", PROPERTY_HINT_NONE, "", ro), "set_project_name", "get_project_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bpm", PROPERTY_HINT_NONE, "", ro), "set_bpm", "get_bpm");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "engines", PROPERTY_HINT_NONE, "", ro), "set_engines", "get_engines");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "firmware", PROPERTY_HINT_NONE, "", ro), "set_firmware", "get_firmware");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "slot", PROPERTY_HINT_NONE, "", ro), "set_slot", "get_slot");
	// (stored, not shown: FeluccaPart / FeluccaSong read them while no instance plays the project)
	const uint32_t st = PROPERTY_USAGE_STORAGE;
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "engine_ids", PROPERTY_HINT_NONE, "", st), "set_engine_ids", "get_engine_ids");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "track_values", PROPERTY_HINT_NONE, "", st), "set_track_values", "get_track_values");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "song_values", PROPERTY_HINT_NONE, "", st), "set_song_values", "get_song_values");
}

Error FeluccaProject::parse_json(const PackedByteArray &p_json, String &r_error) {
	fel_project_file_t f;
	fel_project_info_t info;
	int e = fel_project_json(reinterpret_cast<const char *>(p_json.ptr()), size_t(p_json.size()), &f);
	if (e != FEL_PJ_OK) {
		r_error = fel_project_json_error(e);
		return ERR_INVALID_DATA;
	}
	// the firmware's own reading (any copy: project_info leaves its instance alone)
	const fel_core_t *c = fel_pool_core(0);
	if (c->project_info(f.data, f.size, &info) != 0) {
		r_error = "the firmware does not read it as a project (magic, hash or an engine it does not have)";
		return ERR_INVALID_DATA;
	}
	data.resize(int64_t(f.size));
	memcpy(data.ptrw(), f.data, f.size);
	firmware = String(f.firmware);
	slot = f.slot;
	project_name = String(info.name);
	bpm = info.bpm;
	engines.clear();
	for (int t = 0; t < FEL_TRACKS; t++) {
		const char *n = c->engine_name(info.engine[t]);
		engines.push_back(n ? String(n) : String("?"));
	}
	engine_ids.clear();
	fill_values();
	return OK;
}

// its values as loading would put them (the core's project_info): made with the resource, or from its bytes
void FeluccaProject::fill_values() const {
	fel_project_info_t info;
	if (engine_ids.size() == FEL_TRACKS || data.is_empty() ||
			fel_pool_core(0)->project_info(data.ptr(), uint32_t(data.size()), &info) != 0) {
		return;
	}
	engine_ids.resize(FEL_TRACKS);
	track_values.resize(FEL_TRACKS * FEL_PARAMS);
	song_values.resize(FEL_GLOBALS);
	for (int t = 0; t < FEL_TRACKS; t++) {
		engine_ids.set(t, info.engine[t]);
		for (int i = 0; i < FEL_PARAMS; i++) {
			track_values.set(t * FEL_PARAMS + i, info.p[t][i]);
		}
	}
	for (int i = 0; i < FEL_GLOBALS; i++) {
		song_values.set(i, info.g[i]);
	}
}

Ref<FeluccaProject> FeluccaProject::from_json(const String &p_text) {
	Ref<FeluccaProject> p;
	String err;
	p.instantiate();
	if (p->parse_json(p_text.to_utf8_buffer(), err) != OK) {
		UtilityFunctions::push_error("FeluccaProject: ", err);
		return Ref<FeluccaProject>();
	}
	return p;
}

Ref<FeluccaProject> FeluccaProject::load_json(const String &p_path) {
	Ref<FeluccaProject> p;
	String err;
	PackedByteArray b = FileAccess::get_file_as_bytes(p_path);
	if (b.is_empty()) {
		UtilityFunctions::push_error("FeluccaProject: cannot read ", p_path);
		return p;
	}
	p.instantiate();
	if (p->parse_json(b, err) != OK) {
		UtilityFunctions::push_error("FeluccaProject: ", p_path, ": ", err);
		return Ref<FeluccaProject>();
	}
	return p;
}

} // namespace godot
