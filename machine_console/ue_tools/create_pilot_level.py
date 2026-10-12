import unreal

map_path = "/Game/Maps/Pilot00Raiser"
mesh_path = "/Game/Imported/00Raiser/00Raiser_1132252/SkeletalMeshes/00Raiser_1132252.00Raiser_1132252"
if not unreal.EditorLevelLibrary.new_level(map_path):
    raise RuntimeError("Could not create pilot level")

def spawn(cls, location, rotation=unreal.Rotator(0, 0, 0)):
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(cls, location, rotation)
    if not actor:
        raise RuntimeError("Could not spawn " + cls.__name__)
    return actor

floor = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, -5))
floor.set_actor_label("Preview floor")
floor_comp = floor.get_component_by_class(unreal.StaticMeshComponent)
floor_mesh = unreal.load_object(None, "/Engine/BasicShapes/Plane.Plane")
floor_comp.set_static_mesh(floor_mesh)
floor_comp.set_world_scale3d(unreal.Vector(50, 50, 1))

light = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 700), unreal.Rotator(-45, -30, 0))
light.set_actor_label("Preview key light")
light_comp = light.get_component_by_class(unreal.DirectionalLightComponent)
light_comp.set_editor_property("intensity", 8.0)

preview = spawn(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
preview.set_actor_label("00 Raiser 1132252 — imported DXG/VRA pilot")
skeletal = preview.get_component_by_class(unreal.SkeletalMeshComponent)
mesh = unreal.load_asset(mesh_path)
if not mesh:
    raise RuntimeError("Imported skeletal mesh missing: " + mesh_path)
skeletal.set_skeletal_mesh(mesh)
preview.set_actor_hidden_in_game(True)

start = spawn(unreal.PlayerStart, unreal.Vector(0, -500, 110), unreal.Rotator(0, 90, 0))
start.set_actor_label("Pilot spawn")

if not unreal.EditorLevelLibrary.save_current_level():
    raise RuntimeError("Could not save pilot level")
unreal.log("MACHINE_CONSOLE_LEVEL=" + map_path)
