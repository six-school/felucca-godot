#include "felucca_project.h"

#include "fel_pool.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/marshalls.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace godot {

// the object sizes the firmware's backup path reads (FUN10 FUN9 FUN8 FUN7: editor_backup.c)
static const int64_t SIZES[] = { 3840, 3648, 3584, 3388 };

// CRC-32 (zlib), as the web app's backup code computes it
static uint32_t crc32_of(const uint8_t *p_b, int64_t p_n) {
	uint32_t c = 0xFFFFFFFFu;
	for (int64_t i = 0; i < p_n; i++) {
		c ^= p_b[i];
		for (int k = 0; k < 8; k++) {
			c = c >> 1 ^ (c & 1u ? 0xEDB88320u : 0u);
		}
	}
	return ~c;
}

// a JSON number that is a whole number in lo .. hi (Godot's JSON reads every number as a float)
static bool whole(const Variant &p_v, int64_t p_lo, int64_t p_hi, int64_t &r_v) {
	if (p_v.get_type() != Variant::INT && p_v.get_type() != Variant::FLOAT) {
		return false;
	}
	const double d = p_v;
	if (!(d >= double(p_lo) && d <= double(p_hi)) || d != std::floor(d)) {
		return false;
	}
	r_v = int64_t(d);
	return true;
}

// strict base64: a multiple of 4 of its alphabet's characters, '=' only as the last one or two (Marshalls would print
// an engine error of its own for anything else)
static bool base64_ok(const String &p_s) {
	const int64_t n = p_s.length();
	int64_t pad = 0;
	if (n % 4) {
		return false;
	}
	for (int64_t i = 0; i < n; i++) {
		const char32_t c = p_s[i];
		if (c == '=') {
			if (i < n - 2) {
				return false;
			}
			pad++;
		} else if (pad || !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/')) {
			return false;
		}
	}
	return true;
}

void FeluccaProject::_bind_methods() {
	ClassDB::bind_static_method("FeluccaProject", D_METHOD("from_json", "text"), &FeluccaProject::from_json);
	ClassDB::bind_static_method("FeluccaProject", D_METHOD("load_json", "path"), &FeluccaProject::load_json);
	ClassDB::bind_static_method("FeluccaProject", D_METHOD("check_json", "text"), &FeluccaProject::check_json);
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

// the web app's export, Godot's JSON reading it:
//   {"format": "felucca-project", "version": 1, "firmware": "FELUCCA v1.6.1", "slot": 3,
//    "object": {"size": 3840, "crc": <CRC-32 (zlib) of the bytes>, "data": "<base64 of the bytes>"}}
// checked as the web app writes it; whether the bytes are a project is the firmware's to say (project_info)
Error FeluccaProject::parse_json(const PackedByteArray &p_json, String &r_error) {
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_json.get_string_from_utf8()) != OK) {
		r_error = vformat("not JSON (line %d: %s)", json->get_error_line(), json->get_error_message());
		return ERR_INVALID_DATA;
	}
	const Variant root = json->get_data();
	const Dictionary f = root.get_type() == Variant::DICTIONARY ? Dictionary(root) : Dictionary();
	const Variant fmt = f.get("format", Variant());
	int64_t version = 0, size = 0, crc = 0, s = -1;
	if (fmt.get_type() != Variant::STRING || String(fmt) != "felucca-project") {
		r_error = "not a Felucca project (\"format\")";
		return ERR_INVALID_DATA;
	}
	if (!whole(f.get("version", Variant()), 1, 1, version)) {
		r_error = "a project file version other than 1";
		return ERR_INVALID_DATA;
	}
	const Variant ov = f.get("object", Variant());
	const Dictionary o = ov.get_type() == Variant::DICTIONARY ? Dictionary(ov) : Dictionary();
	const Variant b64 = o.get("data", Variant());
	if (!o.has("size") || !whole(o.get("crc", Variant()), 0, 0xFFFFFFFFll, crc) || b64.get_type() != Variant::STRING) {
		r_error = "\"object\" missing or not {size, crc, data}";
		return ERR_INVALID_DATA;
	}
	if (!whole(o["size"], 1, FEL_PROJECT_SIZE, size) ||
			std::find(std::begin(SIZES), std::end(SIZES), size) == std::end(SIZES)) {
		r_error = "an object size the firmware does not read";
		return ERR_INVALID_DATA;
	}
	if (!base64_ok(b64)) {
		r_error = "\"data\" is not base64";
		return ERR_INVALID_DATA;
	}
	const PackedByteArray bytes = Marshalls::get_singleton()->base64_to_raw(b64);
	if (bytes.size() != size) {
		r_error = "\"data\" does not hold \"size\" bytes";
		return ERR_INVALID_DATA;
	}
	if (crc32_of(bytes.ptr(), bytes.size()) != uint32_t(crc)) {
		r_error = "CRC-32 mismatch: the file is damaged";
		return ERR_INVALID_DATA;
	}
	// the firmware's own reading (any copy: project_info leaves its instance alone)
	fel_project_info_t info;
	const fel_core_t *c = fel_pool_core(0);
	if (c->project_info(bytes.ptr(), uint32_t(bytes.size()), &info) != 0) {
		r_error = "the firmware does not read it as a project (magic, hash or an engine it does not have)";
		return ERR_INVALID_DATA;
	}
	data = bytes;
	const Variant fw = f.get("firmware", Variant());
	firmware = fw.get_type() == Variant::STRING ? String(fw) : String();
	slot = whole(f.get("slot", Variant()), 0, 1 << 20, s) ? s : -1;
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

String FeluccaProject::check_json(const String &p_text) {
	Ref<FeluccaProject> p;
	String err;
	p.instantiate();
	return p->parse_json(p_text.to_utf8_buffer(), err) == OK ? String() : err;
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
