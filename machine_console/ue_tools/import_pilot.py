import unreal

source = r"F:/9ymsgo/9you/viewer/machine_console/ue/Content/Imported/00Raiser/00Raiser_1132252.gltf"
destination = "/Game/Imported/00Raiser"

task = unreal.AssetImportTask()
task.filename = source
task.destination_path = destination
task.destination_name = "00Raiser_1132252"
task.automated = True
task.replace_existing = True
task.save = True
unreal.EditorAssetLibrary.delete_directory(destination + "/00Raiser_1132252")
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
unreal.log("MACHINE_CONSOLE_IMPORT_RESULT=" + repr(list(task.imported_object_paths)))
if not task.imported_object_paths:
    raise RuntimeError("UE Interchange did not import the pilot glTF")
