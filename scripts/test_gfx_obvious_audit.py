#!/usr/bin/env python3
#ai: fixtures pin the GFX obvious-audit rules and their conservative boundaries.
import tempfile
import unittest
from pathlib import Path

import gfx_obvious_audit as audit


class UniformActivationOrderTests(unittest.TestCase):
    def audit_source(self, source):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "src" / "renderer" / "gfx" / "fixture.c"
            path.parent.mkdir(parents=True)
            path.write_text(source)
            findings = []
            audit.audit_uniform_activation_order(root, findings)
            return findings

    def test_raw_uniform_before_activation_is_reported(self):
        findings = self.audit_source(
            "void broken(void) {\n"
            "    gX_RHI_UniformInt(location, 1);\n"
            "    gX_RHI_Shader_Use(program);\n"
            "}\n"
        )
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0][0:4],
                         ("WARN", "src/renderer/gfx/fixture.c", 2, "GFX001"))

    def test_shader_activation_apis_and_source_order_are_accepted(self):
        findings = self.audit_source(
            "void good(void) {\n"
            "    // gX_RHI_UniformInt(hidden, 0);\n"
            "    gX_Drawing_BeginShader(shader);\n"
            "    gX_RHI_UniformInt(location, 1);\n"
            "}\n"
            "void wrapper_upload(void) {\n"
            "    gX_Shader_SetValue_Int(shader, location, 1);\n"
            "    gX_RHI_UniformInt(location, 2);\n"
            "    gX_RHI_Shader_Use(shader);\n"
            "}\n"
            "void fullscreen_helper(void) {\n"
            "    gX_RendererGFX_Fullscreen_Begin(target, shader);\n"
            "    gX_RHI_UniformInt(location, 1);\n"
            "}\n"
            "void delegated(void) {\n"
            "    gX_RHI_UniformInt(location, 1);\n"
            "}\n"
            "void string_only(void) {\n"
            '    log("gX_RHI_UniformInt(x, 1); gX_RHI_Shader_Use(y);");\n'
            "}\n"
        )
        self.assertEqual(findings, [])


class SkinningCapacityTests(unittest.TestCase):
    def make_fixture(self, root, caps, alias=False):
        constants = root / "src" / "renderer" / "gfx" / "constants.h"
        constants.parent.mkdir(parents=True)
        if alias:
            constants.write_text(
                "#define gX_GFX_SKIN_MAX_BONES gX_SKIN_MAX_BONES\n")
            shared = root / audit.SKIN_CAP_SHARED_HEADER
            shared.parent.mkdir(parents=True)
            shared.write_text("#define gX_SKIN_MAX_BONES 128\n")
        else:
            constants.write_text("#define gX_GFX_SKIN_MAX_BONES 128\n")
        for relative, cap in zip(audit.SKIN_SHADER_FILES, caps):
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"uniform mat4 boneMatrices[{cap}];\n")

    def test_matching_shader_arrays_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root, (128, 128, 128))
            findings = []
            audit.audit_skinning_capacity(root, findings)
            self.assertEqual(findings, [])

    def test_shader_capacity_drift_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root, (128, 64, 128))
            findings = []
            audit.audit_skinning_capacity(root, findings)
            self.assertEqual(len(findings), 1)
            self.assertEqual(findings[0][0], "ERROR")
            self.assertEqual(findings[0][1], "resources/shader/gfx_depth.vert")
            self.assertEqual(findings[0][3], "GFX002")

    def test_aliased_cap_resolves_shared_constant(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root, (128, 128, 128), alias=True)
            findings = []
            audit.audit_skinning_capacity(root, findings)
            self.assertEqual(findings, [])

    def test_unresolved_alias_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root, (128, 128, 128), alias=True)
            (root / audit.SKIN_CAP_SHARED_HEADER).unlink()
            findings = []
            audit.audit_skinning_capacity(root, findings)
            self.assertEqual(len(findings), 1)
            self.assertEqual(findings[0][3], "GFX002")
            self.assertIn("unresolved", findings[0][4])


class SkinningAttributeLocationTests(unittest.TestCase):
    def make_fixture(self, root, declarations):
        for relative, declaration in zip(audit.SKIN_SHADER_FILES, declarations):
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(declaration)

    def valid_declarations(self):
        return ("layout(location = 7) in vec4 vertexBoneIndices;\n"
                "layout(location = 8) in vec4 vertexBoneWeights;\n")

    def test_all_skinned_shader_attributes_match_fixed_locations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root, (self.valid_declarations(),) * 3)
            findings = []
            audit.audit_skinning_attribute_locations(root, findings)
            self.assertEqual(findings, [])

    def test_swapped_locations_are_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            swapped = ("layout(location = 8) in vec4 vertexBoneIndices;\n"
                       "layout(location = 7) in vec4 vertexBoneWeights;\n")
            self.make_fixture(root, (self.valid_declarations(), swapped,
                                     self.valid_declarations()))
            findings = []
            audit.audit_skinning_attribute_locations(root, findings)
            self.assertEqual(len(findings), 2)
            self.assertTrue(all(finding[3] == "GFX004" for finding in findings))
            self.assertTrue(all(finding[1] == "resources/shader/gfx_depth.vert"
                                for finding in findings))

    def test_missing_explicit_location_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            missing = ("in vec4 vertexBoneIndices;\n"
                       "layout(location = 8) in vec4 vertexBoneWeights;\n")
            self.make_fixture(root, (missing, self.valid_declarations(),
                                     self.valid_declarations()))
            findings = []
            audit.audit_skinning_attribute_locations(root, findings)
            self.assertEqual(len(findings), 1)
            self.assertEqual(findings[0][3], "GFX004")
            self.assertIn("explicit location 7", findings[0][4])


class RaylibSkinningBuildTests(unittest.TestCase):
    def audit_cmake(self, text):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "CMakeLists.txt").write_text(text)
            findings = []
            audit.audit_raylib_skinning_build(root, findings)
            return findings

    def test_raylib_target_definition_passes_across_lines(self):
        findings = self.audit_cmake(
            "target_compile_definitions(\n"
            "    raylib PRIVATE\n"
            "    SUPPORT_GPU_SKINNING=1\n"
            ")\n"
        )
        self.assertEqual(findings, [])

    def test_other_target_wrong_value_and_comments_do_not_satisfy_contract(self):
        findings = self.audit_cmake(
            "# target_compile_definitions(raylib PRIVATE SUPPORT_GPU_SKINNING=1)\n"
            "#[[\n"
            "target_compile_definitions(raylib PRIVATE SUPPORT_GPU_SKINNING=1)\n"
            "]]\n"
            "target_compile_definitions(gX_build PRIVATE SUPPORT_GPU_SKINNING=1)\n"
            "target_compile_definitions(raylib PRIVATE SUPPORT_GPU_SKINNING=0)\n"
        )
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0][0], "ERROR")
        self.assertEqual(findings[0][3], "GFX005")


class MaterialUniformSourceTests(unittest.TestCase):
    def make_fixture(self, root, overrides=None):
        overrides = overrides or {}
        grouped = {}
        for relative, function, setter, location, source, label in audit.MATERIAL_UNIFORM_SOURCES:
            grouped.setdefault((relative, function), []).append(
                f"    {setter}(shader, {location}, {overrides.get(label, source)});"
            )
        for (relative, function), calls in grouped.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(
                f"void {function}(void) {{\n" + "\n".join(calls) + "\n}\n"
            )

    def test_canonical_material_uniform_sources_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root)
            findings = []
            audit.audit_material_uniform_sources(root, findings)
            self.assertEqual(findings, [])

    def test_literal_feature_factor_and_has_flags_are_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            overrides = {
                "enableMapAlbedo": "true",
                "albedoFactor": "1.0f",
                "albedoMapFactor": "1.0f",
            }
            for *_, label in audit.MATERIAL_UNIFORM_SOURCES:
                if label.startswith("has"):
                    overrides[label] = "1"
            self.make_fixture(root, overrides)
            findings = []
            audit.audit_material_uniform_sources(root, findings)
            self.assertEqual(len(findings), len(overrides))
            self.assertTrue(all(finding[3] == "GFX006" for finding in findings))
            messages = " ".join(finding[4] for finding in findings)
            for label in overrides:
                self.assertIn(label, messages)


class MeshMapPresenceTests(unittest.TestCase):
    def make_fixture(self, root):
        path = root / "src" / "renderer" / "gfx" / "model.c"
        path.parent.mkdir(parents=True)
        path.write_text(
            "void gX_RendererGFX_Model_MeshBindMaps(void) {\n"
            "    gX_I32 mapFlags = 0;\n"
            "    for (gX_I32 mapIndex = 0; mapIndex <= gX_MATERIAL_MAP_HEIGHT; mapIndex++) {\n"
            "        gX_Texture texture = {0};\n"
            "        if (meshMat->maps) texture = meshMat->maps[mapIndex].texture;\n"
            "        if (injectedMaps[mapIndex] > 0) texture.id = injectedMaps[mapIndex];\n"
            "        if (mapIndex == gX_MATERIAL_MAP_ALBEDO\n"
            "            && (mask & gX_GFX_MMASK_ALBEDO_MAP)\n"
            "            && model->params.effectiveAlbedoMap[meshIndex] > 0) {\n"
            "            texture.id = (gX_U32)model->params.effectiveAlbedoMap[meshIndex];\n"
            "        }\n"
            "        if (mapIndex == gX_MATERIAL_MAP_EMISSION\n"
            "            && (mask & gX_GFX_MMASK_EMISSIVE_MAP)\n"
            "            && model->params.effectiveEmissiveMap[meshIndex] > 0) {\n"
            "            texture.id = (gX_U32)model->params.effectiveEmissiveMap[meshIndex];\n"
            "        }\n"
            "        if (texture.id > 0) mapFlags |= 1 << mapIndex;\n"
            "        if (texture.id == 0) texture.id = gX_RHI_Tex_GetDefault();\n"
            "    }\n"
            "    if (boundFlags && *boundFlags != mapFlags) {\n"
            "        for (gX_I32 mapIndex = 0; mapIndex <= gX_MATERIAL_MAP_HEIGHT; mapIndex++) {\n"
            "            if (flagLocations[mapIndex] >= 0)\n"
            "                gX_Shader_SetValue_Int(shaderProg, flagLocations[mapIndex],\n"
            "                    (mapFlags >> mapIndex) & 1);\n"
            "        }\n"
            "        *boundFlags = mapFlags;\n"
            "    }\n"
            "}\n"
        )
        draw = root / "src" / "renderer" / "gfx" / "model.c"
        with draw.open("a") as output:
            output.write(
                "void gX_RendererGFX_Model_Draw(void) {\n"
                "    gX_U32 boundMaps[8] = {0};\n"
                "    gX_I32 boundMapFlags = -1;\n"
                "    for (gX_I32 mi = 0; mi < meshCount; mi++) {\n"
                "        gX_RendererGFX_Model_MeshBindMaps(model, program, locations,\n"
                "            baseMat, meshMat, mi, injectedMaps, boundMaps, &boundMapFlags);\n"
                "    }\n"
                "}\n"
            )

    def audit_fixture(self, root):
        findings = []
        audit.audit_mesh_map_presence(root, findings)
        return findings

    def test_mesh_injected_and_sidecar_maps_are_classified_before_default(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root)
            self.assertEqual(self.audit_fixture(root), [])

    def test_default_fallback_cannot_make_a_missing_map_present(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root)
            path = root / "src" / "renderer" / "gfx" / "model.c"
            text = path.read_text()
            presence = "        if (texture.id > 0) mapFlags |= 1 << mapIndex;\n"
            fallback = "        if (texture.id == 0) texture.id = gX_RHI_Tex_GetDefault();\n"
            path.write_text(text.replace(presence + fallback, fallback + presence))
            findings = self.audit_fixture(root)
            self.assertTrue(any("fallback must occur after presence" in finding[4]
                                for finding in findings))

    def test_injection_and_per_mesh_reset_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root)
            path = root / "src" / "renderer" / "gfx" / "model.c"
            text = path.read_text()
            text = text.replace("    gX_I32 mapFlags = 0;\n", "")
            text = text.replace(
                "        if (injectedMaps[mapIndex] > 0) texture.id = injectedMaps[mapIndex];\n",
                "",
            )
            path.write_text(text)
            findings = self.audit_fixture(root)
            descriptions = " ".join(finding[4] for finding in findings)
            self.assertIn("reset map-presence bits", descriptions)
            self.assertIn("apply injected map overrides", descriptions)

    def test_sidecar_override_must_match_the_mesh_and_map_kind(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root)
            path = root / "src" / "renderer" / "gfx" / "model.c"
            path.write_text(path.read_text().replace(
                "gX_GFX_MMASK_ALBEDO_MAP", "gX_GFX_MMASK_EMISSIVE_MAP", 1))
            findings = self.audit_fixture(root)
            self.assertTrue(any("effective albedo override" in finding[4]
                                for finding in findings))

    def test_flag_cache_starts_invalid_for_first_mapless_or_neighbor_mesh(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root)
            draw = root / "src" / "renderer" / "gfx" / "model.c"
            draw.write_text(draw.read_text().replace(
                "gX_I32 boundMapFlags = -1;", "gX_I32 boundMapFlags = 0;"))
            findings = self.audit_fixture(root)
            self.assertTrue(any("must start at -1" in finding[4]
                                for finding in findings))


#ai: owned source fixtures exercise available and absent effect-resource paths independently.
class EffectResourceFlagTests(unittest.TestCase):
    def render_call(self, name, arguments):
        return f"{name}({', '.join(arguments)});"

    def make_fixture(self, root):
        grouped = {}
        sources = {}
        for spec in audit.EFFECT_RESOURCE_FLAG_PATHS:
            grouped.setdefault((spec["relative"], spec["function"]), []).append(spec)
        for (relative, function), specs in grouped.items():
            body = []
            for spec in specs:
                guards = {
                    (condition, name, tuple(arguments))
                    for condition, name, arguments in spec["present_guards"]
                }
                present = []
                for name, arguments in spec["present_calls"]:
                    guarded = next((condition for condition, guard_name, guard_args in guards
                                    if guard_name == name and guard_args == tuple(arguments)), None)
                    call = self.render_call(name, arguments)
                    if guarded is None:
                        present.append(f"        {call}")
                    else:
                        present.extend((
                            f"        if ({guarded}) {{",
                            f"            {call}",
                            "        }",
                        ))
                absent = [f"        {self.render_call(name, arguments)}"
                          for name, arguments in spec["absent_calls"]]
                body.append(f"    if ({spec['present_condition']}) {{")
                body.extend(present)
                if spec["absent_condition"] is None:
                    body.append("    } else {")
                else:
                    body.append(f"    }} else if ({spec['absent_condition']}) {{")
                body.extend(absent)
                body.append("    }")
            if function == "gX_RendererGFX_Draw_SSR":
                body.extend((
                    "    gX_I1 ssrHasEnv = (gfx->reflection.cubeUpdatedThisFrame",
                    "        && gfx->reflection.dynamicCubemap.id > 0);",
                    "    gX_Texture ssrEnvTex = gfx->reflection.dynamicCubemap;",
                    "    if (gfx->reflection.probesReady && gfx->reflection.probeCount > 0) {",
                    "        gX_F32 bestD = -1.0f;",
                    "        for (gX_U32 epi = 0; epi < gfx->reflection.probeCount; epi++) {",
                    "            gX_RendererGFX_Probe *ep = &gfx->reflection.probes[epi];",
                    "            if (!ep->baked || ep->cube.id == 0) continue;",
                    "            if (bestD < 0.0f || dist < bestD) {",
                    "                bestD = dist;",
                    "                ssrEnvTex = ep->cube;",
                    "            }",
                    "        }",
                    "        if (bestD >= 0.0f) ssrHasEnv = true;",
                    "    }",
                    "    if (gfx->ssr.envMapLoc >= 0 && ssrHasEnv)",
                    "        gX_Shader_SetValue_CubemapSlot(sha, gfx->ssr.envMapLoc,",
                    "            ssrEnvTex, gX_GFX_TEXUNIT_SSR_ENV);",
                    "    if (gfx->ssr.hasEnvMapLoc >= 0)",
                    "        gX_Shader_SetValue_Int(sha, gfx->ssr.hasEnvMapLoc,",
                    "            ssrHasEnv ? 1 : 0);",
                ))
            sources.setdefault(relative, []).append(
                f"void {function}(void) {{\n" + "\n".join(body) + "\n}\n"
            )
        for relative, functions in sources.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("\n".join(functions))

    def audit_fixture(self, root):
        findings = []
        audit.audit_effect_resource_flag_paths(root, findings)
        return findings

    def test_all_effect_resources_have_present_and_absent_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root)
            self.assertEqual(self.audit_fixture(root), [])

    def test_each_present_path_must_bind_and_set_its_flag(self):
        for spec in audit.EFFECT_RESOURCE_FLAG_PATHS:
            with self.subTest(label=spec["label"]), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.make_fixture(root)
                path = root / spec["relative"]
                text = path.read_text()
                name, arguments = spec["present_calls"][0]
                text = text.replace(self.render_call(name, arguments), "", 1)
                path.write_text(text)
                findings = self.audit_fixture(root)
                self.assertTrue(any(
                    spec["label"] in item[4] and "bind the available" in item[4]
                    for item in findings
                ))

    def test_each_absent_path_must_reset_its_flag(self):
        for spec in audit.EFFECT_RESOURCE_FLAG_PATHS:
            with self.subTest(label=spec["label"]), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.make_fixture(root)
                path = root / spec["relative"]
                text = path.read_text()
                name, arguments = spec["absent_calls"][0]
                text = text.replace(self.render_call(name, arguments), "", 1)
                path.write_text(text)
                findings = self.audit_fixture(root)
                self.assertTrue(any(spec["label"] in item[4]
                                    and ("reset path" in item[4] or "zero" in item[4])
                                    for item in findings))

    def test_environment_flag_is_not_unconditionally_enabled(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(root)
            path = root / "src" / "renderer" / "gfx" / "draw.c"
            text = path.read_text().replace("ssrHasEnv ? 1 : 0", "1")
            path.write_text(text)
            findings = self.audit_fixture(root)
            self.assertTrue(any("hasEnvMap" in item[4]
                                and "availability value" in item[4]
                                for item in findings))


class TextureUnitBoundsTests(unittest.TestCase):
    def make_fixture(self, root, constants, source):
        header = root / "src" / "renderer" / "gfx" / "constants.h"
        header.parent.mkdir(parents=True)
        header.write_text(constants)
        calls = root / "src" / "renderer" / "gfx" / "bind_fixture.c"
        calls.write_text(source)

    def test_zero_and_31_are_valid_and_dynamic_slots_are_skipped(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(
                root,
                "#define gX_GFX_TEXUNIT_FIRST 0\n"
                "#define gX_GFX_TEXUNIT_LAST 31\n",
                "void bind(gX_U32 texture, gX_U32 slot) {\n"
                "    gX_RHI_Tex_BindOnUnit(texture, 0);\n"
                "    gX_RHI_Tex_BindCubemapOnUnit(texture, 31);\n"
                "    gX_RHI_Tex_BindOnUnit(texture, (gX_U32)0u);\n"
                "    gX_RHI_Tex_BindCubemapOnUnit(texture, 0x1fu);\n"
                "    gX_Shader_SetValue_TextureSlot(shader, loc, tex, 31);\n"
                "    gX_Shader_SetValue_CubemapSlot(shader, loc, tex, 31);\n"
                "    gX_RHI_Tex_BindOnUnit(texture, slot + 1);\n"
                "    gX_Shader_SetValue_TextureSlot(shader, loc, tex, slot + 1);\n"
                "}\n",
            )
            findings = []
            counts = audit.audit_texture_unit_bounds(root, findings)
            self.assertEqual(findings, [])
            self.assertEqual(counts, (2, 6))

    def test_out_of_range_constant_and_literal_bind_slots_are_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_fixture(
                root,
                "#define gX_GFX_TEXUNIT_BAD 32\n",
                "void bind(gX_U32 texture, gX_U32 slot) {\n"
                "    gX_RHI_Tex_BindOnUnit(texture, 32);\n"
                "    gX_RHI_Tex_BindCubemapOnUnit(texture, -1);\n"
                "    gX_Shader_SetValue_TextureSlot(shader, loc, tex, 32);\n"
                "    gX_Shader_SetValue_CubemapSlot(shader, loc, tex, -1);\n"
                "    gX_RHI_Tex_BindOnUnit(texture, slot + 1);\n"
                "}\n",
            )
            findings = []
            audit.audit_texture_unit_bounds(root, findings)
            self.assertEqual(len(findings), 5)
            self.assertTrue(all(finding[3] == "GFX003" for finding in findings))
            self.assertEqual([finding[0] for finding in findings],
                             ["ERROR"] * 5)


if __name__ == "__main__":
    unittest.main()
