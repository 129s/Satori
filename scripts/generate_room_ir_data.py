#!/usr/bin/env python3
"""
Generate C++ sources that embed a small set of mono impulse responses (IRs).

Input:  assets/ir_src/*.wav (mono PCM16/PCM32float)
Output: RoomIrData.h / RoomIrData.cpp with static arrays (no runtime file IO).
"""

from __future__ import annotations

import argparse
import math
import os
import re
import struct
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import List, Tuple


def _title_from_id(stem: str) -> str:
    stem = re.sub(r"[_\\-]+", " ", stem.strip())
    return " ".join(w[:1].upper() + w[1:] for w in stem.split(" ") if w)


def _read_wav_mono(path: Path) -> Tuple[int, List[float]]:
    with wave.open(str(path), "rb") as w:
        ch = w.getnchannels()
        sr = w.getframerate()
        sampwidth = w.getsampwidth()
        nframes = w.getnframes()
        comptype = w.getcomptype()
        if comptype != "NONE":
            raise ValueError(f"{path}: unsupported compression {comptype}")
        if ch != 1:
            raise ValueError(f"{path}: expected mono wav, got {ch} channels")
        raw = w.readframes(nframes)

    if sampwidth == 2:
        # PCM16 little-endian
        count = len(raw) // 2
        ints = struct.unpack("<" + "h" * count, raw)
        samples = [max(-1.0, min(1.0, v / 32768.0)) for v in ints]
        return sr, samples
    if sampwidth == 4:
        # Assume IEEE float32 little-endian (common for internal tools)
        count = len(raw) // 4
        floats = struct.unpack("<" + "f" * count, raw)
        samples = [max(-1.0, min(1.0, float(v))) for v in floats]
        return sr, samples
    raise ValueError(f"{path}: unsupported sample width {sampwidth}")


def _normalize_peak(samples: List[float], target: float = 0.95) -> List[float]:
    peak = 0.0
    for s in samples:
        peak = max(peak, abs(s))
    if peak < 1e-12:
        return samples[:]
    scale = target / peak
    return [float(s * scale) for s in samples]


def _build_preview(samples: List[float], max_samples: int) -> List[float]:
    if max_samples <= 0 or not samples:
        return []
    if len(samples) <= max_samples:
        return samples[:]
    out: List[float] = []
    step = (len(samples) - 1) / (max_samples - 1)
    for i in range(max_samples):
        idx = int(round(i * step))
        idx = max(0, min(len(samples) - 1, idx))
        out.append(samples[idx])
    return out


def _format_float_array(name: str, values: List[float]) -> str:
    # Keep file size reasonable: 7 significant digits is plenty for IRs.
    parts: List[str] = []
    line: List[str] = []
    for v in values:
        base = f"{v:.7g}"
        # Ensure it's a valid float literal when suffixed with 'f' (e.g. "0f" is invalid).
        if re.fullmatch(r"-?\d+", base):
            base = base + ".0"
        s = f"{base}f"
        line.append(s)
        if len(line) >= 12:
            parts.append(", ".join(line))
            line = []
    if line:
        parts.append(", ".join(line))
    body = ",\n    ".join(parts)
    return f"static const float {name}[] = {{\n    {body}\n}};\n"


@dataclass(frozen=True)
class IrItem:
    ir_id: str
    display: str
    sample_rate: int
    samples: List[float]
    preview: List[float]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input_dir", required=True)
    ap.add_argument("--out_h", required=True)
    ap.add_argument("--out_cpp", required=True)
    ap.add_argument("--preview", type=int, default=512)
    args = ap.parse_args()

    in_dir = Path(args.input_dir)
    wavs = sorted(in_dir.glob("*.wav"))
    if not wavs:
        raise SystemExit(f"no wav files found in {in_dir}")

    items: List[IrItem] = []
    for wav in wavs:
        ir_id = wav.stem
        display = _title_from_id(ir_id)
        sr, samples = _read_wav_mono(wav)
        samples = _normalize_peak(samples)
        preview = _build_preview(samples, int(args.preview))
        items.append(IrItem(ir_id=ir_id, display=display, sample_rate=sr, samples=samples, preview=preview))

    out_h = Path(args.out_h)
    out_cpp = Path(args.out_cpp)
    out_h.parent.mkdir(parents=True, exist_ok=True)
    out_cpp.parent.mkdir(parents=True, exist_ok=True)

    h = []
    h.append("#pragma once\n")
    h.append("\n")
    h.append("#include <cstddef>\n")
    h.append("\n")
    h.append("namespace dsp::room_ir {\n")
    h.append("\n")
    h.append("struct Item {\n")
    h.append("    const char* id;\n")
    h.append("    const char* displayName;\n")
    h.append("    int sampleRate;\n")
    h.append("    const float* samples;\n")
    h.append("    std::size_t sampleCount;\n")
    h.append("    const float* preview;\n")
    h.append("    std::size_t previewCount;\n")
    h.append("};\n")
    h.append("\n")
    h.append("const Item* items(std::size_t* outCount);\n")
    h.append("\n")
    h.append("}  // namespace dsp::room_ir\n")

    cpp = []
    cpp.append('#include "room_ir/RoomIrData.h"\n')
    cpp.append("\n")
    cpp.append("namespace dsp::room_ir {\n")
    cpp.append("\n")

    for idx, it in enumerate(items):
        base = re.sub(r"[^A-Za-z0-9_]", "_", it.ir_id)
        cpp.append(_format_float_array(f"kIr_{base}_samples", it.samples))
        cpp.append("\n")
        cpp.append(_format_float_array(f"kIr_{base}_preview", it.preview))
        cpp.append("\n")

    cpp.append("static const Item kItems[] = {\n")
    for it in items:
        base = re.sub(r"[^A-Za-z0-9_]", "_", it.ir_id)
        cpp.append("    {\n")
        cpp.append(f'        "{it.ir_id}",\n')
        cpp.append(f'        "{it.display}",\n')
        cpp.append(f"        {it.sample_rate},\n")
        cpp.append(f"        kIr_{base}_samples,\n")
        cpp.append(f"        sizeof(kIr_{base}_samples) / sizeof(float),\n")
        cpp.append(f"        kIr_{base}_preview,\n")
        cpp.append(f"        sizeof(kIr_{base}_preview) / sizeof(float),\n")
        cpp.append("    },\n")
    cpp.append("};\n")
    cpp.append("\n")
    cpp.append("const Item* items(std::size_t* outCount) {\n")
    cpp.append("    if (outCount) {\n")
    cpp.append("        *outCount = sizeof(kItems) / sizeof(Item);\n")
    cpp.append("    }\n")
    cpp.append("    return kItems;\n")
    cpp.append("}\n")
    cpp.append("\n")
    cpp.append("}  // namespace dsp::room_ir\n")

    out_h.write_text("".join(h), encoding="utf-8", newline="\n")
    out_cpp.write_text("".join(cpp), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
