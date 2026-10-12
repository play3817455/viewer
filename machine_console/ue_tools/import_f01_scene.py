import unreal

ROOT = r"F:/9ymsgo/9you/viewer/machine_console/ue/Content/Imported/F01"
DESTINATION = "/Game/Imported/F01"

def import_file(filename, name):
    task = unreal.AssetImportTask()
    task.filename = ROOT + "/" + filename
    task.destination_path = DESTINATION
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    return list(task.imported_object_paths)

visual_paths = import_file("Map_100102_scene.gltf", "Map_100102_scene")
unreal.log("MACHINE_CONSOLE_MAP_100102_VISUAL_IMPORT_RESULT=" + repr(visual_paths))
collision_paths = import_file("Map_100102_collision.gltf", "Map_100102_collision")
unreal.log("MACHINE_CONSOLE_MAP_100102_COLLISION_IMPORT_RESULT=" + repr(collision_paths))
visual_meshes = [p for p in visual_paths if p.endswith("Map_100102_scene.Map_100102_scene")]
collision_meshes = [p for p in collision_paths if p.endswith("Map_100102_collision.Map_100102_collision")]
if not visual_meshes:
    raise RuntimeError("UE Interchange did not import the map 100102 static mesh: " + repr(visual_paths))
if not collision_meshes:
    raise RuntimeError("UE Interchange did not import the map 100102 collision static mesh: " + repr(collision_paths))

collision_mesh = unreal.load_asset(collision_meshes[0])
body_setup = collision_mesh.get_editor_property("body_setup")
if not body_setup:
    raise RuntimeError("Imported map 100102 collision mesh has no BodySetup")
body_setup.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
if not unreal.EditorAssetLibrary.save_loaded_asset(collision_mesh):
    raise RuntimeError("Could not save configured map 100102 collision mesh")
unreal.log("MACHINE_CONSOLE_MAP_100102_COLLISION_COMPLEXITY=UseComplexAsSimple")
