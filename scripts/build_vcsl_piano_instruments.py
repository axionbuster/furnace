#!/usr/bin/env -S uv run

"""Build 31-EDO Furnace instruments from LatticeKeys' CC0 piano bank."""

from __future__ import annotations

import argparse
import array
import hashlib
import math
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

VERSION = 251
NOTE_COUNT = 465
MIDDLE_C = 279
A4 = 302
QSOUND_TYPE = 40
ES5506_TYPE = 27
QSOUND_RATE = 13_132
ES5506_RATE = 26_250
QSOUND_MAX_SECONDS = 4.9
QSOUND_FADE_SECONDS = 0.2
ES5506_FADE_SECONDS = 0.1
QSOUND_MEMORY = 16_777_216
ES5506_MEMORY = 16_777_216

SCRIPT_DIR = Path(__file__).resolve().parent
REPOSITORY_DIR = SCRIPT_DIR.parent
DEFAULT_SOURCE_DIR = (
    REPOSITORY_DIR.parent / "latticekeys" / "App" / "Audio" / "PianoSamples"
)
DEFAULT_OUTPUT_DIR = REPOSITORY_DIR / "instruments" / "Piano"


@dataclass(frozen=True)
class Anchor:
    resource: str
    midi_note: int

    @property
    def source_name(self) -> str:
        return f"{self.resource}.m4a"

    @property
    def label(self) -> str:
        return self.resource.removeprefix("piano_")

    @property
    def position(self) -> float:
        """Return the anchor's fractional position in the 31-EDO note domain."""
        return MIDDLE_C + 31.0 * (self.midi_note - 60) / 12.0

    @property
    def calibration_slot(self) -> int:
        """Return a nearby integer slot used to calibrate sample center rate."""
        return math.floor(self.position + 0.5)


ANCHORS = (
    Anchor("piano_As0", 22),
    Anchor("piano_C1", 24),
    Anchor("piano_D1", 26),
    Anchor("piano_E1", 28),
    Anchor("piano_Fs1", 30),
    Anchor("piano_Gs1", 32),
    Anchor("piano_As1", 34),
    Anchor("piano_C2", 36),
    Anchor("piano_D2", 38),
    Anchor("piano_E2", 40),
    Anchor("piano_Fs2", 42),
    Anchor("piano_Gs2", 44),
    Anchor("piano_As2", 46),
    Anchor("piano_C3", 48),
    Anchor("piano_D3", 50),
    Anchor("piano_E3", 52),
    Anchor("piano_Fs3", 54),
    Anchor("piano_Gs3", 56),
    Anchor("piano_As3", 58),
    Anchor("piano_C4", 60),
    Anchor("piano_D4", 62),
    Anchor("piano_E4", 64),
    Anchor("piano_Fs4", 66),
    Anchor("piano_Gs4", 68),
    Anchor("piano_As4", 70),
    Anchor("piano_C5", 72),
    Anchor("piano_D5", 74),
    Anchor("piano_E5", 76),
    Anchor("piano_Fs5", 78),
    Anchor("piano_Gs5", 80),
    Anchor("piano_As5", 82),
    Anchor("piano_C6", 84),
    Anchor("piano_D6", 86),
    Anchor("piano_E6", 88),
    Anchor("piano_Fs6", 90),
    Anchor("piano_Gs6", 92),
    Anchor("piano_As6", 94),
    Anchor("piano_C7", 96),
    Anchor("piano_D7", 98),
    Anchor("piano_E7", 100),
    Anchor("piano_Fs7", 102),
    Anchor("piano_Gs7", 104),
)


@dataclass(frozen=True)
class InstrumentProfile:
    filename: str
    name: str
    instrument_type: int
    sample_rate: int
    depth: int
    volume_top: int


QSOUND = InstrumentProfile(
    "LatticeKeys Grand Piano (QSound).fui",
    "LatticeKeys Grand Piano (QSound)",
    QSOUND_TYPE,
    QSOUND_RATE,
    8,
    16_383,
)
ES5506 = InstrumentProfile(
    "LatticeKeys Grand Piano (ES5506).fui",
    "LatticeKeys Grand Piano (ES5506)",
    ES5506_TYPE,
    ES5506_RATE,
    16,
    4_095,
)
PROFILES = (QSOUND, ES5506)


def decode_pcm16(source: Path, sample_rate: int) -> array.array[int]:
    result = subprocess.run(
        (
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(source),
            "-ac",
            "1",
            "-ar",
            str(sample_rate),
            "-f",
            "s16le",
            "-acodec",
            "pcm_s16le",
            "-",
        ),
        check=True,
        stdout=subprocess.PIPE,
    )
    pcm = array.array("h")
    pcm.frombytes(result.stdout)
    if sys.byteorder != "little":
        pcm.byteswap()
    if not pcm:
        raise ValueError(f"decoded sample is empty: {source}")
    return pcm


def fade_tail(pcm: array.array[int], sample_rate: int, seconds: float) -> None:
    fade_length = min(len(pcm), max(2, round(seconds * sample_rate)))
    fade_start = len(pcm) - fade_length
    denominator = fade_length - 1
    for index in range(fade_length):
        gain_numerator = denominator - index
        source = pcm[fade_start + index]
        pcm[fade_start + index] = round(source * gain_numerator / denominator)


def render_sample(source: Path, profile: InstrumentProfile) -> tuple[bytes, int]:
    pcm16 = decode_pcm16(source, profile.sample_rate)
    if profile is QSOUND:
        del pcm16[round(QSOUND_MAX_SECONDS * profile.sample_rate) :]
        fade_tail(pcm16, profile.sample_rate, QSOUND_FADE_SECONDS)
        pcm8 = array.array("b", (sample >> 8 for sample in pcm16))
        return pcm8.tobytes(), len(pcm8)

    fade_tail(pcm16, profile.sample_rate, ES5506_FADE_SECONDS)
    if sys.byteorder != "little":
        pcm16.byteswap()
    return pcm16.tobytes(), len(pcm16)


def center_rate(anchor: Anchor, sample_rate: int) -> int:
    source_octaves = (anchor.midi_note - 69) / 12.0
    target_octaves = (anchor.calibration_slot - A4) / 31.0
    return round(sample_rate * math.exp2(target_octaves - source_octaves))


def feature(code: bytes, data: bytes) -> bytes:
    if len(code) != 2 or len(data) > 0xFFFF:
        raise ValueError("invalid instrument feature")
    return code + struct.pack("<H", len(data)) + data


def volume_adsr_data(profile: InstrumentProfile) -> bytes:
    accumulator_top = (profile.volume_top << 8) | 0xFF
    release_rate = math.ceil(accumulator_top / 12)
    values = (
        0,
        profile.volume_top,
        accumulator_top,
        0,
        0,
        profile.volume_top,
        0,
        0,
        release_rate,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    )
    macro_header = struct.pack(
        "<8B",
        0,
        16,
        255,
        255,
        0,
        0xC3,
        0,
        1,
    )
    return struct.pack("<H", 8) + macro_header + struct.pack("<16i", *values) + b"\xff"


def note_map_data() -> bytes:
    note_map = bytearray()
    for note in range(NOTE_COUNT):
        sample_index = min(
            range(len(ANCHORS)),
            key=lambda index: (abs(note - ANCHORS[index].position), index),
        )
        root = ANCHORS[sample_index].calibration_slot
        playback_note = MIDDLE_C + note - root
        if not 0 <= playback_note < NOTE_COUNT:
            raise ValueError(f"mapped playback note {playback_note} is out of range")
        note_map.extend(struct.pack("<hh", playback_note, sample_index))
    return struct.pack("<hBB", 0, 1, 31) + note_map


def es5506_data() -> bytes:
    return struct.pack(
        "<BHHHbbbbBB",
        3,
        0xFFFF,
        0xFFFF,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    )


def sample_block(
    name: str,
    raw: bytes,
    frames: int,
    profile: InstrumentProfile,
    calibrated_center_rate: int,
) -> bytes:
    expected_bytes = frames * (profile.depth // 8)
    if len(raw) != expected_bytes:
        raise ValueError("PCM byte count does not match sample metadata")
    metadata = b"".join(
        (
            name.encode("utf-8") + b"\0",
            struct.pack(
                "<iii",
                frames,
                profile.sample_rate,
                calibrated_center_rate,
            ),
            struct.pack("<BBBB", profile.depth, 0, 0, 0),
            struct.pack("<ii", -1, -1),
            struct.pack(
                "<IIII",
                0xFFFFFFFF,
                0xFFFFFFFF,
                0xFFFFFFFF,
                0xFFFFFFFF,
            ),
            raw,
        )
    )
    return b"SMP2" + struct.pack("<I", len(metadata)) + metadata


def instrument_prefix(
    profile: InstrumentProfile, sample_pointers: tuple[int, ...]
) -> bytes:
    sample_indices = struct.pack("<" + "H" * len(ANCHORS), *range(len(ANCHORS)))
    sample_list = (
        struct.pack("<H", len(ANCHORS))
        + sample_indices
        + struct.pack("<" + "I" * len(sample_pointers), *sample_pointers)
    )
    parts = (
        b"FINS" + struct.pack("<HH", VERSION, profile.instrument_type),
        feature(b"NA", profile.name.encode("utf-8") + b"\0"),
        feature(b"MA", volume_adsr_data(profile)),
        feature(b"SM", note_map_data()),
        feature(b"LS", sample_list),
    )
    if profile is ES5506:
        return b"".join((*parts, feature(b"ES", es5506_data()), b"EN"))
    return b"".join((*parts, b"EN"))


def build_instrument(
    source_dir: Path, profile: InstrumentProfile
) -> tuple[bytes, tuple[int, ...]]:
    rendered: list[tuple[bytes, int]] = []
    blocks: list[bytes] = []
    for anchor in ANCHORS:
        raw, frames = render_sample(source_dir / anchor.source_name, profile)
        rendered.append((raw, frames))
        blocks.append(
            sample_block(
                f"VCSL Grand Piano {anchor.label}",
                raw,
                frames,
                profile,
                center_rate(anchor, profile.sample_rate),
            )
        )

    placeholder = instrument_prefix(profile, (0,) * len(blocks))
    pointers: list[int] = []
    cursor = len(placeholder)
    for block in blocks:
        pointers.append(cursor)
        cursor += len(block)
    prefix = instrument_prefix(profile, tuple(pointers))
    if len(prefix) != len(placeholder):
        raise AssertionError("sample pointer rewrite changed the prefix size")

    validate_memory(profile, tuple(frames for _, frames in rendered))
    blob = prefix + b"".join(blocks)
    validate_instrument(blob, profile, tuple(frames for _, frames in rendered))
    return blob, tuple(frames for _, frames in rendered)


def validate_memory(profile: InstrumentProfile, frame_counts: tuple[int, ...]) -> None:
    if profile is QSOUND:
        position = 0
        for frames in frame_counts:
            if frames > 65_536 - 16:
                raise AssertionError("QSound sample exceeds its 16-bit address window")
            if (position & 0xFF0000) != ((position + frames) & 0xFF0000):
                position = (position + 0xFFFF) & 0xFF0000
            position += frames + 16
        if position >= QSOUND_MEMORY:
            raise AssertionError("QSound sample bank exceeds 16 MiB")
        return

    position = 2
    for frames in frame_counts:
        length = frames * 2
        if length > 4_194_304 - 2:
            raise AssertionError("ES5506 sample exceeds one memory bank")
        if (position & 0xC00000) != ((position + length + 2) & 0xC00000):
            position = ((position + 0x3FFFFF) & 0xFFC00000) + 2
        position += length + 4
    if position >= ES5506_MEMORY:
        raise AssertionError("ES5506 sample bank exceeds 16 MiB")


def validate_instrument(
    blob: bytes,
    profile: InstrumentProfile,
    frame_counts: tuple[int, ...],
) -> None:
    expected_header = (VERSION, profile.instrument_type)
    if blob[:4] != b"FINS" or struct.unpack_from("<HH", blob, 4) != expected_header:
        raise AssertionError("bad FUI header")

    position = 8
    features: dict[bytes, bytes] = {}
    while blob[position : position + 2] != b"EN":
        code = blob[position : position + 2]
        length = struct.unpack_from("<H", blob, position + 2)[0]
        data_start = position + 4
        data_end = data_start + length
        if data_end > len(blob):
            raise AssertionError("instrument feature exceeds file length")
        features[code] = blob[data_start:data_end]
        position = data_end

    required = {b"NA", b"MA", b"SM", b"LS"}
    if profile is ES5506:
        required.add(b"ES")
    if not required.issubset(features):
        raise AssertionError("instrument is missing a required feature")
    if features[b"NA"] != profile.name.encode("utf-8") + b"\0":
        raise AssertionError("bad instrument name")
    if features[b"MA"] != volume_adsr_data(profile):
        raise AssertionError("bad volume macro")
    if features[b"SM"] != note_map_data():
        raise AssertionError("bad 31-EDO sample map")
    if profile is ES5506 and features[b"ES"] != es5506_data():
        raise AssertionError("bad ES5506 feature")

    sample_list = features[b"LS"]
    count = struct.unpack_from("<H", sample_list)[0]
    if count != len(ANCHORS):
        raise AssertionError("bad embedded sample count")
    pointer_start = 2 + 2 * count
    pointers = struct.unpack_from("<" + "I" * count, sample_list, pointer_start)
    for index, (pointer, expected_frames) in enumerate(zip(pointers, frame_counts)):
        if blob[pointer : pointer + 4] != b"SMP2":
            raise AssertionError("bad embedded sample pointer")
        block_length = struct.unpack_from("<I", blob, pointer + 4)[0]
        block_end = pointer + 8 + block_length
        if block_end > len(blob):
            raise AssertionError("embedded sample exceeds file length")
        metadata = pointer + 8
        metadata = blob.index(0, metadata, block_end) + 1
        frames, rate, calibrated_rate = struct.unpack_from("<iii", blob, metadata)
        depth = blob[metadata + 12]
        expected_rate = center_rate(ANCHORS[index], profile.sample_rate)
        if (frames, rate, calibrated_rate, depth) != (
            expected_frames,
            profile.sample_rate,
            expected_rate,
            profile.depth,
        ):
            raise AssertionError("bad embedded sample metadata")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=DEFAULT_SOURCE_DIR,
        help="directory containing LatticeKeys' piano_*.m4a files",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="directory for generated .fui instruments",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if shutil.which("ffmpeg") is None:
        raise RuntimeError("ffmpeg is required")
    missing = [
        args.source_dir / anchor.source_name
        for anchor in ANCHORS
        if not (args.source_dir / anchor.source_name).is_file()
    ]
    if missing:
        raise FileNotFoundError(f"missing piano sample: {missing[0]}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for profile in PROFILES:
        blob, frame_counts = build_instrument(args.source_dir, profile)
        output = args.output_dir / profile.filename
        output.write_bytes(blob)
        pcm_bytes = sum(frame_counts) * (profile.depth // 8)
        print(
            f"{output}: {len(blob)} bytes, {len(frame_counts)} samples, "
            f"{pcm_bytes} PCM bytes, sha256 {hashlib.sha256(blob).hexdigest()}"
        )


if __name__ == "__main__":
    main()
