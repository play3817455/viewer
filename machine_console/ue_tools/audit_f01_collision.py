import unreal

MAP_PATH = "/Game/Maps/Map100102Preview"
if not unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH):
    raise RuntimeError("Could not load map 100102 pilot for collision audit")

actors = unreal.EditorLevelLibrary.get_all_level_actors()
def by_label(label):
    return next((a for a in actors if a.get_actor_label() == label), None)

collision_actor = by_label("Map 100102 — physics=1 collision")
if not collision_actor:
    raise RuntimeError("Map 100102 collision actor missing")
collision_component = collision_actor.get_component_by_class(unreal.StaticMeshComponent)
collision_mesh = collision_component.get_editor_property("static_mesh")
body = collision_mesh.get_editor_property("body_setup")
unreal.log("MAP_100102_COLLISION_MESH=" + collision_mesh.get_path_name())
origin, extent = collision_actor.get_actor_bounds(False, False)
unreal.log("MAP_100102_COLLISION_BOUNDS=" + str(origin) + "; extent=" + str(extent))
unreal.log("MAP_100102_COLLISION_TRACE_FLAG=" + str(body.get_editor_property("collision_trace_flag")))
unreal.log("MAP_100102_COLLISION_ENABLED=" + str(collision_component.get_collision_enabled()))
unreal.log("MAP_100102_RUNTIME_GROUND_TRACE=verify in PIE; commandlet worlds do not provide a usable physics query scene")
unreal.log("MAP_100102_COLLISION_AUDIT_DONE")
