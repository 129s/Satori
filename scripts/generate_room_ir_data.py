#!/usr/bin/env python3
"""
Generate C++ sources that embed a small set of impulse responses (IRs).

Input:  assets/ir_src/*.wav / *.wv (mono or stereo; WAV supports PCM16/Float32)
Output: RoomIrData.h / RoomIrData.cpp with static arrays (no runtime file IO).

Notes:
- `.wv` (WavPack) decoding is done via an external tool:
  - Prefer `ffmpeg` if available on PATH, otherwise try `wvunpack`.
"""

from __future__ import annotations

import argparse
import math
import os
import re
import shutil
import struct
import subprocess
import tempfile
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple


def _title_from_id(stem: str) -> str:
    stem = re.sub(r"[_\\-]+", " ", stem.strip())
    return " ".join(w[:1].upper() + w[1:] for w in stem.split(" ") if w)


def _read_wav(path: Path) -> Tuple[int, int, List[float], Optional[List[float]]]:
    with wave.open(str(path), "rb") as w:
        ch = w.getnchannels()
        sr = w.getframerate()
        sampwidth = w.getsampwidth()
        nframes = w.getnframes()
        comptype = w.getcomptype()
        if comptype != "NONE":
            raise ValueError(f"{path}: unsupported compression {comptype}")
        if ch not in (1, 2):
            raise ValueError(f"{path}: expected mono/stereo wav, got {ch} channels")
        raw = w.readframes(nframes)

    if sampwidth == 2:
        # PCM16 little-endian
        count = len(raw) // 2
        ints = struct.unpack("<" + "h" * count, raw)
        samples = [max(-1.0, min(1.0, v / 32768.0)) for v in ints]
    if sampwidth == 4:
        # Assume IEEE float32 little-endian (common for internal tools)
        count = len(raw) // 4
        floats = struct.unpack("<" + "f" * count, raw)
        samples = [max(-1.0, min(1.0, float(v))) for v in floats]
    if sampwidth not in (2, 4):
        raise ValueError(f"{path}: unsupported sample width {sampwidth}")

    if ch == 1:
        return sr, 1, samples, None

    # Interleaved stereo frames: L0, R0, L1, R1, ...
    left = samples[0::2]
    right = samples[1::2]
    return sr, 2, left, right


def _decode_to_wav_if_needed(path: Path) -> Tuple[Path, bool]:
    """
    Returns (wav_path, should_delete).
    """
    if path.suffix.lower() == ".wav":
        return path, False

    if path.suffix.lower() != ".wv":
        raise ValueError(f"{path}: unsupported extension {path.suffix}")

    tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".wav")
    tmp_path = Path(tmp.name)
    tmp.close()

    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg:
        cmd = [ffmpeg, "-v", "error", "-y", "-i", str(path), "-c:a", "pcm_s16le", str(tmp_path)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            tmp_path.unlink(missing_ok=True)
            raise ValueError(f"{path}: ffmpeg decode failed:\n{r.stderr.strip()}")
        return tmp_path, True

    wvunpack = shutil.which("wvunpack")
    if wvunpack:
        cmd = [wvunpack, "-q", "-o", str(tmp_path), str(path)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            tmp_path.unlink(missing_ok=True)
            raise ValueError(f"{path}: wvunpack decode failed:\n{r.stderr.strip()}")
        return tmp_path, True

    tmp_path.unlink(missing_ok=True)
    raise ValueError(f"{path}: decoding .wv requires ffmpeg or wvunpack on PATH")


def _normalize_peak(samples_l: List[float],
                    samples_r: Optional[List[float]],
                    target: float = 0.95) -> Tuple[List[float], Optional[List[float]]]:
    peak = 0.0
    for s in samples_l:
        peak = max(peak, abs(s))
    if samples_r is not None:
        for s in samples_r:
            peak = max(peak, abs(s))
    if peak < 1e-12:
        return samples_l[:], None if samples_r is None else samples_r[:]
    scale = target / peak
    out_l = [float(s * scale) for s in samples_l]
    out_r = None if samples_r is None else [float(s * scale) for s in samples_r]
    return out_l, out_r


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
    channels: int
    samples_l: List[float]
    samples_r: Optional[List[float]]
    preview: List[float]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--input_dir", required=True)
    ap.add_argument("--out_h", required=True)
    ap.add_argument("--out_cpp", required=True)
    ap.add_argument("--preview", type=int, default=512)
    args = ap.parse_args()

    in_dir = Path(args.input_dir)
    inputs = sorted([p for p in in_dir.iterdir()
                     if p.is_file() and p.suffix.lower() in (".wav", ".wv")])
    if not inputs:
        raise SystemExit(f"no .wav/.wv files found in {in_dir}")

    items: List[IrItem] = []
    seen_ids = set()
    for src in inputs:
        ir_id = src.stem
        if ir_id in seen_ids:
            raise ValueError(f"duplicate IR id (filename stem): {ir_id}")
        seen_ids.add(ir_id)
        display = _title_from_id(ir_id)
        decoded, should_delete = _decode_to_wav_if_needed(src)
        try:
            sr, ch, samples_l, samples_r = _read_wav(decoded)
        finally:
            if should_delete:
                decoded.unlink(missing_ok=True)
        if ch == 2 and (samples_r is None or len(samples_l) != len(samples_r)):
            raise ValueError(f"{src}: stereo decode produced mismatched channel lengths")
        samples_l, samples_r = _normalize_peak(samples_l, samples_r)
        if ch == 1 or samples_r is None:
            preview_src = samples_l
        else:
            preview_src = [(l + r) * 0.5 for (l, r) in zip(samples_l, samples_r)]
        preview = _build_preview(preview_src, int(args.preview))
        items.append(IrItem(ir_id=ir_id,
                            display=display,
                            sample_rate=sr,
                            channels=ch,
                            samples_l=samples_l,
                            samples_r=samples_r,
                            preview=preview))

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
    h.append("    int channels;\n")
    h.append("    const float* samplesL;\n")
    h.append("    const float* samplesR;\n")
    h.append("    std::size_t frameCount;\n")
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
        cpp.append(_format_float_array(f"kIr_{base}_samplesL", it.samples_l))
        cpp.append("\n")
        if it.channels == 2 and it.samples_r is not None:
            cpp.append(_format_float_array(f"kIr_{base}_samplesR", it.samples_r))
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
        cpp.append(f"        {it.channels},\n")
        cpp.append(f"        kIr_{base}_samplesL,\n")
        if it.channels == 2 and it.samples_r is not None:
            cpp.append(f"        kIr_{base}_samplesR,\n")
        else:
            cpp.append("        nullptr,\n")
        cpp.append(f"        sizeof(kIr_{base}_samplesL) / sizeof(float),\n")
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
