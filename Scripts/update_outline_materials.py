"""Rebuild only the shared outer-outline material; preserve surface, face and scene assets."""
from pathlib import Path
import runpy

library = runpy.run_path(str(Path(__file__).with_name("setup_viewer.py")), run_name="honkai_material_library")
library["material"]("M_Outline", outline=True)
library["u"].log("MMD_OUTLINE_UPDATE_COMPLETE")
