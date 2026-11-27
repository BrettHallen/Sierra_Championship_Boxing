#!/usr/bin/env python3
"""
SUPERSEDED: the real White left-side set was found as WBXR0.SPT in the SIERRACH
release (see restored/BOXING.WR). This script is kept as a record of the
approximation experiment; it scored 87.8% of pixels against the real art.

Rebuild an APPROXIMATE BOXING.WR (White boxer, left-side set), which is missing
from every known copy of the IBM PC version of Championship Boxing.

How: within one side, the Black, White and Cartoon sets are the same animation
with the same frame list, sizes and screen offsets. Only the drawing differs
(the White boxer is a hand-touched recolour of the Black one). So:

  1. Learn "Black + Cartoon + Kangaroo pixels -> White pixel" from the
     xB sets (BB, CB, KB -> WB) with a random forest that looks at
     each pixel's neighbourhood. Cross-validated accuracy on WB: ~91% of pixels.
  2. Apply it to the xR sets (BR, CR, KR) to predict WR's pixels.
  3. Write them into a copy of BOXING.BR, keeping BR's headers, offsets,
     transparency masks and slack bytes, so the file layout is exactly what the
     game expects.

The result plays and looks like a white boxer, but it is a reconstruction, not
the original art: expect speckle and simplified faces that want hand touch-up.

Usage:  python3 reconstruct_wr.py [GAME_DIR] [OUT_FILE]
Needs:  pip install pillow numpy scikit-learn
Only reads the original files.
"""
import os
import sys

import numpy as np
from sklearn.ensemble import RandomForestClassifier

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_graphics as eg  # noqa: E402

IDX = eg.PALETTES[(1, True)]
KIND = {"body": 0, "head": 1, "arms": 2}


def index_frames(data):
    meta, _ = eg.parse_boxer(data, IDX)
    out = []
    for kind, o, w, h, off, im in meta:
        a = np.array(im)
        idx = np.full(a.shape[:2], 4, np.int8)  # 4 = transparent
        for i, c in enumerate(IDX):
            idx[(a[..., 0] == c[0]) & (a[..., 1] == c[1]) & (a[..., 2] == c[2]) & (a[..., 3] == 255)] = i
        out.append(idx)
    return meta, out


def features(sets, meta, i):
    shape = sets[0][0][i].shape
    H, W = shape
    cols = []
    for frames, r in sets:
        a = frames[i] if frames[i].shape == shape else np.full(shape, 6, np.int8)
        p = np.pad(a, r, constant_values=5)
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                cols.append(p[r + dy:r + dy + H, r + dx:r + dx + W].ravel())
    yy, xx = np.mgrid[0:H, 0:W]
    cols += [yy.ravel(), xx.ravel(), np.full(H * W, KIND[meta[i][0]])]
    return np.stack(cols, 1)


def encode(base, meta, pix):
    out = bytearray(base)
    for (kind, o, w, h, off, _), a in zip(meta, pix):
        p = o + (8 if kind == "head" else 6)

        def byte(y, x0, mask=False):
            b = 0
            for k in range(4):
                v = a[y, x0 + k]
                b = (b << 2) | ((3 if v == 4 else 0) if mask else (0 if v == 4 else int(v)))
            return b

        for r in range(h):
            for c in range(w):
                edge = kind == "arms" and c in (0, w - 1)
                for line in (0, 1):
                    y = r * 2 + line
                    if edge:
                        out[p:p + 4] = bytes([byte(y, c * 8, True), byte(y, c * 8 + 4, True),
                                              byte(y, c * 8), byte(y, c * 8 + 4)])
                        p += 4
                    else:
                        out[p:p + 2] = bytes([byte(y, c * 8), byte(y, c * 8 + 4)])
                        p += 2
    return out


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    game = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "Original_IBM_CGA", "1987_Crack")
    dest = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "reconstructed", "BOXING.WR")
    raw = {k: open(os.path.join(game, "BOXING." + k), "rb").read() for k in ("BB", "CB", "KB", "WB", "BR", "CR", "KR")}
    S = {k: index_frames(v) for k, v in raw.items()}

    def build(names, radii, target=None):
        meta = S[names[0]][0]
        sets = [(S[n][1], r) for n, r in zip(names, radii)]
        X = [features(sets, meta, i) for i in range(len(meta))]
        Y = [S[target][1][i].ravel() for i in range(len(meta))] if target else None
        return X, Y

    X, Y = build(["BB", "CB", "KB"], [2, 1, 1], "WB")
    model = RandomForestClassifier(300, random_state=0, min_samples_leaf=2, n_jobs=-1)
    model.fit(np.concatenate(X), np.concatenate(Y))

    Xr, _ = build(["BR", "CR", "KR"], [2, 1, 1])
    br_meta, br_pix = S["BR"]
    pix = []
    for x, ref in zip(Xr, br_pix):
        p = model.predict(x).reshape(ref.shape).astype(np.int8)
        t = ref == 4
        p[t] = 4                          # keep BR's transparency exactly
        bad = (p == 4) & ~t
        p[bad] = ref[bad]
        pix.append(p)

    os.makedirs(os.path.dirname(os.path.abspath(dest)), exist_ok=True)
    with open(dest, "wb") as f:
        f.write(encode(raw["BR"], br_meta, pix))
    print("wrote", dest)


if __name__ == "__main__":
    main()
