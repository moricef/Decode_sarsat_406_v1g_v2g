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
import os
import sys
import time
import urllib.request
from dataclasses import dataclass
from pathlib import Path

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


def download(url: str) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": "dec406-meosar-visible/1.0"})
    with urllib.request.urlopen(req, timeout=30) as response:
        return response.read().decode("utf-8")


def load_tle_groups(cache: Path, refresh: bool) -> dict[int, tuple[str, str, str]]:
    cache.mkdir(parents=True, exist_ok=True)
    by_norad: dict[int, tuple[str, str, str]] = {}

    for group, url in CELESTRAK_GROUPS.items():
        path = cache / f"{group}.tle"
        if refresh or not path.exists():
            text = download(url)
            path.write_text(text, encoding="utf-8")
        else:
            text = path.read_text(encoding="utf-8")

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

    rows = []
    missing = []
    for info in selected_sar_satellites(args.constellation):
        tle_entry = tle.get(info.norad)
        if not tle_entry:
            missing.append(info)
            continue
        tle_name, line1, line2 = tle_entry
        satellite = EarthSatellite(line1, line2, tle_name, ts)
        difference = satellite - observer
        topocentric = difference.at(t)
        alt, az, distance = topocentric.altaz()
        if alt.degrees >= args.min_el:
            rows.append((alt.degrees, az.degrees, distance.km, info, tle_name))

    rows.sort(reverse=True, key=lambda row: row[0])

    print(f"{tstamp:%Y-%m-%d %H:%M:%S} UTC  {qth_name}  lat={lat:.6f} lon={lon:.6f}")
    print()
    for el, az, distance_km, info, tle_name in rows:
        status = "" if info.status == "SAR" else f" {info.status}"
        print(
            f"{info.constellation:<8} {info.cs_id:>3}{status:<3} "
            f"{info.label:<15} Az {az:6.1f}  El {el:5.1f}  Range {distance_km:8.0f} km"
        )

    print()
    print(f"MEOSAR visible >= {args.min_el:.1f} deg: {len(rows)}")
    if missing:
        print(f"TLE missing for {len(missing)} SAR IDs in selected CelesTrak groups", file=sys.stderr)


def main() -> int:
    args = parse_args()
    while True:
        render_once(args)
        if not args.watch:
            return 0
        time.sleep(args.watch)
        args.utc = None
        print()


if __name__ == "__main__":
    raise SystemExit(main())
