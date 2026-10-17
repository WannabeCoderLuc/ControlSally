"""Build "Sahelanthropus Unbound" test build 1 (.mgsv) from the extracted reference game files.

Test build 1 goal: an Infinite Heaven boss type `TppSahelan2` that spawns Sahelanthropus (Dominion AI) in free roam,
with Episode 31's arena navmesh, routes, REX sniping points and search-missile point loaded so it can move and fight.
Map-wide roaming comes in later builds (nav-world load limit, generated navmesh).

All game data is copied from the player's own extracted files; nothing is redistributed by this script itself.
Run with the ATOMIZER venv:  python build_sahelan_unbound.py [--omit nav|points|sound|searchmissile]

--omit builds a death-reload-hang test variant that leaves exactly one suspect dataset out of the Sahelan script
block (TppSahelan2_block never reaches EMPTIED once unloaded; see the maintainer's research notes (OptionB_B1_Findings)):
  nav            NavxNavBlock (Sahelan navmesh) from the arena dataset
  points         TppHidePointData + TppSnipePoint2Data from the arena dataset
  sound          the se_e_mgs SoundPackage dataset and se_e_mgs.sdf
  searchmissile  the TppSearchMissilePointData dataset
Variants are named "t2a-no<what>".

t2a adds an Africa free-roam (30020) game-object pack, TppSahelan2_gameobject_mafr, used by the sub type addon
sahelan_mgs0_dominion_mafr: same GameObject, motions, sound and weapon extras, the locator moved to Nova Braga Airport's
north landing zone (MAFR_LOCATOR), and none of the Afghanistan-only Episode 31 data (sahelan navmesh, routes .frt,
REX hide/snipe points, search-missile point): the blank-slate set of Arch doc 8.5. The Afghanistan pack is unchanged.
"""

import json
from pathlib import Path

from atomizer_mcp.extractors.engines.fox.dataset import (
    DataSetFile,
    Entity,
    Property,
    StrCodeNames,
    build,
    parse,
    strcode64,
)
from atomizer_mcp.extractors.engines.fox.fpk import build as build_fpk
from atomizer_mcp.extractors.engines.fox.mgsv import MgsvPackage, build_mgsv
from atomizer_mcp.utils.safe_io import long_path

MG = Path(__import__("os").environ.get("SALLY_WORKSPACE", "."))  # maintainer workspace root
CURRENT = Path(__import__("os").environ.get("SALLY_GAME_DATA", MG / "game_data"))  # your extracted 1.0.15.4 game files
OUT = MG / "Mods/_build"
LUA = MG / "Tools/sahelan_unbound_lua"
NAMES = StrCodeNames([MG / "Research/sources/FoxTool/FoxTool/fox_dictionary.txt",
                      MG / "Research/sources/GzsTool/GzsTool/qar_dictionary.txt"])

VERSION = "t2a"
OMIT: set[str] = set()  # hang-test variant: suspect datasets left out (see --omit)
# Script block budget. IH's own boss blocks use 1.5 MB; vanilla blocks go up to 12.45 MB (Episode 31's demo_block).
# We size ours from the packages it loads (computed in main), clamped to that proven range.
BLOCK_SIZE_MIN, BLOCK_SIZE_MAX = 1_572_864, 12_450_000
PARTS_PACK = "/Assets/tpp/pack/mission2/common/mis_com_sahelan"  # vanilla parts/model/textures, loaded as-is
CONDITIONER = "TexturePackLoadConditioner0000"  # every vanilla and IH DataSet carries one; keep it in subsets
BOSS_ROOT = "/Assets/tpp/level/mission2/boss/ih/TppSahelan2"
GAMEOBJECT_PACK = "/Assets/tpp/pack/boss/ih/TppSahelan2/TppSahelan2_gameobject"
GAMEOBJECT_PACK_MAFR = GAMEOBJECT_PACK + "_mafr"
# Africa locator: ground point of the Nova Braga Airport north LZ (IH InfLZ lz_drp_pfCamp_N0000), ~420 m north of the
# airfield (mafr_pfCamp_cp). Must match arenaPos in sahelan_mgs0_dominion_mafr.lua.
MAFR_LOCATOR = (1061.84, 6.78, 731.21)
BLOCK_PACK = "/Assets/tpp/pack/mission2/boss/ih/TppSahelan2_scriptblockdata"

_inventory: list[dict] | None = None


def inventory() -> list[dict]:
    global _inventory
    if _inventory is None:
        _inventory = json.loads((CURRENT / "FileInventory.json").read_text(encoding="utf-8"))["files"]
    return _inventory


def vanilla(asset: str, pack_hint: str) -> bytes:
    """Bytes of `asset` (/Assets/...) from the extracted package whose path contains `pack_hint`.
    The patch archive (master/0/00.dat) wins over the base chunks, as in the game."""
    suffix = asset.lstrip("/")
    hits = [r for r in inventory() if r["path"].endswith(suffix) and pack_hint in r["origin"].get("parent", "")]
    if not hits:
        raise SystemExit(f"missing vanilla file {asset} (pack {pack_hint})")
    hits.sort(key=lambda r: "/0/00.dat/" not in r["path"])
    with open(long_path((CURRENT / hits[0]["path"]).resolve()), "rb") as handle:
        return handle.read()


def subset(source: DataSetFile, keep_names: set[str]) -> DataSetFile:
    """Keep the root DataSet entity, the named entities, and every entity they point to (transforms etc.)."""
    by_address = {e.address: e for e in source.entities}
    root = next(e for e in source.entities if e.class_name == "DataSet")
    keep_names = set(keep_names) | {CONDITIONER}
    keep = {e.address for e in source.entities if e.get("name") and e.get("name").values[0] in keep_names}
    frontier = list(keep)
    while frontier:  # follow EntityPtr references (not owner handles, which point back up)
        entity = by_address[frontier.pop()]
        for prop in entity.properties:
            if prop.data_type == "EntityPtr" and prop.container != "StringMap":
                for value in prop.values:
                    address = int(value, 16) if value else 0
                    if address and address in by_address and address not in keep:
                        keep.add(address)
                        frontier.append(address)
    data_list = root.get("dataList")
    kept_list = {k: v for k, v in data_list.values.items() if int(v, 16) in keep}
    new_root = Entity(root.class_name, root.class_version, root.address, 0,
                      [Property(p.name, p.data_type, p.container, kept_list if p.name == "dataList" else p.values,
                                p.dynamic, p.name_hash) for p in root.properties],
                      root.class_hash, root.unknown1, root.unknown2)
    entities = [new_root] + [e for e in source.entities if e.address in keep]
    used = _used_strings(entities)
    strings = {h: s for h, s in source.strings.items() if s in used}
    return DataSetFile(entities=entities, strings=strings)


def _used_strings(entities: list[Entity]) -> set[str]:
    """Literals a DataSet's string table lists, as in the game's files: class names, property names and values."""
    used = {e.class_name for e in entities}
    used |= {p.name for e in entities for p in e.properties}
    used |= {v for e in entities for p in e.properties for v in _strings_of(p)}
    return used


def _strings_of(prop: Property):
    values = prop.values.values() if isinstance(prop.values, dict) else prop.values
    keys = prop.values.keys() if isinstance(prop.values, dict) else ()
    for v in [*values, *keys]:
        if isinstance(v, str) and not v.startswith("0x"):
            yield v


def locator_dataset(position: tuple[float, float, float] | None = None) -> bytes:
    """GameObjectLocator for TppSahelan2: Episode 31's arena position, or `position` (TppSahelan2 has no Warp command,
    so the locator is where it appears)."""
    s10151 = parse(vanilla("/Assets/tpp/level/mission2/story/s10151/s10151_npc_sp.fox2", "s10151_special_npc"), NAMES)
    ds = subset(s10151, {"Sahelanthropus"})
    if position is not None:
        moved = 0
        for entity in ds.entities:
            if entity.class_name == "TransformEntity":
                entity.get("transform_translation").values = [[float(position[0]), float(position[1]), float(position[2]), 0.0]]
                moved += 1
        if moved != 1:
            raise SystemExit(f"expected one TransformEntity in the locator subset, found {moved}")
    return build(ds)


def arena_dataset() -> bytes:
    """Episode 31 arena: Sahelanthropus navmesh block, REX tower/hide points (TppHidePointData) and buddy Quiet's
    sniping points for this fight (TppSnipePoint2Data qui_battleSahelan_*; t1/t1b crashed when Quiet arrived)."""
    block01 = parse(vanilla("/Assets/tpp/level/mission2/story/s10151/s10151_block01.fox2", "s10151_area01"), NAMES)
    classes = {"NavxNavBlock", "TppHidePointData", "TppSnipePoint2Data"}
    if "nav" in OMIT:
        classes -= {"NavxNavBlock"}
    if "points" in OMIT:
        classes -= {"TppHidePointData", "TppSnipePoint2Data"}
    keep = {e.get("name").values[0] for e in block01.entities
            if e.class_name in classes and e.get("name")}
    return build(subset(block01, keep))


def search_missile_dataset() -> bytes:
    route = parse(vanilla("/Assets/tpp/level/mission2/story/s10151/s10151_route.fox2", "s10151_area01"), NAMES)
    return build(subset(route, {"TppSearchMissilePointData0000"}))


def sound_dataset() -> bytes:
    """Only the SoundPackage that loads se_e_mgs (Sahelanthropus sound effects)."""
    se = parse(vanilla("/Assets/tpp/level/mission2/story/s10151/s10151_sound_se.fox2", "s10151_area01"), NAMES)
    keep = {e.get("name").values[0] for e in se.entities
            if e.class_name == "SoundPackage" and any("se_e_mgs" in str(v) for p in e.properties for v in _flat(p))}
    if not keep:
        raise SystemExit("no SoundPackage for se_e_mgs found")
    return build(subset(se, keep))


def _flat(prop: Property):
    yield from (prop.values.values() if isinstance(prop.values, dict) else prop.values)


def script_block_dataset(block_size: int) -> bytes:
    """ScriptBlockData `TppSahelan2_block`: Episode 31's demo_block entity (same class version and layout), renamed,
    resized and with IH's free-roam prerequisite list."""
    block01 = parse(vanilla("/Assets/tpp/level/mission2/story/s10151/s10151_block01.fox2", "s10151_area01"), NAMES)
    ds = subset(block01, {"demo_block"})
    for entity in ds.entities:
        if entity.class_name == "DataSet":
            data_list = entity.get("dataList")
            data_list.values = {("TppSahelan2_block" if k == "demo_block" else k): v for k, v in data_list.values.items()}
        elif entity.class_name == "ScriptBlockData":
            entity.get("name").values = ["TppSahelan2_block"]
            entity.get("sizeInBytes").values = [block_size]
            entity.get("prerequisites").values = ["mission_block"]
    used = _used_strings(ds.entities)
    strings = {h: t for h, t in block01.strings.items() if t in used}
    strings.update({strcode64(t): t for t in ("TppSahelan2_block", "mission_block") if t not in strings.values()})
    return build(DataSetFile(entities=ds.entities, strings=strings))


PLAYER_MTAR = "/Assets/tpp/motion/mtar/player2/Mission/pl_mission_s10151.mtar"


def player_motion_dataset() -> bytes:
    """Episode 31's TppPlayer2AdditionalMtarData: the extra player motions for the Sahelanthropus fight
    (pl_mission_s10151.mtar). t1 left it out and crashed in player code during the fight."""
    block01 = parse(vanilla("/Assets/tpp/level/mission2/story/s10151/s10151_block01.fox2", "s10151_area01"), NAMES)
    keep = {e.get("name").values[0] for e in block01.entities if e.class_name == "TppPlayer2AdditionalMtarData"}
    return build(subset(block01, keep))


def sahelan_extras(pack: str) -> list[tuple[str, bytes]]:
    """Every Sahelanthropus file (mecha/mgs) Episode 31's NPC pack adds on top of the shared mis_com_sahelan pack:
    skeletons (.gskl) and connection points (.fcnp) of the missiles, missile pod, search missile and ground part,
    their .parts and hitboxes, the search-missile and rock models, the colour-variation fova. t1-t1c lacked them and
    crashed reading a bone matrix (mgsvtpp+0xadbfbb) once Sahelanthropus started using its weapons."""
    in_parts_pack = {r["path"].split("~/", 1)[1] for r in inventory()
                     if "mis_com_sahelan.fpk" in r["origin"].get("parent", "") and "~/" in r["path"]}
    files = {}
    for r in inventory():
        if not r["origin"].get("parent", "").endswith(f"s10151_special_npc.{pack}") or "~/" not in r["path"]:
            continue
        inner = r["path"].split("~/", 1)[1]
        if "/mecha/mgs/" in inner and inner not in in_parts_pack and not inner.endswith("sahelan_afgn.fox2"):
            files["/" + inner] = vanilla("/" + inner, f"s10151_special_npc.{pack}")
    return sorted(files.items())


def gameobject_packs(location: str = "afgh") -> tuple[bytes, bytes]:
    """afgh: t1e pack (Episode 31 locator + arena data). mafr: blank slate with the locator at MAFR_LOCATOR."""
    africa = location == "mafr"
    fpk_files = [
        ("/Assets/tpp/motion/motion_graph/sahelan2/TppSahelan2_layers.mog", "s10151_special_npc.fpk"),
        ("/Assets/tpp/motion/mtar/sahelan2/TppSahelan2_layers.mtar", "s10151_special_npc.fpk"),
        ("/Assets/tpp/motion/mtar/equip/EQP_IT_mgs0_msbl0.mtar", "s10151_special_npc.fpk"),  # missile pod motion
    ]
    if not africa:
        fpk_files += [
            ("/Assets/tpp/level/mission2/story/s10151/s10151_nav_sahelan.nav2", "s10151_area01.fpk"),
            ("/Assets/tpp/pack/mission2/story/s10151/s10151_area01.frt", "s10151_area01.fpk"),
        ]
    fpk = build_fpk("fpk", [(path, vanilla(path, hint)) for path, hint in fpk_files] + sahelan_extras("fpk"))

    gameobject = vanilla("/Assets/tpp/level_asset/chara/mecha/sahelan_afgn.fox2", "s10151_special_npc.fpkd")
    effects = sorted({v for e in parse(gameobject, NAMES).entities for p in e.properties
                      if p.data_type == "FilePtr" for v in _flat(p) if isinstance(v, str) and v.endswith(".vfx")})
    fpkd_files = [
        ("/Assets/tpp/level_asset/chara/mecha/sahelan_afgn.fox2", gameobject),
        (f"{BOSS_ROOT}/TppSahelan2_locator.fox2", locator_dataset(MAFR_LOCATOR if africa else None)),
    ]
    if not africa:
        fpkd_files.append((f"{BOSS_ROOT}/TppSahelan2_arena.fox2", arena_dataset()))
    if "searchmissile" not in OMIT and not africa:
        fpkd_files.append((f"{BOSS_ROOT}/TppSahelan2_searchmissile.fox2", search_missile_dataset()))
    if "sound" not in OMIT:
        fpkd_files += [
            (f"{BOSS_ROOT}/TppSahelan2_sound.fox2", sound_dataset()),
            ("/Assets/tpp/sound/package/se_e_mgs.sdf", vanilla("/Assets/tpp/sound/package/se_e_mgs.sdf", "s10151_area01.fpkd")),
        ]
    in_parts_pack = {r["path"].split("~/", 1)[1] for r in inventory()
                     if "mis_com_sahelan.fpkd" in r["origin"].get("parent", "") and "~/" in r["path"]}
    for effect in effects:
        if effect.lstrip("/") in in_parts_pack:
            continue  # already loaded by the vanilla parts pack
        fpkd_files.append((effect, vanilla(effect, "s10151_special_npc.fpkd")))
    fpkd_files += sahelan_extras("fpkd")
    return fpk, build_fpk("fpkd", fpkd_files)


def pack_size(path: str) -> int:
    """Size of a vanilla top-level package (patch archive wins)."""
    hits = [r for r in inventory() if r["path"].endswith(path.lstrip("/")) and "~" not in r["path"]]
    hits.sort(key=lambda r: "/0/00.dat/" not in r["path"])
    return hits[0]["size"]


def main() -> None:
    import argparse
    global VERSION
    ap = argparse.ArgumentParser()
    ap.add_argument("--omit", choices=["nav", "points", "sound", "searchmissile"], action="append", default=[])
    args = ap.parse_args()
    OMIT.update(args.omit)
    if OMIT:
        VERSION = VERSION + "-no" + "-no".join(sorted(OMIT))
    OUT.mkdir(parents=True, exist_ok=True)
    fpk, fpkd = gameobject_packs("afgh")
    fpk_m, fpkd_m = gameobject_packs("mafr")
    loaded = max(len(fpk) + len(fpkd), len(fpk_m) + len(fpkd_m)) + pack_size(f"{PARTS_PACK}.fpk") + pack_size(f"{PARTS_PACK}.fpkd")
    block_size = min(max(-(-int(loaded * 1.1) // 65536) * 65536, BLOCK_SIZE_MIN), BLOCK_SIZE_MAX)
    package = MgsvPackage(
        name="Sahelanthropus Unbound", version=VERSION, author="Luc + ATOMIZER",
        description=("Test build 2a: adds Sahelanthropus (TppSahelan2) as an Infinite Heaven boss type in Afghanistan "
                     "free roam (Episode 31 battlefield, navmesh, routes, REX points) and Africa free roam (blank slate, "
                     "starts at Nova Braga Airport). Requires Infinite Heaven r267 and IHHook r25. Offline single-player "
                     "only. Local test build, not for distribution."),
        qar_files={
            f"{GAMEOBJECT_PACK}.fpk": fpk,
            f"{GAMEOBJECT_PACK}.fpkd": fpkd,
            f"{GAMEOBJECT_PACK_MAFR}.fpk": fpk_m,
            f"{GAMEOBJECT_PACK_MAFR}.fpkd": fpkd_m,
            # mission pack (loaded with Afghanistan free roam): script block declaration + player fight motions
            f"{BLOCK_PACK}.fpk": build_fpk("fpk", [(PLAYER_MTAR, vanilla(PLAYER_MTAR, "s10151_area01.fpk"))]),
            f"{BLOCK_PACK}.fpkd": build_fpk("fpkd", [
                (f"{BOSS_ROOT}/TppSahelan2_scriptblockdata.fox2", script_block_dataset(block_size)),
                (f"{BOSS_ROOT}/TppSahelan2_player.fox2", player_motion_dataset()),
            ]),
        },
        game_dir_files={f"/{p.relative_to(LUA).as_posix()}": p.read_bytes() for p in LUA.rglob("*.lua")},
    )
    target = OUT / f"Sahelanthropus Unbound {VERSION}.mgsv"
    target.write_bytes(build_mgsv(package))
    print(f"wrote {target} ({target.stat().st_size / 1e6:.2f} MB)")
    print(f"script block: {block_size} bytes for {loaded / 1e6:.2f} MB of block packages")
    for path, data in sorted(package.qar_files.items()):
        print(f"  {len(data) / 1e6:7.2f} MB  {path}")
    for path in sorted(package.game_dir_files):
        print(f"            GameDir{path}")


if __name__ == "__main__":
    main()
