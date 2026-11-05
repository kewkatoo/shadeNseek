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
  M_ShadeBody  (C++: Burn, Dissolve, Pulse)  unlit ink silhouette, violet rim, ember burn edge, dissolve
  M_ShadeEye   (C++: Glow, Burn)
  M_CatFur     (C++: CoatTint, CoatDark)   vertex-colour tabby + fur normal + sheen
  M_CatEye     slit-pupil amber eye
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
    g.out((al, "R"), MP.MP_OPACITY_MASK)
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
    g.out((bc, "A"), MP.MP_OPACITY_MASK)
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
    g = Graph("M_Water")
    wp = g.wpos()
    xy = g.mask(wp, r=True, g=True)
    uvw = g.div(xy, g.scalar("WaveScale", 900.0))
    p1 = g.node(unreal.MaterialExpressionPanner, speed_x=0.006, speed_y=0.011)
    g.link(uvw, p1, "Coordinate")
    p2 = g.node(unreal.MaterialExpressionPanner, speed_x=-0.009, speed_y=0.004)
    g.link(g.mul(uvw, g.const(2.3)), p2, "Coordinate")
    wn = tex("T_WaterNormal")
    n1 = g.texs(wn, S_NORMAL, (p1, ""))
    n2 = g.texs(wn, S_NORMAL, (p2, ""))
    n = g.add((n1, "RGB"), (n2, "RGB"))
    normal = g.lerp(g.const3((0, 0, 1)), n, g.scalar("NormalStrength", 0.55))
    # Deep sea far out, turquoise over the sandy shallows near the beach.
    shore = g.sat(g.div(g.sub(g.mask(wp, g=True), g.scalar("ShoreY", 3000.0)), g.scalar("ShoreFade", 2500.0)))
    col = g.lerp(g.vec("Shallow", (0.03, 0.2, 0.22)), g.vec("Deep", (0.005, 0.035, 0.075)), shore)
    g.out(col, MP.MP_BASE_COLOR)
    g.out(g.scalar("Roughness", 0.06), MP.MP_ROUGHNESS)
    g.out(g.const(1.0), MP.MP_SPECULAR)
    g.out(normal, MP.MP_NORMAL)
    return g.finish()


def build_shade_body():
    g = Graph("M_ShadeBody", blend=unreal.BlendMode.BLEND_MASKED, shading=unreal.MaterialShadingModel.MSM_UNLIT)
    wp = g.wpos()
    tt = g.time()
    uvn = g.div(g.add(g.mask(wp, r=True, g=True), g.mask(wp, b=True)), g.const(38.0))
    pan = g.node(unreal.MaterialExpressionPanner, speed_x=0.05, speed_y=0.08)
    g.link(uvn, pan, "Coordinate")
    nz = g.texs(tex("T_InkNoise"), S_GRAY, (pan, ""))
    nzr = (nz, "R")
    rim = g.fresnel(4.0, 0.0)
    ink = g.mul(g.vec("InkColor", (0.0012, 0.001, 0.0028)), g.lerp(g.const(0.6), g.const(1.4), nzr))
    rimc = g.mul(g.mul(rim, g.vec("RimColor", (0.022, 0.014, 0.06))), g.add(g.const(0.75), g.mul(g.scalar("Pulse", 0.5), g.const(0.5))))
    rimc = g.mul(rimc, g.mul(g.scalar("Glow", 1.0), g.lerp(g.const(0.5), g.const(1.4), nzr)))
    # Burn: an ember front creeps across the body as exposure rises.
    burn = g.scalar("Burn", 0.0)
    front = g.sub(g.const(1.0), burn)
    edge = g.sat(g.sub(g.const(1.0), g.mul(g.absv(g.sub(nzr, front)), g.const(7.0))))
    ember = g.mul(g.mul(edge, g.vec("EmberColor", (6.0, 1.6, 0.25))), g.sat(g.mul(burn, g.const(3.0))))
    charred = g.mul(g.sat(g.mul(g.sub(nzr, front), g.const(6.0))), g.mul(burn, g.const3((0.25, 0.08, 0.02))))
    body = g.add(g.add(g.add(ink, rimc), ember), charred)
    # Eyes live in the same section: vertex alpha is 0 on the eyeballs. Amber glow with a slit pupil.
    # Eyeballs are painted pure blue in the vertex colours; no coat colour is ever bluer than it is red.
    vce = (g.node(unreal.MaterialExpressionVertexColor), "")
    eye = g.sat(g.mul(g.sub(g.mask(vce, b=True), g.mask(vce, r=True)), g.const(4.0)))
    tc = (g.node(unreal.MaterialExpressionTextureCoordinate), "")
    slit = g.node(unreal.MaterialExpressionDistance)
    g.link(g.mul(g.sub(tc, (g.node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5), "")), (g.node(unreal.MaterialExpressionConstant2Vector, r=3.4, g=1.0), "")), slit, "A")
    g.link((g.node(unreal.MaterialExpressionConstant2Vector, r=0.0, g=0.0), ""), slit, "B")
    pupil = g.sat(g.mul(g.sub((slit, ""), g.const(0.36)), g.const(18.0)))
    ecol = g.lerp(g.vec("EyeColor", (1.0, 0.55, 0.12)), g.const3((1.0, 0.95, 0.8)), burn)
    eyes = g.mul(g.mul(g.mul(ecol, g.scalar("EyeIntensity", 1.6)), g.scalar("EyeGlow", 1.0)), g.lerp(g.const(0.04), g.const(1.0), pupil))
    g.out(g.lerp(body, eyes, eye), MP.MP_EMISSIVE_COLOR)
    # Dissolve (evaporate / merge).
    dis = g.scalar("Dissolve", 0.0)
    g.out(g.add(g.sub(nzr, g.mul(dis, g.const(1.2))), g.const(0.5)), MP.MP_OPACITY_MASK)
    return g.finish(clip=0.5, skeletal=True)


def build_shade_eye():
    g = Graph("M_ShadeEye", shading=unreal.MaterialShadingModel.MSM_UNLIT)
    tc = (g.node(unreal.MaterialExpressionTextureCoordinate), "")
    slit = g.node(unreal.MaterialExpressionDistance)
    g.link(g.mul(g.sub(tc, (g.node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5), "")), (g.node(unreal.MaterialExpressionConstant2Vector, r=3.4, g=1.0), "")), slit, "A")
    g.link((g.node(unreal.MaterialExpressionConstant2Vector, r=0.0, g=0.0), ""), slit, "B")
    pupil = g.sat(g.mul(g.sub((slit, ""), g.const(0.36)), g.const(18.0)))
    glow = g.scalar("Glow", 1.0)
    col = g.lerp(g.vec("EyeColor", (1.0, 0.55, 0.12)), g.vec("HotColor", (1.0, 0.95, 0.8)), g.scalar("Burn", 0.0))
    g.out(g.mul(g.mul(g.mul(col, g.scalar("Intensity", 9.0)), glow), g.lerp(g.const(0.05), g.const(1.0), pupil)), MP.MP_EMISSIVE_COLOR)
    return g.finish(skeletal=True)


def build_cat_fur():
    g = Graph("M_CatFur", shading=unreal.MaterialShadingModel.MSM_SUBSURFACE)
    vc = (g.node(unreal.MaterialExpressionVertexColor), "")
    vc3 = g.mask(vc, r=True, g=True, b=True)
    lin = g.pow(vc3, g.const(2.2))  # vertex colours were authored as sRGB bytes
    coat = g.mul(g.desat(lin, g.scalar("CoatDesat", 0.0)), g.vec("CoatTint", (1, 1, 1)))
    coat = g.mul(g.mul(coat, g.const(0.72)), g.lerp(g.const(1.0), g.const(0.18), g.scalar("CoatDark", 0.0)))
    nm = g.texs(tex("T_FurNormal"), S_NORMAL, g.uv(g.const(1.5)))
    sheen = g.fresnel(2.2, 0.0)
    col = g.lerp(coat, g.add(g.mul(coat, g.const(1.5)), g.const3((0.04, 0.035, 0.03))), g.mul(sheen, g.const(0.45)))
    eye = g.sat(g.mul(g.sub(g.mask(vc, b=True), g.mask(vc, r=True)), g.const(4.0)))
    tc = (g.node(unreal.MaterialExpressionTextureCoordinate), "")
    c2 = (g.node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5), "")
    dd = g.node(unreal.MaterialExpressionDistance)
    g.link(tc, dd, "A")
    g.link(c2, dd, "B")
    slit = g.node(unreal.MaterialExpressionDistance)
    g.link(g.mul(g.sub(tc, c2), (g.node(unreal.MaterialExpressionConstant2Vector, r=2.6, g=1.0), "")), slit, "A")
    g.link((g.node(unreal.MaterialExpressionConstant2Vector, r=0.0, g=0.0), ""), slit, "B")
    pupil = g.sat(g.mul(g.sub((slit, ""), g.const(0.3)), g.const(16.0)))
    ring = g.sat(g.mul(g.sub((dd, ""), g.const(0.25)), g.const(9.0)))
    iris = g.mul(g.lerp(g.vec("IrisInner", (0.62, 0.42, 0.04)), g.vec("IrisOuter", (0.28, 0.34, 0.04)), ring), pupil)
    g.out(g.lerp(col, iris, eye), MP.MP_BASE_COLOR)
    g.out(g.lerp(g.const(0.88), g.const(0.05), eye), MP.MP_ROUGHNESS)
    g.out(g.lerp(g.const(0.18), g.const(1.0), eye), MP.MP_SPECULAR)
    g.out(g.lerp(g.lerp(g.const3((0, 0, 1)), (nm, "RGB"), g.const(0.25)), g.const3((0, 0, 1)), eye), MP.MP_NORMAL)
    g.out(g.mul(coat, g.const(0.45)), MP.MP_SUBSURFACE_COLOR)
    g.out(g.const(0.2), MP.MP_OPACITY)
    return g.finish(skeletal=True)


def build_cat_eye():
    g = Graph("M_CatEye")
    tc = (g.node(unreal.MaterialExpressionTextureCoordinate), "")
    c = (g.node(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5), "")
    d = g.node(unreal.MaterialExpressionDistance)
    g.link(tc, d, "A")
    g.link(c, d, "B")
    slit = g.node(unreal.MaterialExpressionDistance)
    g.link(g.mul(g.sub(tc, c), (g.node(unreal.MaterialExpressionConstant2Vector, r=2.6, g=1.0), "")), slit, "A")
    g.link((g.node(unreal.MaterialExpressionConstant2Vector, r=0.0, g=0.0), ""), slit, "B")
    pupil = g.sat(g.mul(g.sub((slit, ""), g.const(0.3)), g.const(16.0)))
    ring = g.sat(g.mul(g.sub((d, ""), g.const(0.25)), g.const(9.0)))
    iris = g.lerp(g.vec("IrisInner", (0.78, 0.55, 0.06)), g.vec("IrisOuter", (0.35, 0.42, 0.05)), ring)
    col = g.mul(iris, pupil)
    g.out(col, MP.MP_BASE_COLOR)
    g.out(g.const(0.04), MP.MP_ROUGHNESS)
    g.out(g.const(1.0), MP.MP_SPECULAR)
    g.out(g.mul(col, g.const(0.15)), MP.MP_EMISSIVE_COLOR)
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
    mi("MI_Wall_Peach", S, wall, {"Tint": (1.0, 0.76, 0.56)}, surf("white_stucco_02"))
    mi("MI_Wall_Salmon", S, wall, {"Tint": (1.0, 0.6, 0.47)}, surf("white_stucco"))
    mi("MI_Wall_Lemon", S, wall, {"Tint": (1.0, 0.84, 0.36)}, surf("white_stucco_02"))
    mi("MI_Wall_Ochre", S, wall, {"Tint": (1.0, 0.66, 0.3)}, surf("white_stucco_02"))
    mi("MI_Wall_Rose", S, wall, {"Tint": (1.0, 0.64, 0.66)}, surf("white_stucco"))
    mi("MI_Wall_Sky", S, dict(wall, Tiling=1.0, Brightness=1.1), {"Tint": (1.05, 1.12, 1.12)}, surf("blue_plaster_weathered"))
    mi("MI_Wall_Aged", S, dict(wall, Tiling=1.0), {"Tint": (1.0, 0.96, 0.9)}, surf("white_rough_plaster"))
    mi("MI_Wall_Beige", S, dict(wall, Tiling=1.0), {"Tint": (1.0, 0.95, 0.86)}, surf("painted_plaster_wall"))
    mi("MI_Wall_Stone", S, {"Tiling": 0.9, "DirtAmount": 0.4, "MacroAmount": 0.15}, {"Tint": (1.0, 0.95, 0.88)}, surf("rustic_stone_wall"))
    mi("MI_Wall_Stacked", S, {"Tiling": 1.0, "DirtAmount": 0.3, "MacroAmount": 0.15}, None, surf("stacked_stone_wall"))
    mi("MI_Trim_White", S, {"Tiling": 2.5, "MacroAmount": 0.06, "NormalStrength": 0.5}, {"Tint": (1.05, 1.03, 1.0)}, surf("white_stucco"))
    ground = {"MacroAmount": 0.2, "DirtAmount": 0.0}
    mi("MI_Cobble", S, dict(ground, Tiling=1.3), {"Tint": (1.0, 0.97, 0.92)}, surf("cobblestone_floor_08"))
    mi("MI_Flag", S, dict(ground, Tiling=0.8), {"Tint": (1.02, 0.98, 0.92)}, surf("stone_pathway"))
    mi("MI_StepStone", S, dict(ground, Tiling=1.2), {"Tint": (1.0, 0.97, 0.92)}, surf("grey_stone_path"))
    mi("MI_Terracotta", S, dict(ground, Tiling=1.6), None, surf("terracotta_floor_tiles"))
    mi("MI_Sand", S, dict(ground, Tiling=0.6), None, surf("coast_sand_01"))
    mi("MI_Cliff", S, {"Tiling": 0.35, "MacroAmount": 0.25}, {"Tint": (0.95, 0.9, 0.84)}, surf("marble_cliff_02"))
    mi("MI_RoofTiles", S, {"Tiling": 1.4, "MacroAmount": 0.2, "DirtAmount": 0.0}, None, surf("clay_roof_tiles_02"))

    P = M["M_Paint"]
    planks = surf("blue_painted_planks")
    mi("MI_Shutter_Green", P, {"WearAmount": 0.25, "Tiling": 1.6}, {"Tint": (0.2, 0.42, 0.3)}, planks)
    mi("MI_Shutter_Blue", P, {"WearAmount": 0.3, "Tiling": 1.6}, {"Tint": (0.12, 0.3, 0.55)}, planks)
    mi("MI_Shutter_Teal", P, {"WearAmount": 0.28, "Tiling": 1.6}, {"Tint": (0.16, 0.45, 0.48)}, planks)
    mi("MI_Shutter_Olive", P, {"WearAmount": 0.35, "Tiling": 1.6}, {"Tint": (0.38, 0.42, 0.22)}, planks)
    mi("MI_Door_Blue", P, {"WearAmount": 0.35, "Tiling": 1.2}, {"Tint": (0.1, 0.22, 0.42)}, surf("distressed_painted_planks"))
    mi("MI_Door_Red", P, {"WearAmount": 0.35, "Tiling": 1.2}, {"Tint": (0.5, 0.12, 0.08)}, surf("distressed_painted_planks"))
    mi("MI_Door_Wood", P, {"WearAmount": 1.0, "Tiling": 1.2}, None, surf("weathered_planks"))
    mi("MI_Wood_Crate", P, {"WearAmount": 1.0, "Tiling": 1.4}, None, surf("weathered_planks"))
    mi("MI_Wood_Beam", P, {"WearAmount": 0.9, "Tiling": 0.8}, None, surf("weathered_planks"))
    mi("MI_Paint_Cream", P, {"WearAmount": 0.35, "Tiling": 1.4, "PaintRough": 0.5}, {"Tint": (0.85, 0.8, 0.68)}, planks)

    mi("MI_Iron_Black", M["M_Iron"], {"Rust": 0.35}, {"Tint": (0.02, 0.022, 0.022)})
    mi("MI_Iron_Green", M["M_Iron"], {"Rust": 0.5}, {"Tint": (0.03, 0.08, 0.05)})
    mi("MI_Glass", M["M_Glass"], {"InteriorGlow": 0.0})
    mi("MI_Glass_Warm", M["M_Glass"], {"InteriorGlow": 1.2})
    mi("MI_Lantern_Glow", M["M_Emissive"], {"Intensity": 5.0}, {"Color": (1.0, 0.7, 0.38)})
    mi("MI_Bulb_Warm", M["M_Emissive"], {"Intensity": 40.0}, {"Color": (1.0, 0.62, 0.3)})

    C = M["M_Cloth"]
    for v in ("Terracotta", "Sea", "Lemon"):
        mi(f"MI_Awning_{v}", C, {"Tiling": 1.0, "WeaveTiling": 6.0, "FlutterAmp": 0.0, "Roughness": 0.9}, None, {"BaseColorTex": tex(f"T_Awning_{v}_D")})
    for v in ("White", "BlueStripe", "Coral", "Lemon", "Sea"):
        mi(f"MI_Linen_{v}", C, {"Tiling": 1.0, "WeaveTiling": 8.0, "FlutterAmp": 7.0, "HangLength": 90.0}, None, {"BaseColorTex": tex(f"T_Linen_{v}_D")})
    mi("MI_Towel_Terry", C, {"Tiling": 1.0, "WeaveTiling": 3.0, "FlutterAmp": 5.0, "HangLength": 70.0}, None, {"BaseColorTex": ph("terry_cloth", "diff")})
    mi("MI_Tablecloth", C, {"Tiling": 1.5, "WeaveTiling": 6.0, "FlutterAmp": 0.6, "HangLength": 25.0}, None, {"BaseColorTex": ph("fabric_pattern_07", "diff")})

    K = M["M_Card"]
    mi("MI_Bougain", K, {"WindAmp": 2.0}, {"Tint": (1.0, 1.0, 1.0)}, {"BaseColorTex": tex("T_Bougain_D")})
    mi("MI_Bougain_Deep", K, {"WindAmp": 2.0}, {"Tint": (0.85, 0.7, 0.95)}, {"BaseColorTex": tex("T_Bougain_D")})
    mi("MI_Leafy", K, {"WindAmp": 1.5}, {"Tint": (1.0, 1.0, 1.0)}, {"BaseColorTex": tex("T_Leafy_D")})

    mi("MI_Majolica", M["M_Majolica"], {"Tiling": 2.0})
    Pp = M["M_Prop"]
    for n in ("Caffe", "Limoni", "Street", "Menu"):
        mi(f"MI_Sign_{n}", Pp, {"RoughMul": 0.75 if n != "Street" else 0.25}, None, {"BaseColorTex": tex(f"T_Sign_{n}")})
    mi("MI_Lemon_Skin", Pp, {"RoughMul": 0.6}, {"Tint": (1.0, 0.95, 0.55)}, {"BaseColorTex": WHITE})
    mi("MI_CatFur", M["M_CatFur"], {"CoatDark": 0.0}, {"CoatTint": (1, 1, 1)})
    mi("MI_CatEye", M["M_CatEye"])


FLAT_N = tex("T_FlatNormal")
FLAT_ARM = tex("T_FlatARM")
WHITE = tex("T_White")
M = {}
for fn in (build_surface, build_paint, build_prop, build_foliage, build_card, build_cloth, build_iron, build_glass, build_emissive,
           build_majolica, build_water, build_shade_body, build_shade_eye, build_cat_fur, build_cat_eye, build_wisp):
    try:
        m = fn()
        M[m.get_name()] = m
    except Exception as e:
        import traceback
        log("ERROR", fn.__name__, e, traceback.format_exc())
build_instances(M)
eal.save_directory("/Game/Shade/Materials", only_if_is_dirty=True, recursive=True)
log("ALL DONE")
