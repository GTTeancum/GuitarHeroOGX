"""Read authored music_start MIDI events directly from a PS2 ARK-v3 ISO."""
import argparse
import hashlib
import io
import json
from pathlib import Path

import mido
import pycdlib
from audit_gh1_camera_source import entry_index, read_entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    disc = pycdlib.PyCdlib()
    disc.open(str(args.iso))
    results = []
    try:
        index = entry_index(disc)
        for path, _, _ in index[1]:
            if not path.startswith("songs/") or not path.endswith(".mid"):
                continue
            data = read_entry(disc, path, index)
            midi = mido.MidiFile(file=io.BytesIO(data))
            events = []
            ticks = 0
            seconds = 0.0
            tempo = 500000
            for event in mido.merge_tracks(midi.tracks):
                ticks += event.time
                seconds += mido.tick2second(event.time, midi.ticks_per_beat, tempo)
                if event.type == "set_tempo":
                    tempo = event.tempo
                text = getattr(event, "text", "")
                if "music_start" in text:
                    events.append(dict(tick=ticks, seconds=seconds, text=text))
            results.append(dict(path=path, sha256=hashlib.sha256(data).hexdigest(),
                                music_start=events))
    finally:
        disc.close()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(dict(
        scope="Authored chart events; not proof of executable event dispatch",
        charts=results), indent=2), encoding="utf-8")
    print(f"Checked {len(results)} charts; "
          f"{sum(bool(row['music_start']) for row in results)} contain music_start")


if __name__ == "__main__":
    main()
