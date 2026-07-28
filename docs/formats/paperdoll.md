# Paperdoll art (Might and Magic VI)

Status: **measured**. The pieces, their sizes, and the joins between them are
established; the drawing order and the face-to-body mapping are not. Each
claim is tagged `observed`, `inferred`, or `unknown`.

Reproduce everything below with `doll_info`.

## Where it lives

All of it is in `icons.lod`, as ordinary palette bitmaps (see
[`bitmap.md`](bitmap.md)). Nothing about the doll is in any design table
except the two `Equip X`/`Equip Y` columns of `ITEMS.TXT`. `observed`

## The doll itself

| Entry | Size | Reading |
| --- | --- | --- |
| `BACKDOLL` | 173x353 | the panel behind the doll |
| `mlabod`..`mlhbod` | 111..114 x 297..300 | eight male bodies |
| `grlabod`..`grldbod` | 110..114 x 298..299 | four female bodies |
| `mla`/`grl`…`arm1` | ~46x101 | an arm, one pose |
| `mla`/`grl`…`arm2` | ~92x98 | an arm, the other pose |
| `LEFTHAND` | 20x19 | `unknown` |
| `backhand` | 153x353 | `unknown` — panel-sized |

Twelve bodies, and they are **uniform**: every one is within two pixels of
112x298. That is what lets one pair of coordinates per item fit every
character. `observed` for the sizes; the male/female split is read off the
`ml`/`grl` stems and the art. `inferred`

The bodies are the faces. The game's twelve portraits are `MaleA`..`MaleH`
and `GirlA`..`GirlD` — eight and four — and the dolls are `mla`..`mlh` and
`grla`..`grld`: the same split by the same letters, so a face's letter names
its body. `inferred` from the names alone.

## Body armor swaps the torso

Worn body armor is not drawn from its inventory icon. Each armor's picture
stem names a three-piece overlay set — `<stem>bod`, `<stem>arm1`,
`<stem>arm2` — that replaces the bare torso and arms:

| Set | Tiers | Torso size |
| --- | --- | --- |
| `lr1`..`lr5` (leather) | 5 | 74x119 .. 104x144 |
| `chn1`..`chn5` (chain) | 5 | 89x151 .. 117x150 |
| `pl1`..`pl3` (plate) | 3 | 117..120 x 198..204 |

The join is the `ITEMS.TXT` picture column: `Chain Mail`'s picture is
`chn1icon` and the overlays are `chn1bod`, `chn1arm1`, `chn1arm2`. Body
armor's equip type is also the only doll-worn type whose `Equip X`/`Equip Y`
are zero on every row — it does not need a position because it replaces the
torso. `observed` for the names and sizes, `inferred` for the swap.

## Everything else is the item's own art at a recorded point

`ITEMS.TXT` carries `Equip X`/`Equip Y` per item. Every item of a type that
appears on the doll has them, and no item of any other type does: `observed`

| Equip type | Items | With X/Y |
| --- | ---: | ---: |
| weapon, two-handed, missile | 78 | 78 |
| shield, helm, belt, cloak | 39 | 39 |
| boots, wand | 32 | 32 |
| body armor | 17 | 0 — the overlay set instead |
| gauntlets | 5 | 0 — not drawn? `unknown` |
| ring, amulet, everything unworn | 407 | 0 |

X runs 0..578 and Y 0..289 — wider than the 173-pixel `BACKDOLL`, so the
coordinates are points on the 640x480 screen, not offsets within the panel.
Confirmed by drawing: with the panel at (451, 60) and the body 30x28 inside
it, a longsword drawn at its own coordinates lands grip-first in the doll's
raised fist, and a boot on its leg. `observed` A helm drawn the same way sits
about a panel-offset above the head, so helms anchor differently. `unknown`

## A garment is one or two layers

21 worn items' pictures end in `a` and the archive holds a `b` twin — hats,
crowns, belts and cloaks. The cloaks say what the pair is for: `cape1a` is
74x19, a collar, and `cape1b` is **144x221** — the cloth, taller than any
armor and drawn on the far side of the body. One garment, one layer in front
of the doll and one behind. `observed` for the pairs and sizes, `inferred`
for front/behind.

## Open questions

- The drawing order of body, armor, arms, and each item type. `unknown`
- Helms: their coordinates put them above the head where a weapon's put it in
  the hand. A head-relative anchor, or another origin. `unknown`
- `BODY000`..`BODY011`: twelve more entries, 68x51 up to 126x82 — body-count
  many, but head-sized. `unknown`
- Where the doll and its body sit on the 640x480 screen. `unknown`
- What `arm1` against `arm2` is — two poses, or two arms. `unknown`
- `LEFTHAND`, `backhand`, `FACEMASK`. `unknown`
- Where the `b` twin of an `a` garment is anchored: the pair shares one
  `Equip X`/`Equip Y`, and the cloak's back layer is far larger than the
  front. `unknown`
