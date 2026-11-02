"""Rebuild the six model materials, then inspect their actual alpha connections.

Run in an Unreal Python commandlet. Scene, postprocess and shading parameters
stay in setup_viewer's shared factory; no character assets are read or created.
"""
from pathlib import Path
import runpy

library = runpy.run_path(str(Path(__file__).with_name("setup_viewer.py")), run_name="honkai_material_library")
u = library["u"]
lib = library["lib"]


def check_visibility(material):
    translucent = material.get_editor_property("blend_mode") == u.BlendMode.BLEND_TRANSLUCENT
    assert translucent or material.get_editor_property("blend_mode") == u.BlendMode.BLEND_MASKED
    prop = u.MaterialProperty.MP_OPACITY if translucent else u.MaterialProperty.MP_OPACITY_MASK
    root = lib.get_material_property_input_node(material, prop)
    assert root, material.get_name()

    def walk(expression):
        pending, seen = [expression], set()
        while pending:
            current = pending.pop()
            if not current or current in seen:
                continue
            seen.add(current)
            yield current
            pending.extend(child for child in lib.get_inputs_for_material_expression(material, current) if child)

    alpha_edges = []
    for expression in walk(root):
        for child in lib.get_inputs_for_material_expression(material, expression):
            if isinstance(child, u.MaterialExpressionVertexColor):
                pin = lib.get_input_node_output_name_for_material_expression(expression, child)
                # Python bindings return the out string (or a bool/string tuple).
                if isinstance(pin, tuple):
                    assert pin[0]
                    pin = pin[-1]
                assert pin == "A", (material.get_name(), pin)
                alpha_edges.append(child)
    assert len(alpha_edges) == 1, material.get_name()
    for color_property in (u.MaterialProperty.MP_BASE_COLOR, u.MaterialProperty.MP_EMISSIVE_COLOR, u.MaterialProperty.MP_WORLD_POSITION_OFFSET):
        color_root = lib.get_material_property_input_node(material, color_property)
        assert not any(isinstance(node, u.MaterialExpressionVertexColor) for node in walk(color_root)), material.get_name()
    u.log("MMD_PART_VISIBILITY_MATERIAL " + material.get_name() + " alpha=A color_unchanged=1")


for material in library["model_materials"]():
    check_visibility(material)
u.log("MMD_PART_VISIBILITY_MATERIALS_COMPLETE count=6")
