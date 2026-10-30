#!/usr/bin/env python3
#opencode: inspect fixed-color controls in captured GFX editor prompt dialogs.

from pathlib import Path

import numpy as np
from PIL import Image


#opencode: find both preset-overwrite buttons independent of wrapped path text.
def overwrite_button_centers(imagePath):
    pixels = np.asarray(Image.open(Path(imagePath)).convert("RGB"))
    height, width = pixels.shape[:2]
    red, green, blue = pixels[:, :, 0], pixels[:, :, 1], pixels[:, :, 2]
    buttonColor = ((red >= 30) & (red <= 50) & (green >= 58)
                   & (green <= 90) & (blue >= 95) & (blue <= 122))
    x0, x1 = int(width * 0.25), int(width * 0.75)
    y0, y1 = int(height * 0.17), int(height * 0.45)
    columnCounts = buttonColor[y0:y1, x0:x1].sum(axis=0)
    columns = np.where(columnCounts > 6)[0] + x0
    if columns.size == 0:
        raise RuntimeError("overwrite dialog buttons were not visible")
    gaps = np.where(np.diff(columns) > 2)[0]
    bounds = []
    for start, end in zip(np.r_[0, gaps + 1], np.r_[gaps, len(columns) - 1]):
        left, right = int(columns[start]), int(columns[end])
        if right - left + 1 >= 20:
            bounds.append((left, right))
    if len(bounds) != 2:
        raise RuntimeError(f"expected two overwrite dialog buttons, found {bounds}")
    rowCounts = buttonColor[y0:y1, bounds[0][0]:bounds[1][1] + 1].sum(axis=1)
    peak = int(rowCounts.max())
    rows = np.where(rowCounts >= max(24, peak * 0.45))[0]
    if rows.size == 0:
        raise RuntimeError("overwrite dialog button row was not detected")
    buttonY = y0 + int(np.median(rows))
    centers = [round((left + right) * 0.5) for left, right in bounds]
    return centers[0], centers[1], buttonY
