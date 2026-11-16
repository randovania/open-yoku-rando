import zlib

import pytest

from open_yoku_rando import game_files

from .conftest import CEREMONY, GAME_SCRIPTS, TUNGUSKA_SCRIPT

TARGET = f"{game_files.ITEMS_FOLDER}/{game_files.NOTHING_ITEM_ID}_x102.sim"
TEXTS = game_files.TEXTS_PATH
SCRIPTS = game_files.SCRIPTS_PATH
DLL = f"{game_files.MOD_FOLDER}/open_yoku_rando.dll"
PROXY = "xinput9_1_0.dll"
GAME_STRINGS = "processed/text/dialog_en.strings"


def shipped_model():
    return game_files.installed_file_source(f"items/{game_files.DEFAULT_MODEL}_x102.sim")


def default_models() -> dict[str, bytes]:
    return {game_files.NOTHING_ITEM_ID: shipped_bytes()}


def shipped_bytes() -> bytes:
    return shipped_model().read_bytes()


def all_categories() -> dict[str, dict[str, str]]:
    """A category for every location a category line is about."""
    return {
        line.location: {"determiner": "an ", "noun": "upgrade"} for line in game_files.load_category_lines()
    }


def add_game_model(game_path, relative: str) -> bytes:
    """A model file of the game, game-relative without the suffix; returns its content."""
    path = game_path.joinpath(f"{relative}_x102.sim")
    path.parent.mkdir(parents=True, exist_ok=True)
    data = f"the game's {relative}".encode()
    path.write_bytes(data)
    return data


def game_files_snapshot(game_path) -> dict[str, bytes]:
    """Every file outside the mod's own, as it is now."""
    return {
        path.relative_to(game_path).as_posix(): path.read_bytes()
        for path in game_path.rglob("*")
        if path.is_file()
        and path.relative_to(game_path).parts[0] != game_files.MOD_FOLDER
        and path.name != PROXY
    }


def test_the_shipped_texture_is_a_sim_the_game_can_read():
    data = shipped_bytes()
    magic, width, height, padded_width, padded_height, fmt, zero, uncompressed, compressed = (
        int.from_bytes(data[i:i + 4], "little") for i in range(0, 0x24, 4)
    )
    assert magic == 0x73696D0B
    assert (fmt, zero) == (7, 0)
    assert (padded_width, padded_height) == ((width + 3) // 4 * 4, (height + 3) // 4 * 4)
    assert compressed == len(data) - 0x24

    # The game reads the mip chain from one zlib stream and trusts these two sizes.
    assert len(zlib.decompress(data[0x24:])) == uncompressed


def test_install_puts_everything_into_the_mod_folder(fake_game_path):
    install = game_files.install_files(fake_game_path, default_models(), {}, all_categories())

    assert sorted(install.written) == sorted([DLL, PROXY, TARGET, SCRIPTS, TEXTS])
    assert install.removed == ()
    assert fake_game_path.joinpath(TARGET).read_bytes() == shipped_bytes()
    assert fake_game_path.joinpath(DLL).is_file()
    assert fake_game_path.joinpath(PROXY).is_file()
    assert fake_game_path.joinpath(SCRIPTS).read_bytes() == GAME_SCRIPTS + PATCHED_SCRIPT_LINES


def test_install_changes_no_file_of_the_game(fake_game_path):
    before = game_files_snapshot(fake_game_path)

    game_files.install_files(fake_game_path, default_models(), {42: "A caption"}, all_categories())

    assert game_files_snapshot(fake_game_path) == before
    assert not fake_game_path.joinpath("processed", "items").exists()


def test_the_default_model_is_the_shipped_icon(fake_game_path):
    assert game_files.model_source(fake_game_path, game_files.DEFAULT_MODEL) == shipped_model()


@pytest.mark.parametrize(
    ("model", "relative"),
    [
        ("toolbox", "processed/items/toolbox"),
        ("abilities/dive", "processed/items/abilities/dive"),
        ("wickerling", "processed/items/collectible"),
        ("reward_fruit_big", "processed/pickups/fruit_large"),
        ("reward_fruit_medium", "processed/pickups/fruit_medium"),
    ],
)
def test_a_model_comes_from_the_game_install(fake_game_path, model, relative):
    add_game_model(fake_game_path, relative)

    assert game_files.model_source(fake_game_path, model) == fake_game_path.joinpath(f"{relative}_x102.sim")


def test_a_model_missing_from_the_install_is_an_error(fake_game_path):
    with pytest.raises(ValueError, match="has no model for 'toolbox'"):
        game_files.model_source(fake_game_path, "toolbox")


def test_install_copies_a_game_model_under_its_id(fake_game_path):
    data = add_game_model(fake_game_path, "processed/items/abilities/dive")
    before = game_files_snapshot(fake_game_path)
    models = {
        **default_models(),
        "nothing_abilities_dive": game_files.model_data(fake_game_path, "abilities/dive"),
    }

    install = game_files.install_files(fake_game_path, models, {}, all_categories())

    target = f"{game_files.ITEMS_FOLDER}/nothing_abilities_dive_x102.sim"
    assert target in install.written
    assert fake_game_path.joinpath(target).read_bytes() == data
    assert game_files_snapshot(fake_game_path) == before


def test_install_writes_the_texts(fake_game_path):
    game_files.install_files(fake_game_path, default_models(), {42: "Line one\nLine two; and more"}, all_categories())

    text = fake_game_path.joinpath(TEXTS).read_bytes().decode("utf-8")
    assert text.endswith("\r\n")
    lines = text.split("\r\n")[:-1]
    assert "items/nothing_name;Nothing" in lines
    # The game's .strings format: no real line break and no semicolon inside a value.
    assert "items/nothing_42_1;Line one\\nLine two, and more" in lines


def test_install_writes_the_category_lines(fake_game_path):
    categories = all_categories()
    categories["jungle_canyon_caper:2679"] = {"determiner": "a ", "noun": "key"}
    categories["cave_temple_terror_lower:4744"] = {"determiner": "some ", "noun": "fruit"}

    game_files.install_files(fake_game_path, default_models(), {}, categories)

    lines = fake_game_path.joinpath(TEXTS).read_bytes().decode("utf-8").split("\r\n")
    assert "kickback_2b;Here, take a key as thanks for letting me out!" in lines
    assert "willo_temple_1b;According to my scrying, some fruit is nearby!" in lines
    assert "willo_temple_2;Trouble finding the fruit? It should be somewhere below! Look for its magical gleam!" in (
        lines
    )


def test_every_category_line_fills_from_the_seeds_categories(shuffled_configuration):
    categories = {pickup["location"]: pickup["category"] for pickup in shuffled_configuration["pickups"]}

    filled = dict(game_files.category_strings(categories))

    assert len(filled) == len(game_files.load_category_lines())
    assert not any("{" in line for line in filled.values())


def test_install_removes_models_it_no_longer_uses(fake_game_path):
    old = fake_game_path.joinpath(game_files.ITEMS_FOLDER, "nothing_missiles_x102.sim")
    old.parent.mkdir(parents=True)
    old.write_bytes(b"an earlier seed's model")

    install = game_files.install_files(fake_game_path, default_models(), {}, all_categories())

    assert not old.exists()
    assert install.removed == (f"{game_files.ITEMS_FOLDER}/nothing_missiles_x102.sim",)
    assert fake_game_path.joinpath(TARGET).is_file()


def test_installing_twice_gives_the_same_texts(fake_game_path):
    game_files.install_files(fake_game_path, default_models(), {42: "A caption"}, all_categories())
    once = fake_game_path.joinpath(TEXTS).read_bytes()

    game_files.install_files(fake_game_path, default_models(), {42: "A caption"}, all_categories())

    assert fake_game_path.joinpath(TEXTS).read_bytes() == once


def test_install_explains_missing_dlls(fake_game_path, mocker, tmp_path):
    mocker.patch("open_yoku_rando.game_files.files_path", return_value=tmp_path)

    with pytest.raises(ValueError, match=r"native/build\.cmd"):
        game_files.install_files(fake_game_path, {}, {}, all_categories())


def test_nothing_strings_default_model_only():
    keys = [key for key, _ in game_files.nothing_strings({}, ("nothing",))]

    assert keys == ["items/nothing_name", "items/nothing_desc", "items/nothing_1"]


def test_nothing_strings_names_every_model():
    # A pickup that skips the DLL's pickup_dialog hook keeps `nothing_missiles` in the inventory.
    entries = dict(game_files.nothing_strings({}, ("nothing", "nothing_missiles")))

    assert entries["items/nothing_missiles_name"] == entries["items/nothing_name"]
    assert entries["items/nothing_missiles_desc"] == entries["items/nothing_desc"]
    assert "items/nothing_missiles_1" not in entries


def test_nothing_strings_keeps_the_captions_on_the_base_id():
    # `nothing_missiles` is collapsed back to `nothing` before a caption is swapped in.
    entries = game_files.nothing_strings({42: "Another player's item"}, ("nothing", "nothing_missiles"))
    keys = [key for key, _ in entries]

    assert "items/nothing_42_1" in keys
    assert "items/nothing_missiles_42_1" not in keys


GAME_ANIMS = b"nim_94;talk_1,hold_convo_idle\r\nnim_95;talk_1,hold_convo_idle\0"
TUNGUSKA = "peak_filthy_flat:1030"
TUNGUSKA_PATCHED = (
    'function self.talk3() dialogBegin("tunguska") dialogLine(3) triggerOutput("talk3done")'
    ' addItem("tunguska_reward") end'
)
PATCHED_SCRIPTS = {
    TUNGUSKA: TUNGUSKA_PATCHED,
    "jens_soaring_heights:106": (
        'function self.run() i = getDrainCount() if i > 4 and isStateSet("scene_3") == 0 then'
        ' triggerOutput("scene_3") startEvent() triggerOutput("onevent_3") end end'
    ),
    "jungle_spikey_stockade:335": (
        'function self.run() i = getDrainCount() if i > 4 and isStateSet("stage 3 (flee)") == 0 then'
        ' triggerOutput("set_fx_state_2") elseif i > 0 and i < 5 and isStateSet("stage 2") == 0 then'
        ' triggerOutput("set_fx_state_1") end end'
    ),
}
"""The fake install's scripts, as the patcher serves them."""
PATCHED_SCRIPT_LINES = "".join(
    f"replace_script\t{spawn_id}\t{script}\r\n" for spawn_id, script in PATCHED_SCRIPTS.items()
).encode()


def add_beacon_sources(game_path) -> None:
    """The game file the beacon goal needs besides randomizer_scripts.csv."""
    game_path.joinpath("processed", "text", "dialog_anim.strings").write_bytes(GAME_ANIMS)


def test_one_line_script_drops_comments_and_joins_lines():
    assert game_files.one_line_script(TUNGUSKA_SCRIPT) == (
        'function self.talk3() dialogBegin("tunguska") dialogLine(3) triggerOutput("talk3done") end'
    )


def test_object_script_reads_the_level(fake_game_path):
    assert game_files.object_script(fake_game_path, TUNGUSKA) == TUNGUSKA_SCRIPT


def test_object_script_needs_the_object(fake_game_path):
    with pytest.raises(ValueError, match="has no object 1 with a script"):
        game_files.object_script(fake_game_path, "peak_filthy_flat:1")


def test_patched_object_scripts_applies_every_patch(fake_game_path):
    assert game_files.patched_object_scripts(fake_game_path) == PATCHED_SCRIPTS


def test_patched_object_scripts_applies_the_replacements_in_order(fake_game_path, mocker):
    mocker.patch.dict(game_files.SCRIPT_PATCHES, {TUNGUSKA: (("dialogLine(3)", "A"), ("A", "B"))}, clear=True)

    assert game_files.patched_object_scripts(fake_game_path) == {
        TUNGUSKA: 'function self.talk3() dialogBegin("tunguska") B triggerOutput("talk3done") end'
    }


@pytest.mark.parametrize("old", ["no such text", "dialog"])
def test_patched_object_scripts_needs_the_old_text_once(fake_game_path, mocker, old):
    mocker.patch.dict(game_files.SCRIPT_PATCHES, {TUNGUSKA: ((old, "new"),)}, clear=True)

    with pytest.raises(ValueError, match=f"does not have {old} "):
        game_files.patched_object_scripts(fake_game_path)


def test_patch_scripts_adds_a_line_per_override_before_the_end():
    patched = game_files.patch_scripts(GAME_SCRIPTS + b"\0", {TUNGUSKA: TUNGUSKA_PATCHED})

    assert patched == GAME_SCRIPTS + f"replace_script\t{TUNGUSKA}\t{TUNGUSKA_PATCHED}\r\n".encode() + b"\0"


def test_patch_scripts_ends_the_last_line_first():
    patched = game_files.patch_scripts(GAME_SCRIPTS.rstrip(), {TUNGUSKA: "script"})

    assert patched == GAME_SCRIPTS + f"replace_script\t{TUNGUSKA}\tscript\r\n".encode()


def test_patch_scripts_refuses_an_override_the_game_has():
    with pytest.raises(ValueError, match="already replaces the script of hub_festival:1"):
        game_files.patch_scripts(GAME_SCRIPTS, {"hub_festival:1": "script"})


def test_patch_scripts_without_beacons_leaves_the_ceremony():
    assert game_files.patch_scripts(GAME_SCRIPTS, {}) == GAME_SCRIPTS


def test_patch_scripts_refuses_the_ceremony_until_the_required_beacon_is_lit():
    gated = game_files.patch_scripts(GAME_SCRIPTS, {}, 5)

    assert gated == GAME_SCRIPTS.replace(
        b'dialogBegin("nim")',
        b'dialogBegin("nim") if hasCompletedQuest("@open-yoku-rando:beacon:5") == 0 then'
        b" dialogLine(900) dialogEnd() return end",
    )


@pytest.mark.parametrize("scripts", [b"no ceremony here", CEREMONY + CEREMONY])
def test_patch_scripts_with_beacons_needs_exactly_one_ceremony(scripts):
    with pytest.raises(ValueError, match="Nim's ceremony"):
        game_files.patch_scripts(scripts, {}, 1)


@pytest.mark.parametrize(("required", "expected"), [(1, "1 beacon"), (5, "5 beacons")])
def test_beacon_hint_counts_the_beacons(required, expected):
    assert f"<style1>{expected}</style>" in game_files.beacon_hint(required)


@pytest.mark.parametrize(
    ("anims", "expected"),
    [
        # The game's own file: a NUL and no final line break.
        (
            GAME_ANIMS,
            (
                b"nim_94;talk_1,hold_convo_idle\r\nnim_95;talk_1,hold_convo_idle\r\n"
                b"nim_900;talk_1,hold_convo_idle\r\n\0"
            ),
        ),
        (b"nim_94;talk_1,hold_convo_idle\r\n", b"nim_94;talk_1,hold_convo_idle\r\nnim_900;talk_1,hold_convo_idle\r\n"),
    ],
)
def test_animate_hint_adds_nims_line_before_the_end(anims, expected):
    assert game_files.animate_hint(anims) == expected


def test_install_with_beacons_serves_the_gated_ceremony(fake_game_path):
    add_beacon_sources(fake_game_path)
    before = game_files_snapshot(fake_game_path)

    install = game_files.install_files(fake_game_path, default_models(), {}, all_categories(), required_beacons=3)

    assert {game_files.SCRIPTS_PATH, game_files.ANIMS_PATH} <= set(install.written)
    scripts = fake_game_path.joinpath(game_files.SCRIPTS_PATH).read_bytes()
    assert b'hasCompletedQuest("@open-yoku-rando:beacon:3") == 0' in scripts
    anims = fake_game_path.joinpath(game_files.ANIMS_PATH).read_bytes()
    assert b"nim_900;talk_1,hold_convo_idle\r\n" in anims
    texts = fake_game_path.joinpath(TEXTS).read_bytes().decode("utf-8").split("\r\n")
    assert f"nim_900;{game_files.beacon_hint(3)}" in texts
    assert game_files_snapshot(fake_game_path) == before


def test_install_without_beacons_removes_the_beacon_files(fake_game_path):
    add_beacon_sources(fake_game_path)
    game_files.install_files(fake_game_path, default_models(), {}, all_categories(), required_beacons=3)

    install = game_files.install_files(fake_game_path, default_models(), {}, all_categories())

    assert install.removed == (game_files.ANIMS_PATH,)
    assert b"hasCompletedQuest" not in fake_game_path.joinpath(game_files.SCRIPTS_PATH).read_bytes()
    assert not fake_game_path.joinpath(game_files.ANIMS_PATH).exists()
    assert "nim_900" not in fake_game_path.joinpath(TEXTS).read_text(encoding="utf-8")
