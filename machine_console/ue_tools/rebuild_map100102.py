"""Run in the UE 5.8 Python commandlet to refresh the map 100102 pilot."""
from pathlib import Path

TOOLS = Path(__file__).resolve().parent

def run(name):
    source = (TOOLS / name).read_text(encoding="utf-8")
    exec(compile(source, str(TOOLS / name), "exec"), globals(), globals())

run("import_f01_scene.py")
run("create_f01_level.py")
unreal.log("MACHINE_CONSOLE_MAP_100102_REBUILD_DONE")
