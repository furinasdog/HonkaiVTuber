"""Rebuild the plugin's shared outline material in Unreal Python."""
from pathlib import Path
import runpy

host = runpy.run_path(str(Path(__file__).with_name("setup_viewer.py")), run_name="honkai_host_library")
shared = host["shared"]
shared["initialize_assets"]()
shared["material"]("M_Outline", outline=True)
shared["u"].log("MMD_OUTLINE_UPDATE_COMPLETE")
