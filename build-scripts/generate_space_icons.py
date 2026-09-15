#!/usr/bin/env python3
# Copyright 2026 Avora. All rights reserved.
"""Generates the curated Lucide icon catalog used by Avora Spaces.

The catalog is a hand-picked subset of https://lucide.dev, chosen to cover the
Space types people actually create (work, personal, development, ...) without
dumping the whole library into the picker.

Usage:
    python3 build-scripts/generate_space_icons.py --icons <lucide-static>/icons

Downloading the icon source:
    curl -sSL -o lucide.tgz \\
      https://registry.npmjs.org/lucide-static/-/lucide-static-1.46.0.tgz
    tar xzf lucide.tgz   # icons land in package/icons

Every <circle>, <rect>, <line>, <ellipse>, and <polyline> element is rewritten
as SVG path data so the browser only has to understand a single primitive.
Geometry is kept in Lucide's native 24x24 coordinate space; stroke width, caps,
joins, and colour are applied at paint time by AvoraLucideIcon.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
import xml.etree.ElementTree as ElementTree

SVG_NS = "{http://www.w3.org/2000/svg}"

# (category, lucide icon id, human-readable label).  Order matters: the picker
# renders the catalog top to bottom and starts a new section whenever the
# category changes.
CURATED_ICONS = [
    ("Work", "briefcase", "Briefcase"),
    ("Work", "building-2", "Office"),
    ("Work", "calendar", "Calendar"),
    ("Work", "clipboard-list", "Task list"),
    ("Work", "presentation", "Presentation"),
    ("Work", "mail", "Mail"),
    ("Personal", "user", "Person"),
    ("Personal", "users", "People"),
    ("Personal", "heart", "Heart"),
    ("Personal", "smile", "Smile"),
    ("Personal", "star", "Star"),
    ("Development", "code", "Code"),
    ("Development", "terminal", "Terminal"),
    ("Development", "git-branch", "Git branch"),
    ("Development", "bug", "Bug"),
    ("Development", "database", "Database"),
    ("Development", "server", "Server"),
    ("Development", "cpu", "Chip"),
    ("Design", "palette", "Palette"),
    ("Design", "pen-tool", "Pen tool"),
    ("Design", "brush", "Brush"),
    ("Design", "layers", "Layers"),
    ("Design", "shapes", "Shapes"),
    ("Design", "ruler", "Ruler"),
    ("Shopping", "shopping-cart", "Shopping cart"),
    ("Shopping", "shopping-bag", "Shopping bag"),
    ("Shopping", "store", "Store"),
    ("Shopping", "tag", "Tag"),
    ("Shopping", "gift", "Gift"),
    ("Travel", "plane", "Plane"),
    ("Travel", "map", "Map"),
    ("Travel", "compass", "Compass"),
    ("Travel", "luggage", "Luggage"),
    ("Travel", "globe", "Globe"),
    ("Travel", "car", "Car"),
    ("Travel", "tent", "Camping"),
    ("School", "graduation-cap", "Graduation cap"),
    ("School", "book-open", "Open book"),
    ("School", "pencil", "Pencil"),
    ("School", "backpack", "Backpack"),
    ("School", "microscope", "Microscope"),
    ("School", "calculator", "Calculator"),
    ("Finance", "wallet", "Wallet"),
    ("Finance", "credit-card", "Credit card"),
    ("Finance", "banknote", "Banknote"),
    ("Finance", "piggy-bank", "Piggy bank"),
    ("Finance", "chart-line", "Chart"),
    ("Finance", "coins", "Coins"),
    ("Gaming", "gamepad-2", "Gamepad"),
    ("Gaming", "joystick", "Joystick"),
    ("Gaming", "dices", "Dice"),
    ("Gaming", "swords", "Swords"),
    ("Gaming", "trophy", "Trophy"),
    ("Music", "music", "Music"),
    ("Music", "headphones", "Headphones"),
    ("Music", "mic", "Microphone"),
    ("Music", "radio", "Radio"),
    ("Music", "disc-3", "Record"),
    ("Reading", "book", "Book"),
    ("Reading", "library", "Library"),
    ("Reading", "newspaper", "Newspaper"),
    ("Reading", "bookmark", "Bookmark"),
    ("Reading", "scroll", "Scroll"),
    ("Fitness", "dumbbell", "Dumbbell"),
    ("Fitness", "bike", "Bike"),
    ("Fitness", "activity", "Activity"),
    ("Fitness", "footprints", "Footprints"),
    ("Fitness", "heart-pulse", "Heart rate"),
    ("Fitness", "apple", "Apple"),
    ("Home", "house", "House"),
    ("Home", "sofa", "Sofa"),
    ("Home", "lamp", "Lamp"),
    ("Home", "coffee", "Coffee"),
    ("Home", "utensils", "Utensils"),
    ("Home", "leaf", "Leaf"),
    ("Photography", "camera", "Camera"),
    ("Photography", "image", "Image"),
    ("Photography", "aperture", "Aperture"),
    ("Photography", "film", "Film"),
    ("Photography", "video", "Video"),
    ("Ideas", "lightbulb", "Lightbulb"),
    ("Ideas", "sparkles", "Sparkles"),
    ("Ideas", "brain", "Brain"),
    ("Ideas", "rocket", "Rocket"),
    ("Ideas", "zap", "Bolt"),
    ("Ideas", "target", "Target"),
    ("General", "folder", "Folder"),
    ("General", "file-text", "Document"),
    ("General", "message-circle", "Messages"),
    ("General", "bell", "Bell"),
    ("General", "cloud", "Cloud"),
    ("General", "lock", "Lock"),
    ("General", "settings", "Settings"),
    ("General", "search", "Search"),
    ("General", "link", "Link"),
    ("General", "pin", "Pin"),
    ("General", "flag", "Flag"),
    ("General", "sun", "Sun"),
    ("General", "moon", "Moon"),
    ("General", "flame", "Flame"),
    ("General", "anchor", "Anchor"),
    ("General", "atom", "Atom"),
    ("General", "box", "Box"),
    ("General", "key", "Key"),
    ("General", "layout-grid", "Grid"),
    ("General", "list", "List"),
    ("General", "map-pin", "Map pin"),
    ("General", "paperclip", "Paperclip"),
    ("General", "phone", "Phone"),
    ("General", "shield", "Shield"),
    ("General", "ticket", "Ticket"),
    ("General", "umbrella", "Umbrella"),
    ("General", "wrench", "Wrench"),
    ("General", "feather", "Feather"),
    ("General", "hash", "Hash"),
    ("General", "inbox", "Inbox"),
    ("General", "puzzle", "Puzzle"),
    ("General", "tree-pine", "Pine tree"),
    ("General", "dog", "Dog"),
    ("General", "cat", "Cat"),
    ("General", "circle", "Circle"),
]


def fmt(value: float) -> str:
    text = f"{value:.4f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


def attr(element: ElementTree.Element, name: str, default: float = 0.0) -> float:
    raw = element.get(name)
    return default if raw is None else float(raw)


NUMBER_RE = re.compile(r"[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?")


def absolutize_first_moveto(data: str) -> str:
    """Rewrites a leading relative moveto so the subpath can be concatenated.

    Lucide draws every element as its own <path>, where a leading "m" is
    relative to the origin.  Once several elements are merged into one path,
    that same "m" would be measured from the previous subpath's current point,
    so it has to become an absolute moveto.  Any extra coordinate pairs after a
    moveto are implicit linetos and stay relative, hence the explicit "l".
    """
    if not data.startswith("m"):
        return data

    numbers = list(NUMBER_RE.finditer(data, 1))
    if len(numbers) < 2:
        raise ValueError(f"moveto without a coordinate pair: {data}")

    x, y = numbers[0].group(), numbers[1].group()
    rest = data[numbers[1].end():].lstrip(" ,")
    if not rest:
        return f"M{x} {y}"
    separator = "" if rest[0].isalpha() else "l"
    return f"M{x} {y} {separator}{rest}"


def rect_to_path(element: ElementTree.Element) -> str:
    x, y = attr(element, "x"), attr(element, "y")
    w, h = attr(element, "width"), attr(element, "height")
    rx_raw, ry_raw = element.get("rx"), element.get("ry")
    rx = float(rx_raw) if rx_raw is not None else (float(ry_raw) if ry_raw else 0.0)
    ry = float(ry_raw) if ry_raw is not None else rx
    rx, ry = min(rx, w / 2), min(ry, h / 2)

    if rx <= 0 or ry <= 0:
        return (
            f"M{fmt(x)} {fmt(y)}H{fmt(x + w)}V{fmt(y + h)}H{fmt(x)}Z"
        )

    radii = f"{fmt(rx)} {fmt(ry)} 0 0 1 "
    return (
        f"M{fmt(x + rx)} {fmt(y)}"
        f"H{fmt(x + w - rx)}"
        f"A{radii}{fmt(x + w)} {fmt(y + ry)}"
        f"V{fmt(y + h - ry)}"
        f"A{radii}{fmt(x + w - rx)} {fmt(y + h)}"
        f"H{fmt(x + rx)}"
        f"A{radii}{fmt(x)} {fmt(y + h - ry)}"
        f"V{fmt(y + ry)}"
        f"A{radii}{fmt(x + rx)} {fmt(y)}"
        "Z"
    )


def ellipse_to_path(cx: float, cy: float, rx: float, ry: float) -> str:
    # Two half-turn arcs: SVG cannot express a full ellipse in a single arc.
    return (
        f"M{fmt(cx - rx)} {fmt(cy)}"
        f"A{fmt(rx)} {fmt(ry)} 0 1 0 {fmt(cx + rx)} {fmt(cy)}"
        f"A{fmt(rx)} {fmt(ry)} 0 1 0 {fmt(cx - rx)} {fmt(cy)}"
        "Z"
    )


def points_to_path(element: ElementTree.Element, close: bool) -> str:
    raw = (element.get("points") or "").replace(",", " ").split()
    coords = [float(value) for value in raw]
    if len(coords) < 4:
        return ""
    pairs = list(zip(coords[0::2], coords[1::2]))
    head = f"M{fmt(pairs[0][0])} {fmt(pairs[0][1])}"
    tail = "".join(f"L{fmt(x)} {fmt(y)}" for x, y in pairs[1:])
    return head + tail + ("Z" if close else "")


def element_to_path(element: ElementTree.Element) -> str:
    tag = element.tag.removeprefix(SVG_NS)
    if tag == "path":
        return absolutize_first_moveto((element.get("d") or "").strip())
    if tag == "rect":
        return rect_to_path(element)
    if tag == "circle":
        radius = attr(element, "r")
        return ellipse_to_path(attr(element, "cx"), attr(element, "cy"), radius, radius)
    if tag == "ellipse":
        return ellipse_to_path(
            attr(element, "cx"), attr(element, "cy"),
            attr(element, "rx"), attr(element, "ry"),
        )
    if tag == "line":
        return (
            f"M{fmt(attr(element, 'x1'))} {fmt(attr(element, 'y1'))}"
            f"L{fmt(attr(element, 'x2'))} {fmt(attr(element, 'y2'))}"
        )
    if tag in ("polyline", "polygon"):
        return points_to_path(element, close=tag == "polygon")
    raise ValueError(f"unsupported SVG element <{tag}>")


def icon_path_data(svg_path: pathlib.Path) -> str:
    root = ElementTree.parse(svg_path).getroot()
    if root.get("viewBox") != "0 0 24 24":
        raise ValueError(f"{svg_path.name}: unexpected viewBox {root.get('viewBox')}")
    parts = [element_to_path(child) for child in root]
    return " ".join(part for part in parts if part)


def cpp_literal(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def wrap_literal(text: str, indent: str, width: int = 74) -> list[str]:
    """Splits a long path string into adjacent C++ literals on token bounds."""
    chunks: list[str] = []
    current = ""
    for token in text.split(" "):
        candidate = token if not current else f"{current} {token}"
        if len(candidate) > width and current:
            chunks.append(current + " ")
            current = token
        else:
            current = candidate
    if current:
        chunks.append(current)
    return [f"{indent}{cpp_literal(chunk)}" for chunk in chunks]


def generate(icons_dir: pathlib.Path) -> str:
    lines = [
        "// Copyright 2026 Avora. All rights reserved.",
        "//",
        "// Generated by build-scripts/generate_space_icons.py. DO NOT EDIT.",
        "//",
        "// Icon geometry comes from Lucide (https://lucide.dev), which Avora uses as",
        "// its standard icon library.  Lucide is distributed under the ISC License,",
        "// Copyright (c) Lucide Icons and Contributors.  Paths are stored in Lucide's",
        "// native 24x24 coordinate space and are stroked at paint time, so a Space's",
        "// accent colour -- not the stored data -- decides how an icon looks.",
        "",
        '#include "chrome/browser/avora/avora_space_icon_data.h"',
        "",
        "#include <iterator>",
        "",
        "namespace avora {",
        "",
        "const SpaceIcon kSpaceIconCatalog[] = {",
    ]

    for category, icon_id, label in CURATED_ICONS:
        svg_path = icons_dir / f"{icon_id}.svg"
        if not svg_path.exists():
            raise SystemExit(f"error: missing Lucide icon '{icon_id}' in {icons_dir}")
        path_data = icon_path_data(svg_path)
        lines.append(
            f"    {{{cpp_literal(icon_id)}, {cpp_literal(label)},"
            f" {cpp_literal(category)},"
        )
        literal_lines = wrap_literal(path_data, "     ")
        literal_lines[-1] += "},"
        lines.extend(literal_lines)

    lines += [
        "};",
        "",
        "const size_t kSpaceIconCatalogSize = std::size(kSpaceIconCatalog);",
        "",
        "}  // namespace avora",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--icons",
        type=pathlib.Path,
        required=True,
        help="path to the lucide-static 'icons' directory",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=repo_root / "src/chrome/browser/avora/avora_space_icon_data.cc",
    )
    args = parser.parse_args()

    args.output.write_text(generate(args.icons), encoding="utf-8")
    print(f"wrote {len(CURATED_ICONS)} icons to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
