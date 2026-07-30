#!/usr/bin/env python3
"""
Show MEOSAR SAR-equipped GNSS satellites visible from a QTH.

Requires:
    pip install skyfield

The satellite table is keyed by Cospas-Sarsat satellite ID and NORAD catalog
number from C/S MEOSAR satellite identification parameters. TLEs are loaded
from CelesTrak GNSS groups and matched by NORAD ID.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import os
import sys
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from urllib.error import URLError

EarthSatellite = None
load = None
wgs84 = None


CELESTRAK_GROUPS = {
    "gps": "https://celestrak.org/NORAD/elements/gp.php?GROUP=gps-ops&FORMAT=tle",
    "galileo": "https://celestrak.org/NORAD/elements/gp.php?GROUP=galileo&FORMAT=tle",
    "glonass": "https://celestrak.org/NORAD/elements/gp.php?GROUP=glo-ops&FORMAT=tle",
}


QTH_PRESETS = {
    "JN02QX": ("JN02QX", None),
    # Radio Club F4KLO / Parc de La Villette QO-100 station:
    # 48.893995 N, 2.387856 E, QTH Locator JN18EV64nn.
    "F4KLO": ("JN18EV64NN", "Radio Club F4KLO"),
}


@dataclass(frozen=True)
class SarSatellite:
    cs_id: int
    norad: int
    constellation: str
    label: str
    status: str = "SAR"


@dataclass
class VisibleSatellite:
    info: SarSatellite
    tle_name: str
    az_deg: float
    el_deg: float
    range_km: float
    set_seconds: int | None = None
    set_utc: dt.datetime | None = None
    trend: str = "level"


@dataclass(frozen=True)
class DopSummary:
    gdop: float
    pdop: float
    hdop: float
    vdop: float
    tdop: float


@dataclass(frozen=True)
class GeometrySummary:
    grade: str
    count: int
    az_span_deg: float
    largest_gap_deg: float
    avg_el_deg: float
    dop: DopSummary | None = None


SAR_SATELLITES = [
    # GPS / DASS
    SarSatellite(301, 62339, "GPS", "GPS-III-7"),
    SarSatellite(302, 28474, "GPS", "GPS BIIR-13"),
    SarSatellite(303, 40294, "GPS", "GPS BIIF-8"),
    SarSatellite(304, 43873, "GPS", "GPS-III-1"),
    SarSatellite(306, 39741, "GPS", "GPS BIIF-6"),
    SarSatellite(308, 40730, "GPS", "GPS IIF-10"),
    SarSatellite(309, 40105, "GPS", "GPS BIIF-7"),
    SarSatellite(310, 41019, "GPS", "GPS IIF-11"),
    SarSatellite(311, 48859, "GPS", "GPS-III-5"),
    SarSatellite(312, 29601, "GPS", "GPS BIIRM-3"),
    SarSatellite(314, 46826, "GPS", "GPS-III-4"),
    SarSatellite(315, 32260, "GPS", "GPS BIIRM-4"),
    SarSatellite(316, 27663, "GPS", "GPS BIIR-8"),
    SarSatellite(317, 28874, "GPS", "GPS BIIRM-1"),
    SarSatellite(318, 44506, "GPS", "GPS-III-2"),
    SarSatellite(319, 28190, "GPS", "GPS BIIR-11"),
    SarSatellite(321, 64202, "GPS", "GPS BIII-8"),
    SarSatellite(323, 45854, "GPS", "GPS-III-3"),
    SarSatellite(324, 38833, "GPS", "GPS BIIF-3"),
    SarSatellite(326, 40534, "GPS", "GPS IIF-9"),
    SarSatellite(327, 39166, "GPS", "GPS BIIF-4"),
    SarSatellite(328, 55268, "GPS", "GPS-III-6"),
    SarSatellite(329, 32384, "GPS", "GPS BIIRM-5"),
    SarSatellite(330, 39533, "GPS", "GPS BIIF-5"),
    SarSatellite(332, 41328, "GPS", "GPS IIF-12"),
    # Galileo SAR. OFF/decommissioned/non-SAR IDs from QARS are deliberately
    # omitted: 401, 411, 412, 420, 422, 424.
    SarSatellite(402, 41549, "Galileo", "GSAT0211"),
    SarSatellite(403, 41860, "Galileo", "GSAT0212"),
    SarSatellite(404, 41861, "Galileo", "GSAT0213"),
    SarSatellite(405, 41862, "Galileo", "GSAT0214"),
    SarSatellite(406, 59600, "Galileo", "GSAT0227"),
    SarSatellite(407, 41859, "Galileo", "GSAT0207"),
    SarSatellite(408, 41175, "Galileo", "GSAT0208"),
    SarSatellite(409, 41174, "Galileo", "GSAT0209"),
    SarSatellite(410, 49810, "Galileo", "GSAT0224"),
    SarSatellite(413, 43567, "Galileo", "GSAT0220"),
    SarSatellite(414, 40129, "Galileo", "GSAT0202"),
    SarSatellite(415, 43564, "Galileo", "GSAT0221"),
    SarSatellite(416, 61182, "Galileo", "GSAT0232"),
    SarSatellite(418, 40128, "Galileo", "GSAT0201"),
    SarSatellite(419, 38857, "Galileo", "GSAT0103"),
    SarSatellite(421, 43055, "Galileo", "GSAT0215"),
    SarSatellite(423, 61183, "Galileo", "GSAT0226"),
    SarSatellite(425, 43056, "Galileo", "GSAT0216"),
    SarSatellite(426, 40544, "Galileo", "GSAT0203"),
    SarSatellite(427, 43057, "Galileo", "GSAT0217"),
    SarSatellite(428, 67160, "Galileo", "GSAT0233"),
    SarSatellite(429, 59598, "Galileo", "GSAT0225"),
    SarSatellite(430, 40890, "Galileo", "GSAT0206"),
    SarSatellite(431, 43058, "Galileo", "GSAT0218"),
    SarSatellite(432, 67162, "Galileo", "GSAT0234", "UT"),
    # GLONASS SAR. 501 is decommissioned and omitted.
    SarSatellite(502, 40315, "GLONASS", "Glonass-K1-#2"),
    SarSatellite(503, 46805, "GLONASS", "Glonass-K1-#3"),
    SarSatellite(504, 52984, "GLONASS", "Glonass-K1-#4"),
    SarSatellite(505, 57517, "GLONASS", "Glonass-K2-#1"),
    SarSatellite(506, 63130, "GLONASS", "Glonass-K2-#2"),
]


def maidenhead_to_latlon(locator: str) -> tuple[float, float]:
    loc = locator.strip().upper()
    if len(loc) < 2 or len(loc) % 2:
        raise ValueError(f"invalid Maidenhead locator: {locator}")

    lon = -180.0
    lat = -90.0
    lon_size = 20.0
    lat_size = 10.0

    for pair_index in range(0, len(loc), 2):
        a = loc[pair_index]
        b = loc[pair_index + 1]
        level = pair_index // 2

        if level == 0:
            if not ("A" <= a <= "R" and "A" <= b <= "R"):
                raise ValueError(f"invalid Maidenhead field: {locator}")
            lon += (ord(a) - ord("A")) * lon_size
            lat += (ord(b) - ord("A")) * lat_size
        elif level % 2 == 1:
            if not (a.isdigit() and b.isdigit()):
                raise ValueError(f"invalid Maidenhead square: {locator}")
            lon_size /= 10.0
            lat_size /= 10.0
            lon += int(a) * lon_size
            lat += int(b) * lat_size
        else:
            if not ("A" <= a <= "X" and "A" <= b <= "X"):
                raise ValueError(f"invalid Maidenhead subsquare: {locator}")
            lon_size /= 24.0
            lat_size /= 24.0
            lon += (ord(a) - ord("A")) * lon_size
            lat += (ord(b) - ord("A")) * lat_size

    return lat + lat_size / 2.0, lon + lon_size / 2.0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="List MEOSAR satellites visible from a QTH.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    qth = parser.add_mutually_exclusive_group()
    qth.add_argument(
        "--qth",
        default="JN02QX",
        help="Maidenhead locator or preset name: JN02QX, F4KLO",
    )
    qth.add_argument("--latlon", nargs=2, type=float, metavar=("LAT", "LON"))
    parser.add_argument("--min-el", type=float, default=0.0, help="minimum elevation in degrees")
    parser.add_argument("--refresh", action="store_true", help="force TLE download")
    parser.add_argument("--cache-dir", default=None, help="override TLE cache directory")
    parser.add_argument("--watch", type=int, metavar="SECONDS", help="refresh display interval")
    parser.add_argument("--utc", default=None, help="UTC timestamp, e.g. 2026-07-20T08:15:00Z")
    parser.add_argument(
        "--sort",
        choices=("elevation", "constellation", "azimuth", "range", "id"),
        default="elevation",
        help="visible satellite sort order",
    )
    parser.add_argument("--compact", action="store_true", help="compact terminal output")
    parser.add_argument("--pointing", action="store_true", help="operator pointing table")
    parser.add_argument("--target", default=None, help="single satellite: C/S ID, NORAD ID, or name")
    parser.add_argument("--clear", action="store_true", help="clear terminal before each watch update")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument("--no-set-time", action="store_true", help="skip time-to-set calculation")
    parser.add_argument(
        "--geom-min-el",
        type=float,
        default=15.0,
        help="minimum elevation for geometry summary candidates",
    )
    parser.add_argument(
        "--constellation",
        choices=("all", "gps", "galileo", "glonass"),
        default="all",
        help="constellation filter",
    )
    return parser.parse_args()


def resolve_qth(args: argparse.Namespace) -> tuple[float, float, str]:
    if args.latlon:
        lat, lon = args.latlon
        return lat, lon, f"{lat:.6f},{lon:.6f}"

    key = args.qth.strip().upper()
    locator, label = QTH_PRESETS.get(key, (key, None))
    lat, lon = maidenhead_to_latlon(locator)
    name = f"{key} ({locator})" if label is None else f"{key} ({label}, {locator})"
    return lat, lon, name


def cache_dir(value: str | None) -> Path:
    if value:
        return Path(value).expanduser()
    base = os.environ.get("XDG_CACHE_HOME")
    if base:
        return Path(base) / "dec406-meosar"
    return Path.home() / ".cache" / "dec406-meosar"


def download(url: str, attempts: int = 3) -> str:
    last_error: Exception | None = None
    req = urllib.request.Request(url, headers={"User-Agent": "dec406-meosar-visible/1.0"})
    for attempt in range(1, attempts + 1):
        try:
            with urllib.request.urlopen(req, timeout=30) as response:
                return response.read().decode("utf-8")
        except (OSError, URLError) as exc:
            last_error = exc
            if attempt < attempts:
                time.sleep(0.5 * attempt)
    raise RuntimeError(f"download failed after {attempts} attempts: {url}") from last_error


def load_tle_text(group: str, url: str, cache: Path, refresh: bool) -> tuple[str, str]:
    path = cache / f"{group}.tle"
    if refresh or not path.exists():
        try:
            text = download(url)
        except RuntimeError as exc:
            if not path.exists():
                raise
            print(f"warning: {exc}; using cached {path}", file=sys.stderr)
            return group, path.read_text(encoding="utf-8")
        path.write_text(text, encoding="utf-8")
        return group, text

    return group, path.read_text(encoding="utf-8")


def parse_tle_text(text: str) -> dict[int, tuple[str, str, str]]:
    by_norad: dict[int, tuple[str, str, str]] = {}
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    for i in range(0, len(lines) - 2, 3):
        name, line1, line2 = lines[i], lines[i + 1], lines[i + 2]
        if not line1.startswith("1 ") or not line2.startswith("2 "):
            continue
        try:
            norad = int(line1[2:7])
        except ValueError:
            continue
        by_norad[norad] = (name, line1, line2)
    return by_norad


def load_tle_groups(cache: Path, refresh: bool) -> dict[int, tuple[str, str, str]]:
    cache.mkdir(parents=True, exist_ok=True)
    by_norad: dict[int, tuple[str, str, str]] = {}

    with ThreadPoolExecutor(max_workers=len(CELESTRAK_GROUPS)) as executor:
        futures = [
            executor.submit(load_tle_text, group, url, cache, refresh)
            for group, url in CELESTRAK_GROUPS.items()
        ]
        for future in as_completed(futures):
            _group, text = future.result()
            by_norad.update(parse_tle_text(text))

    return by_norad


def selected_sar_satellites(constellation: str) -> list[SarSatellite]:
    if constellation == "all":
        return list(SAR_SATELLITES)
    return [sat for sat in SAR_SATELLITES if sat.constellation.lower() == constellation]


def parse_utc(value: str | None) -> dt.datetime:
    if value is None:
        return dt.datetime.now(dt.timezone.utc)
    text = value.strip()
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    parsed = dt.datetime.fromisoformat(text)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=dt.timezone.utc)
    return parsed.astimezone(dt.timezone.utc)


def format_duration(seconds: int | None) -> str:
    if seconds is None:
        return "--"
    if seconds < 0:
        seconds = 0
    if seconds < 60:
        return "<1m"
    minutes = (seconds + 30) // 60
    hours, mins = divmod(minutes, 60)
    if hours:
        return f"{hours:d}h{mins:02d}"
    return f"{mins:d}m"


def next_set_event(
    satellite,
    observer,
    ts,
    start: dt.datetime,
    min_el: float,
) -> tuple[int | None, dt.datetime | None]:
    t0 = ts.from_datetime(start)
    t1 = ts.from_datetime(start + dt.timedelta(hours=18.0))
    event_times, events = satellite.find_events(observer, t0, t1, altitude_degrees=min_el)

    for event_time, event in zip(event_times, events):
        if int(event) != 2:
            continue
        event_dt = event_time.utc_datetime().replace(tzinfo=dt.timezone.utc)
        return max(0, int((event_dt - start).total_seconds())), event_dt
    return None, None


def trend_at(satellite, observer, ts, start: dt.datetime) -> str:
    now = ts.from_datetime(start)
    later = ts.from_datetime(start + dt.timedelta(seconds=60))
    alt_now, _az_now, _distance_now = (satellite - observer).at(now).altaz()
    alt_later, _az_later, _distance_later = (satellite - observer).at(later).altaz()
    delta = alt_later.degrees - alt_now.degrees
    if delta > 0.02:
        return "up"
    if delta < -0.02:
        return "down"
    return "level"


def sort_visible(rows: list[VisibleSatellite], sort_mode: str) -> None:
    if sort_mode == "constellation":
        rows.sort(key=lambda row: (row.info.constellation, -row.el_deg, row.info.cs_id))
    elif sort_mode == "azimuth":
        rows.sort(key=lambda row: (row.az_deg, -row.el_deg))
    elif sort_mode == "range":
        rows.sort(key=lambda row: (row.range_km, -row.el_deg))
    elif sort_mode == "id":
        rows.sort(key=lambda row: (row.info.cs_id, -row.el_deg))
    else:
        rows.sort(key=lambda row: (-row.el_deg, row.info.constellation, row.info.cs_id))


def target_matches(info: SarSatellite, target: str | None) -> bool:
    if not target:
        return False
    needle = target.strip().lower()
    return (
        needle == str(info.cs_id)
        or needle == str(info.norad)
        or needle in info.label.lower()
        or needle in info.constellation.lower()
    )


def safe_sqrt(value: float) -> float:
    return math.sqrt(max(0.0, value))


def calculate_dop(rows: list[VisibleSatellite], min_el: float) -> DopSummary | None:
    candidates = [row for row in rows if row.el_deg >= min_el]
    if len(candidates) < 4:
        return None

    try:
        import numpy as np
    except ImportError:
        return None

    matrix = []
    for row in candidates:
        az = math.radians(row.az_deg)
        el = math.radians(row.el_deg)
        cos_el = math.cos(el)
        east = cos_el * math.sin(az)
        north = cos_el * math.cos(az)
        up = math.sin(el)
        matrix.append([east, north, up, 1.0])

    h = np.array(matrix, dtype=float)
    normal = h.T @ h
    try:
        if np.linalg.cond(normal) > 1e12:
            return None
        q = np.linalg.inv(normal)
    except np.linalg.LinAlgError:
        return None

    return DopSummary(
        gdop=safe_sqrt(float(q[0, 0] + q[1, 1] + q[2, 2] + q[3, 3])),
        pdop=safe_sqrt(float(q[0, 0] + q[1, 1] + q[2, 2])),
        hdop=safe_sqrt(float(q[0, 0] + q[1, 1])),
        vdop=safe_sqrt(float(q[2, 2])),
        tdop=safe_sqrt(float(q[3, 3])),
    )


def format_dop(dop: DopSummary | None) -> str:
    if dop is None:
        return "DOP unavailable"
    return (
        f"DOP pdop={dop.pdop:.1f} hdop={dop.hdop:.1f} vdop={dop.vdop:.1f} "
        f"gdop={dop.gdop:.1f} tdop={dop.tdop:.1f}"
    )


def summarize_geometry(rows: list[VisibleSatellite], min_el: float) -> GeometrySummary:
    candidates = [row for row in rows if row.el_deg >= min_el]
    dop = calculate_dop(rows, min_el)
    if not candidates:
        return GeometrySummary("weak", 0, 0.0, 360.0, 0.0, dop)

    azimuths = sorted(row.az_deg % 360.0 for row in candidates)
    if len(azimuths) == 1:
        largest_gap = 360.0
    else:
        gaps = [azimuths[i + 1] - azimuths[i] for i in range(len(azimuths) - 1)]
        gaps.append(azimuths[0] + 360.0 - azimuths[-1])
        largest_gap = max(gaps)
    az_span = 360.0 - largest_gap
    avg_el = sum(row.el_deg for row in candidates) / len(candidates)

    if len(candidates) >= 4 and az_span >= 240.0 and largest_gap <= 160.0:
        grade = "good"
    elif len(candidates) >= 3 and az_span >= 180.0:
        grade = "fair"
    else:
        grade = "weak"

    return GeometrySummary(grade, len(candidates), az_span, largest_gap, avg_el, dop)


def visible_to_dict(row: VisibleSatellite) -> dict[str, object]:
    return {
        "cs_id": row.info.cs_id,
        "norad": row.info.norad,
        "constellation": row.info.constellation,
        "label": row.info.label,
        "status": row.info.status,
        "tle_name": row.tle_name,
        "az_deg": round(row.az_deg, 3),
        "el_deg": round(row.el_deg, 3),
        "range_km": round(row.range_km, 1),
        "set_seconds": row.set_seconds,
        "set_utc": row.set_utc.isoformat().replace("+00:00", "Z") if row.set_utc else None,
        "trend": row.trend,
    }


def dop_to_dict(dop: DopSummary | None) -> dict[str, float] | None:
    if dop is None:
        return None
    return {
        "gdop": round(dop.gdop, 3),
        "pdop": round(dop.pdop, 3),
        "hdop": round(dop.hdop, 3),
        "vdop": round(dop.vdop, 3),
        "tdop": round(dop.tdop, 3),
    }


def constellation_abbrev(name: str) -> str:
    return {
        "GPS": "GPS",
        "Galileo": "GAL",
        "GLONASS": "GLO",
    }.get(name, name[:3].upper())


def format_set_utc(value: dt.datetime | None) -> str:
    if value is None:
        return "--"
    return f"{value:%H:%M}Z"


def trend_symbol(trend: str) -> str:
    return {
        "up": "↑",
        "down": "↓",
        "level": "→",
    }.get(trend, "?")


def render_text(
    tstamp: dt.datetime,
    qth_name: str,
    lat: float,
    lon: float,
    rows: list[VisibleSatellite],
    geometry: GeometrySummary,
    args: argparse.Namespace,
) -> None:
    print(f"{tstamp:%Y-%m-%d %H:%M:%S} UTC  {qth_name}  lat={lat:.6f} lon={lon:.6f}")
    print()
    for row in rows:
        status = "" if row.info.status == "SAR" else f" {row.info.status}"
        geo_mark = "*" if row.el_deg >= args.geom_min_el else " "
        print(
            f"{geo_mark} {row.info.constellation:<8} {row.info.cs_id:>3}{status:<3} "
            f"{row.info.label:<15} Az {row.az_deg:6.1f}  El {row.el_deg:5.1f}  "
            f"Set {format_duration(row.set_seconds):>5}  Range {row.range_km:8.0f} km"
        )

    print()
    print(f"MEOSAR visible >= {args.min_el:.1f} deg: {len(rows)}")
    print(
        f"Geometry >= {args.geom_min_el:.1f} deg: {geometry.grade}  "
        f"count={geometry.count}  az_span={geometry.az_span_deg:.0f} deg  "
        f"largest_gap={geometry.largest_gap_deg:.0f} deg  avg_el={geometry.avg_el_deg:.1f} deg"
    )
    print(f"{format_dop(geometry.dop)}")


def render_pointing(
    tstamp: dt.datetime,
    qth_name: str,
    rows: list[VisibleSatellite],
    args: argparse.Namespace,
) -> None:
    print(f"MEOSAR POINTING - {qth_name}")
    print(f"{tstamp:%Y-%m-%d %H:%M:%S} UTC")
    print(f"Antenna mask: {args.min_el:.1f} deg")
    print()
    print(f"{'Satellite':<15} {'C/S':>5} {'Const':<8} {'Az':>7} {'El':>7} {'Trend':>5} {'Visible':>8}")
    for row in rows:
        visible = format_duration(row.set_seconds) if row.el_deg >= args.min_el else "below"
        print(
            f"{row.info.label:<15} {row.info.cs_id:>5} {constellation_abbrev(row.info.constellation):<8} "
            f"{row.az_deg:6.1f} {row.el_deg:6.1f} {trend_symbol(row.trend):>5} {visible:>8}"
        )


def render_target(
    tstamp: dt.datetime,
    qth_name: str,
    rows: list[VisibleSatellite],
    args: argparse.Namespace,
) -> None:
    if not rows:
        print(f"Target not found: {args.target}", file=sys.stderr)
        return
    row = rows[0]
    visible = row.el_deg >= args.min_el
    print(f"MEOSAR TARGET - {qth_name}")
    print(f"{tstamp:%Y-%m-%d %H:%M:%S} UTC")
    print()
    print(f"{row.info.label} - {row.info.constellation} C/S {row.info.cs_id}")
    print()
    print(f"Azimuth:     {row.az_deg:7.1f} deg")
    print(f"Elevation:   {row.el_deg:7.1f} deg  {trend_symbol(row.trend)}")
    print(f"Range:       {row.range_km:7.0f} km")
    print(f"Set:         {format_set_utc(row.set_utc):>7}  ({format_duration(row.set_seconds)})")
    print(f"Mask:        {args.min_el:7.1f} deg")
    print(f"Status:      {'visible' if visible else 'below mask'}")


def render_compact(
    tstamp: dt.datetime,
    qth_name: str,
    rows: list[VisibleSatellite],
    geometry: GeometrySummary,
    args: argparse.Namespace,
) -> None:
    pdop_field = f"{geometry.dop.pdop:.1f}" if geometry.dop else "--"
    header = (
        f"{tstamp:%H:%M:%S}Z {qth_name}  "
        f"vis={len(rows)} geom={geometry.grade}/{geometry.count} "
        f"gap={geometry.largest_gap_deg:.0f} pdop={pdop_field}"
    )
    print(header)
    for row in rows:
        if row.el_deg < args.geom_min_el and args.sort == "elevation" and len(rows) > 12:
            continue
        set_field = "" if args.no_set_time else f" set={format_duration(row.set_seconds):>5}"
        print(
            f"{constellation_abbrev(row.info.constellation)}{row.info.cs_id:03d} "
            f"az={row.az_deg:03.0f} el={row.el_deg:02.0f}"
            f" {trend_symbol(row.trend)}{set_field} {row.info.label}"
        )


def render_json(
    tstamp: dt.datetime,
    qth_name: str,
    lat: float,
    lon: float,
    rows: list[VisibleSatellite],
    geometry: GeometrySummary,
    args: argparse.Namespace,
) -> None:
    payload = {
        "utc": tstamp.isoformat().replace("+00:00", "Z"),
        "qth": qth_name,
        "lat": lat,
        "lon": lon,
        "min_el_deg": args.min_el,
        "geometry_min_el_deg": args.geom_min_el,
        "geometry": {
            "grade": geometry.grade,
            "count": geometry.count,
            "az_span_deg": round(geometry.az_span_deg, 3),
            "largest_gap_deg": round(geometry.largest_gap_deg, 3),
            "avg_el_deg": round(geometry.avg_el_deg, 3),
            "dop": dop_to_dict(geometry.dop),
        },
        "visible_count": len(rows),
        "satellites": [visible_to_dict(row) for row in rows],
    }
    print(json.dumps(payload, separators=(",", ":"), sort_keys=True))


def render_once(args: argparse.Namespace) -> None:
    global EarthSatellite, load, wgs84
    if EarthSatellite is None:
        try:
            from skyfield.api import EarthSatellite as SkyfieldEarthSatellite
            from skyfield.api import load as skyfield_load
            from skyfield.api import wgs84 as skyfield_wgs84
        except ImportError:
            print("Missing dependency: skyfield", file=sys.stderr)
            print("Install it with: python3 -m pip install skyfield", file=sys.stderr)
            sys.exit(2)
        EarthSatellite = SkyfieldEarthSatellite
        load = skyfield_load
        wgs84 = skyfield_wgs84

    lat, lon, qth_name = resolve_qth(args)
    tstamp = parse_utc(args.utc)
    tle = load_tle_groups(cache_dir(args.cache_dir), args.refresh)
    ts = load.timescale()
    t = ts.from_datetime(tstamp)
    observer = wgs84.latlon(lat, lon)

    rows: list[VisibleSatellite] = []
    missing = []
    for info in selected_sar_satellites(args.constellation):
        is_target = target_matches(info, args.target)
        if args.target and not is_target:
            continue
        tle_entry = tle.get(info.norad)
        if not tle_entry:
            missing.append(info)
            continue
        tle_name, line1, line2 = tle_entry
        satellite = EarthSatellite(line1, line2, tle_name, ts)
        difference = satellite - observer
        topocentric = difference.at(t)
        alt, az, distance = topocentric.altaz()
        if alt.degrees >= args.min_el or is_target:
            set_seconds = None
            set_utc = None
            if not args.no_set_time and alt.degrees >= args.min_el:
                set_seconds, set_utc = next_set_event(satellite, observer, ts, tstamp, args.min_el)
            rows.append(
                VisibleSatellite(
                    info=info,
                    tle_name=tle_name,
                    az_deg=az.degrees,
                    el_deg=alt.degrees,
                    range_km=distance.km,
                    set_seconds=set_seconds,
                    set_utc=set_utc,
                    trend=trend_at(satellite, observer, ts, tstamp),
                )
            )

    sort_visible(rows, args.sort)
    geometry = summarize_geometry(rows, args.geom_min_el)

    if args.json:
        render_json(tstamp, qth_name, lat, lon, rows, geometry, args)
    elif args.target:
        render_target(tstamp, qth_name, rows, args)
    elif args.pointing:
        render_pointing(tstamp, qth_name, rows, args)
    elif args.compact:
        render_compact(tstamp, qth_name, rows, geometry, args)
    else:
        render_text(tstamp, qth_name, lat, lon, rows, geometry, args)

    if missing:
        print(f"TLE missing for {len(missing)} SAR IDs in selected CelesTrak groups", file=sys.stderr)


def main() -> int:
    args = parse_args()
    while True:
        if args.clear:
            print("\033[2J\033[H", end="")
        render_once(args)
        if not args.watch:
            return 0
        time.sleep(args.watch)
        args.utc = None
        if not args.clear:
            print()


if __name__ == "__main__":
    raise SystemExit(main())
