"""Make every single-channel map (alpha / wear / wisp / ink noise) a linear TC_Masks texture, matching the Masks samplers."""
import unreal

eal = unreal.EditorAssetLibrary
n = 0
for root in ("/Game/Shade/Props", "/Game/Shade/Textures"):
    for p in eal.list_assets(root, recursive=True, include_folder=False):
        name = p.split("/")[-1].split(".")[0].lower()
        if "/mesh/" in p.lower():
            continue
        if any(k in name for k in ("alpha", "wearmask", "t_wisp", "inknoise")):
            t = eal.load_asset(p)
            if isinstance(t, unreal.Texture2D):
                t.set_editor_property("srgb", False)
                t.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
                eal.save_loaded_asset(t)
                n += 1
unreal.log(f"SNS_MASKS fixed {n} textures")
