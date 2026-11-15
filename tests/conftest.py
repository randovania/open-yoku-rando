import json
import os
from pathlib import Path

import pytest

from open_yoku_rando import running_game
from open_yoku_rando.game_install import level_json_path, randomizer_csv_path, scripts_csv_path
from open_yoku_rando.locations import load_locations

_FAIL_INSTEAD_OF_SKIP = False

CEREMONY = b'function self.instruments_2() triggerOutput("visit_nim") dialogBegin("nim") dialogLine(94) dialogEnd() end'
GAME_SCRIPTS = b"replace_script\thub_festival:1\t" + CEREMONY + b"\r\n"
"""A randomizer_scripts.csv with Nim's ceremony, as the game's own has it."""

TUNGUSKA_SCRIPT = (
    "function self.talk3()\n"
    '    dialogBegin("tunguska") -- the reward talk\n'
    "    dialogLine(3)\n"
    '    triggerOutput("talk3done")\n'
    "end\n"
)
TUNGUSKA_OBJECT = {"id": 1030, "script": TUNGUSKA_SCRIPT}


def get_env_or_skip(env_name: str) -> str:
    if env_name not in os.environ:
        if _FAIL_INSTEAD_OF_SKIP:
            pytest.fail(f"Missing environment variable {env_name}")
        else:
            pytest.skip(f"Skipped due to missing environment variable {env_name}")
    return os.environ[env_name]


class TestFilesDir:
    def __init__(self, root: Path):
        self.root = root

    def joinpath(self, *paths) -> Path:
        return self.root.joinpath(*paths)

    def read_json(self, *paths) -> dict:
        with self.joinpath(*paths).open(encoding="utf-8") as f:
            return json.load(f)


@pytest.fixture(scope="session")
def yoku_game_path() -> Path:
    return Path(get_env_or_skip("YOKU_GAME_PATH"))


@pytest.fixture(scope="session")
def test_files_dir() -> TestFilesDir:
    return TestFilesDir(Path(__file__).parent.joinpath("test_files"))


@pytest.fixture
def shuffled_configuration(test_files_dir) -> dict:
    return test_files_dir.read_json("patcher_files", "shuffled.json")


@pytest.fixture
def vanilla_configuration(test_files_dir) -> dict:
    return test_files_dir.read_json("patcher_files", "vanilla.json")


@pytest.fixture
def fake_game_path(tmp_path) -> Path:
    """A minimal install: an empty Yoku.exe, the randomizer CSV matching the bundled locations and one text file."""
    game_path = tmp_path.joinpath("game")
    game_path.mkdir()
    game_path.joinpath("Yoku.exe").write_bytes(b"")
    lines = ["Name\tItem\tSpawn ID\tTracker\tSpawn"]
    lines.extend(
        "\t".join([
            location.name,
            location.vanilla_item,
            location.spawn_id,
            location.tracker or "",
            "1" if location.spawn else "",
        ])
        for location in load_locations()
        if not location.added
    )
    csv_path = randomizer_csv_path(game_path)
    csv_path.parent.mkdir(parents=True)
    csv_path.write_bytes("\r\n".join(lines).encode("utf-8"))
    scripts_csv_path(game_path).write_bytes(GAME_SCRIPTS)
    level_path = level_json_path(game_path, "peak_filthy_flat")
    level_path.parent.mkdir(parents=True)
    level_path.write_bytes(json.dumps({"objects": [TUNGUSKA_OBJECT]}).encode("utf-8") + b"\0")
    # A game file the patcher must leave alone.
    strings_path = game_path.joinpath("processed", "text", "dialog_en.strings")
    strings_path.parent.mkdir(parents=True)
    strings_path.write_bytes("﻿items/wallet_name;Wallet\r\nitems/wallet_desc;Holds fruit.\r\n".encode())
    return game_path


@pytest.fixture(autouse=True)
def game_not_running(mocker):
    """Makes every test see a closed game, so the suite does not depend on the developer's own game."""
    return mocker.patch(
        "open_yoku_rando.running_game.game_process_state",
        return_value=running_game.GameProcessState.NOT_RUNNING,
    )
