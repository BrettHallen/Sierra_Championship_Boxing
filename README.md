# Sierra Championship Boxing
Analysis of the original IBM PC version of the game, and a cross-platform remake engine that runs on the player's own copy of the game files.

| Folder | What is there |
|---|---|
| `docs/ANALYSIS.md` | The files, the 1987 loader, and the graphics formats |
| `docs/GAMEPLAY.md` | Reverse-engineered game logic: ratings, corners, the round itself (hits, knock-downs, the count), energy, injuries, judges, animation |
| `tools/` | `extract_graphics.py` (PNG extractor) and `re/` (disassembler and pseudo-C lifter) |
| `Remake/` | The C + SDL3 remake engine. See `Remake/README.md` |
