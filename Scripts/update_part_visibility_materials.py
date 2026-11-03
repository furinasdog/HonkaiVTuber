"""Rebuild shared model parents and verify the plugin's actual alpha graphs."""
from pathlib import Path
import runpy

host = runpy.run_path(str(Path(__file__).with_name("setup_viewer.py")), run_name="honkai_host_library")
host["shared"]["model_materials"]()
runpy.run_path(str(host["plugin"] / "Scripts" / "verify_materials.py"), run_name="__main__")
