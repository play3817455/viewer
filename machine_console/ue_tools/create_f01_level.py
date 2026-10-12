import unreal

MAP_PATH = "/Game/Maps/Map100102Preview"
SCENE_MESH_PATH = "/Game/Imported/F01/Map_100102_scene/StaticMeshes/Map_100102_scene.Map_100102_scene"
COLLISION_MESH_PATH = "/Game/Imported/F01/Map_100102_collision/StaticMeshes/Map_100102_collision.Map_100102_collision"

if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
    if not unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH):
        raise RuntimeError("Could not load existing F01 pilot level")
else:
    if not unreal.EditorLevelLibrary.new_level(MAP_PATH):
        raise RuntimeError("Could not create F01 pilot level")

def find_label(label):
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None

for stale_label in ("F01 00 — baked DXG/VRA scene", "F01 00 — source physics=1 collision",
                    "F01 00 — measured spawn landing pad"):
    stale = find_label(stale_label)
    if stale:
        stale.destroy_actor()

def spawn(cls, location, rotation=unreal.Rotator(0, 0, 0), label=None):
    if label:
        existing = find_label(label)
        if existing:
            return existing
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(cls, location, rotation)
    if not actor:
        raise RuntimeError("Could not spawn " + cls.__name__)
    return actor

scene_actor = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, 0), label="Map 100102 — map.csv scene + sky")
scene_actor.set_actor_label("Map 100102 — map.csv scene + sky")
scene_component = scene_actor.get_component_by_class(unreal.StaticMeshComponent)
scene_mesh = unreal.load_asset(SCENE_MESH_PATH)
if not scene_mesh:
    raise RuntimeError("Imported F01 static mesh missing: " + SCENE_MESH_PATH)
scene_component.set_static_mesh(scene_mesh)
scene_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)

collision_actor = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, 0), label="Map 100102 — physics=1 collision")
collision_actor.set_actor_label("Map 100102 — physics=1 collision")
collision_component = collision_actor.get_component_by_class(unreal.StaticMeshComponent)
collision_mesh = unreal.load_asset(COLLISION_MESH_PATH)
if not collision_mesh:
    raise RuntimeError("Imported F01 collision mesh missing: " + COLLISION_MESH_PATH)
collision_component.set_static_mesh(collision_mesh)
collision_component.set_collision_profile_name("BlockAll")
collision_component.set_visibility(False)
collision_actor.set_actor_hidden_in_game(True)

light = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 30000), unreal.Rotator(-45, -30, 0), label="F01 key light")
light.set_actor_label("F01 key light")
light.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("intensity", 8.0)

# map.csv debug spawn is source XYZ=(-14000, 2000, 12000); convert the source Y-up
# coordinates to UE's Z-up axes. The player starts slightly above this map-authored point.
start = spawn(unreal.PlayerStart, unreal.Vector(-14000, 12000, 2200), unreal.Rotator(0, 0, 0), label="Map 100102 debug-spawn preview start")
start.set_actor_label("Map 100102 debug-spawn preview start")

if not unreal.EditorLevelLibrary.save_current_level():
    raise RuntimeError("Could not save F01 pilot level")
unreal.log("MACHINE_CONSOLE_LEVEL=" + MAP_PATH)
