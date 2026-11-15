import dataclasses
import functools
import json

from open_yoku_rando.files import files_path


def make_id(level_number: int, object_id: int) -> int:
    """The id the save's randomizer list uses."""
    return level_number << 16 | object_id


@dataclasses.dataclass(frozen=True)
class Location:
    """One randomizer location; `id` and the order are what the save needs."""

    spawn_id: str
    """`level:object`, as in the CSV's Spawn ID column."""
    name: str
    level: str
    level_number: int
    object_id: int
    id: int
    """`level_number << 16 | object_id`, the id the game uses in the save's randomizer list."""
    spawn: bool
    """A new game starts with these locations revealed."""
    vanilla_item: str
    tracker: str | None
    """The map marker as `x,y`, or None where the CSV cell is empty."""
    added: bool = False
    """Not in the game's CSV; the DLL keeps its entry (native/dll/added_locations.hpp)."""


@functools.cache
def load_locations() -> tuple[Location, ...]:
    """The locations in the order the save's randomizer list must follow."""
    with files_path().joinpath("locations.json").open(encoding="utf-8") as f:
        return tuple(Location(**entry) for entry in json.load(f))
