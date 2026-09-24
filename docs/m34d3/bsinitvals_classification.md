# d11ac1bsinitvals42 — machine-generated classification (M3.4D3)

- source: `/lib/firmware/brcm/bcm4352-d11ac1bsinitvals42.bin`
- size: 592 bytes, sha256 `e81a645c79f55557c87f4662702d7c57599918c9b2340bc4e440ce1bdbd014da`
- data records: **73** (terminator at index 73)
- status: **ANALYSIS ONLY** (no hardware access)

## Summary

| kind | count |
|---|---|
| direct | 5 |
| obj_sel | 34 |
| obj_data | 34 |

| category | count |
|---|---|
| IHR | 5 |
| OBJ | 68 |

| side-effect class | count |
|---|---|
| SHM state (config) | 34 |
| object-memory selector | 34 |
| timing/IFS configuration | 4 |
| unknown side effect | 1 |
| **TOTAL** | **73** |

## Full per-record classification

| # | offset | w | value | kind | space | target | category | ctx | side effect | conf |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0x0686 | 2 | 0x000009d0 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 1 | 0x0680 | 2 | 0x00003e3e | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 2 | 0x0682 | 2 | 0x0000023e | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 3 | 0x0700 | 2 | 0x0000003c | direct | IHR/NAV | IHR/NAV | IHR | UNKNOWN | unknown side effect | C2 (offset/value); region C3; field UNKNOWN |
| 4 | 0x0684 | 2 | 0x00000212 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 5 | 0x0160 | 4 | 0x00010003 | obj_sel | SHM | SHM window, base byte offset 0x000c, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 6 | 0x0164 | 2 | 0x000000c0 | obj_data | SHM | SHM byte offset 0x000c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 7 | 0x0160 | 4 | 0x00010003 | obj_sel | SHM | SHM window, base byte offset 0x000c, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 8 | 0x0166 | 2 | 0x0000000a | obj_data | SHM | SHM byte offset 0x000c (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 9 | 0x0160 | 4 | 0x00010004 | obj_sel | SHM | SHM window, base byte offset 0x0010, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 10 | 0x0164 | 2 | 0x00000014 | obj_data | SHM | SHM byte offset 0x0010 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 11 | 0x0160 | 4 | 0x00010007 | obj_sel | SHM | SHM window, base byte offset 0x001c, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 12 | 0x0164 | 2 | 0x00000183 | obj_data | SHM | SHM byte offset 0x001c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 13 | 0x0160 | 4 | 0x00010025 | obj_sel | SHM | SHM window, base byte offset 0x0094, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 14 | 0x0164 | 2 | 0x000001f4 | obj_data | SHM | SHM byte offset 0x0094 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 15 | 0x0160 | 4 | 0x000105f4 | obj_sel | SHM | SHM window, base byte offset 0x17d0, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 16 | 0x0166 | 2 | 0x0000042b | obj_data | SHM | SHM byte offset 0x17d0 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 17 | 0x0160 | 4 | 0x000105f5 | obj_sel | SHM | SHM window, base byte offset 0x17d4, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 18 | 0x0164 | 2 | 0x00000100 | obj_data | SHM | SHM byte offset 0x17d4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 19 | 0x0160 | 4 | 0x00010264 | obj_sel | SHM | SHM window, base byte offset 0x0990, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 20 | 0x0166 | 2 | 0x0000003c | obj_data | SHM | SHM byte offset 0x0990 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 21 | 0x0160 | 4 | 0x00010267 | obj_sel | SHM | SHM window, base byte offset 0x099c, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 22 | 0x0164 | 2 | 0x00000054 | obj_data | SHM | SHM byte offset 0x099c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 23 | 0x0160 | 4 | 0x00010267 | obj_sel | SHM | SHM window, base byte offset 0x099c, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 24 | 0x0166 | 2 | 0x00000044 | obj_data | SHM | SHM byte offset 0x099c (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 25 | 0x0160 | 4 | 0x00010268 | obj_sel | SHM | SHM window, base byte offset 0x09a0, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 26 | 0x0164 | 2 | 0x00000014 | obj_data | SHM | SHM byte offset 0x09a0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 27 | 0x0160 | 4 | 0x00010268 | obj_sel | SHM | SHM window, base byte offset 0x09a0, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 28 | 0x0166 | 2 | 0x000001cf | obj_data | SHM | SHM byte offset 0x09a0 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 29 | 0x0160 | 4 | 0x00010269 | obj_sel | SHM | SHM window, base byte offset 0x09a4, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 30 | 0x0164 | 2 | 0x00000002 | obj_data | SHM | SHM byte offset 0x09a4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 31 | 0x0160 | 4 | 0x00010269 | obj_sel | SHM | SHM window, base byte offset 0x09a4, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 32 | 0x0166 | 2 | 0x00000034 | obj_data | SHM | SHM byte offset 0x09a4 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 33 | 0x0160 | 4 | 0x0001026c | obj_sel | SHM | SHM window, base byte offset 0x09b0, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 34 | 0x0164 | 2 | 0x00000044 | obj_data | SHM | SHM byte offset 0x09b0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 35 | 0x0160 | 4 | 0x0001026c | obj_sel | SHM | SHM window, base byte offset 0x09b0, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 36 | 0x0166 | 2 | 0x0000003c | obj_data | SHM | SHM byte offset 0x09b0 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 37 | 0x0160 | 4 | 0x0001026e | obj_sel | SHM | SHM window, base byte offset 0x09b8, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 38 | 0x0166 | 2 | 0x00000030 | obj_data | SHM | SHM byte offset 0x09b8 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 39 | 0x0160 | 4 | 0x00010271 | obj_sel | SHM | SHM window, base byte offset 0x09c4, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 40 | 0x0164 | 2 | 0x0000003c | obj_data | SHM | SHM byte offset 0x09c4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 41 | 0x0160 | 4 | 0x00010271 | obj_sel | SHM | SHM window, base byte offset 0x09c4, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 42 | 0x0166 | 2 | 0x00000034 | obj_data | SHM | SHM byte offset 0x09c4 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 43 | 0x0160 | 4 | 0x00010273 | obj_sel | SHM | SHM window, base byte offset 0x09cc, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 44 | 0x0166 | 2 | 0x0000002c | obj_data | SHM | SHM byte offset 0x09cc (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 45 | 0x0160 | 4 | 0x00010276 | obj_sel | SHM | SHM window, base byte offset 0x09d8, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 46 | 0x0164 | 2 | 0x00000034 | obj_data | SHM | SHM byte offset 0x09d8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 47 | 0x0160 | 4 | 0x00010276 | obj_sel | SHM | SHM window, base byte offset 0x09d8, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 48 | 0x0166 | 2 | 0x00000030 | obj_data | SHM | SHM byte offset 0x09d8 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 49 | 0x0160 | 4 | 0x00010278 | obj_sel | SHM | SHM window, base byte offset 0x09e0, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 50 | 0x0166 | 2 | 0x0000002c | obj_data | SHM | SHM byte offset 0x09e0 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 51 | 0x0160 | 4 | 0x0001027b | obj_sel | SHM | SHM window, base byte offset 0x09ec, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 52 | 0x0164 | 2 | 0x00000030 | obj_data | SHM | SHM byte offset 0x09ec (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 53 | 0x0160 | 4 | 0x0001027b | obj_sel | SHM | SHM window, base byte offset 0x09ec, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 54 | 0x0166 | 2 | 0x0000002c | obj_data | SHM | SHM byte offset 0x09ec (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 55 | 0x0160 | 4 | 0x0001027d | obj_sel | SHM | SHM window, base byte offset 0x09f4, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 56 | 0x0166 | 2 | 0x00000028 | obj_data | SHM | SHM byte offset 0x09f4 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 57 | 0x0160 | 4 | 0x00010280 | obj_sel | SHM | SHM window, base byte offset 0x0a00, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 58 | 0x0164 | 2 | 0x0000002c | obj_data | SHM | SHM byte offset 0x0a00 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 59 | 0x0160 | 4 | 0x00010280 | obj_sel | SHM | SHM window, base byte offset 0x0a00, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 60 | 0x0166 | 2 | 0x0000002c | obj_data | SHM | SHM byte offset 0x0a00 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 61 | 0x0160 | 4 | 0x00010282 | obj_sel | SHM | SHM window, base byte offset 0x0a08, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 62 | 0x0166 | 2 | 0x00000028 | obj_data | SHM | SHM byte offset 0x0a08 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 63 | 0x0160 | 4 | 0x00010285 | obj_sel | SHM | SHM window, base byte offset 0x0a14, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 64 | 0x0164 | 2 | 0x0000002c | obj_data | SHM | SHM byte offset 0x0a14 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 65 | 0x0160 | 4 | 0x00010285 | obj_sel | SHM | SHM window, base byte offset 0x0a14, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 66 | 0x0166 | 2 | 0x00000028 | obj_data | SHM | SHM byte offset 0x0a14 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 67 | 0x0160 | 4 | 0x00010287 | obj_sel | SHM | SHM window, base byte offset 0x0a1c, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 68 | 0x0166 | 2 | 0x00000028 | obj_data | SHM | SHM byte offset 0x0a1c (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |
| 69 | 0x0160 | 4 | 0x0001028a | obj_sel | SHM | SHM window, base byte offset 0x0a28, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 70 | 0x0164 | 2 | 0x0000002c | obj_data | SHM | SHM byte offset 0x0a28 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 71 | 0x0160 | 4 | 0x0001028a | obj_sel | SHM | SHM window, base byte offset 0x0a28, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 72 | 0x0166 | 2 | 0x00000028 | obj_data | SHM | SHM byte offset 0x0a28 (high half) | OBJ | OBJDATA+2 | SHM state (config) | C2/C3 |

## Logical indirect transactions and direct writes

| tx | kind | selector/offset | space | byte target | writes | value(s) |
|---|---|---|---|---|---|---|
| 0 | direct | 0x0686 | IHR/IFS | 0x0686 | 1 | 0x000009d0 |
| 1 | direct | 0x0680 | IHR/IFS | 0x0680 | 1 | 0x00003e3e |
| 2 | direct | 0x0682 | IHR/IFS | 0x0682 | 1 | 0x0000023e |
| 3 | direct | 0x0700 | IHR/NAV | 0x0700 | 1 | 0x0000003c |
| 4 | direct | 0x0684 | IHR/IFS | 0x0684 | 1 | 0x00000212 |
| 5 | indirect | 0x00010003 | SHM | 0x000c | 1 | low@0x000c=0x00c0 |
| 7 | indirect | 0x00010003 | SHM | 0x000c | 1 | high@0x000e=0x000a |
| 9 | indirect | 0x00010004 | SHM | 0x0010 | 1 | low@0x0010=0x0014 |
| 11 | indirect | 0x00010007 | SHM | 0x001c | 1 | low@0x001c=0x0183 |
| 13 | indirect | 0x00010025 | SHM | 0x0094 | 1 | low@0x0094=0x01f4 |
| 15 | indirect | 0x000105f4 | SHM | 0x17d0 | 1 | high@0x17d2=0x042b |
| 17 | indirect | 0x000105f5 | SHM | 0x17d4 | 1 | low@0x17d4=0x0100 |
| 19 | indirect | 0x00010264 | SHM | 0x0990 | 1 | high@0x0992=0x003c |
| 21 | indirect | 0x00010267 | SHM | 0x099c | 1 | low@0x099c=0x0054 |
| 23 | indirect | 0x00010267 | SHM | 0x099c | 1 | high@0x099e=0x0044 |
| 25 | indirect | 0x00010268 | SHM | 0x09a0 | 1 | low@0x09a0=0x0014 |
| 27 | indirect | 0x00010268 | SHM | 0x09a0 | 1 | high@0x09a2=0x01cf |
| 29 | indirect | 0x00010269 | SHM | 0x09a4 | 1 | low@0x09a4=0x0002 |
| 31 | indirect | 0x00010269 | SHM | 0x09a4 | 1 | high@0x09a6=0x0034 |
| 33 | indirect | 0x0001026c | SHM | 0x09b0 | 1 | low@0x09b0=0x0044 |
| 35 | indirect | 0x0001026c | SHM | 0x09b0 | 1 | high@0x09b2=0x003c |
| 37 | indirect | 0x0001026e | SHM | 0x09b8 | 1 | high@0x09ba=0x0030 |
| 39 | indirect | 0x00010271 | SHM | 0x09c4 | 1 | low@0x09c4=0x003c |
| 41 | indirect | 0x00010271 | SHM | 0x09c4 | 1 | high@0x09c6=0x0034 |
| 43 | indirect | 0x00010273 | SHM | 0x09cc | 1 | high@0x09ce=0x002c |
| 45 | indirect | 0x00010276 | SHM | 0x09d8 | 1 | low@0x09d8=0x0034 |
| 47 | indirect | 0x00010276 | SHM | 0x09d8 | 1 | high@0x09da=0x0030 |
| 49 | indirect | 0x00010278 | SHM | 0x09e0 | 1 | high@0x09e2=0x002c |
| 51 | indirect | 0x0001027b | SHM | 0x09ec | 1 | low@0x09ec=0x0030 |
| 53 | indirect | 0x0001027b | SHM | 0x09ec | 1 | high@0x09ee=0x002c |
| 55 | indirect | 0x0001027d | SHM | 0x09f4 | 1 | high@0x09f6=0x0028 |
| 57 | indirect | 0x00010280 | SHM | 0x0a00 | 1 | low@0x0a00=0x002c |
| 59 | indirect | 0x00010280 | SHM | 0x0a00 | 1 | high@0x0a02=0x002c |
| 61 | indirect | 0x00010282 | SHM | 0x0a08 | 1 | high@0x0a0a=0x0028 |
| 63 | indirect | 0x00010285 | SHM | 0x0a14 | 1 | low@0x0a14=0x002c |
| 65 | indirect | 0x00010285 | SHM | 0x0a14 | 1 | high@0x0a16=0x0028 |
| 67 | indirect | 0x00010287 | SHM | 0x0a1c | 1 | high@0x0a1e=0x0028 |
| 69 | indirect | 0x0001028a | SHM | 0x0a28 | 1 | low@0x0a28=0x002c |
| 71 | indirect | 0x0001028a | SHM | 0x0a28 | 1 | high@0x0a2a=0x0028 |

## Common-initvals vs band-switch override

- shared logical SHM targets: **3**
- overrides (band-switch value differs): **3**
- shared direct offsets: **none**

| space | byte target | common values | band-switch values | override |
|---|---|---|---|---|
| SHM | 0x0010 | 0x00000000 | 0x00000014 | yes |
| SHM | 0x001c | 0x00640000 | 0x00000183 | yes |
| SHM | 0x0094 | 0x00320000 | 0x000001f4 | yes |
