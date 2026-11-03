"""Build shared plugin materials and the host's backdrop/viewer level in Unreal Python."""
import unreal as u
from pathlib import Path
import runpy

result = u.PluginBlueprintLibrary.get_plugin_base_dir("HonkaiToonRenderer")
if isinstance(result, tuple):
    assert result[0] is not False, result
    result = result[-1]
assert isinstance(result, str) and result, "Enable HonkaiToonRenderer first"
plugin = Path(result)
shared = runpy.run_path(str(plugin / "Scripts" / "build_materials.py"), run_name="honkai_material_library")
model_materials, cel_lighting = shared["model_materials"], shared["cel_lighting"]
ROOT = "/Game/MmdViewer"
tools, lib = u.AssetToolsHelpers.get_asset_tools(), u.MaterialEditingLibrary


def backdrop_material():
    path = ROOT + "/M_Backdrop"
    material = u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else tools.create_asset("M_Backdrop", ROOT, u.Material, u.MaterialFactoryNew())
    lib.delete_all_material_expressions(material)
    material.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("blend_mode", u.BlendMode.BLEND_OPAQUE)
    tint = lib.create_material_expression(material, u.MaterialExpressionVectorParameter)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", u.LinearColor(.046665, .046665, .046665, 1))
    assert lib.connect_material_property(tint, "RGB", u.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.layout_material_expressions(material)
    assert not lib.recompile_material(material)
    assert u.EditorAssetLibrary.save_loaded_asset(material)
    return material


def main():
    model_materials()
    cel_lighting()
    background = backdrop_material()

    level_path = ROOT + "/L_Viewer"
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    if not u.EditorAssetLibrary.does_asset_exist(level_path):
        assert levels.new_level(level_path)
        actors = u.get_editor_subsystem(u.EditorActorSubsystem)
        sphere = actors.spawn_actor_from_class(u.StaticMeshActor, u.Vector(0, 0, 0))
        sphere.set_actor_label("Studio backdrop")
        comp = sphere.static_mesh_component
        comp.set_static_mesh(u.load_asset("/Engine/BasicShapes/Sphere"))
        comp.set_material(0, background)
        comp.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        comp.set_cast_shadow(False)
        sphere.set_actor_scale3d(u.Vector(500, 500, 500))
        levels.save_current_level()
    else:
        assert levels.load_level(level_path)
        for actor in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
            if isinstance(actor, u.StaticMeshActor):
                comp = actor.static_mesh_component
                if comp.get_material(0) == background:
                    comp.set_cast_shadow(False)
        assert levels.save_current_level()
    u.log("MMD_VIEWER_SETUP_COMPLETE")


if __name__ == "__main__":
    main()
