"""Stage F - sky, light, atmosphere, post and the cinematic cameras of L_Vellara (run on its own to iterate on the look).

Golden late afternoon on the coast:
  * Sky: a CC0 Poly Haven HDRI (evening_road_01_puresky) on an "Is Sky" dome, rotated so its warm glow sits behind our
    sun over the sea; the real-time sky light captures it, so ambient light comes from a real sky with real clouds.
  * Sun: golden, soft-edged, 30 deg over the sea (gameplay shade is traced against it - do not change SUN_ROT here).
    Cascaded + distance-field shadows (VSM hung the 6 GB GPU), screen-space contact shadows for small detail.
  * Warm bounce: a shadowless fill from the land side stands in for sunlight reflected off whitewash into the lanes.
  * Air: height fog with warm directional inscattering + volumetric fog for light shafts down the alleys.
  * Post: histogram exposure with local exposure (keeps sunlit plaster from blowing out next to deep shade), warm
    split-toning, gentle bloom / lens flare / grain / vignette, a touch of depth of field.

Run:  Scripts/run_ue_python.ps1 -Script Scripts/unreal/sns_lighting.py   (sns_level.py also runs it at the end)
"""
import math
import os
import sys

sys.path.append(os.path.dirname(__file__))
import unreal  # noqa: E402

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lvl = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
PROJECT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
MAP = "/Game/Shade/Maps/L_Vellara"
SUN_ROT = (-30.0, -90.0, 0.0)
HDRI_ID = "evening_road_01_puresky"
HDRI_FILE = os.path.join(PROJECT, "asset-library", "HDRIs", "polyhaven", HDRI_ID, f"{HDRI_ID}_4k.hdr")
HDRI_SUN_U = 0.597  # measured: brightest texel of the HDRI (u = longitude / 360)
# Dome rotation (radians). The HDRI's golden glow ends up behind the village (north), which reads as the coziest sky
# for the views along the lanes and from the harbour; the sun light itself stays over the sea for gameplay.
SKY_YAW = (HDRI_SUN_U - 0.5) * 2 * math.pi - math.radians(90.0)


def log(*a):
    unreal.log("SNS_LIGHT " + " ".join(str(x) for x in a))


def V(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def Rot(pitch=0, yaw=0, roll=0):
    return unreal.Rotator(roll=float(roll), pitch=float(pitch), yaw=float(yaw))


def rgb(r, g, b, a=255):
    """FColor by channel name: positional Color args fill B, G, R (memory order), which silently turned warm light blue."""
    return unreal.Color(r=int(r), g=int(g), b=int(b), a=int(a))


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:  # noqa: BLE001
        log("skip", type(obj).__name__, name, e)
        return False


def spawn(cls, loc, rot=(0, 0, 0), folder="Lighting/Main", tags=()):
    a = actors.spawn_actor_from_class(cls, V(*loc), Rot(*rot))
    a.set_folder_path(folder)
    if tags:
        a.set_editor_property("tags", [unreal.Name(t) for t in tags])
    return a


# =====================================================================================================================
# Sky dome material + texture
# =====================================================================================================================
def import_sky_texture():
    dest = "/Game/Shade/Textures/Sky"
    name = "T_Sky_EveningRoad"
    path = f"{dest}/{name}"
    if not eal.does_asset_exist(path):
        task = unreal.AssetImportTask()
        task.filename = HDRI_FILE
        task.destination_path = dest
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = True
        tools.import_asset_tasks([task])
    tex = eal.load_asset(path)
    log("sky texture", tex.get_class().get_name() if tex else None)
    return tex


def build_sky_material(tex):
    path = "/Game/Shade/Materials/M_SkyDome"
    if eal.does_asset_exist(path):
        m = eal.load_asset(path)
        for e in list(mel.get_material_expressions(m)):
            mel.delete_material_expression(m, e)
    else:
        m = tools.create_asset("M_SkyDome", "/Game/Shade/Materials", unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    setp(m, "is_sky", True)
    y = [-900]

    def node(cls, **props):
        e = mel.create_material_expression(m, cls, -900, y[0])
        y[0] += 140
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def link(a, b, pin=""):
        if not mel.connect_material_expressions(a, "", b, pin):
            log("link failed", a.get_name(), "->", b.get_name(), pin)

    def scalar(name, v):
        return node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(v), group="Sky")

    def vec(name, rgb):
        return node(unreal.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0), group="Sky")

    cam = node(unreal.MaterialExpressionCameraVectorWS)
    yaw = scalar("SkyYaw", 0.0)
    # View direction (camera -> sky), rotated about Z by SkyYaw (radians).
    rotc = node(unreal.MaterialExpressionCustom, code="float s = sin(Yaw), c = cos(Yaw); float3 d = -Cam; return float3(c * d.x - s * d.y, s * d.x + c * d.y, d.z);",
                output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3, description="SkyDir")
    ins = []
    for n in ("Cam", "Yaw"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    rotc.set_editor_property("inputs", ins)
    link(cam, rotc, "Cam")
    link(yaw, rotc, "Yaw")
    is_cube = isinstance(tex, unreal.TextureCube)
    if is_cube:
        samp = node(unreal.MaterialExpressionTextureSampleParameterCube, parameter_name="SkyTex", texture=tex,
                    sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        link(rotc, samp, "UVs")
    else:
        uvc = node(unreal.MaterialExpressionCustom, code="return float2(atan2(D.y, D.x) / 6.2831853 + 0.5, acos(clamp(D.z, -1, 1)) / 3.1415927);",
                   output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT2, description="LongLat")
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", "D")
        uvc.set_editor_property("inputs", [ci])
        link(rotc, uvc, "D")
        samp = node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name="SkyTex", texture=tex,
                    sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        link(uvc, samp, "UVs")
    # Clamp the HDRI sun, tint, then fade into warm haze toward and below the horizon (meets the fogged sea).
    clampn = node(unreal.MaterialExpressionMin)
    link(samp, clampn, "A")
    link(scalar("SunClamp", 14.0), clampn, "B")
    tinted = node(unreal.MaterialExpressionMultiply)
    link(clampn, tinted, "A")
    link(vec("SkyTint", (1.0, 0.93, 0.86)), tinted, "B")
    bright = node(unreal.MaterialExpressionMultiply)
    link(tinted, bright, "A")
    link(scalar("SkyBrightness", 1.0), bright, "B")
    hz = node(unreal.MaterialExpressionCustom, code="float h = saturate(1.0 - max(D.z, 0.0) * Spread); return h * h * (3.0 - 2.0 * h);",
              output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1, description="Haze")
    hins = []
    for n in ("D", "Spread"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        hins.append(ci)
    hz.set_editor_property("inputs", hins)
    link(rotc, hz, "D")
    link(scalar("HazeSpread", 5.0), hz, "Spread")
    hazec = node(unreal.MaterialExpressionMultiply)
    link(vec("HazeColor", (1.0, 0.78, 0.6)), hazec, "A")
    link(scalar("HazeBrightness", 1.0), hazec, "B")
    mix = node(unreal.MaterialExpressionLinearInterpolate)
    link(bright, mix, "A")
    link(hazec, mix, "B")
    amt = node(unreal.MaterialExpressionMultiply)
    link(hz, amt, "A")
    link(scalar("HazeAmount", 0.55), amt, "B")
    link(amt, mix, "Alpha")
    mel.connect_material_property(mix, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.layout_material_expressions(m)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("sky material built, cube" if is_cube else "sky material built, long-lat 2D")
    return m


# =====================================================================================================================
# Scene
# =====================================================================================================================
def clear_folder(prefixes, exact=()):
    n = 0
    for a in actors.get_all_level_actors():
        f = str(a.get_folder_path())
        if f in exact or any(f == p or f.startswith(p + "/") for p in prefixes):
            actors.destroy_actor(a)
            n += 1
    log("cleared", n, "actors in", prefixes)


def look_at(a, target):
    loc = a.get_actor_location()
    a.set_actor_rotation(unreal.MathLibrary.make_rot_from_x(V(target[0] - loc.x, target[1] - loc.y, target[2] - loc.z)), False)


def camera(tag, loc, target, fov, focus=None, fstop=None, folder="Cinematic"):
    c = spawn(unreal.CameraActor, loc, folder=folder, tags=(tag,))
    look_at(c, target)
    cc = c.camera_component
    cc.set_editor_property("field_of_view", float(fov))
    # Cinematic cameras keep the far town sharp (the gameplay post volume adds a little depth of field).
    s = cc.get_editor_property("post_process_settings")
    s.set_editor_property("override_depth_of_field_focal_distance", True)
    s.set_editor_property("depth_of_field_focal_distance", float(focus if focus else 5000.0))
    s.set_editor_property("override_depth_of_field_fstop", True)
    s.set_editor_property("depth_of_field_fstop", float(fstop if fstop else 22.0))
    cc.set_editor_property("post_process_settings", s)
    cc.set_editor_property("post_process_blend_weight", 1.0)
    return c


def tune_practical_lights():
    """Festoon bulbs and street lamps (placed by sns_level.py in Lighting/Warm): a daytime glow, not the key light."""
    n = 0
    for a in actors.get_all_level_actors():
        if str(a.get_folder_path()) != "Lighting/Warm":
            continue
        c = a.get_component_by_class(unreal.PointLightComponent)
        if not c:
            continue
        r = c.get_editor_property("attenuation_radius")
        c.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
        c.set_editor_property("intensity", 260.0 if r > 600 else 170.0)
        c.set_editor_property("light_color", rgb(255, 200, 150))
        c.set_editor_property("attenuation_radius", min(r, 520.0))
        c.set_editor_property("volumetric_scattering_intensity", 0.3)
        n += 1
    log("practical lights tuned", n)


def build_cameras():
    clear_folder(["Cinematic"])
    # Title: from over the harbour to the west, pastel houses stepping up to the dome and the campanile against the
    # golden cirrus; the left of the frame is house fronts, which the title wash darkens for the type.
    camera("TitleCam", (-3000, 4400, -800), (2200, -200, 450), 40.0)
    # End: wide sunset shot of the terrace and tower from over the lower town.
    camera("EndCam", (6200, 2600, 500), (3700, -350, 520), 46.0)
    # QA candidates for -camshots (screenshots without UI, one per camera).
    cands = [
        ((-3600, 4200, -700), (1400, 0, 250), 42.0),
        ((-3000, 4400, -800), (2200, -200, 450), 40.0),
        ((-1500, 4800, -950), (2800, -100, 400), 38.0),
        ((6200, 2600, 500), (3700, -350, 520), 46.0),
        ((-100, -50, 120), (800, -60, 40), 78.0),
        ((2650, -60, 60), (3200, -60, 330), 78.0),
    ]
    for i, (loc, tgt, fov) in enumerate(cands):
        camera(f"CamCandidate_{i}", loc, tgt, fov, folder="Cinematic/Candidates")
    log("cameras placed")


def build_lighting(sky_mat, sky_tex):
    # Only the main rig: festoon / lamp point lights live in Lighting/Warm and stay.
    clear_folder(["Lighting/Main", "Sky"], exact=("Lighting",))
    EXP = os.environ.get("SNS_EXPERIMENT", "")
    # ---------------------------------------------------------------- sun
    sun = spawn(unreal.DirectionalLight, (0, 0, 3000), SUN_ROT)
    sc = sun.get_component_by_class(unreal.DirectionalLightComponent)
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    for k, v in (("intensity", 15.0), ("light_color", rgb(255, 0, 0) if "redsun" in EXP else rgb(255, 228, 196)), ("atmosphere_sun_light", True),
                 ("cast_shadows", True), ("light_source_angle", 0.85), ("light_source_soft_angle", 0.4),
                 ("volumetric_scattering_intensity", 2.2), ("contact_shadow_length", 0.035),
                 ("dynamic_shadow_distance_movable_light", 6500.0), ("dynamic_shadow_cascades", 4),
                 ("cascade_distribution_exponent", 3.0), ("cascade_transition_fraction", 0.2), ("shadow_distance_fadeout_fraction", 0.15),
                 ("use_ray_traced_distance_field_shadows", True), ("distance_field_shadow_distance", 60000.0),
                 ("shadow_bias", 0.4), ("shadow_slope_bias", 0.5), ("cast_cloud_shadows", False)):
        setp(sc, k, v)
    # ---------------------------------------------------------------- atmosphere (aerial perspective; the dome is the visible sky)
    atm = spawn(unreal.SkyAtmosphere, (0, 0, -1300))
    ac = atm.get_component_by_class(unreal.SkyAtmosphereComponent)
    if "noatmo" in EXP:
        actors.destroy_actor(atm)
        log("EXPERIMENT: no SkyAtmosphere")
    for k, v in (("mie_scattering_scale", 0.006), ("mie_anisotropy", 0.82), ("mie_absorption_scale", 0.0008),
                 ("rayleigh_scattering_scale", 0.028), ("multi_scattering_factor", 1.0),
                 ("sky_luminance_factor", unreal.LinearColor(1.0, 0.92, 0.84, 1.0)), ("aerial_pespective_view_distance_scale", 1.6),
                 ("aerial_perspective_view_distance_scale", 1.6), ("height_fog_contribution", 0.6)):
        setp(ac, k, v)
    # ---------------------------------------------------------------- sky dome (CC0 HDRI)
    dome_mesh = eal.load_asset("/Engine/BasicShapes/Sphere")
    dome = actors.spawn_actor_from_object(dome_mesh, V(0, 0, -1300), Rot())
    dome.set_folder_path("Sky")
    dome.set_actor_scale3d(V(36000, 36000, 36000))   # ~18 km radius: beyond the sea plane
    dc = dome.static_mesh_component
    dc.set_material(0, sky_mat)
    dc.set_cast_shadow(False)
    dc.set_collision_profile_name("NoCollision")
    dc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    dc.set_editor_property("can_character_step_up_on", unreal.CanBeCharacterBase.ECB_NO)
    for k, v in (("affect_distance_field_lighting", False), ("affect_dynamic_indirect_lighting", False), ("visible_in_ray_tracing", False),
                 ("never_distance_cull", True), ("bounds_scale", 2.0)):
        setp(dc, k, v)
    mi_path = "/Game/Shade/Materials/MI_SkyDome"
    mi = eal.load_asset(mi_path) if eal.does_asset_exist(mi_path) else tools.create_asset("MI_SkyDome", "/Game/Shade/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, sky_mat)
    # Rotate the HDRI so its glow is behind our sun (sun azimuth: +Y, i.e. 90 deg in UE yaw terms).
    mel.set_material_instance_scalar_parameter_value(mi, "SkyYaw", float(SKY_YAW))
    mel.set_material_instance_scalar_parameter_value(mi, "SkyBrightness", 0.0 if "nodome" in EXP else 0.95)
    mel.set_material_instance_scalar_parameter_value(mi, "SunClamp", 9.0)
    mel.set_material_instance_scalar_parameter_value(mi, "HazeAmount", 0.6)
    mel.set_material_instance_scalar_parameter_value(mi, "HazeBrightness", 0.0 if "nodome" in EXP else 0.78)
    mel.update_material_instance(mi)
    eal.save_loaded_asset(mi)
    dc.set_material(0, mi)
    # ---------------------------------------------------------------- warm bounce from the land side
    fill = spawn(unreal.DirectionalLight, (0, 0, 3200), (-35.0, 100.0, 0.0))
    fc_ = fill.get_component_by_class(unreal.DirectionalLightComponent)
    fc_.set_mobility(unreal.ComponentMobility.MOVABLE)
    for k, v in (("intensity", 0.0 if "nosky" in EXP else 1.3), ("light_color", rgb(255, 224, 196)), ("atmosphere_sun_light", False),
                 ("cast_shadows", False), ("affects_world", True), ("volumetric_scattering_intensity", 0.0), ("specular_scale", 0.25)):
        setp(fc_, k, v)
    # ---------------------------------------------------------------- sky light (captures dome + atmosphere)
    # Ambient straight from the HDRI (not a real-time capture, which would also pick up the deep-blue Rayleigh sky of the
    # SkyAtmosphere and turn every lane cyan). Rotated to match the dome.
    sky = spawn(unreal.SkyLight, (0, 0, 1000))
    slc = sky.get_component_by_class(unreal.SkyLightComponent)
    slc.set_mobility(unreal.ComponentMobility.MOVABLE)
    setp(slc, "source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    setp(slc, "cubemap", sky_tex)
    setp(slc, "source_cubemap_angle", float(-math.degrees(SKY_YAW)))
    for k, v in (("real_time_capture", False), ("intensity", 0.0 if "nosky" in EXP else 0.9), ("light_color", rgb(255, 248, 242)),
                 ("lower_hemisphere_is_black", False), ("lower_hemisphere_color", unreal.LinearColor(0.32, 0.24, 0.17, 1.0)),
                 ("volumetric_scattering_intensity", 0.2)):
        setp(slc, k, v)
    # ---------------------------------------------------------------- height fog + volumetric fog (light shafts)
    fog = spawn(unreal.ExponentialHeightFog, (0, 0, -1300))
    fgc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    if "nofog" in EXP:
        fgc.set_editor_property("enable_volumetric_fog", False)
        fgc.set_editor_property("fog_density", 0.0)
        log("EXPERIMENT: no fog")
    for k, v in (("fog_density", 0.0032), ("fog_height_falloff", 0.05), ("start_distance", 3500.0),
                 ("fog_inscattering_luminance", unreal.LinearColor(0.5, 0.46, 0.44, 1)),
                 ("directional_inscattering_exponent", 7.0), ("directional_inscattering_start_distance", 3000.0),
                 ("directional_inscattering_luminance", unreal.LinearColor(1.0, 0.64, 0.38, 1)),
                 ("enable_volumetric_fog", True), ("volumetric_fog_scattering_distribution", 0.78),
                 ("volumetric_fog_albedo", rgb(255, 246, 236)), ("volumetric_fog_extinction_scale", 0.6),
                 ("volumetric_fog_distance", 5000.0), ("volumetric_fog_start_distance", 80.0)):
        if "nofog" in EXP and k in ("fog_density", "enable_volumetric_fog"):
            continue
        setp(fgc, k, v)
    # ---------------------------------------------------------------- post
    ppv = spawn(unreal.PostProcessVolume, (0, 0, 0))
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")

    def pp(name, value):
        try:
            s.set_editor_property("override_" + name, True)
            s.set_editor_property(name, value)
        except Exception as e:  # noqa: BLE001
            log("pp skip", name, e)

    V4 = unreal.Vector4
    pp("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM)
    pp("auto_exposure_min_brightness", 0.9)
    pp("auto_exposure_max_brightness", 2.2)
    pp("auto_exposure_bias", 0.55)
    pp("auto_exposure_speed_up", 1.6)
    pp("auto_exposure_speed_down", 1.2)
    pp("local_exposure_highlight_contrast_scale", 0.62)
    pp("local_exposure_shadow_contrast_scale", 0.82)
    pp("local_exposure_detail_strength", 1.15)
    pp("bloom_intensity", 0.62)
    pp("bloom_threshold", -1.0)
    pp("lens_flare_intensity", 0.18)
    pp("lens_flare_tint", unreal.LinearColor(1.0, 0.82, 0.62, 1.0))
    pp("vignette_intensity", 0.36)
    pp("film_grain_intensity", 0.05)
    pp("scene_fringe_intensity", 0.22)
    pp("white_temp", 3500.0 if "wbtest" in EXP else 6150.0)
    pp("white_tint", 0.015)
    pp("color_saturation", V4(1.0, 1.0, 1.0, 1.04))
    pp("color_contrast", V4(1.0, 1.0, 1.0, 1.04))
    pp("color_gain_midtones", V4(1.01, 1.0, 0.99, 1.0))
    pp("color_gain_highlights", V4(1.01, 1.0, 0.98, 1.0))
    pp("color_saturation_highlights", V4(1.0, 1.0, 1.0, 0.92))
    pp("color_gain_shadows", V4(1.02, 1.0, 0.99, 1.0))
    pp("color_saturation_shadows", V4(1.0, 1.0, 1.0, 0.9))
    pp("film_shoulder", 0.24)
    pp("film_toe", 0.56)
    pp("ambient_occlusion_intensity", 0.55)
    pp("ambient_occlusion_radius", 90.0)
    pp("depth_of_field_focal_distance", 330.0)
    pp("depth_of_field_fstop", 4.0)
    pp("depth_of_field_sensor_width", 24.576)
    pp("motion_blur_amount", 0.18)
    pp("motion_blur_max", 2.0)
    pp("lumen_scene_lighting_quality", 1.25)
    pp("lumen_scene_detail", 1.5)
    pp("lumen_final_gather_quality", 2.0)
    pp("lumen_reflection_quality", 1.0)
    pp("indirect_lighting_intensity", 1.85)
    ppv.set_editor_property("settings", s)
    log("lighting built")


def main():
    if str(lvl.get_current_level().get_path_name()).split(".")[0].split(":")[0] != MAP:
        lvl.load_level(MAP)
    tex = import_sky_texture()
    mat = build_sky_material(tex)
    build_lighting(mat, tex)
    tune_practical_lights()
    build_cameras()
    lvl.save_current_level()
    eal.save_directory("/Game/Shade/Materials", only_if_is_dirty=True, recursive=True)
    log("ALL DONE")


main()
