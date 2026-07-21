#!/usr/bin/env python3
"""Small package-time rasterizer for deliberately restricted static SVG icons.

This is not an SVG renderer. It accepts the compact icon vocabulary documented
by package_app.py and emits a 32-bit BMP that existing host image decoders can
consume. Keeping it in tooling avoids SVG parsing, vector state and decoder
cost in the embedded runtime.
"""

from __future__ import annotations

import math
import re
import struct
import xml.etree.ElementTree as ET


class SvgRasterError(ValueError):
    pass


_NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
_PATH_TOKEN = re.compile(rf"[AaCcHhLlMmQqSsTtVvZz]|{_NUMBER}")
_COLOR_NAMES = {
    "black": (0, 0, 0, 255), "white": (255, 255, 255, 255),
    "red": (255, 0, 0, 255), "green": (0, 128, 0, 255),
    "blue": (0, 0, 255, 255), "transparent": (0, 0, 0, 0),
}
_ALLOWED_TAGS = {"svg", "g", "path", "circle", "ellipse", "rect", "line", "polyline", "polygon"}
_ALLOWED_STYLE = {
    "fill", "stroke", "stroke-width", "stroke-linecap", "stroke-linejoin",
    "fill-opacity", "stroke-opacity", "opacity", "fill-rule",
}


def _number(value: str, field: str) -> float:
    if not re.fullmatch(_NUMBER, value.strip()):
        raise SvgRasterError(f"{field} must be a unitless SVG number")
    return float(value)


def _dimension(value: str | None, fallback: float, field: str) -> float:
    if value is None:
        return fallback
    cleaned = value.strip()
    if cleaned.endswith("px"):
        cleaned = cleaned[:-2]
    result = _number(cleaned, field)
    if result <= 0:
        raise SvgRasterError(f"{field} must be positive")
    return result


def _opacity(value: str, field: str) -> float:
    result = _number(value, field)
    if not 0 <= result <= 1:
        raise SvgRasterError(f"{field} must be between 0 and 1")
    return result


def _color(value: str, field: str) -> tuple[int, int, int, int] | None:
    cleaned = value.strip().lower()
    if cleaned == "none":
        return None
    if cleaned in _COLOR_NAMES:
        return _COLOR_NAMES[cleaned]
    if re.fullmatch(r"#[0-9a-f]{3}", cleaned):
        return tuple(int(char * 2, 16) for char in cleaned[1:]) + (255,)
    if re.fullmatch(r"#[0-9a-f]{6}", cleaned):
        return tuple(int(cleaned[index:index + 2], 16) for index in (1, 3, 5)) + (255,)
    match = re.fullmatch(rf"rgba?\(\s*({_NUMBER})\s*,\s*({_NUMBER})\s*,\s*({_NUMBER})(?:\s*,\s*({_NUMBER})\s*)?\)", cleaned)
    if match:
        red, green, blue = (round(_number(match.group(index), field)) for index in range(1, 4))
        alpha = round(255 * _opacity(match.group(4), field)) if match.group(4) else 255
        if any(component < 0 or component > 255 for component in (red, green, blue)):
            raise SvgRasterError(f"{field} RGB components must be in 0..255")
        return red, green, blue, alpha
    raise SvgRasterError(f"{field} uses an unsupported color {value!r}")


def _style(element: ET.Element, inherited: dict) -> dict:
    style = dict(inherited)
    for key, value in element.attrib.items():
        name = key.rsplit("}", 1)[-1]
        if name in _ALLOWED_STYLE:
            style[name] = value
        elif name in {"id", "class", "style", "d", "cx", "cy", "r", "rx", "ry", "x", "y", "x1", "x2", "y1", "y2", "width", "height", "points", "viewBox", "xmlns"}:
            continue
        else:
            raise SvgRasterError(f"unsupported SVG attribute {name!r}")
    raw_style = element.attrib.get("style")
    if raw_style:
        for declaration in raw_style.split(";"):
            if not declaration.strip():
                continue
            if ":" not in declaration:
                raise SvgRasterError("invalid inline SVG style")
            name, value = (part.strip() for part in declaration.split(":", 1))
            if name not in _ALLOWED_STYLE:
                raise SvgRasterError(f"unsupported SVG style property {name!r}")
            style[name] = value
    if style.get("fill-rule", "nonzero") != "nonzero":
        raise SvgRasterError("only fill-rule: nonzero is supported")
    if style.get("stroke-linecap", "butt") not in {"butt", "round"}:
        raise SvgRasterError("only stroke-linecap: butt or round is supported")
    if style.get("stroke-linejoin", "miter") not in {"miter", "round"}:
        raise SvgRasterError("only stroke-linejoin: miter or round is supported")
    return style


def _point_list(value: str, field: str) -> list[tuple[float, float]]:
    values = [float(token) for token in re.findall(_NUMBER, value)]
    if len(values) < 4 or len(values) % 2:
        raise SvgRasterError(f"{field} needs at least two coordinate pairs")
    return list(zip(values[::2], values[1::2]))


def _cubic(start, control1, control2, end) -> list[tuple[float, float]]:
    return [(
        (1 - t) ** 3 * start[0] + 3 * (1 - t) ** 2 * t * control1[0] + 3 * (1 - t) * t ** 2 * control2[0] + t ** 3 * end[0],
        (1 - t) ** 3 * start[1] + 3 * (1 - t) ** 2 * t * control1[1] + 3 * (1 - t) * t ** 2 * control2[1] + t ** 3 * end[1],
    ) for t in (index / 12 for index in range(1, 13))]


def _quadratic(start, control, end) -> list[tuple[float, float]]:
    return [(
        (1 - t) ** 2 * start[0] + 2 * (1 - t) * t * control[0] + t ** 2 * end[0],
        (1 - t) ** 2 * start[1] + 2 * (1 - t) * t * control[1] + t ** 2 * end[1],
    ) for t in (index / 10 for index in range(1, 11))]


def _path_contours(data: str) -> list[tuple[list[tuple[float, float]], bool]]:
    tokens = _PATH_TOKEN.findall(data)
    if not tokens:
        raise SvgRasterError("path d is empty")
    index = 0
    command = ""
    current = (0.0, 0.0)
    start = current
    previous_control = None
    contours: list[tuple[list[tuple[float, float]], bool]] = []
    points: list[tuple[float, float]] = []

    def read(count: int) -> list[float]:
        nonlocal index
        if index + count > len(tokens) or any(token.isalpha() for token in tokens[index:index + count]):
            raise SvgRasterError("path command has too few coordinates")
        result = [float(token) for token in tokens[index:index + count]]
        index += count
        return result

    def finish(closed: bool) -> None:
        nonlocal points
        if points:
            contours.append((points, closed))
            points = []

    while index < len(tokens):
        if tokens[index].isalpha():
            command = tokens[index]
            index += 1
        if not command:
            raise SvgRasterError("path must begin with a move command")
        relative = command.islower()
        op = command.upper()
        if op == "Z":
            if points:
                current = start
                finish(True)
            previous_control = None
            command = ""
            continue
        if op == "M":
            pair = read(2)
            point = (pair[0] + (current[0] if relative else 0), pair[1] + (current[1] if relative else 0))
            finish(False)
            points = [point]
            current = start = point
            previous_control = None
            command = "l" if relative else "L"
        elif op == "L":
            pair = read(2)
            current = (pair[0] + (current[0] if relative else 0), pair[1] + (current[1] if relative else 0))
            points.append(current)
            previous_control = None
        elif op == "H":
            value = read(1)[0]
            current = (value + (current[0] if relative else 0), current[1])
            points.append(current)
            previous_control = None
        elif op == "V":
            value = read(1)[0]
            current = (current[0], value + (current[1] if relative else 0))
            points.append(current)
            previous_control = None
        elif op == "C":
            values = read(6)
            base = current if relative else (0.0, 0.0)
            control1 = (values[0] + base[0], values[1] + base[1])
            control2 = (values[2] + base[0], values[3] + base[1])
            end = (values[4] + base[0], values[5] + base[1])
            points.extend(_cubic(current, control1, control2, end))
            current, previous_control = end, control2
        elif op == "S":
            values = read(4)
            control1 = (2 * current[0] - previous_control[0], 2 * current[1] - previous_control[1]) if previous_control else current
            base = current if relative else (0.0, 0.0)
            control2 = (values[0] + base[0], values[1] + base[1])
            end = (values[2] + base[0], values[3] + base[1])
            points.extend(_cubic(current, control1, control2, end))
            current, previous_control = end, control2
        elif op == "Q":
            values = read(4)
            base = current if relative else (0.0, 0.0)
            control = (values[0] + base[0], values[1] + base[1])
            end = (values[2] + base[0], values[3] + base[1])
            points.extend(_quadratic(current, control, end))
            current, previous_control = end, control
        elif op == "T":
            values = read(2)
            control = (2 * current[0] - previous_control[0], 2 * current[1] - previous_control[1]) if previous_control else current
            base = current if relative else (0.0, 0.0)
            end = (values[0] + base[0], values[1] + base[1])
            points.extend(_quadratic(current, control, end))
            current, previous_control = end, control
        elif op == "A":
            raise SvgRasterError("path arc commands are not supported; convert the arc to cubic curves")
        else:
            raise SvgRasterError(f"unsupported path command {command!r}")
    finish(False)
    return contours


def _ellipse(cx: float, cy: float, rx: float, ry: float) -> list[tuple[float, float]]:
    if rx <= 0 or ry <= 0:
        raise SvgRasterError("ellipse radius must be positive")
    return [(cx + math.cos(math.tau * index / 32) * rx, cy + math.sin(math.tau * index / 32) * ry)
            for index in range(32)]


def _rounded_rect(x: float, y: float, width: float, height: float, rx: float, ry: float) -> list[tuple[float, float]]:
    if width <= 0 or height <= 0:
        raise SvgRasterError("rect width and height must be positive")
    if rx == 0 and ry == 0:
        return [(x, y), (x + width, y), (x + width, y + height), (x, y + height)]
    rx = min(rx, width / 2)
    ry = min(ry, height / 2)
    points = []
    for cx, cy, start in ((x + width - rx, y + ry, -90), (x + width - rx, y + height - ry, 0),
                          (x + rx, y + height - ry, 90), (x + rx, y + ry, 180)):
        points.extend((cx + math.cos(math.radians(start + step * 90 / 8)) * rx,
                       cy + math.sin(math.radians(start + step * 90 / 8)) * ry) for step in range(9))
    return points


def _shapes(root: ET.Element) -> tuple[list[tuple[list[tuple[float, float]], bool, dict]], tuple[float, float, float, float], float, float]:
    if root.tag.rsplit("}", 1)[-1] != "svg":
        raise SvgRasterError("root element must be <svg>")
    view_box = root.attrib.get("viewBox")
    if view_box:
        values = [float(value) for value in re.findall(_NUMBER, view_box)]
        if len(values) != 4 or values[2] <= 0 or values[3] <= 0:
            raise SvgRasterError("viewBox must contain min-x min-y positive-width positive-height")
        box = tuple(values)
    else:
        width = _dimension(root.attrib.get("width"), 32, "svg width")
        height = _dimension(root.attrib.get("height"), 32, "svg height")
        box = (0.0, 0.0, width, height)
    width = _dimension(root.attrib.get("width"), box[2], "svg width")
    height = _dimension(root.attrib.get("height"), box[3], "svg height")
    output = []

    def visit(element: ET.Element, inherited: dict) -> None:
        tag = element.tag.rsplit("}", 1)[-1]
        if tag not in _ALLOWED_TAGS:
            raise SvgRasterError(f"unsupported SVG element <{tag}>")
        style = _style(element, inherited)
        if tag in {"svg", "g"}:
            for child in element:
                visit(child, style)
            return
        if tag == "path":
            data = element.attrib.get("d", "")
            for points, closed in _path_contours(data):
                output.append((points, closed, style))
            return
        if tag == "circle":
            points = _ellipse(_number(element.attrib.get("cx", "0"), "cx"), _number(element.attrib.get("cy", "0"), "cy"),
                              _number(element.attrib.get("r", ""), "r"), _number(element.attrib.get("r", ""), "r"))
            output.append((points, True, style))
        elif tag == "ellipse":
            points = _ellipse(_number(element.attrib.get("cx", "0"), "cx"), _number(element.attrib.get("cy", "0"), "cy"),
                              _number(element.attrib.get("rx", ""), "rx"), _number(element.attrib.get("ry", ""), "ry"))
            output.append((points, True, style))
        elif tag == "rect":
            x = _number(element.attrib.get("x", "0"), "x")
            y = _number(element.attrib.get("y", "0"), "y")
            points = _rounded_rect(x, y, _number(element.attrib.get("width", ""), "width"),
                                   _number(element.attrib.get("height", ""), "height"),
                                   _number(element.attrib.get("rx", "0"), "rx"), _number(element.attrib.get("ry", element.attrib.get("rx", "0")), "ry"))
            output.append((points, True, style))
        elif tag == "line":
            output.append(([(_number(element.attrib.get("x1", "0"), "x1"),
                            _number(element.attrib.get("y1", "0"), "y1")),
                           (_number(element.attrib.get("x2", "0"), "x2"),
                            _number(element.attrib.get("y2", "0"), "y2"))], False, style))
        else:
            output.append((_point_list(element.attrib.get("points", ""), "points"), tag == "polygon", style))

    visit(root, {})
    return output, box, width, height


def _winding(point: tuple[float, float], contour: list[tuple[float, float]]) -> int:
    winding = 0
    px, py = point
    for start, end in zip(contour, contour[1:] + contour[:1]):
        if start[1] <= py < end[1] or end[1] <= py < start[1]:
            side = (end[0] - start[0]) * (py - start[1]) - (px - start[0]) * (end[1] - start[1])
            if end[1] > start[1] and side > 0:
                winding += 1
            elif end[1] < start[1] and side < 0:
                winding -= 1
    return winding


def _distance_to_segment(point, start, end) -> float:
    dx, dy = end[0] - start[0], end[1] - start[1]
    length_squared = dx * dx + dy * dy
    if length_squared == 0:
        return math.hypot(point[0] - start[0], point[1] - start[1])
    t = max(0.0, min(1.0, ((point[0] - start[0]) * dx + (point[1] - start[1]) * dy) / length_squared))
    return math.hypot(point[0] - (start[0] + t * dx), point[1] - (start[1] + t * dy))


def _coverage(point, points, closed, style) -> tuple[float, float]:
    fill = 0.0
    if len(points) >= 3 and _color(style.get("fill", "black"), "fill") is not None:
        fill = 1.0 if _winding(point, points) != 0 else 0.0
    stroke = 0.0
    if len(points) >= 2 and _color(style.get("stroke", "none"), "stroke") is not None:
        radius = _number(style.get("stroke-width", "1"), "stroke-width") / 2
        segments = list(zip(points, points[1:]))
        if closed:
            segments.append((points[-1], points[0]))
        if any(_distance_to_segment(point, start, end) <= radius for start, end in segments):
            stroke = 1.0
    return fill, stroke


def _blend(destination: tuple[float, float, float, float], source: tuple[int, int, int, int], coverage: float) -> tuple[float, float, float, float]:
    alpha = source[3] / 255 * coverage
    inverse = 1 - alpha
    return (source[0] * alpha + destination[0] * inverse,
            source[1] * alpha + destination[1] * inverse,
            source[2] * alpha + destination[2] * inverse,
            alpha + destination[3] * inverse)


def rasterize_svg(svg: str | bytes, max_dimension: int = 32) -> tuple[bytes, dict]:
    """Rasterize a static icon to transparent 32-bit BMP bytes and metadata."""
    if max_dimension < 1 or max_dimension > 256:
        raise SvgRasterError("raster size must be between 1 and 256 pixels")
    try:
        root = ET.fromstring(svg)
    except ET.ParseError as error:
        raise SvgRasterError(f"invalid XML: {error}") from error
    shapes, view_box, declared_width, declared_height = _shapes(root)
    scale = min(1.0, max_dimension / max(declared_width, declared_height))
    width = max(1, round(declared_width * scale))
    height = max(1, round(declared_height * scale))
    transform_x = -view_box[0] * width / view_box[2]
    transform_y = -view_box[1] * height / view_box[3]
    scale_x = width / view_box[2]
    scale_y = height / view_box[3]
    transformed = []
    for points, closed, style in shapes:
        transformed.append(([(x * scale_x + transform_x, y * scale_y + transform_y) for x, y in points], closed, style))
    samples = ((0.125, 0.125), (0.375, 0.125), (0.625, 0.125), (0.875, 0.125),
               (0.125, 0.375), (0.375, 0.375), (0.625, 0.375), (0.875, 0.375),
               (0.125, 0.625), (0.375, 0.625), (0.625, 0.625), (0.875, 0.625),
               (0.125, 0.875), (0.375, 0.875), (0.625, 0.875), (0.875, 0.875))
    pixels = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            color = (0.0, 0.0, 0.0, 0.0)
            for points, closed, style in transformed:
                fill_color = _color(style.get("fill", "black"), "fill")
                stroke_color = _color(style.get("stroke", "none"), "stroke")
                opacity = _opacity(style.get("opacity", "1"), "opacity")
                fill_opacity = opacity * _opacity(style.get("fill-opacity", "1"), "fill-opacity")
                stroke_opacity = opacity * _opacity(style.get("stroke-opacity", "1"), "stroke-opacity")
                fill_coverage = stroke_coverage = 0.0
                for sx, sy in samples:
                    fill, stroke = _coverage((x + sx, y + sy), points, closed, style)
                    fill_coverage += fill / len(samples)
                    stroke_coverage += stroke / len(samples)
                if fill_color and fill_coverage:
                    color = _blend(color, fill_color[:3] + (round(fill_color[3] * fill_opacity),), fill_coverage)
                if stroke_color and stroke_coverage:
                    color = _blend(color, stroke_color[:3] + (round(stroke_color[3] * stroke_opacity),), stroke_coverage)
            offset = (y * width + x) * 4
            pixels[offset:offset + 4] = bytes((round(color[2]), round(color[1]), round(color[0]), round(color[3] * 255)))
    header = struct.pack("<2sIHHIIiiHHIIiiII", b"BM", 54 + len(pixels), 0, 0, 54, 40, width, -height,
                         1, 32, 0, len(pixels), 2835, 2835, 0, 0)
    return header + bytes(pixels), {"width": width, "height": height, "shapeCount": len(shapes), "format": "bmp-32-rgba"}
