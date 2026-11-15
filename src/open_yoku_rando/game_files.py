"""
Installs the mod into the game folder without changing a file of the game.

Everything goes into MOD_FOLDER, except the xinput9_1_0.dll proxy, which Windows only finds next to Yoku.exe:
- `open_yoku_rando.dll`, which the proxy starts;
- `texts.strings`, the Nothing texts, each location's line (`items/nothing_<location id>_1`) and the TEXT_FIXES,
  which the DLL sets in the dialog table;
- `items/<id>_x102.sim`, models the DLL serves in place of `processed/items/<name>`;
- `randomizer_scripts.csv`, the game's own with Nim's ceremony waiting for the required beacons, which the DLL serves
  in place of `data/text/randomizer_scripts.csv`;
- `dialog_anim.strings`, the game's own with an animation for Nim's beacon hint, which the DLL serves in place of
  `processed/text/dialog_anim.strings`;
- `seed.txt`, written by seed_file.
"""

import dataclasses
from pathlib import Path

from open_yoku_rando import sim_texture
from open_yoku_rando.file_util import atomic_write_bytes
from open_yoku_rando.files import files_path
from open_yoku_rando.game_install import dialog_anims_path, scripts_csv_path
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


def gate_ceremony(scripts: bytes, required: int) -> bytes:
    """The game's randomizer_scripts.csv with Nim's ceremony refused until `required` beacons are lit."""
    if scripts.count(_CEREMONY_START) != 1:
        raise ValueError("randomizer_scripts.csv does not start Nim's ceremony the way the patcher expects")
    gate = (f' if hasCompletedQuest("{BEACON_MARKER}{required}") == 0 then'
            f" dialogLine({BEACON_HINT_LINE}) dialogEnd() return end")
    return scripts.replace(_CEREMONY_START, _CEREMONY_START + gate.encode("utf-8"))


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
}
"""Dialog lines that are wrong in a randomizer, by key; English only, the DLL keeps the game in English."""


@dataclasses.dataclass(frozen=True)
class Install:
    written: tuple[str, ...]
    """Game-relative paths that were written."""
    removed: tuple[str, ...]
    """Files a previous install left that this one does not use: models, script overrides and dialog animations."""


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
    required_beacons: int = 0,
) -> Install:
    """
    Installs the DLLs, one `.sim` per entry in `models` and the texts, replacing a previous install.

    `models` maps the game item id to the `.sim` its bubbles show (see model_data); `captions` maps a location id
    to that location's dialog line. With `required_beacons`, Nim starts the ceremony only once that many beacons are
    lit.
    """
    # Read every source first, so a missing package file fails before the install is touched.
    contents = {target: installed_file_source(source).read_bytes() for target, source in NATIVE_FILES.items()}
    for item_id, data in models.items():
        contents[f"{ITEMS_FOLDER}/{item_id}{MODEL_SUFFIX}"] = data
    texts = nothing_strings(captions, tuple(models))
    if required_beacons > 0:
        contents[SCRIPTS_PATH] = gate_ceremony(scripts_csv_path(game_path).read_bytes(), required_beacons)
        contents[ANIMS_PATH] = animate_hint(dialog_anims_path(game_path).read_bytes())
        texts.append((f"nim_{BEACON_HINT_LINE}", beacon_hint(required_beacons)))
    texts.extend(TEXT_FIXES.items())
    contents[TEXTS_PATH] = strings_file(texts)

    removed = []
    for override in (SCRIPTS_PATH, ANIMS_PATH):
        stale = game_path.joinpath(override)
        if override not in contents and stale.is_file():
            stale.unlink()
            removed.append(override)
            LOG.debug("Removed %s", override)
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
