# The project file's checks in Godot 4.7.2 (extension/run_tests.sh): the web app's export read with Godot's own JSON
# (FeluccaProject.parse_json, through check_json), on a test project (res://tests/project.json, the C tests' fixture
# too) and on damaged copies of it: each damage is named by the first problem in it, as the importer would report
# it.
extends Node

const M_JSON := "not JSON"
const M_FORMAT := "not a Felucca project"
const M_VERSION := "a project file version other than 1"
const M_OBJECT := "\"object\" missing or not {size, crc, data}"
const M_SIZE := "an object size the firmware does not read"
const M_BASE64 := "\"data\" is not base64"
const M_LENGTH := "\"data\" does not hold \"size\" bytes"
const M_CRC := "CRC-32 mismatch"
const M_FIRMWARE := "the firmware does not read it as a project"

var fails := 0
var text := ""

func check(ok: bool, what: String) -> void:
	print(("ok   " if ok else "FAIL ") + what)
	if not ok:
		fails += 1

# the export with one change (from must be in it once: else the case itself is wrong)
func edit(from: String, to: String) -> String:
	return text.replace(from, to) if text.count(from) == 1 else "(%s is not in the file once)" % from

# json reads as a project ("" expected) or is refused with the message that begins with expected
func expect(json: String, expected: String, what: String) -> void:
	var got := FeluccaProject.check_json(json)
	var ok := got == "" if expected == "" else got.begins_with(expected)
	check(ok, "%s: %s" % [what, "reads" if got == "" else got])

func crc32(b: PackedByteArray) -> int:
	var c := 0xFFFFFFFF
	for x in b:
		c ^= x
		for k in 8:
			c = (c >> 1) ^ (0xEDB88320 if c & 1 else 0)
	return c ^ 0xFFFFFFFF

func _ready() -> void:
	text = FileAccess.get_file_as_string("res://tests/project.json")
	expect(text, "", "the test project")
	expect("".join(Array(text.split("\n")).map(func(l: String) -> String: return l.strip_edges())), "", "on one line")
	expect(text.replace("/", "\\/"), "", "every / escaped as \\/")
	expect(edit("\"slot\"", "\"extra\": {\"a\": [1, -2.5e3, true, null, \"x\\u00e9\"]}, \"slot\""), "",
		"a member it does not read, values of every kind")
	expect(edit("\"version\": 1", "\"version\": 2, \"version\": 1"), "", "a repeated member: the last one counts")
	var d: Dictionary = JSON.parse_string(text)
	expect(JSON.stringify(d), "", "written again by Godot (every number a float)")

	expect(edit("felucca-project", "felucca-backup"), M_FORMAT, "another format")
	expect(edit("\"format\"", "\"formats\""), M_FORMAT, "no format")
	expect("[1]", M_FORMAT, "an array")
	expect(edit("\"version\": 1", "\"version\": 2"), M_VERSION, "version 2")
	expect(edit("\"object\"", "\"objects\""), M_OBJECT, "no object")
	expect(edit("\"crc\"", "\"crcs\""), M_OBJECT, "no crc")
	expect(edit("\"crc\": 1680607063", "\"crc\": 4294967296"), M_OBJECT, "a CRC past 32 bits")
	expect(edit("\"size\": 3840", "\"size\": 3841"), M_SIZE, "size 3841")
	expect(edit("\"size\": 3840", "\"size\": \"3840\""), M_SIZE, "size as a string")
	expect(edit("\"size\": 3840", "\"size\": 3648"), M_LENGTH, "size 3648 (FUN9's) for 3840 bytes")
	expect(edit("QU5VRg", "QU5V*g"), M_BASE64, "a character outside base64")
	expect(edit("QU5VRg", "QU5VR"), M_BASE64, "a base64 length not a multiple of 4")
	expect(edit("QU5VRgAP", "QU5V==AP"), M_BASE64, "padding in the middle")
	expect(edit("QU5VRgAP", "QU5V"), M_LENGTH, "4 characters short (3 bytes)")
	expect(edit("1680607063", "1680607064"), M_CRC, "the CRC off by one")
	expect(edit("QU5VRg", "QU5VRw"), M_CRC, "one byte of the data changed (still base64)")
	expect(text.substr(0, text.length() / 2), M_JSON, "the file cut short")
	expect(text + " x", M_JSON, "something after the object")
	expect(edit("\"slot\"", "\"slot\" 3, \"x\""), M_JSON, "a member without a colon")
	expect("", M_JSON, "empty")

	# a whole file (base64, size, CRC all right) whose bytes are not a project: the firmware says so
	var o: Dictionary = d["object"]
	var raw := Marshalls.base64_to_raw(o["data"] as String)
	raw[0] = 66                                 # (the magic ANUF -> BNUF)
	o["data"] = Marshalls.raw_to_base64(raw)
	o["crc"] = crc32(raw)
	expect(JSON.stringify(d), M_FIRMWARE, "the bytes damaged, the CRC made to match")

	var p := FeluccaProject.from_json(text)
	check(p != null and p.project_name == "SOLAR SIGNAL" and p.bpm == 68 and p.firmware == "FELUCCA v1.6.1" and p.slot == 3
		and p.data.size() == 3840, "from_json: \"%s\", %d BPM, %s, slot %d, %d bytes" % [p.project_name, p.bpm,
		p.firmware, p.slot, p.data.size()] if p else "from_json: null")
	var imported: FeluccaProject = load("res://music/solar_signal.felucca")
	check(imported != null and imported.data == p.data, "the importer: the same bytes as from_json")
	print("project_test: %d failed" % fails)
	get_tree().quit(1 if fails else 0)
