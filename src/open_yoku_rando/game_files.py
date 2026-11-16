"""
Installs the mod into the game folder without changing a file of the game.

Everything goes into MOD_FOLDER, except the xinput9_1_0.dll proxy, which Windows only finds next to Yoku.exe:
- `open_yoku_rando.dll`, which the proxy starts;
- `texts.strings`, the Nothing texts, each location's line (`items/nothing_<location id>_1`), the TEXT_FIXES and
  the NPC lines naming their reward's category (category_lines.json), which the DLL sets in the dialog table;
- `items/<id>_x102.sim`, models the DLL serves in place of `processed/items/<name>`;
- `randomizer_scripts.csv`, the game's own with the mod's script changes, which the DLL serves in place of
  `data/text/randomizer_scripts.csv`;
- `dialog_anim.strings`, the game's own with an animation for Nim's beacon hint, which the DLL serves in place of
  `processed/text/dialog_anim.strings`;
- `seed.txt`, written by seed_file.
"""

import dataclasses
import functools
import json
from pathlib import Path

from open_yoku_rando import sim_texture
from open_yoku_rando.file_util import atomic_write_bytes
from open_yoku_rando.files import files_path
from open_yoku_rando.game_install import dialog_anims_path, level_json_path, scripts_csv_path
from open_yoku_rando.items import DEFAULT_MODEL, MODEL_SUFFIX, NOTHING_ITEM_ID
from open_yoku_rando.logger import LOG

__all__ = ["DEFAULT_MODEL", "NOTHING_ITEM_ID"]

MOD_FOLDER = "open-yoku-rando"
TEXTS_PATH = f"{MOD_FOLDER}/texts.strings"
ITEMS_FOLDER = f"{MOD_FOLDER}/items"
SCRIPTS_PATH = f"{MOD_FOLDER}/randomizer_scripts.csv"
ANIMS_PATH = f"{MOD_FOLDER}/dialog_anim.strings"
NOTHING_KEY_PREFIX = f"items/{NOTHING_ITEM_ID}"
NOTHING_NAME = "Nothing"
NOTHING_DESC = "An empty spot, or another player's item."
NOTHING_DEFAULT_TEXT = "You found Nothing!"
"""Shown for a Nothing without a caption."""

NATIVE_FILES = {
    f"{MOD_FOLDER}/open_yoku_rando.dll": "native/open_yoku_rando.dll",
    "xinput9_1_0.dll": "native/xinput9_1_0.dll",
}
"""The DLL and the proxy that starts it, built by native/build.cmd into files/native/."""


MODEL_EXCEPTIONS = {
    "wickerling": "processed/items/collectible",
    "reward_fruit_big": "processed/pickups/fruit_large",
    "reward_fruit_medium": "processed/pickups/fruit_medium",
}
"""Items whose look the game does not take from `processed/items/<item>`, game-relative without the suffix."""


def model_source(game_path: Path, model: str) -> Path:
    if model == DEFAULT_MODEL:
        return installed_file_source(f"items/{DEFAULT_MODEL}{MODEL_SUFFIX}")
    relative = MODEL_EXCEPTIONS.get(model, f"processed/items/{model}")
    source = game_path.joinpath(f"{relative}{MODEL_SUFFIX}")
    if not source.is_file():
        raise ValueError(f"the game install has no model for {model!r}: {source} is missing")
    return source


WIDENED_MODELS = frozenset({"wickerling"})
"""
Models the bubble would show far too large: it scales a texture to a fixed width (`60 / width`), and
`items/collectible` is narrow and tall, since the game never puts it into an item bubble.
"""


def model_data(game_path: Path, model: str) -> bytes:
    """The `.sim` a bubble showing the item `model` gets, see model_source."""
    data = model_source(game_path, model).read_bytes()
    return sim_texture.widen_to_square(data) if model in WIDENED_MODELS else data


def nothing_strings(captions: dict[int, str], game_item_ids: tuple[str, ...] = ()) -> list[tuple[str, str]]:
    entries = []
    for game_item_id in sorted({NOTHING_ITEM_ID, *game_item_ids}):
        entries.append((f"items/{game_item_id}_name", NOTHING_NAME))
        entries.append((f"items/{game_item_id}_desc", NOTHING_DESC))
    # Only the base id gets a dialog line: the DLL collapses a model id to it before the dialog is read.
    entries.append((f"{NOTHING_KEY_PREFIX}_1", NOTHING_DEFAULT_TEXT))
    for location_id, caption in sorted(captions.items()):
        entries.append((f"{NOTHING_KEY_PREFIX}_{location_id}_1", caption))
    return entries


def _strings_value(text: str) -> str:
    # A value cannot hold a semicolon or a real line break;
    return text.replace("\r\n", "\n").replace("\n", "\\n").replace(";", ",")


def strings_file(entries: list[tuple[str, str]]) -> bytes:
    """`key;value` lines, CRLF, the way the game's own `.strings` files are written."""
    return "".join(f"{key};{_strings_value(value)}\r\n" for key, value in entries).encode("utf-8")


BEACON_HINT_LINE = 900
"""Nim's dialog line for a ceremony started too early; the game ships no `nim_900`."""
BEACON_MARKER = "@open-yoku-rando:beacon:"
"""The DLL adds `<BEACON_MARKER><n>` to the save's quests_completed when the n-th beacon is lit."""
_CEREMONY_START = b'function self.instruments_2() triggerOutput("visit_nim") dialogBegin("nim")'


def beacon_hint(required: int) -> str:
    beacons = "1 beacon" if required == 1 else f"{required} beacons"
    return f"The ritual needs the light of <style1>{beacons}</style>. Come back once they burn!"


SCRIPT_PATCHES = {
    # Tunguska's reward talk, after the sixth clean Sootling
    "peak_filthy_flat:1030": (
        ('triggerOutput("talk3done")', 'triggerOutput("talk3done") addItem("tunguska_reward")'),
    ),
    # The drain cutscenes
    "jens_soaring_heights:106": (
        (
            (
                'if i == 10 then startEvent() triggerOutput("onevent_1") fadeOutOverlay(1000) wait(5000) stopEvent()'
                ' elseif i == 30 then startEvent() triggerOutput("onevent_2") fadeOutOverlay(1000) wait(8000)'
                ' stopEvent() elseif i > 49 and isStateSet("scene_3") == 0 and i < 61 then'
            ),
            'if i > 4 and isStateSet("scene_3") == 0 then',
        ),
    ),
    # Spikey Stockade's cage glow on load, scaled to the 5 drains
    "jungle_spikey_stockade:335": (
        ("i > 30 and", "i > 4 and"),
        ("i > 10 and i < 31 and", "i > 0 and i < 5 and"),
    ),
}
"""Object scripts of the game's levels, by spawn id: (old, new) pairs, each old text found exactly once."""


def one_line_script(script: str) -> str:
    """A level's Lua script as one line, as a `replace_script` cell needs it: comments dropped, lines joined."""
    lines = (line.split("--", 1)[0].strip() for line in script.splitlines())
    return " ".join(line for line in lines if line)


def object_script(game_path: Path, spawn_id: str) -> str:
    """The Lua script of the object `level:id` in that level's level.json."""
    level, object_id = spawn_id.split(":")
    # The file ends with a NUL after the JSON.
    text = level_json_path(game_path, level).read_bytes().decode("utf-8").rstrip("\0")
    for game_object in json.loads(text)["objects"]:
        if game_object.get("id") == int(object_id) and "script" in game_object:
            return game_object["script"]
    raise ValueError(f"{level} has no object {object_id} with a script")


def patched_object_scripts(game_path: Path) -> dict[str, str]:
    """Each of SCRIPT_PATCHES applied to the game's script, on one line, by spawn id."""
    patched = {}
    for spawn_id, replacements in SCRIPT_PATCHES.items():
        script = one_line_script(object_script(game_path, spawn_id))
        for old, new in replacements:
            if script.count(old) != 1:
                raise ValueError(f"the script of {spawn_id} does not have {old} the way the patcher expects")
            script = script.replace(old, new)
        patched[spawn_id] = script
    return patched


def patch_scripts(scripts: bytes, overrides: dict[str, str], required_beacons: int = 0) -> bytes:
    """
    The game's randomizer_scripts.csv with a `replace_script` line for each of `overrides` (spawn id -> script) and,
    with `required_beacons`, Nim's ceremony (the game's own override) refused until that many beacons are lit.
    """
    body = scripts.rstrip(b"\0")
    end = scripts[len(body):]
    if required_beacons > 0:
        if body.count(_CEREMONY_START) != 1:
            raise ValueError("randomizer_scripts.csv does not start Nim's ceremony the way the patcher expects")
        gate = (f' if hasCompletedQuest("{BEACON_MARKER}{required_beacons}") == 0 then'
                f" dialogLine({BEACON_HINT_LINE}) dialogEnd() return end")
        body = body.replace(_CEREMONY_START, _CEREMONY_START + gate.encode("utf-8"))
    if not body.endswith(b"\n"):
        body += b"\r\n"
    for spawn_id, script in overrides.items():
        if f"replace_script\t{spawn_id}\t".encode() in body:
            raise ValueError(f"randomizer_scripts.csv already replaces the script of {spawn_id}")
        body += f"replace_script\t{spawn_id}\t{script}\r\n".encode()
    return body + end


_HINT_ANIMATION = "talk_1,hold_convo_idle"
"""As Nim's other lines at the ceremony; a line without an entry is spoken hanging from the root of the intro."""


def animate_hint(anims: bytes) -> bytes:
    """The game's dialog_anim.strings with an animation for Nim's beacon hint."""
    # The game's file ends with a NUL and no line break; the NUL stays at the end.
    body = anims.rstrip(b"\0")
    end = anims[len(body):]
    if not body.endswith(b"\n"):
        body += b"\r\n"
    return body + f"nim_{BEACON_HINT_LINE};{_HINT_ANIMATION}\r\n".encode() + end


TEXT_FIXES = {
    # Fosfor sends a player without the Old Postal Badge to Posterodactyl, who only has it in vanilla
    "fosfor_11": "As much as I want those <style1>Cove Mushrooms</style1>. "
                 "First you need the <style3>Old Postal Badge</style>.",
    "willo_peak_1b": "I've located the next clue, it's somewhere around here!",
    "willo_peak_5": "What are you waiting for? Let's break that icicle and crack the ice!",
    "willo_peak_6": "...That didn't go as planned. I'm afraid I don't do tricks involving water, "
                    "so you're gonna have to dive in yourself!",
}
"""Dialog lines that are wrong in a randomizer, by key; English only, the DLL keeps the game in English."""


@dataclasses.dataclass(frozen=True)
class CategoryLine:
    """An NPC line that names the category of the item the NPC hands out, see files/category_lines.json."""

    key: str
    location: str
    """The spawn id of the location whose pickup the line is about."""
    template: str
    """
    The line. `{a_cat}` stands for the category's determiner plus its noun, `{the_cat}` for "the " plus the noun.
    The filled-in text is highlighted in purple (`style4`).
    Where the game has alternative lines under the key, the DLL sets every one of them to it.
    """

    def fill(self, category: dict[str, str]) -> str:
        noun = category["noun"]
        a_cat = f"<style4>{category['determiner']}{noun}</style>"
        the_cat = f"<style4>the {noun}</style>"
        return self.template.replace("{a_cat}", a_cat).replace("{the_cat}", the_cat)


@functools.cache
def load_category_lines() -> tuple[CategoryLine, ...]:
    with files_path().joinpath("category_lines.json").open(encoding="utf-8") as f:
        return tuple(CategoryLine(**entry) for entry in json.load(f))


def category_strings(categories: dict[str, dict[str, str]]) -> list[tuple[str, str]]:
    """The category lines filled from `categories` (spawn id -> category)."""
    return [(line.key, line.fill(categories[line.location])) for line in load_category_lines()]


@dataclasses.dataclass(frozen=True)
class Install:
    written: tuple[str, ...]
    """Game-relative paths that were written."""
    removed: tuple[str, ...]
    """Files a previous install left that this one does not use."""


def installed_file_source(package_relative: str) -> Path:
    source = files_path().joinpath(package_relative)
    if not source.is_file():
        hint = " (build it with native/build.cmd)" if package_relative in NATIVE_FILES.values() else ""
        raise ValueError(f"{source} is missing from the package{hint}")
    return source


def install_files(
    game_path: Path,
    models: dict[str, bytes],
    captions: dict[int, str],
    categories: dict[str, dict[str, str]],
    required_beacons: int = 0,
) -> Install:
    """
    Installs the DLLs, one `.sim` per entry in `models` and the texts, replacing a previous install.

    `models` maps the game item id to the `.sim` its bubbles show (see model_data); `captions` maps a location id
    to that location's dialog line; `categories` maps a spawn id to the category of its pickup, which the NPC handing
    it out names. With `required_beacons`, Nim starts the ceremony only once that many beacons are lit.
    """
    # Read every source first, so a missing package file fails before the install is touched.
    contents = {target: installed_file_source(source).read_bytes() for target, source in NATIVE_FILES.items()}
    for item_id, data in models.items():
        contents[f"{ITEMS_FOLDER}/{item_id}{MODEL_SUFFIX}"] = data
    contents[SCRIPTS_PATH] = patch_scripts(
        scripts_csv_path(game_path).read_bytes(), patched_object_scripts(game_path), required_beacons
    )
    texts = nothing_strings(captions, tuple(models))
    if required_beacons > 0:
        contents[ANIMS_PATH] = animate_hint(dialog_anims_path(game_path).read_bytes())
        texts.append((f"nim_{BEACON_HINT_LINE}", beacon_hint(required_beacons)))
    texts.extend(TEXT_FIXES.items())
    texts.extend(category_strings(categories))
    contents[TEXTS_PATH] = strings_file(texts)

    removed = []
    stale_anims = game_path.joinpath(ANIMS_PATH)
    if ANIMS_PATH not in contents and stale_anims.is_file():
        stale_anims.unlink()
        removed.append(ANIMS_PATH)
        LOG.debug("Removed %s", ANIMS_PATH)
    items_folder = game_path.joinpath(ITEMS_FOLDER)
    if items_folder.is_dir():
        for stale in sorted(items_folder.glob(f"*{MODEL_SUFFIX}")):
            relative = f"{ITEMS_FOLDER}/{stale.name}"
            if relative not in contents:
                stale.unlink()
                removed.append(relative)
                LOG.debug("Removed %s", relative)

    written = []
    for target_relative, data in contents.items():
        target = game_path.joinpath(target_relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        atomic_write_bytes(target, data)
        written.append(target_relative)
        LOG.debug("Installed %s", target_relative)

    return Install(written=tuple(written), removed=tuple(removed))
