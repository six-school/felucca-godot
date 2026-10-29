# The web tests' main scene (the "Web tests" export's, its custom feature web_tests): a release
# web template takes no scene on its command line (built without path overrides), so ?scene=m3_test on the page's URL
# names the one under res://tests/ to run (tools/web_run.mjs, through tools/serve_web.py)
extends Node

func _ready() -> void:
	var scene := str(JavaScriptBridge.eval("new URLSearchParams(location.search).get('scene') || ''"))
	var path := "res://tests/%s.tscn" % scene
	if scene.is_empty() or not ResourceLoader.exists(path):
		push_error("web_tests: no test scene named \"%s\" (?scene=m3_test)" % scene)
		return
	get_tree().change_scene_to_file.call_deferred(path)
