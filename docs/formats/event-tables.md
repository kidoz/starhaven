# Event-table records (.ddm / .dlv) (Might and Magic VI)

Status: **draft, evidence-backed — gross structure only.** This documents the
first event table inside a decompressed `.ddm`/`.dlv` payload: a fixed-offset
array of 548-byte records. The per-record field layout (beyond type and name)
is `unknown` and is the subject of follow-up research. Each claim is tagged
`observed`, `inferred`, or `unknown`.

## Scope

This document covers the **first event table** found in the decompressed event
payload (see [`event-data.md`](event-data.md) for the wrapper). It documents the
table's fixed start offset, record stride, and the two verified leading fields
(type, name). It does **not** cover:

- the full per-record field layout (548 bytes, mostly unconfirmed);
- the other tables that follow (spawn points, chests, objects, etc.);
- the `.dlv`-specific indoor differences.

## Source provenance (non-expressive)

| Field | Value |
| --- | --- |
| Product | Might and Magic VI: The Mandate of Heaven (GOG.com edition) |
| Files verified | `outb2.ddm` (21 populated records), `outa1.ddm` (0 populated) |
| Record size | 548 bytes (0x224) `observed` |
| Table start | 0x798 (decompressed-payload offset) `observed` |

## First event table

In the decompressed payload, the first event table begins at **offset 0x798**.
It is a fixed-capacity array of 548-byte records; empty slots are all-zero.

| Offset | Size | Type | Field | Status | Notes |
| --- | --- | --- | --- | --- | --- |
| 0x798 + n×548 | 4 | i32 | type | observed | record type (e.g. 20 = NPC-bearing); 0 = empty |
| 0x798 + n×548 + 4 | ~12 | char[] | name | observed | NUL-padded ASCII, e.g. `"Peasant"` |
| +0x10 onward | ~536 | — | (record body) | unknown | unconfirmed fields |

### Verified observations

On `outb2.ddm`: 21 consecutive records populated, all with name `"Peasant"`,
record 0 carrying type=20 and the rest type=0. Records are exactly 548 bytes
apart (19 strides of 548 + one boundary of 552). `observed`.

On `outa1.ddm`: the table is empty (type=0, no name at the first slot) — a
nearly-empty starting map. `observed`. The fixed start offset 0x798 is identical
across both files.

## Why the full layout is deferred

The engine's runtime structs (e.g. `Events2DItem` at 48 bytes, `NPC` at 60
bytes) do **not** match the 548-byte on-disk record — the on-disk record is much
larger, embedding inline strings/arrays that the runtime stores by pointer.
This is the same runtime-vs-file discrepancy found in the ODM model facets.
Confirming the 548-byte body reliably requires tracing the `.ddm` loader in
`MM6.exe`, which is a dedicated follow-up.

## Decoding (this slice)

1. Decompress the `.ddm`/`.dlv` payload (see `event-data.md`).
2. The first event table starts at offset 0x798.
3. Walk records at stride 548; a record is "populated" if its type field is
   nonzero OR its name field has any nonzero byte.
4. Report each populated record's type and name.

## Invalid-input behavior

The enumeration rejects, deterministically and without reading out of bounds:

- a payload shorter than 0x798 + record_size (cannot hold even one record);
- a record whose fields would read past the end of the payload (stops early).

## Open questions (next slice)

- The 548-byte record body layout (fields beyond type/name). Needs the `MM6.exe`
  `.ddm`-loader trace.
- The other tables in the payload (the sparser regions at 0x440C+, the flag
  array at 0xB700+).
- The total capacity of this table (how many slots).
- `.dlv` (indoor) event-table differences.
