"""Offline UE editor authoring: preserve existing graphs and enable vertex morph permutations."""
import unreal

for name in ("M_Toon", "M_ToonTwoSided", "M_ToonTranslucent", "M_ToonTranslucentTwoSided", "M_Outline", "M_FaceOutline"):
    material = unreal.load_asset("/HonkaiToonRenderer/Materials/" + name)
    assert material, name
    unreal.MaterialEditingLibrary.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_MORPH_TARGETS)
    unreal.MaterialEditingLibrary.recompile_material(material)
    assert material.get_editor_property("used_with_morph_targets"), name
    assert unreal.EditorAssetLibrary.save_loaded_asset(material), name
    unreal.log("VTUBER_MATERIAL_READY " + name)
