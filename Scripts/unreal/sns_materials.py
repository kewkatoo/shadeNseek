"""Stage B - master materials + instances.

Masters (parameter names are shared with C++ where noted):
  M_Surface    architecture/ground PBR: Tint, Brightness, Tiling, RoughMul, NormalStrength, Dirt at the object's base,
               macro variation so plaster never looks tiled
  M_Paint      painted planks (shutters/doors/crates) with chipped wear showing the wood beneath
  M_Prop       Poly Haven props (diff / nor_dx / arm)
  M_Foliage    masked two-sided foliage with wind (alpha texture)
  M_Card       masked two-sided card (RGBA diffuse) - bougainvillea / leafy sprays
  M_Cloth      two-sided fabric with optional hanging flutter (laundry), used static for awnings
  M_Iron, M_Glass, M_Emissive, M_Water, M_Majolica
  M_ShadeBody / M_ShadeShell / M_ShadeWhisker  (C++: Burn, Dissolve, Pulse)  black-velvet ink cat, smoky fur shells,
               faint violet sheen, ember burn front, dissolve
  M_ShadeEye   (C++: Glow, Burn, Lid)  glowing amber eyes with slit pupils and eyelids
  M_CatFur / M_CatShell  (C++: CoatTint, CoatDark)  vertex-colour tabby, fur normal + sheen; shells = combed fur strands
               (UV1.x = shell height, T_FurStrands)
  M_CatEye     (C++: Lid, LidColor)  wet amber-green eye, slit pupil, limbal ring, blinking lids
  M_CatWhisker
  M_Wisp       translucent ink/ember/dust puffs driven by custom primitive data 0..2
"""
import unreal

tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary
MAT_DIR = "/Game/Shade/Materials"


def log(*a):
    unreal.log("SNS_MAT " + " ".join(str(x) for x in a))


def tex(path):
    p = path if path.startswith("/Game") else f"/Game/Shade/Textures/Gen/{path}"
    if eal.does_asset_exist(p):
        return eal.load_asset(p)
    log("missing texture", p)
    return None


def ph(set_id, m):
    return tex(f"/Game/Shade/Textures/{set_id}/T_{set_id}_{m}")


FLAT_N = None
FLAT_ARM = None
WHITE = None


class Graph:
    def __init__(self, name, blend=None, shading=None, two_sided=False):
        path = f"{MAT_DIR}/{name}"
        if eal.does_asset_exist(path):
            self.m = eal.load_asset(path)
            # delete_all_material_expressions() leaves the graph intact headless; remove nodes one by one.
            for e in list(mel.get_material_expressions(self.m)):
                mel.delete_material_expression(self.m, e)
            mel.delete_all_material_expressions(self.m)
            left = mel.get_num_material_expressions(self.m)
            if left:
                log("WARNING", name, left, "expressions survived the reset")
        else:
            self.m = tools.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        self.m.set_editor_property("blend_mode", blend if blend is not None else unreal.BlendMode.BLEND_OPAQUE)
        self.m.set_editor_property("shading_model", shading if shading is not None else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        self.m.set_editor_property("two_sided", two_sided)
        # Headless builds never get the editor's automatic usage flags; without them the game falls back to the
        # default material on Nanite meshes.
        self.m.set_editor_property("used_with_nanite", True)
        self.m.set_editor_property("used_with_static_lighting", False)
        self.x, self.y = -1800, -800

    def node(self, cls, **props):
        e = mel.create_material_expression(self.m, cls, self.x, self.y)
        self.y += 130
        if self.y > 1600:
            self.y = -800
            self.x += 300
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def link(self, pin, to, inp=""):
        if not mel.connect_material_expressions(pin[0], pin[1], to, inp):
            log("link failed", self.m.get_name(), pin[0].get_name(), pin[1], "->", to.get_name(), inp)

    def out(self, pin, prop):
        mel.connect_material_property(pin[0], pin[1], prop)

    def scalar(self, name, v, group="Shade", cpd=None):
        e = self.node(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(v), group=group)
        if cpd is not None:
            e.set_editor_property("use_custom_primitive_data", True)
            e.set_editor_property("primitive_data_index", int(cpd))
        return (e, "")

    def vec(self, name, rgb, group="Shade"):
        return (self.node(unreal.MaterialExpressionVectorParameter, parameter_name=name,
                          default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0), group=group), "")

    def const(self, v):
        return (self.node(unreal.MaterialExpressionConstant, r=float(v)), "")

    def const3(self, rgb):
        return (self.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1)), "")

    def texp(self, name, default, sampler, uv=None):
        e = self.node(unreal.MaterialExpressionTextureSampleParameter2D, parameter_name=name, sampler_type=sampler, group="Textures")
        if default:
            e.set_editor_property("texture", default)
        if uv:
            self.link(uv, e, "UVs")
        return e

    def texs(self, texture, sampler, uv=None):
        e = self.node(unreal.MaterialExpressionTextureSample, sampler_type=sampler)
        if texture:
            e.set_editor_property("texture", texture)
        if uv:
            self.link(uv, e, "UVs")
        return e

    def bin(self, cls, a, b):
        e = self.node(cls)
        self.link(a, e, "A")
        self.link(b, e, "B")
        return (e, "")

    def mul(self, a, b): return self.bin(unreal.MaterialExpressionMultiply, a, b)
    def add(self, a, b): return self.bin(unreal.MaterialExpressionAdd, a, b)
    def sub(self, a, b): return self.bin(unreal.MaterialExpressionSubtract, a, b)
    def div(self, a, b): return self.bin(unreal.MaterialExpressionDivide, a, b)
    def dot(self, a, b): return self.bin(unreal.MaterialExpressionDotProduct, a, b)
    def append(self, a, b): return self.bin(unreal.MaterialExpressionAppendVector, a, b)

    def lerp(self, a, b, alpha):
        e = self.node(unreal.MaterialExpressionLinearInterpolate)
        self.link(a, e, "A")
        self.link(b, e, "B")
        self.link(alpha, e, "Alpha")
        return (e, "")

    def one(self, cls, a):
        e = self.node(cls)
        self.link(a, e, "")
        return (e, "")

    def sat(self, a): return self.one(unreal.MaterialExpressionSaturate, a)
    def absv(self, a): return self.one(unreal.MaterialExpressionAbs, a)
    def sin(self, a): return self.one(unreal.MaterialExpressionSine, a)
    def inv(self, a): return self.one(unreal.MaterialExpressionOneMinus, a)

    def pow(self, a, b):
        e = self.node(unreal.MaterialExpressionPower)
        self.link(a, e, "Base")
        self.link(b, e, "Exp")
        return (e, "")

    def mask(self, a, r=False, g=False, b=False, al=False):
        e = self.node(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=al)
        self.link(a, e, "")
        return (e, "")

    def desat(self, a, frac):
        e = self.node(unreal.MaterialExpressionDesaturation)
        self.link(a, e, "")
        self.link(frac, e, "Fraction")
        return (e, "")

    def uv(self, tiling):
        return self.mul((self.node(unreal.MaterialExpressionTextureCoordinate), ""), tiling)

    def wpos(self):
        return (self.node(unreal.MaterialExpressionWorldPosition), "")

    def opos(self):
        return (self.node(unreal.MaterialExpressionObjectPositionWS), "")

    def time(self):
        return (self.node(unreal.MaterialExpressionTime), "")

    def fresnel(self, exponent=3.0, base=0.04):
        return (self.node(unreal.MaterialExpressionFresnel, exponent=float(exponent), base_reflect_fraction=float(base)), "")

    def finish(self, clip=None, skeletal=False):
        if skeletal:
            self.m.set_editor_property("used_with_skeletal_mesh", True)
        if clip is not None:
            self.m.set_editor_property("opacity_mask_clip_value", float(clip))
        mel.layout_material_expressions(self.m)
        mel.recompile_material(self.m)
        eal.save_loaded_asset(self.m)
        log("built", self.m.get_name())
        return self.m


S_COLOR = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
S_NORMAL = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
S_MASKS = unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
S_GRAY = unreal.MaterialSamplerType.SAMPLERTYPE_MASKS  # all single-channel maps are imported as TC_Masks
S_LCOLOR = unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
MP = unreal.MaterialProperty


def height_above_pivot(g):
    """World Z minus the object's pivot Z (our meshes have their pivot at the base)."""
    wz = g.mask(g.wpos(), b=True)
    oz = g.mask(g.opos(), b=True)
    return g.sub(wz, oz)


def macro_noise(g, scale=1800.0):
    """Large-scale variation from world position (breaks texture repetition across facades)."""
    wp = g.wpos()
    xy = g.mask(wp, r=True, g=True)
    z = g.mask(wp, b=True)
    uv = g.div(g.add(xy, z), g.const(scale))
    t = g.texs(tex("T_WearMask"), S_GRAY, uv)
    return (t, "R")


def camera_fade(g, alpha, near=35.0, span=110.0):
    """Foliage the camera pushes into thins out instead of filling the screen with blurry leaves."""
    depth = (g.node(unreal.MaterialExpressionPixelDepth), "")
    fade = g.sat(g.div(g.sub(depth, g.const(near)), g.const(span)))
    return g.mul(alpha, fade)


def wind_wpo(g, amp_name="WindAmp", amp=2.0, speed=1.6, height=180.0):
    wp = g.wpos()
    phase = g.div(g.add(g.mask(wp, r=True), g.mask(wp, g=True)), g.const(90.0))
    tt = g.mul(g.time(), g.scalar("WindSpeed", speed))
    s1 = g.sin(g.add(tt, phase))
    s2 = g.sin(g.add(g.mul(tt, g.const(2.3)), g.mul(phase, g.const(1.7))))
    sway = g.add(s1, g.mul(s2, g.const(0.35)))
    hf = g.sat(g.div(height_above_pivot(g), g.scalar("WindHeight", height)))
    a = g.mul(g.mul(sway, hf), g.scalar(amp_name, amp))
    return g.append(g.append(a, g.mul(a, g.const(0.6))), g.const(0.0))


# =====================================================================================================================
def build_surface():
    g = Graph("M_Surface")
    uv = g.uv(g.scalar("Tiling", 1.0))
    bc = g.texp("BaseColorTex", ph("white_stucco", "diff"), S_COLOR, uv)
    nm = g.texp("NormalTex", ph("white_stucco", "nor_dx"), S_NORMAL, uv)
    arm = g.texp("ARMTex", ph("white_stucco", "arm"), S_MASKS, uv)
    base = g.mul(g.mul((bc, "RGB"), g.vec("Tint", (1, 1, 1))), g.scalar("Brightness", 1.0))
    mn = macro_noise(g)
    var = g.lerp(g.sub(g.const(1.0), g.scalar("MacroAmount", 0.12)), g.add(g.const(1.0), g.mul(g.scalar("MacroAmount", 0.12), g.const(0.5))), mn)
    base = g.mul(base, var)
    # Grime creeping up from the ground (rain splash, street dust) + slightly warmer/darker at the very bottom.
    dirt = g.mul(g.sat(g.sub(g.const(1.0), g.div(height_above_pivot(g), g.scalar("DirtHeight", 90.0)))), g.scalar("DirtAmount", 0.0))
    dirt = g.mul(dirt, g.lerp(g.const(0.6), g.const(1.0), mn))
    color = g.lerp(base, g.mul(base, g.vec("DirtTint", (0.55, 0.47, 0.38))), dirt)
    g.out(color, MP.MP_BASE_COLOR)
    g.out(g.sat(g.add(g.mul((arm, "G"), g.scalar("RoughMul", 1.0)), g.mul(dirt, g.const(0.1)))), MP.MP_ROUGHNESS)
    g.out(g.lerp(g.const3((0, 0, 1)), (nm, "RGB"), g.scalar("NormalStrength", 1.0)), MP.MP_NORMAL)
    g.out((arm, "R"), MP.MP_AMBIENT_OCCLUSION)
    g.out(g.scalar("Metallic", 0.0), MP.MP_METALLIC)
    return g.finish()


def build_paint():
    g = Graph("M_Paint")
    uv = g.uv(g.scalar("Tiling", 1.0))
    bc = g.texp("BaseColorTex", ph("weathered_planks", "diff"), S_COLOR, uv)
    nm = g.texp("NormalTex", ph("weathered_planks", "nor_dx"), S_NORMAL, uv)
    arm = g.texp("ARMTex", ph("weathered_planks", "arm"), S_MASKS, uv)
    wm = g.texs(tex("T_WearMask"), S_GRAY, g.uv(g.scalar("WearTiling", 0.7)))
    lum = g.desat((bc, "RGB"), g.const(1.0))
    paint = g.mul(g.vec("Tint", (0.25, 0.45, 0.35)), g.lerp(g.const(0.72), g.const(1.08), g.mask(lum, r=True)))
    wear = g.sat(g.mul(g.sub((wm, "R"), g.sub(g.const(1.0), g.scalar("WearAmount", 0.3))), g.const(6.0)))
    g.out(g.lerp(paint, (bc, "RGB"), wear), MP.MP_BASE_COLOR)
    g.out(g.lerp(g.scalar("PaintRough", 0.6), (arm, "G"), wear), MP.MP_ROUGHNESS)
    g.out((nm, "RGB"), MP.MP_NORMAL)
    g.out((arm, "R"), MP.MP_AMBIENT_OCCLUSION)
    return g.finish()


def build_prop():
    g = Graph("M_Prop")
    uv = g.uv(g.scalar("Tiling", 1.0))
    bc = g.texp("BaseColorTex", WHITE, S_COLOR, uv)
    nm = g.texp("NormalTex", FLAT_N, S_NORMAL, uv)
    arm = g.texp("ARMTex", FLAT_ARM, S_MASKS, uv)
    g.out(g.mul((bc, "RGB"), g.vec("Tint", (1, 1, 1))), MP.MP_BASE_COLOR)
    g.out(g.sat(g.mul((arm, "G"), g.scalar("RoughMul", 1.0))), MP.MP_ROUGHNESS)
    g.out((arm, "B"), MP.MP_METALLIC)
    g.out((nm, "RGB"), MP.MP_NORMAL)
    g.out((arm, "R"), MP.MP_AMBIENT_OCCLUSION)
    g.out(g.mul((bc, "RGB"), g.scalar("Emissive", 0.0)), MP.MP_EMISSIVE_COLOR)
    return g.finish()


def build_foliage():
    g = Graph("M_Foliage", blend=unreal.BlendMode.BLEND_MASKED, shading=unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE, two_sided=True)
    bc = g.texp("BaseColorTex", WHITE, S_COLOR)
    al = g.texp("AlphaTex", FLAT_ARM, S_GRAY)
    nm = g.texp("NormalTex", FLAT_N, S_NORMAL)
    arm = g.texp("ARMTex", FLAT_ARM, S_MASKS)
    col = g.mul((bc, "RGB"), g.vec("Tint", (1, 1, 1)))
    g.out(col, MP.MP_BASE_COLOR)
    g.out((arm, "G"), MP.MP_ROUGHNESS)
    g.out((nm, "RGB"), MP.MP_NORMAL)
    g.out((arm, "R"), MP.MP_AMBIENT_OCCLUSION)
    g.out(camera_fade(g, (al, "R")), MP.MP_OPACITY_MASK)
    g.out(g.mul(col, g.const(0.55)), MP.MP_SUBSURFACE_COLOR)
    g.out(wind_wpo(g, amp=1.6), MP.MP_WORLD_POSITION_OFFSET)
    return g.finish(clip=0.4)


def build_card():
    g = Graph("M_Card", blend=unreal.BlendMode.BLEND_MASKED, shading=unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE, two_sided=True)
    bc = g.texp("BaseColorTex", tex("T_Bougain_D"), S_COLOR)
    col = g.mul((bc, "RGB"), g.vec("Tint", (1, 1, 1)))
    mn = macro_noise(g, 600.0)
    col = g.mul(col, g.lerp(g.const(0.8), g.const(1.1), mn))
    g.out(col, MP.MP_BASE_COLOR)
    g.out(g.scalar("Roughness", 0.65), MP.MP_ROUGHNESS)
    g.out(camera_fade(g, (bc, "A")), MP.MP_OPACITY_MASK)
    g.out(g.mul(col, g.const(0.6)), MP.MP_SUBSURFACE_COLOR)
    g.out(wind_wpo(g, amp=2.2, height=120.0), MP.MP_WORLD_POSITION_OFFSET)
    return g.finish(clip=0.45)


def build_cloth():
    g = Graph("M_Cloth", shading=unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE, two_sided=True)
    uv = g.uv(g.scalar("Tiling", 1.0))
    bc = g.texp("BaseColorTex", tex("T_Linen_White_D"), S_COLOR, uv)
    nm = g.texp("NormalTex", tex("T_Awning_N"), S_NORMAL, g.uv(g.scalar("WeaveTiling", 4.0)))
    col = g.mul((bc, "RGB"), g.vec("Tint", (1, 1, 1)))
    g.out(col, MP.MP_BASE_COLOR)
    g.out(g.scalar("Roughness", 0.85), MP.MP_ROUGHNESS)
    g.out((nm, "RGB"), MP.MP_NORMAL)
    g.out(g.mul(col, g.const(0.45)), MP.MP_SUBSURFACE_COLOR)
    # Hanging flutter: pinned at the pivot (top), swinging more toward the hem.
    wp = g.wpos()
    hang = g.sat(g.div(g.sub(g.mask(g.opos(), b=True), g.mask(wp, b=True)), g.scalar("HangLength", 90.0)))
    phase = g.div(g.add(g.mask(wp, r=True), g.mask(wp, g=True)), g.const(60.0))
    tt = g.mul(g.time(), g.scalar("FlutterSpeed", 2.2))
    s = g.add(g.sin(g.add(tt, phase)), g.mul(g.sin(g.add(g.mul(tt, g.const(2.7)), g.mul(phase, g.const(2.0)))), g.const(0.4)))
    a = g.mul(g.mul(s, g.mul(hang, hang)), g.scalar("FlutterAmp", 0.0))
    g.out(g.append(g.append(g.mul(a, g.const(0.4)), a), g.mul(a, g.const(0.15))), MP.MP_WORLD_POSITION_OFFSET)
    return g.finish()


def build_iron():
    g = Graph("M_Iron")
    wm = g.texs(tex("T_WearMask"), S_GRAY, g.uv(g.const(2.0)))
    rust = g.sat(g.mul(g.sub((wm, "R"), g.const(0.62)), g.const(4.0)))
    g.out(g.lerp(g.vec("Tint", (0.025, 0.03, 0.028)), g.const3((0.18, 0.08, 0.04)), g.mul(rust, g.scalar("Rust", 0.4))), MP.MP_BASE_COLOR)
    g.out(g.lerp(g.const(0.45), g.const(0.85), rust), MP.MP_ROUGHNESS)
    g.out(g.lerp(g.const(0.55), g.const(0.1), rust), MP.MP_METALLIC)
    return g.finish()


def build_glass():
    g = Graph("M_Glass")
    mn = macro_noise(g, 300.0)
    g.out(g.vec("Tint", (0.018, 0.024, 0.03)), MP.MP_BASE_COLOR)
    g.out(g.lerp(g.const(0.03), g.const(0.12), mn), MP.MP_ROUGHNESS)
    g.out(g.const(1.0), MP.MP_SPECULAR)
    g.out(g.mul(g.vec("Interior", (1.0, 0.62, 0.32)), g.scalar("InteriorGlow", 0.0)), MP.MP_EMISSIVE_COLOR)
    return g.finish()


def build_emissive():
    g = Graph("M_Emissive")
    col = g.vec("Color", (1.0, 0.72, 0.42))
    g.out(g.mul(col, g.const(0.2)), MP.MP_BASE_COLOR)
    g.out(g.const(0.3), MP.MP_ROUGHNESS)
    g.out(g.mul(col, g.scalar("Intensity", 6.0)), MP.MP_EMISSIVE_COLOR)
    return g.finish()


def build_majolica():
    g = Graph("M_Majolica")
    uv = g.uv(g.scalar("Tiling", 1.0))
    bc = g.texp("BaseColorTex", tex("T_Majolica_D"), S_COLOR, uv)
    rr = g.texp("RoughTex", tex("T_Majolica_R"), S_GRAY, uv)
    nm = g.texp("NormalTex", tex("T_Majolica_N"), S_NORMAL, uv)
    g.out(g.mul((bc, "RGB"), g.vec("Tint", (1, 1, 1))), MP.MP_BASE_COLOR)
    g.out((rr, "R"), MP.MP_ROUGHNESS)
    g.out((nm, "RGB"), MP.MP_NORMAL)
    g.out(g.const(0.7), MP.MP_SPECULAR)
    return g.finish()


def build_water():
    """Single Layer Water: colour comes from absorption / scattering of the water column, so the sea is turquoise over
    the sandy shelf off the beach and deep blue where the seabed falls away, with a real sun glint at golden hour."""
    g = Graph("M_Water", shading=unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    xy = g.mask(g.wpos(), r=True, g=True)
    wn = tex("T_WaterNormal")

    def layer(scale, sx, sy):
        p = g.node(unreal.MaterialExpressionPanner, speed_x=sx, speed_y=sy)
        g.link(g.div(xy, g.const(scale)), p, "Coordinate")
        return (g.texs(wn, S_NORMAL, (p, "")), "RGB")

    n = g.add(g.add(layer(700.0, 0.006, 0.011), layer(260.0, -0.010, 0.005)), layer(2600.0, 0.002, -0.004))
    # calmer far out: full ripples near the shore, a gentle swell at the horizon (no shimmer)
    far = g.sat(g.div((g.node(unreal.MaterialExpressionPixelDepth), ""), g.scalar("FlattenDistance", 60000.0)))
    strength = g.lerp(g.scalar("NormalStrength", 0.45), g.scalar("FarNormalStrength", 0.15), far)
    g.out(g.lerp(g.const3((0, 0, 1)), n, strength), MP.MP_NORMAL)
    g.out(g.scalar("Roughness", 0.045), MP.MP_ROUGHNESS)
    g.out(g.const(0.5), MP.MP_SPECULAR)
    g.out(g.vec("SurfaceColor", (0.005, 0.035, 0.075)), MP.MP_BASE_COLOR)
    g.out(g.scalar("FoamOpacity", 0.0), MP.MP_OPACITY)
    o = g.node(unreal.MaterialExpressionSingleLayerWaterMaterialOutput)
    g.link(g.vec("Scattering", (0.012, 0.045, 0.055)), o, "ScatteringCoefficients")   # 1/m
    g.link(g.vec("Absorption", (0.42, 0.075, 0.048)), o, "AbsorptionCoefficients")    # 1/m: clear Mediterranean water
    g.link(g.scalar("PhaseG", 0.2), o, "PhaseG")
    return g.finish()


def triplanar(g, texture, scale, nrm, wp, sharp=4.0):
    """World-space triplanar colour: three planar projections blended by the (sharpened) normal, so slopes never smear
    a top-down texture into streaks."""
    w = g.pow(g.absv(nrm), g.const(sharp))
    w = g.div(w, g.add(g.dot(w, g.const3((1, 1, 1))), g.const(0.0001)))
    sx = g.texs(texture, S_COLOR, g.div(g.append(g.mask(wp, g=True), g.mask(wp, b=True)), scale))
    sy = g.texs(texture, S_COLOR, g.div(g.append(g.mask(wp, r=True), g.mask(wp, b=True)), scale))
    sz = g.texs(texture, S_COLOR, g.div(g.mask(wp, r=True, g=True), scale))
    return g.add(g.add(g.mul((sx, "RGB"), g.mask(w, r=True)), g.mul((sy, "RGB"), g.mask(w, g=True))), g.mul((sz, "RGB"), g.mask(w, b=True)))


def build_terrain():
    """Mountain backdrop: limestone cliff on steep faces, rock with green scrub on the slopes, grassy rock on shelves.
    World-projected at two scales (no visible repeats across hundreds of metres); slope from the vertex normal. The
    slope layer is triplanar (a top-down projection stretched into streaks down the hillsides). Colour only - at this
    distance the vertex normals carry the relief and the fog does the rest."""
    g = Graph("M_Terrain")
    wp = g.wpos()
    S1 = g.scalar("Scale", 2400.0)
    S2 = g.scalar("Scale2", 9000.0)
    nrm = (g.node(unreal.MaterialExpressionVertexNormalWS), "")
    nz = g.mask(nrm, b=True)
    ax = g.absv(g.mask(nrm, r=True))
    ay = g.absv(g.mask(nrm, g=True))
    xy = g.mask(wp, r=True, g=True)
    uv_x = g.div(g.append(g.mask(wp, g=True), g.mask(wp, b=True)), S1)   # faces turned towards +-X
    uv_y = g.div(g.append(g.mask(wp, r=True), g.mask(wp, b=True)), S1)   # faces turned towards +-Y (the sea)
    rock = g.lerp((g.texs(ph("marble_cliff_05", "diff"), S_COLOR, uv_y), "RGB"), (g.texs(ph("marble_cliff_05", "diff"), S_COLOR, uv_x), "RGB"),
                  g.sat(g.div(ax, g.add(g.add(ax, ay), g.const(0.001)))))
    scrub = g.lerp(triplanar(g, ph("aerial_rocks_02", "diff"), S1, nrm, wp),
                   (g.texs(ph("aerial_rocks_02", "diff"), S_COLOR, g.div(xy, S2)), "RGB"), g.mul(nz, g.const(0.5)))
    grass = g.lerp((g.texs(ph("aerial_grass_rock", "diff"), S_COLOR, g.div(xy, S1)), "RGB"),
                   (g.texs(ph("aerial_grass_rock", "diff"), S_COLOR, g.div(xy, S2)), "RGB"), g.const(0.5))
    mn = macro_noise(g, 26000.0)
    flat = g.sat(g.div(g.sub(nz, g.scalar("GrassFrom", 0.82)), g.const(0.12)))
    steep = g.sat(g.div(g.sub(g.scalar("CliffBelow", 0.62), nz), g.const(0.14)))
    ground = g.lerp(scrub, grass, g.mul(flat, g.lerp(g.const(0.35), g.const(1.0), mn)))
    col = g.lerp(ground, g.mul(rock, g.vec("RockTint", (1.0, 0.97, 0.92))), steep)
    col = g.mul(col, g.lerp(g.const(0.85), g.const(1.12), mn))
    g.out(g.mul(col, g.scalar("Brightness", 1.0)), MP.MP_BASE_COLOR)
    g.out(g.scalar("Roughness", 0.92), MP.MP_ROUGHNESS)
    g.out(g.const(0.3), MP.MP_SPECULAR)
    return g.finish()


def tcoord(g, index=0):
    return (g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=index), "")


def shell_height(g):
    return g.mask(tcoord(g, 1), r=True)


def coat_color(g):
    """Vertex-colour tabby (authored as sRGB bytes) x CoatTint, darkened by CoatDark."""
    vc = (g.node(unreal.MaterialExpressionVertexColor), "")
    lin = g.pow(g.mask(vc, r=True, g=True, b=True), g.const(2.2))
    coat = g.mul(g.desat(lin, g.scalar("CoatDesat", 0.0)), g.vec("CoatTint", (1, 1, 1)))
    return g.mul(g.mul(coat, g.scalar("CoatBrightness", 0.92)), g.lerp(g.const(1.0), g.const(0.16), g.scalar("CoatDark", 0.0)))


def strand_tex(g, tiling):
    """T_FurStrands: R per-strand length (0 between strands, tapering into tufts), G tone of the tuft, B tone of the hair."""
    return g.texs(tex("T_FurStrands"), S_MASKS, g.uv(g.scalar("FurTiling", tiling)))


def strand_map(g, tiling):
    return (strand_tex(g, tiling), "R")


def fur_strands(g, h, st=None):
    """Shell opacity: keep texels whose strand is longer than the shell height (strands taper to the tips)."""
    st = st or strand_map(g, 3.6)
    return g.add(g.mul(g.sub(st, g.mul(h, g.const(0.94))), g.const(24.0)), g.const(0.5))


def no_shadow(g, mask):
    """Fur shells never cast shadows (they would speckle the coat with hard shadow-map blotches)."""
    cu = g.node(unreal.MaterialExpressionCustom, code="return GetShadowReplaceState() ? -1.0 : Mask;",
                output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1, description="NoShellShadow")
    ci = unreal.CustomInput()
    ci.set_editor_property("input_name", "Mask")
    cu.set_editor_property("inputs", [ci])
    g.link(mask, cu, "Mask")
    return (cu, "")


def ink_noise(g, tiling=2.2, speed=(0.006, 0.01)):
    """Ink swirl in the cat's own UV space: it travels with the body (world-space noise crawled over a moving cat
    and read as flicker) and drifts only very slowly."""
    pan = g.node(unreal.MaterialExpressionPanner, speed_x=speed[0], speed_y=speed[1])
    g.link(g.uv(g.const(tiling)), pan, "Coordinate")
    return (g.texs(tex("T_InkNoise"), S_GRAY, (pan, "")), "R")


def burn_terms(g, nzr):
    burn = g.scalar("Burn", 0.0)
    front = g.sub(g.const(1.0), burn)
    edge = g.sat(g.sub(g.const(1.0), g.mul(g.absv(g.sub(nzr, front)), g.const(7.0))))
    ember = g.mul(g.mul(edge, g.vec("EmberColor", (6.0, 1.6, 0.25))), g.sat(g.mul(burn, g.const(3.0))))
    charred = g.mul(g.sat(g.mul(g.sub(nzr, front), g.const(6.0))), g.mul(burn, g.const3((0.25, 0.08, 0.02))))
    return g.add(ember, charred)


def eye_shapes(g):
    """Returns (iris_mask, pupil_open, dist, lid_mask, lash) for the cat eye UVs (centre 0.5; opening ~ +-0.28 x +-0.18)."""
    tc = tcoord(g)
    c = (g.node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5), "")
    dd = g.node(unreal.MaterialExpressionDistance)
    g.link(tc, dd, "A")
    g.link(c, dd, "B")
    d = (dd, "")
    rel = g.sub(tc, c)
    # vertical slit pupil (narrow in the afternoon sun)
    sl = g.node(unreal.MaterialExpressionDistance)
    g.link(g.div(rel, g.append(g.scalar("PupilWidth", 0.045), g.const(0.24))), sl, "A")
    g.link((g.node(unreal.MaterialExpressionConstant2Vector, r=0.0, g=0.0), ""), sl, "B")
    pupil_open = g.sat(g.mul(g.sub((sl, ""), g.const(1.0)), g.const(9.0)))
    iris = g.sat(g.mul(g.sub(g.const(0.262), d), g.const(70.0)))
    # Eyelids close from the top (and a little from below); V grows downward in UE.
    v = g.mask(tc, g=True)
    lid = g.scalar("Lid", 0.0)
    top = g.add(g.const(0.3), g.mul(lid, g.const(0.4)))
    upper = g.sat(g.mul(g.sub(top, v), g.const(60.0)))
    bottom = g.sub(g.const(0.7), g.mul(lid, g.const(0.04)))
    lower = g.sat(g.mul(g.sub(v, bottom), g.const(60.0)))
    lidm = g.sat(g.add(upper, lower))
    lash = g.sat(g.sub(g.const(1.0), g.mul(g.absv(g.sub(v, top)), g.const(45.0))))
    lash = g.mul(lash, g.sat(g.mul(lid, g.const(8.0))))
    return iris, pupil_open, d, lidm, lash


def build_shade_body():
    """Shade: a black-velvet cat of living shadow. Lit (so its form reads), ink swirl, faint violet sheen, ember burn."""
    g = Graph("M_ShadeBody", blend=unreal.BlendMode.BLEND_MASKED)
    nzr = ink_noise(g)
    ink = g.mul(g.vec("InkColor", (0.0042, 0.004, 0.0048)), g.lerp(g.const(0.7), g.const(1.3), nzr))
    g.out(ink, MP.MP_BASE_COLOR)
    g.out(g.scalar("Roughness", 0.66), MP.MP_ROUGHNESS)
    g.out(g.scalar("Specular", 0.26), MP.MP_SPECULAR)
    rim = g.mul(g.fresnel(3.2, 0.0), g.vec("RimColor", (0.008, 0.007, 0.013)))
    rim = g.mul(rim, g.mul(g.add(g.const(0.75), g.mul(g.scalar("Pulse", 0.5), g.const(0.5))), g.lerp(g.const(0.5), g.const(1.4), nzr)))
    g.out(g.add(rim, burn_terms(g, nzr)), MP.MP_EMISSIVE_COLOR)
    # Solid until Dissolve rises (the old nzr + 0.5 sat exactly on the clip value where the noise hit 0 -> holes).
    g.out(g.sub(g.add(g.mul(nzr, g.const(0.5)), g.const(0.62)), g.mul(g.scalar("Dissolve", 0.0), g.const(1.4))), MP.MP_OPACITY_MASK)
    return g.finish(clip=0.5, skeletal=True)


def build_shade_shell():
    """Smoky fur: shells thin out into drifting ink at the tips."""
    g = Graph("M_ShadeShell", blend=unreal.BlendMode.BLEND_MASKED)
    h = shell_height(g)
    nzr = ink_noise(g, 3.0, (0.0, 0.0))
    strands = fur_strands(g, h)
    wisp = g.mul(g.sub(nzr, g.const(0.5)), g.mul(h, g.const(0.7)))
    mask = g.add(g.add(strands, wisp), g.mul(g.scalar("Dissolve", 0.0), g.const(-30.0)))
    g.out(no_shadow(g, mask), MP.MP_OPACITY_MASK)
    ink = g.mul(g.vec("InkColor", (0.0042, 0.004, 0.0048)), g.lerp(g.const(0.6), g.const(1.4), h))
    g.out(ink, MP.MP_BASE_COLOR)
    g.out(g.const(0.7), MP.MP_ROUGHNESS)
    g.out(g.const(0.3), MP.MP_SPECULAR)
    rim = g.mul(g.mul(g.fresnel(2.5, 0.0), g.vec("RimColor", (0.009, 0.008, 0.016))), g.add(g.const(0.4), h))
    g.out(g.add(rim, g.mul(burn_terms(g, nzr), h)), MP.MP_EMISSIVE_COLOR)
    return g.finish(clip=0.5, skeletal=True)


def build_shade_eye():
    g = Graph("M_ShadeEye", shading=unreal.MaterialShadingModel.MSM_UNLIT)
    iris, pupil_open, d, lidm, lash = eye_shapes(g)
    glow = g.scalar("Glow", 1.0)
    col = g.lerp(g.vec("EyeColor", (1.0, 0.56, 0.12)), g.vec("HotColor", (1.0, 0.95, 0.8)), g.scalar("Burn", 0.0))
    inner = g.lerp(g.const(1.0), g.const(0.5), g.sat(g.mul(d, g.const(3.8))))
    e = g.mul(g.mul(g.mul(col, g.scalar("Intensity", 5.0)), glow), g.mul(inner, g.lerp(g.const(0.02), g.const(1.0), pupil_open)))
    e = g.mul(e, g.lerp(g.const(0.15), g.const(1.0), iris))
    g.out(g.lerp(e, g.const3((0.0, 0.0, 0.0)), lidm), MP.MP_EMISSIVE_COLOR)
    return g.finish(skeletal=True)


def build_shade_whisker():
    g = Graph("M_ShadeWhisker", blend=unreal.BlendMode.BLEND_MASKED)
    g.out(g.vec("InkColor", (0.01, 0.009, 0.014)), MP.MP_BASE_COLOR)
    g.out(g.const(0.4), MP.MP_ROUGHNESS)
    g.out(g.mul(g.fresnel(2.0, 0.1), g.vec("RimColor", (0.03, 0.025, 0.06))), MP.MP_EMISSIVE_COLOR)
    g.out(g.sub(g.const(1.0), g.scalar("Dissolve", 0.0)), MP.MP_OPACITY_MASK)
    return g.finish(clip=0.5, skeletal=True)


def hair_tips(g, coat, gain, lift):
    """Sunlit hair tips: paler and a little less saturated than the coat (ginger fur greys toward cream at the ends)."""
    return g.add(g.mul(g.desat(coat, g.const(0.28)), g.const(gain)), g.const3(lift))


def build_cat_fur():
    g = Graph("M_CatFur", shading=unreal.MaterialShadingModel.MSM_SUBSURFACE)
    coat = coat_color(g)
    # Neighbouring hairs differ a little in tone, so the coat never reads as painted plastic.
    # The skin-level coat carries the same tufts as the shells above it (tone of each lock of hair).
    tufts = (strand_tex(g, 1.2), "G")
    coat = g.mul(coat, g.lerp(g.const(0.86), g.const(1.07), tufts))
    nm = g.texs(tex("T_FurNormal"), S_NORMAL, g.uv(g.scalar("FurNormalTiling", 8.0)))
    # Grazing light picks out the fuzz of pale tips rather than a glossy rim.
    col = g.lerp(coat, hair_tips(g, coat, 1.5, (0.03, 0.025, 0.018)), g.mul(g.fresnel(2.2, 0.0), g.scalar("Sheen", 0.5)))
    g.out(col, MP.MP_BASE_COLOR)
    g.out(g.scalar("Roughness", 0.9), MP.MP_ROUGHNESS)
    g.out(g.scalar("Specular", 0.2), MP.MP_SPECULAR)
    g.out(g.lerp(g.const3((0, 0, 1)), (nm, "RGB"), g.scalar("FurNormalStrength", 0.55)), MP.MP_NORMAL)
    # Light through thin ears and whisker pads glows red-orange.
    g.out(g.mul(coat, g.const3((0.75, 0.42, 0.3))), MP.MP_SUBSURFACE_COLOR)
    g.out(g.const(0.3), MP.MP_OPACITY)
    return g.finish(skeletal=True)


def build_cat_shell():
    g = Graph("M_CatShell", blend=unreal.BlendMode.BLEND_MASKED, shading=unreal.MaterialShadingModel.MSM_SUBSURFACE)
    h = shell_height(g)
    coat = coat_color(g)
    sm = strand_tex(g, 1.2)
    st = (sm, "R")
    # Each lock of hair its own tone, each hair a little different again; roots sit in the coat's own shadow, tips are
    # pale and catch the light - the depth that makes it read as fur.
    coat = g.mul(g.mul(coat, g.lerp(g.const(0.84), g.const(1.1), (sm, "G"))), g.lerp(g.const(0.93), g.const(1.06), (sm, "B")))
    col = g.lerp(g.mul(coat, g.const(0.5)), hair_tips(g, coat, 1.3, (0.02, 0.016, 0.012)), h)
    g.out(col, MP.MP_BASE_COLOR)
    g.out(g.const(0.88), MP.MP_ROUGHNESS)
    g.out(g.const(0.2), MP.MP_SPECULAR)
    g.out(no_shadow(g, fur_strands(g, h, st)), MP.MP_OPACITY_MASK)
    g.out(g.mul(coat, g.const3((0.8, 0.45, 0.32))), MP.MP_SUBSURFACE_COLOR)
    g.out(g.const(0.35), MP.MP_OPACITY)
    return g.finish(clip=0.5, skeletal=True)


def build_cat_eye():
    g = Graph("M_CatEye")
    iris, pupil_open, d, lidm, lash = eye_shapes(g)
    streak = g.texs(tex("T_WearMask"), S_GRAY, g.uv(g.const(3.0)))
    # Amber-gold round the pupil out to a green-gold rim, with radial fibres.
    ring = g.sat(g.mul(g.sub(d, g.const(0.06)), g.const(5.0)))
    iris_col = g.lerp(g.vec("IrisInner", (0.85, 0.52, 0.08)), g.vec("IrisOuter", (0.46, 0.5, 0.1)), ring)
    iris_col = g.mul(iris_col, g.lerp(g.const(0.7), g.const(1.2), (streak, "R")))
    limbal = g.sat(g.sub(g.const(1.0), g.mul(g.absv(g.sub(d, g.const(0.25))), g.const(45.0))))
    iris_col = g.mul(iris_col, g.sub(g.const(1.0), g.mul(limbal, g.const(0.7))))
    eye = g.lerp(g.vec("Sclera", (0.34, 0.27, 0.15)), iris_col, iris)
    eye = g.mul(eye, pupil_open)
    # Set into the socket: the lids shade the eye toward the edge of the almond opening (+-0.28 x +-0.18 in UV).
    tc = tcoord(g)
    rel = g.sub(tc, (g.node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5), ""))
    rs = g.div(rel, (g.node(unreal.MaterialExpressionConstant2Vector, r=0.29, g=0.18), ""))
    sock = g.lerp(g.const(1.0), g.const(0.3), g.sat(g.mul(g.sub(g.dot(rs, rs), g.const(0.45)), g.const(1.8))))
    eye = g.mul(eye, sock)
    lidc = g.mul(g.vec("LidColor", (0.55, 0.24, 0.07)), g.sub(g.const(1.0), g.mul(lash, g.const(0.85))))
    g.out(g.lerp(eye, lidc, lidm), MP.MP_BASE_COLOR)
    g.out(g.lerp(g.const(0.04), g.const(0.8), lidm), MP.MP_ROUGHNESS)
    g.out(g.lerp(g.const(0.9), g.const(0.25), lidm), MP.MP_SPECULAR)
    # Catchlight: the bright sky in the cornea, high in the eye (on the centre line, so both eyes agree).
    cd = g.node(unreal.MaterialExpressionDistance)
    g.link(rel, cd, "A")
    g.link((g.node(unreal.MaterialExpressionConstant2Vector, r=0.0, g=-0.085), ""), cd, "B")
    catch = g.sat(g.mul(g.sub(g.const(0.032), (cd, "")), g.const(80.0)))
    glow = g.mul(g.mul(iris_col, g.mul(iris, pupil_open)), g.scalar("IrisGlow", 0.12))
    glow = g.add(glow, g.mul(catch, g.mul(g.vec("CatchColor", (1.0, 0.97, 0.9)), g.scalar("Catchlight", 2.4))))
    g.out(g.mul(glow, g.sub(g.const(1.0), lidm)), MP.MP_EMISSIVE_COLOR)
    return g.finish(skeletal=True)


def build_cat_whisker():
    g = Graph("M_CatWhisker")
    g.out(g.vec("Color", (0.82, 0.79, 0.72)), MP.MP_BASE_COLOR)
    g.out(g.const(0.42), MP.MP_ROUGHNESS)
    g.out(g.const(0.5), MP.MP_SPECULAR)
    return g.finish(skeletal=True)


def build_wisp():
    g = Graph("M_Wisp", blend=unreal.BlendMode.BLEND_TRANSLUCENT, shading=unreal.MaterialShadingModel.MSM_UNLIT, two_sided=True)
    t = g.texs(tex("T_Wisp"), S_GRAY)
    alpha = g.scalar("Alpha", 0.5, cpd=0)
    heat = g.scalar("Heat", 0.0, cpd=1)
    dust = g.scalar("Dust", 0.0, cpd=2)
    col = g.lerp(g.vec("Ink", (0.004, 0.003, 0.01)), g.vec("Ember", (3.5, 1.1, 0.2)), g.sat(g.mul(heat, g.const(1.5))))
    col = g.lerp(col, g.vec("DustColor", (0.32, 0.27, 0.2)), dust)
    g.out(col, MP.MP_EMISSIVE_COLOR)
    g.out(g.sat(g.mul((t, "R"), alpha)), MP.MP_OPACITY)
    return g.finish()


# =====================================================================================================================
def mi(name, parent, scalars=None, vectors=None, textures=None, folder=MAT_DIR):
    path = f"{folder}/{name}"
    inst = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(inst, parent)
    for k, v in (scalars or {}).items():
        mel.set_material_instance_scalar_parameter_value(inst, k, float(v))
    for k, v in (vectors or {}).items():
        mel.set_material_instance_vector_parameter_value(inst, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
    for k, v in (textures or {}).items():
        if v:
            mel.set_material_instance_texture_parameter_value(inst, k, v)
    mel.update_material_instance(inst)
    eal.save_loaded_asset(inst)
    return inst


def surf(sid):
    return {"BaseColorTex": ph(sid, "diff"), "NormalTex": ph(sid, "nor_dx"), "ARMTex": ph(sid, "arm")}


def build_instances(M):
    S = M["M_Surface"]
    wall = {"DirtAmount": 0.75, "DirtHeight": 110, "MacroAmount": 0.16, "Tiling": 1.4}
    # Positano palette: whitewash, cream, peach, salmon, ochre, lemon; aged plaster; dry stone.
    mi("MI_Wall_White", S, wall, {"Tint": (1.0, 0.985, 0.95)}, surf("white_stucco"))
    mi("MI_Wall_Cream", S, wall, {"Tint": (1.0, 0.9, 0.72)}, surf("white_stucco"))
    mi("MI_Wall_Peach", S, dict(wall, RoughMul=1.5), {"Tint": (1.0, 0.76, 0.56)}, surf("PaintedPlaster017"))
    mi("MI_Wall_Salmon", S, wall, {"Tint": (1.0, 0.6, 0.47)}, surf("white_stucco"))
    mi("MI_Wall_Lemon", S, dict(wall, RoughMul=2.2), {"Tint": (1.05, 1.0, 0.72)}, surf("PaintedPlaster018"))
    mi("MI_Wall_Ochre", S, dict(wall, RoughMul=2.2), {"Tint": (1.05, 0.8, 0.5)}, surf("PaintedPlaster018"))
    mi("MI_Wall_Rose", S, wall, {"Tint": (1.0, 0.64, 0.66)}, surf("white_stucco"))
    mi("MI_Wall_Sky", S, dict(wall, Tiling=1.0, Brightness=1.1), {"Tint": (1.05, 1.12, 1.12)}, surf("blue_plaster_weathered"))
    mi("MI_Wall_Aged", S, dict(wall, Tiling=1.0), {"Tint": (1.0, 0.96, 0.9)}, surf("white_rough_plaster"))
    mi("MI_Wall_Beige", S, dict(wall, Tiling=1.0), {"Tint": (1.0, 0.95, 0.86)}, surf("painted_plaster_wall"))
    mi("MI_Wall_Stone", S, {"Tiling": 0.8, "DirtAmount": 0.4, "MacroAmount": 0.15}, {"Tint": (1.02, 0.97, 0.9)}, surf("coral_stone_wall"))
    mi("MI_Wall_Stacked", S, {"Tiling": 1.0, "DirtAmount": 0.3, "MacroAmount": 0.15}, None, surf("stacked_stone_wall"))
    mi("MI_Trim_White", S, {"Tiling": 2.5, "MacroAmount": 0.06, "NormalStrength": 0.5}, {"Tint": (1.05, 1.03, 1.0)}, surf("white_stucco"))
    ground = {"MacroAmount": 0.2, "DirtAmount": 0.0}
    mi("MI_Cobble", S, dict(ground, Tiling=1.0), {"Tint": (1.08, 1.0, 0.9)}, surf("PavingStones150"))
    mi("MI_Flag", S, dict(ground, Tiling=0.8, RoughMul=1.9, Brightness=0.86), {"Tint": (1.02, 0.98, 0.94)}, surf("Tiles143"))
    # natural limestone for stairs, sills, door steps and benches (the travertine's veins read as wood grain)
    # (finer tiling: at 0.9 the rock's pebbles read as coarse granite gravel on the benches and steps)
    mi("MI_StepStone", S, dict(ground, Tiling=1.6, RoughMul=1.0, Brightness=0.95, NormalStrength=0.6), {"Tint": (1.0, 0.97, 0.92)},
       surf("worn_rock_natural_01"))
    mi("MI_Terracotta", S, dict(ground, Tiling=1.6, RoughMul=2.5), None, surf("GlazedTerracotta001"))
    mi("MI_Sand", S, dict(ground, Tiling=0.6), None, surf("coast_sand_01"))
    mi("MI_Cliff", S, {"Tiling": 0.35, "MacroAmount": 0.25}, {"Tint": (0.95, 0.9, 0.84)}, surf("marble_cliff_02"))
    mi("MI_Soil", S, {"Tiling": 3.0, "MacroAmount": 0.3, "DirtAmount": 0.0, "RoughMul": 1.1}, {"Tint": (0.36, 0.25, 0.17)}, surf("coast_sand_01"))
    mi("MI_RoofTiles", S, {"Tiling": 1.2, "MacroAmount": 0.2, "DirtAmount": 0.0}, None, surf("RoofingTiles014B"))

    P = M["M_Paint"]
    planks = surf("PaintedWood009B")
    mi("MI_Shutter_Green", P, {"WearAmount": 0.25, "Tiling": 1.6}, {"Tint": (0.2, 0.42, 0.3)}, planks)
    mi("MI_Shutter_Blue", P, {"WearAmount": 0.3, "Tiling": 1.6}, {"Tint": (0.12, 0.3, 0.55)}, surf("PaintedWood007B"))
    mi("MI_Shutter_Teal", P, {"WearAmount": 0.28, "Tiling": 1.6}, {"Tint": (0.16, 0.45, 0.48)}, planks)
    mi("MI_Shutter_Olive", P, {"WearAmount": 0.35, "Tiling": 1.6}, {"Tint": (0.38, 0.42, 0.22)}, planks)
    mi("MI_Door_Blue", P, {"WearAmount": 0.35, "Tiling": 1.2}, {"Tint": (0.1, 0.22, 0.42)}, surf("distressed_painted_planks"))
    mi("MI_Door_Red", P, {"WearAmount": 0.35, "Tiling": 1.2}, {"Tint": (0.5, 0.12, 0.08)}, surf("distressed_painted_planks"))
    mi("MI_Door_Wood", P, {"WearAmount": 1.0, "Tiling": 1.2}, None, surf("weathered_planks"))
    mi("MI_Wood_Crate", P, {"WearAmount": 1.0, "Tiling": 1.4}, None, surf("rough_wood"))
    mi("MI_Wood_Beam", P, {"WearAmount": 1.0, "Tiling": 0.8}, None, surf("rough_wood"))
    mi("MI_Paint_Cream", P, {"WearAmount": 0.35, "Tiling": 1.4, "PaintRough": 0.5}, {"Tint": (0.85, 0.8, 0.68)}, planks)

    mi("MI_Iron_Black", M["M_Iron"], {"Rust": 0.35}, {"Tint": (0.02, 0.022, 0.022)})
    mi("MI_Iron_Green", M["M_Iron"], {"Rust": 0.5}, {"Tint": (0.03, 0.08, 0.05)})
    mi("MI_Glass", M["M_Glass"], {"InteriorGlow": 0.0})
    mi("MI_Glass_Warm", M["M_Glass"], {"InteriorGlow": 1.2})
    mi("MI_Lantern_Glow", M["M_Emissive"], {"Intensity": 5.0}, {"Color": (1.0, 0.7, 0.38)})
    mi("MI_Bulb_Warm", M["M_Emissive"], {"Intensity": 40.0}, {"Color": (1.0, 0.62, 0.3)})

    C = M["M_Cloth"]
    for v in ("Terracotta", "Sea", "Lemon"):
        mi(f"MI_Awning_{v}", C, {"Tiling": 1.0, "WeaveTiling": 3.0, "FlutterAmp": 0.0, "Roughness": 0.9}, None,
           {"BaseColorTex": tex(f"T_Awning_{v}_D"), "NormalTex": ph("Fabric061", "nor_dx")})
    for v in ("White", "BlueStripe", "Coral", "Lemon", "Sea"):
        mi(f"MI_Linen_{v}", C, {"Tiling": 1.0, "WeaveTiling": 4.0, "FlutterAmp": 7.0, "HangLength": 90.0}, None,
           {"BaseColorTex": tex(f"T_Linen_{v}_D"), "NormalTex": ph("Fabric071", "nor_dx")})
    mi("MI_Towel_Terry", C, {"Tiling": 1.0, "WeaveTiling": 3.0, "FlutterAmp": 5.0, "HangLength": 70.0}, None, {"BaseColorTex": ph("terry_cloth", "diff")})
    mi("MI_Tablecloth", C, {"Tiling": 1.5, "WeaveTiling": 6.0, "FlutterAmp": 0.6, "HangLength": 25.0}, None, {"BaseColorTex": ph("fabric_pattern_07", "diff")})

    K = M["M_Card"]
    mi("MI_Bougain", K, {"WindAmp": 2.0}, {"Tint": (1.0, 1.0, 1.0)}, {"BaseColorTex": tex("T_Bougain_D")})
    mi("MI_Bougain_Deep", K, {"WindAmp": 2.0}, {"Tint": (0.85, 0.7, 0.95)}, {"BaseColorTex": tex("T_Bougain_D")})
    mi("MI_Leafy", M["M_Foliage"], {"WindAmp": 1.2}, None, {"BaseColorTex": ph("LeafSet017", "diff"), "NormalTex": ph("LeafSet017", "nor_dx"),
                                                         "ARMTex": ph("LeafSet017", "arm"), "AlphaTex": ph("LeafSet017", "alpha")})

    mi("MI_Majolica", M["M_Majolica"], {"Tiling": 2.0})
    Pp = M["M_Prop"]
    for n in ("Caffe", "Limoni", "Street", "Menu"):
        mi(f"MI_Sign_{n}", Pp, {"RoughMul": 0.75 if n != "Street" else 0.25}, None, {"BaseColorTex": tex(f"T_Sign_{n}")})
    mi("MI_Lemon_Skin", Pp, {"RoughMul": 0.6}, {"Tint": (1.0, 0.95, 0.55)}, {"BaseColorTex": WHITE})
    # Biscotto's collar: worn red leather and a little brass bell.
    mi("MI_Leather_Red", Pp, {"RoughMul": 0.85}, {"Tint": (0.42, 0.055, 0.035)}, {"BaseColorTex": WHITE})
    mi("MI_Brass", M["M_Iron"], {"Rust": 0.12}, {"Tint": (0.72, 0.5, 0.2)})
    mi("MI_CatFur", M["M_CatFur"], {"CoatDark": 0.0}, {"CoatTint": (1, 1, 1)})
    mi("MI_CatShell", M["M_CatShell"], {"CoatDark": 0.0}, {"CoatTint": (1, 1, 1)})
    mi("MI_CatEye", M["M_CatEye"])
    mi("MI_CatWhisker", M["M_CatWhisker"])


FLAT_N = tex("T_FlatNormal")
FLAT_ARM = tex("T_FlatARM")
WHITE = tex("T_White")
M = {}
for fn in (build_surface, build_paint, build_prop, build_foliage, build_card, build_cloth, build_iron, build_glass, build_emissive,
           build_majolica, build_water, build_terrain, build_shade_body, build_shade_shell, build_shade_eye, build_shade_whisker,
           build_cat_fur, build_cat_shell, build_cat_eye, build_cat_whisker, build_wisp):
    try:
        m = fn()
        M[m.get_name()] = m
    except Exception as e:
        import traceback
        log("ERROR", fn.__name__, e, traceback.format_exc())
build_instances(M)
eal.save_directory("/Game/Shade/Materials", only_if_is_dirty=True, recursive=True)
log("ALL DONE")
