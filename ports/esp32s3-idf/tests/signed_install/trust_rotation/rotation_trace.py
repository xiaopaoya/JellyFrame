"""Run the same simulated NOR bytes through separately configured store binaries."""

from pathlib import Path
from shutil import copyfile
import subprocess
import sys
from tempfile import TemporaryDirectory


def main():
    fixtures = Path(sys.argv[1])
    old, overlap, new, empty = sys.argv[2:]
    old_v1 = str(fixtures / "trusted-v1.jfsg")
    old_v2 = str(fixtures / "rotation-old-v2.jfsg")
    new_v2 = str(fixtures / "rotation-new-v2.jfsg")
    new_v3 = str(fixtures / "rotation-new-v3.jfsg")

    def readback(label, firmware, nor, version, generation, invalid=False, *,
                 package="-", install=None, rollback=False):
        action = "install" if install else "rollback-rejected" if rollback else "readback"
        command = [firmware, action, str(nor), str(version), package]
        if install:
            command.append(install)
        result = subprocess.check_output(command, text=True).strip()
        expected = f"{generation} {int(invalid)}"
        if result != expected:
            raise AssertionError(f"{label}: expected generation and recovery {expected}, got {result}")
        print(f"{label}: version={version}, generation={generation}, registry_invalid={invalid}")

    with TemporaryDirectory(dir=fixtures) as scratch:
        root = Path(scratch)
        primary = root / "primary.bin"
        readback("Old-only initializes blank NOR", old, primary, 0, 0, invalid=True)
        readback("Old-only installs v1", old, primary, 1, 1, package=old_v1, install=old_v1)
        before = primary.read_bytes()
        readback("Overlap reads v1", overlap, primary, 1, 1, package=old_v1)
        readback("New-only reads the older empty registry", new, primary, 0, 0)
        readback("Empty trust reads the older empty registry", empty, primary, 0, 0)
        if primary.read_bytes() != before:
            raise AssertionError("readback changed NOR despite a valid empty registry")
        readback("Restoring old trust re-exposes v1", old, primary, 1, 1, package=old_v1)

        all_old = root / "all-old.bin"
        copyfile(primary, all_old)
        readback("Old-only installs v2", old, all_old, 2, 2, package=old_v2, install=old_v2)
        readback("New-only publishes an empty registry when neither copy validates",
                 new, all_old, 0, 0, invalid=True)
        readback("New-only reopens the recovered empty registry", new, all_old, 0, 0)
        readback("Restoring old trust recovers only the surviving v1 registry",
                 old, all_old, 1, 1, package=old_v1)

        readback("Overlap installs new-signed v2", overlap, primary, 2, 2, package=new_v2, install=new_v2)
        before = primary.read_bytes()
        readback("New-only reads v2 and rejects rollback to v1", new, primary, 2, 2,
                 invalid=True, package=new_v2, rollback=True)
        readback("Old-only falls back to the older v1 registry", old, primary, 1, 1, package=old_v1)
        if primary.read_bytes() != before:
            raise AssertionError("trusted fallback or refused rollback changed NOR")
        readback("New-only reads v2 again", new, primary, 2, 2, package=new_v2)
        seeded = root / "both-copies-seeded.bin"
        copyfile(primary, seeded)
        readback("Overlap installs a second new-signed update as v3",
                 overlap, seeded, 3, 3, package=new_v3, install=new_v3)
        seeded_bytes = seeded.read_bytes()
        readback("New-only reads v3 after both copies were seeded", new, seeded, 3, 3, package=new_v3)
        if seeded.read_bytes() != seeded_bytes:
            raise AssertionError("new-only reconstruction changed the seeded NOR")

        # Model one unavailable registry sector at a time in independent NOR
        # copies. All bundle slots and the surviving registry sector are intact.
        registry_sector_bytes = 4096
        for generation, package in ((2, new_v2), (3, new_v3)):
            other_sector_offset = (1 - generation % 2) * registry_sector_bytes
            one_survivor = root / f"only-generation-{generation}.bin"
            one_survivor.write_bytes(seeded_bytes[:other_sector_offset]
                                     + b"\xff" * registry_sector_bytes
                                     + seeded_bytes[other_sector_offset + registry_sector_bytes:])
            isolated_bytes = one_survivor.read_bytes()
            readback(f"New-only reads independently from registry generation {generation}",
                     new, one_survivor, generation, generation, package=package)
            if one_survivor.read_bytes() != isolated_bytes:
                raise AssertionError("independent registry reconstruction changed NOR")

        readback("Empty trust publishes an empty registry when neither copy validates",
                 empty, primary, 0, 0, invalid=True)
        readback("Restoring overlap cannot recover overwritten v2",
                 overlap, primary, 1, 1, package=old_v1)


if __name__ == "__main__":
    main()
