# The engine's trigonometry, and its fixed point

## `0x55e5d0`, the most-referenced runtime table

It is read from **290 sites**, more than any other runtime table in MM6.exe,
and it had no name. It has one now, and it is the dull answer: **it is the
sine table.**

Every one of the 290 is the same instruction — `mov reg, dword [reg*4 +
0x55e5d0]` — and **not one site writes it**. A scan of every four-byte
literal in `.text` pointing anywhere into `0x55e5c0..0x55f640` finds only the
table's own address and four constants just past its end; whatever fills the
table computes its base some other way. `observed`

## What the reading looks like

`0x402850` shows it whole. Given an angle, it folds and then looks up:

```
0x0040285a  cmp eax, ecx                    ; past the fold point?
0x0040285e  mov edx, dword [0x55f618]       ; the half circle
0x00402864  sub edx, eax                    ; angle = half - angle
0x00402868  cmp eax, dword [0x55f614]       ; the quarter circle
0x0040286e  jge 0x402879
0x00402870  mov eax, dword [eax*4 + 0x55e5d0]   ; first quadrant, read straight
0x00402879  sub ecx, eax                        ; otherwise mirror ...
0x0040287b  mov eax, dword [ecx*4 + 0x55e5d0]
0x00402882  neg eax                             ; ... and negate
```

That is quadrant folding around a quarter and a half, which is what a sine
table is for and nothing else is. `observed`

The value that comes out goes straight into `0x4453c0`:

```
0x004453cf  imul dword [ebp - 4]
0x004453d2  shrd eax, edx, 0x10
```

— a 64-bit multiply taken back down by sixteen bits. So **the engine's fixed
point is 16.16**, and the table's entries are sines in it. `observed`

## The four constants, and the table's length

`0x55f610`, `0x55f614`, `0x55f618` and `0x55f61c` sit immediately past the
table and are read 143, 176, 153 and 126 times. They are uninitialised in the
image like the table itself. The fold above uses `0x55f614` as the point
below which the index reads straight and `0x55f618` as the point it subtracts
from, so those two are **the quarter circle and the half**. `observed` for
their roles.

The table runs from `0x55e5d0` to `0x55f610` — `0x1040` bytes, **1040
dwords**. The script scale is 2048 to a turn (see `map_script.hpp`), which
puts the quarter at 512 and the half at 1024; a table covering `0` through
`1024` inclusive is 1025 entries, and 1040 is that rounded up. The fit is
exact but it is a fit, so: `inferred`.

## Why this was still worth the gamble

The stated risk was that the most-referenced table in the executable would
turn out to be trigonometry with no game meaning, and it did. What it settles
anyway is the arithmetic every moving thing in the game runs on — the angle
scale, the quarter and half constants by name, the 16.16 convention and the
routine that applies it. The actor's facing at `+0x90`, its velocity triple at
`+0x84`, and the party's angles at `+0x034` are all on that arithmetic, and
none of them could be reproduced exactly without it.
