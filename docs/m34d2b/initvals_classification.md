# d11ac1initvals42 (common) — machine-generated record classification

- source: `/lib/firmware/brcm/bcm4352-d11ac1initvals42.bin`
- size: 4888 bytes, sha256 `b5a2735d89aab8d230297779004c2b989f6dc4e7c2e19902a173efea5983d938`
- data records: **610** (terminator at index 610)
- status: **ANALYSIS ONLY** (no hardware access)

## Summary by kind

| kind | count |
|---|---|
| direct | 194 |
| obj_sel | 76 |
| obj_data | 340 |

## Summary by category

| category | count |
|---|---|
| IHR | 80 |
| MACINT | 3 |
| MAC_CORE | 1 |
| OBJ | 416 |
| SHM | 33 |
| TEMPLATE | 77 |

## Summary by target space/region

| space/region | count |
|---|---|
| IHR/IFS | 12 |
| IHR/PSM | 20 |
| IHR/RXE | 12 |
| IHR/TSF | 4 |
| IHR/TXE0 | 12 |
| IHR/TXE1 | 20 |
| MAC core | 1 |
| MACINTSTATUS/MASK | 2 |
| SCR | 40 |
| SHM | 376 |
| SHM (direct window) | 33 |
| TX template access | 77 |
| intrcvlazy[0..3] | 1 |

## Summary by side-effect class

| side effect | count |
|---|---|
| FIFO configuration | 44 |
| MAC control (command) | 1 |
| PSM configuration | 20 |
| PSM scratch config | 20 |
| SHM state (config) | 353 |
| interrupt control | 2 |
| object-memory selector | 76 |
| status clear | 1 |
| template/object-memory | 77 |
| timer/TSF | 4 |
| timing/IFS configuration | 12 |
| **TOTAL** | **610** |

## Full per-record classification

| # | offset | w | value | kind | space | target | category | name | side effect | conf |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0x0160 | 4 | 0x03010005 | obj_sel | SHM | SHM window, base byte offset 0x0014, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 1 | 0x0164 | 4 | 0x002a0000 | obj_data | SHM | SHM byte offset 0x0014 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 2 | 0x0124 | 4 | 0x00000004 | direct | MAC core | MAC core | MAC_CORE | MACCOMMAND | MAC control (command) | C2/C3 |
| 3 | 0x0128 | 4 | 0x00000000 | direct | MACINTSTATUS/MASK | MACINTSTATUS/MASK | MACINT | MACINTSTATUS | status clear | C2/C3 |
| 4 | 0x012c | 4 | 0x00000000 | direct | MACINTSTATUS/MASK | MACINTSTATUS/MASK | MACINT | MACINTMASK | interrupt control | C2/C3 |
| 5 | 0x0130 | 4 | 0x00000000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRPTR | template/object-memory | C2/C3 |
| 6 | 0x0134 | 4 | 0x00000094 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 7 | 0x0134 | 4 | 0x75749000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 8 | 0x0134 | 4 | 0x00007776 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 9 | 0x0134 | 4 | 0x00000000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 10 | 0x0134 | 4 | 0xffff0005 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 11 | 0x0134 | 4 | 0x0000ffff | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 12 | 0x0130 | 4 | 0x0000002c | direct | TX template access | TX template access | TEMPLATE | TPLATEWRPTR | template/object-memory | C2/C3 |
| 13 | 0x0134 | 4 | 0x00e0040a | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 14 | 0x0134 | 4 | 0x0048beef | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 15 | 0x0134 | 4 | 0xff000005 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 16 | 0x0134 | 4 | 0xff02ff01 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 17 | 0x0134 | 4 | 0x01181000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 18 | 0x0134 | 4 | 0x10000302 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 19 | 0x0134 | 4 | 0xf3f2f118 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 20 | 0x0134 | 4 | 0x0000ccbb | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 21 | 0x0130 | 4 | 0x00000058 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRPTR | template/object-memory | C2/C3 |
| 22 | 0x0134 | 4 | 0x00000000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 23 | 0x0130 | 4 | 0x00000084 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRPTR | template/object-memory | C2/C3 |
| 24 | 0x0134 | 4 | 0x00000000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 25 | 0x0130 | 4 | 0x00000700 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRPTR | template/object-memory | C2/C3 |
| 26 | 0x0134 | 4 | 0x0033846e | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 27 | 0x0134 | 4 | 0x0050badc | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 28 | 0x0134 | 4 | 0xab0000d4 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 29 | 0x0134 | 4 | 0xdabadaba | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 30 | 0x0134 | 4 | 0xf1181000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 31 | 0x0134 | 4 | 0x1000f3f2 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 32 | 0x0134 | 4 | 0xf3f2f118 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 33 | 0x0134 | 4 | 0x00000010 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 34 | 0x0134 | 4 | 0x00000000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 35 | 0x0134 | 4 | 0x000a0000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 36 | 0x0134 | 4 | 0x0e000001 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 37 | 0x0134 | 4 | 0x4d435242 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 38 | 0x0134 | 4 | 0x5345545f | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 39 | 0x0134 | 4 | 0x53535f54 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 40 | 0x0134 | 4 | 0x04014449 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 41 | 0x0134 | 4 | 0x968b8482 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 42 | 0x0134 | 4 | 0x06010103 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 43 | 0x0134 | 4 | 0x00000002 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 44 | 0x0130 | 4 | 0x00000200 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRPTR | template/object-memory | C2/C3 |
| 45 | 0x0134 | 4 | 0x0228040a | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 46 | 0x0134 | 4 | 0x0080badc | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 47 | 0x0134 | 4 | 0xffff0000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 48 | 0x0134 | 4 | 0xffffffff | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 49 | 0x0134 | 4 | 0xf1181000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 50 | 0x0134 | 4 | 0x1000f3f2 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 51 | 0x0134 | 4 | 0xf3f2f118 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 52 | 0x0134 | 4 | 0x0000afd0 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 53 | 0x0134 | 4 | 0x00000000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 54 | 0x0134 | 4 | 0x01000000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 55 | 0x0134 | 4 | 0x0e000002 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 56 | 0x0134 | 4 | 0x4d435242 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 57 | 0x0134 | 4 | 0x5345545f | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 58 | 0x0134 | 4 | 0x53535f54 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 59 | 0x0134 | 4 | 0x04014449 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 60 | 0x0134 | 4 | 0x968b8482 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 61 | 0x0134 | 4 | 0x06010103 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 62 | 0x0134 | 4 | 0x00000102 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 63 | 0x0130 | 4 | 0x00000480 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRPTR | template/object-memory | C2/C3 |
| 64 | 0x0134 | 4 | 0x0228040a | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 65 | 0x0134 | 4 | 0x0080badc | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 66 | 0x0134 | 4 | 0xffff0000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 67 | 0x0134 | 4 | 0xffffffff | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 68 | 0x0134 | 4 | 0xf1181000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 69 | 0x0134 | 4 | 0x1000f3f2 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 70 | 0x0134 | 4 | 0xf3f2f118 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 71 | 0x0134 | 4 | 0x0000afd0 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 72 | 0x0134 | 4 | 0x00000000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 73 | 0x0134 | 4 | 0x01000000 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 74 | 0x0134 | 4 | 0x0e000002 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 75 | 0x0134 | 4 | 0x4d435242 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 76 | 0x0134 | 4 | 0x5345545f | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 77 | 0x0134 | 4 | 0x53535f54 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 78 | 0x0134 | 4 | 0x04014449 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 79 | 0x0134 | 4 | 0x968b8482 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 80 | 0x0134 | 4 | 0x06010103 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 81 | 0x0134 | 4 | 0x00000102 | direct | TX template access | TX template access | TEMPLATE | TPLATEWRDATA | template/object-memory | C2/C3 |
| 82 | 0x0100 | 4 | 0x01000000 | direct | intrcvlazy[0..3] | intrcvlazy[0..3] | MACINT | INTRCVLAZY0 | interrupt control | C2/C3 |
| 83 | 0x0490 | 2 | 0x00000000 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 84 | 0x04a0 | 2 | 0x0000f3f1 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 85 | 0x04b0 | 2 | 0x0000fdef | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 86 | 0x04a8 | 2 | 0x0000ffff | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 87 | 0x04a8 | 2 | 0x00000000 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 88 | 0x04b8 | 2 | 0x00000000 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 89 | 0x04a2 | 2 | 0x00000003 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 90 | 0x04b2 | 2 | 0x0000ffff | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 91 | 0x04aa | 2 | 0x0000ffff | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 92 | 0x04aa | 2 | 0x00000000 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 93 | 0x04a4 | 2 | 0x00001acf | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 94 | 0x04ac | 2 | 0x00000000 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 95 | 0x04bc | 2 | 0x00000000 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 96 | 0x04a6 | 2 | 0x000004d7 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 97 | 0x04b6 | 2 | 0x0000ffff | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 98 | 0x04ae | 2 | 0x0000ffff | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 99 | 0x0406 | 2 | 0x00000001 | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 100 | 0x0406 | 2 | 0x00000000 | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 101 | 0x040c | 2 | 0x0000001c | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 102 | 0x0406 | 2 | 0x00000101 | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 103 | 0x0406 | 2 | 0x00000100 | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 104 | 0x040c | 2 | 0x0000001c | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 105 | 0x0406 | 2 | 0x00000000 | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 106 | 0x08c0 | 2 | 0x00000001 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 107 | 0x0402 | 2 | 0x000007d4 | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 108 | 0x0502 | 2 | 0x00000060 | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 109 | 0x0500 | 2 | 0x00004000 | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 110 | 0x0502 | 2 | 0x00000064 | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 111 | 0x0500 | 2 | 0x00004000 | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 112 | 0x0502 | 2 | 0x00000068 | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 113 | 0x0500 | 2 | 0x00004000 | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 114 | 0x0502 | 2 | 0x0000006c | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 115 | 0x0500 | 2 | 0x00004000 | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 116 | 0x0502 | 2 | 0x00000060 | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 117 | 0x0a40 | 2 | 0x00000001 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 118 | 0x0a00 | 2 | 0x00000000 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 119 | 0x0504 | 2 | 0x00000024 | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 120 | 0x0580 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 121 | 0x0582 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 122 | 0x0584 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 123 | 0x0586 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 124 | 0x0588 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 125 | 0x058a | 2 | 0x00000000 | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 126 | 0x058c | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 127 | 0x058e | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 128 | 0x0590 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 129 | 0x0592 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 130 | 0x0594 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 131 | 0x0596 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 132 | 0x0598 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 133 | 0x059a | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 134 | 0x059c | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 135 | 0x059e | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 136 | 0x05a0 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 137 | 0x05a2 | 2 | 0x0000fff0 | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 138 | 0x05a4 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 139 | 0x05a6 | 2 | 0x0000ffff | direct | IHR/TXE1 | IHR/TXE1 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 140 | 0x0800 | 2 | 0x00000002 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 141 | 0x0800 | 2 | 0x00000012 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 142 | 0x0800 | 2 | 0x00000022 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 143 | 0x0800 | 2 | 0x00000032 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 144 | 0x0816 | 2 | 0x0000103f | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 145 | 0x0804 | 2 | 0x00000040 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 146 | 0x0812 | 2 | 0x0000ffff | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 147 | 0x0814 | 2 | 0x00000000 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 148 | 0x0818 | 2 | 0x00000000 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 149 | 0x0612 | 2 | 0x00000001 | direct | IHR/TSF | IHR/TSF | IHR | UNKNOWN | timer/TSF | C2 (offset/value); region C3; field UNKNOWN |
| 150 | 0x062e | 2 | 0x00006662 | direct | IHR/TSF | IHR/TSF | IHR | UNKNOWN | timer/TSF | C2 (offset/value); region C3; field UNKNOWN |
| 151 | 0x0630 | 2 | 0x00000006 | direct | IHR/TSF | IHR/TSF | IHR | UNKNOWN | timer/TSF | C2 (offset/value); region C3; field UNKNOWN |
| 152 | 0x0600 | 2 | 0x00008004 | direct | IHR/TSF | IHR/TSF | IHR | UNKNOWN | timer/TSF | C2 (offset/value); region C3; field UNKNOWN |
| 153 | 0x0696 | 2 | 0x00000008 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 154 | 0x069a | 2 | 0x000000e4 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 155 | 0x0688 | 2 | 0x00000000 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 156 | 0x069c | 2 | 0x00000002 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 157 | 0x0688 | 2 | 0x00001000 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 158 | 0x069c | 2 | 0x00000002 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 159 | 0x0688 | 2 | 0x00002000 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 160 | 0x069c | 2 | 0x00000002 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 161 | 0x0688 | 2 | 0x00003000 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 162 | 0x069c | 2 | 0x00000002 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 163 | 0x0688 | 2 | 0x00000f0b | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 164 | 0x069e | 2 | 0x00000007 | direct | IHR/IFS | IHR/IFS | IHR | UNKNOWN | timing/IFS configuration | C2 (offset/value); region C3; field UNKNOWN |
| 165 | 0x0510 | 2 | 0x0000000b | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 166 | 0x0428 | 2 | 0x00000100 | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 167 | 0x0450 | 2 | 0x00004e21 | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 168 | 0x0452 | 2 | 0x0000015b | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 169 | 0x04e4 | 2 | 0x00000090 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 170 | 0x0404 | 2 | 0x000000b4 | direct | IHR/RXE | IHR/RXE | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 171 | 0x0554 | 2 | 0x0000afff | direct | IHR/TXE0 | IHR/TXE0 | IHR | UNKNOWN | FIFO configuration | C2 (offset/value); region C3; field UNKNOWN |
| 172 | 0x0494 | 2 | 0x00000000 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 173 | 0x0496 | 2 | 0x00000001 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 174 | 0x0498 | 2 | 0x00000000 | direct | IHR/PSM | IHR/PSM | IHR | UNKNOWN | PSM configuration | C2 (offset/value); region C3; field UNKNOWN |
| 175 | 0x08cc | 2 | 0x00000005 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 176 | 0x08ce | 2 | 0x00000040 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 177 | 0x08e4 | 2 | 0x00003f00 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 178 | 0x08ec | 2 | 0x00004004 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 179 | 0x08ee | 2 | 0x0000ffff | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 180 | 0x08ec | 2 | 0x00004005 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 181 | 0x08ee | 2 | 0x0000ffff | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 182 | 0x08ec | 2 | 0x00004006 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 183 | 0x08ee | 2 | 0x0000ffff | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 184 | 0x08ec | 2 | 0x00004008 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 185 | 0x08ee | 2 | 0x0000ffff | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 186 | 0x08ec | 2 | 0x00004009 | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 187 | 0x08ee | 2 | 0x0000ffff | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 188 | 0x08ec | 2 | 0x0000400a | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 189 | 0x08ee | 2 | 0x0000ffff | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 190 | 0x08ec | 2 | 0x0000400c | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 191 | 0x08ee | 2 | 0x0000ffff | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 192 | 0x08ec | 2 | 0x0000400d | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 193 | 0x08ee | 2 | 0x0000ffff | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 194 | 0x08ec | 2 | 0x0000400e | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 195 | 0x08ee | 2 | 0x0000ffff | direct | SHM (direct window) | SHM (direct window) | SHM | UNKNOWN | SHM state (config) | C2 (offset/value); region C3; field UNKNOWN |
| 196 | 0x0160 | 4 | 0x03010004 | obj_sel | SHM | SHM window, base byte offset 0x0010, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 197 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x0010 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 198 | 0x0164 | 4 | 0x000000b4 | obj_data | SHM | SHM byte offset 0x0014 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 199 | 0x0164 | 4 | 0x00470047 | obj_data | SHM | SHM byte offset 0x0018 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 200 | 0x0164 | 4 | 0x00640000 | obj_data | SHM | SHM byte offset 0x001c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 201 | 0x0164 | 4 | 0x01000930 | obj_data | SHM | SHM byte offset 0x0020 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 202 | 0x0160 | 4 | 0x0301000c | obj_sel | SHM | SHM window, base byte offset 0x0030, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 203 | 0x0164 | 4 | 0x07ff0000 | obj_data | SHM | SHM byte offset 0x0030 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 204 | 0x0164 | 4 | 0x00020002 | obj_data | SHM | SHM byte offset 0x0034 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 205 | 0x0164 | 4 | 0x02800001 | obj_data | SHM | SHM byte offset 0x0038 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 206 | 0x0164 | 4 | 0x00000005 | obj_data | SHM | SHM byte offset 0x003c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 207 | 0x0164 | 4 | 0x02800000 | obj_data | SHM | SHM byte offset 0x0040 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 208 | 0x0164 | 4 | 0x00640064 | obj_data | SHM | SHM byte offset 0x0044 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 209 | 0x0164 | 4 | 0x0047000e | obj_data | SHM | SHM byte offset 0x0048 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 210 | 0x0164 | 4 | 0x00000500 | obj_data | SHM | SHM byte offset 0x004c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 211 | 0x0160 | 4 | 0x03010015 | obj_sel | SHM | SHM window, base byte offset 0x0054, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 212 | 0x0164 | 4 | 0x087a0000 | obj_data | SHM | SHM byte offset 0x0054 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 213 | 0x0164 | 4 | 0x0f0f0000 | obj_data | SHM | SHM byte offset 0x0058 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 214 | 0x0164 | 4 | 0x0000000a | obj_data | SHM | SHM byte offset 0x005c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 215 | 0x0160 | 4 | 0x0301001a | obj_sel | SHM | SHM window, base byte offset 0x0068, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 216 | 0x0164 | 4 | 0x0000c000 | obj_data | SHM | SHM byte offset 0x0068 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 217 | 0x0160 | 4 | 0x0301001d | obj_sel | SHM | SHM window, base byte offset 0x0074, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 218 | 0x0164 | 4 | 0x00482710 | obj_data | SHM | SHM byte offset 0x0074 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 219 | 0x0164 | 4 | 0x0b960000 | obj_data | SHM | SHM byte offset 0x0078 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 220 | 0x0160 | 4 | 0x03010020 | obj_sel | SHM | SHM window, base byte offset 0x0080, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 221 | 0x0164 | 4 | 0x27100006 | obj_data | SHM | SHM byte offset 0x0080 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 222 | 0x0160 | 4 | 0x03010024 | obj_sel | SHM | SHM window, base byte offset 0x0090, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 223 | 0x0164 | 4 | 0x0acc0bfa | obj_data | SHM | SHM byte offset 0x0090 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 224 | 0x0164 | 4 | 0x00320000 | obj_data | SHM | SHM byte offset 0x0094 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 225 | 0x0164 | 4 | 0x000001c4 | obj_data | SHM | SHM byte offset 0x0098 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 226 | 0x0164 | 4 | 0x079e0000 | obj_data | SHM | SHM byte offset 0x009c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 227 | 0x0164 | 4 | 0x0a8a0000 | obj_data | SHM | SHM byte offset 0x00a0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 228 | 0x0164 | 4 | 0x013f0000 | obj_data | SHM | SHM byte offset 0x00a4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 229 | 0x0164 | 4 | 0x03a0ffff | obj_data | SHM | SHM byte offset 0x00a8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 230 | 0x0164 | 4 | 0x0726054e | obj_data | SHM | SHM byte offset 0x00ac (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 231 | 0x0164 | 4 | 0x0a2a040a | obj_data | SHM | SHM byte offset 0x00b0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 232 | 0x0160 | 4 | 0x0301002e | obj_sel | SHM | SHM window, base byte offset 0x00b8, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 233 | 0x0164 | 4 | 0x80000000 | obj_data | SHM | SHM byte offset 0x00b8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 234 | 0x0160 | 4 | 0x03010033 | obj_sel | SHM | SHM window, base byte offset 0x00cc, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 235 | 0x0164 | 4 | 0x00000044 | obj_data | SHM | SHM byte offset 0x00cc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 236 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x00d0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 237 | 0x0160 | 4 | 0x03010058 | obj_sel | SHM | SHM window, base byte offset 0x0160, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 238 | 0x0164 | 4 | 0x4d435242 | obj_data | SHM | SHM byte offset 0x0160 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 239 | 0x0164 | 4 | 0x5345545f | obj_data | SHM | SHM byte offset 0x0164 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 240 | 0x0164 | 4 | 0x53535f54 | obj_data | SHM | SHM byte offset 0x0168 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 241 | 0x0164 | 4 | 0x00004449 | obj_data | SHM | SHM byte offset 0x016c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 242 | 0x0160 | 4 | 0x03010060 | obj_sel | SHM | SHM window, base byte offset 0x0180, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 243 | 0x0164 | 4 | 0x07ff0527 | obj_data | SHM | SHM byte offset 0x0180 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 244 | 0x0164 | 4 | 0x00000050 | obj_data | SHM | SHM byte offset 0x0184 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 245 | 0x0164 | 4 | 0x000000c0 | obj_data | SHM | SHM byte offset 0x0188 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 246 | 0x0160 | 4 | 0x03010070 | obj_sel | SHM | SHM window, base byte offset 0x01c0, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 247 | 0x0164 | 4 | 0x04c604c6 | obj_data | SHM | SHM byte offset 0x01c0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 248 | 0x0164 | 4 | 0x04c604c6 | obj_data | SHM | SHM byte offset 0x01c4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 249 | 0x0164 | 4 | 0x04c604c6 | obj_data | SHM | SHM byte offset 0x01c8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 250 | 0x0164 | 4 | 0x04c604c6 | obj_data | SHM | SHM byte offset 0x01cc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 251 | 0x0164 | 4 | 0x04ee0502 | obj_data | SHM | SHM byte offset 0x01d0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 252 | 0x0164 | 4 | 0x04c604da | obj_data | SHM | SHM byte offset 0x01d4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 253 | 0x0164 | 4 | 0x04f8050c | obj_data | SHM | SHM byte offset 0x01d8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 254 | 0x0164 | 4 | 0x04d004e4 | obj_data | SHM | SHM byte offset 0x01dc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 255 | 0x0164 | 4 | 0x04c604c6 | obj_data | SHM | SHM byte offset 0x01e0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 256 | 0x0164 | 4 | 0x04c604c6 | obj_data | SHM | SHM byte offset 0x01e4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 257 | 0x0164 | 4 | 0x04c604c6 | obj_data | SHM | SHM byte offset 0x01e8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 258 | 0x0164 | 4 | 0x04c604c6 | obj_data | SHM | SHM byte offset 0x01ec (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 259 | 0x0164 | 4 | 0x04ee0502 | obj_data | SHM | SHM byte offset 0x01f0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 260 | 0x0164 | 4 | 0x04c604da | obj_data | SHM | SHM byte offset 0x01f4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 261 | 0x0164 | 4 | 0x04f8050c | obj_data | SHM | SHM byte offset 0x01f8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 262 | 0x0164 | 4 | 0x04d004e4 | obj_data | SHM | SHM byte offset 0x01fc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 263 | 0x0164 | 4 | 0x05160516 | obj_data | SHM | SHM byte offset 0x0200 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 264 | 0x0164 | 4 | 0x05160516 | obj_data | SHM | SHM byte offset 0x0204 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 265 | 0x0164 | 4 | 0x05160524 | obj_data | SHM | SHM byte offset 0x0208 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 266 | 0x0164 | 4 | 0x05320516 | obj_data | SHM | SHM byte offset 0x020c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 267 | 0x0164 | 4 | 0x05160516 | obj_data | SHM | SHM byte offset 0x0210 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 268 | 0x0164 | 4 | 0x05160516 | obj_data | SHM | SHM byte offset 0x0214 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 269 | 0x0164 | 4 | 0x05160516 | obj_data | SHM | SHM byte offset 0x0218 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 270 | 0x0164 | 4 | 0x05160540 | obj_data | SHM | SHM byte offset 0x021c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 271 | 0x0164 | 4 | 0x05160516 | obj_data | SHM | SHM byte offset 0x0220 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 272 | 0x0164 | 4 | 0x05160516 | obj_data | SHM | SHM byte offset 0x0224 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 273 | 0x0164 | 4 | 0x05160524 | obj_data | SHM | SHM byte offset 0x0228 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 274 | 0x0164 | 4 | 0x05320516 | obj_data | SHM | SHM byte offset 0x022c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 275 | 0x0164 | 4 | 0x05160516 | obj_data | SHM | SHM byte offset 0x0230 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 276 | 0x0164 | 4 | 0x05160516 | obj_data | SHM | SHM byte offset 0x0234 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 277 | 0x0164 | 4 | 0x05160516 | obj_data | SHM | SHM byte offset 0x0238 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 278 | 0x0164 | 4 | 0x05160540 | obj_data | SHM | SHM byte offset 0x023c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 279 | 0x0164 | 4 | 0x001f0000 | obj_data | SHM | SHM byte offset 0x0240 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 280 | 0x0164 | 4 | 0x001f03ff | obj_data | SHM | SHM byte offset 0x0244 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 281 | 0x0164 | 4 | 0x00000002 | obj_data | SHM | SHM byte offset 0x0248 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 282 | 0x0164 | 4 | 0x00000002 | obj_data | SHM | SHM byte offset 0x024c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 283 | 0x0160 | 4 | 0x03010098 | obj_sel | SHM | SHM window, base byte offset 0x0260, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 284 | 0x0164 | 4 | 0x001f0000 | obj_data | SHM | SHM byte offset 0x0260 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 285 | 0x0164 | 4 | 0x001f03ff | obj_data | SHM | SHM byte offset 0x0264 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 286 | 0x0164 | 4 | 0x00000001 | obj_data | SHM | SHM byte offset 0x0268 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 287 | 0x0164 | 4 | 0x00000001 | obj_data | SHM | SHM byte offset 0x026c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 288 | 0x0160 | 4 | 0x030100a0 | obj_sel | SHM | SHM window, base byte offset 0x0280, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 289 | 0x0164 | 4 | 0x001f0000 | obj_data | SHM | SHM byte offset 0x0280 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 290 | 0x0164 | 4 | 0x001f03ff | obj_data | SHM | SHM byte offset 0x0284 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 291 | 0x0164 | 4 | 0x00000001 | obj_data | SHM | SHM byte offset 0x0288 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 292 | 0x0164 | 4 | 0x00000001 | obj_data | SHM | SHM byte offset 0x028c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 293 | 0x0160 | 4 | 0x030100a8 | obj_sel | SHM | SHM window, base byte offset 0x02a0, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 294 | 0x0164 | 4 | 0x001f0000 | obj_data | SHM | SHM byte offset 0x02a0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 295 | 0x0164 | 4 | 0x001f03ff | obj_data | SHM | SHM byte offset 0x02a4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 296 | 0x0164 | 4 | 0x00000001 | obj_data | SHM | SHM byte offset 0x02a8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 297 | 0x0164 | 4 | 0x00000001 | obj_data | SHM | SHM byte offset 0x02ac (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 298 | 0x0160 | 4 | 0x030100b8 | obj_sel | SHM | SHM window, base byte offset 0x02e0, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 299 | 0x0164 | 4 | 0x00001722 | obj_data | SHM | SHM byte offset 0x02e0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 300 | 0x0164 | 4 | 0x17300000 | obj_data | SHM | SHM byte offset 0x02e4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 301 | 0x0160 | 4 | 0x030100bb | obj_sel | SHM | SHM window, base byte offset 0x02ec, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 302 | 0x0164 | 4 | 0x00001734 | obj_data | SHM | SHM byte offset 0x02ec (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 303 | 0x0164 | 4 | 0x00b50000 | obj_data | SHM | SHM byte offset 0x02f0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 304 | 0x0164 | 4 | 0x002900ad | obj_data | SHM | SHM byte offset 0x02f4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 305 | 0x0164 | 4 | 0x000e0000 | obj_data | SHM | SHM byte offset 0x02f8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 306 | 0x0160 | 4 | 0x030100c0 | obj_sel | SHM | SHM window, base byte offset 0x0300, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 307 | 0x0164 | 4 | 0x24372437 | obj_data | SHM | SHM byte offset 0x0300 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 308 | 0x0164 | 4 | 0x24372437 | obj_data | SHM | SHM byte offset 0x0304 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 309 | 0x0160 | 4 | 0x03010175 | obj_sel | SHM | SHM window, base byte offset 0x05d4, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 310 | 0x0164 | 4 | 0x02070001 | obj_data | SHM | SHM byte offset 0x05d4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 311 | 0x0164 | 4 | 0x03070207 | obj_data | SHM | SHM byte offset 0x05d8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 312 | 0x0164 | 4 | 0x00000007 | obj_data | SHM | SHM byte offset 0x05dc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 313 | 0x0160 | 4 | 0x030101bc | obj_sel | SHM | SHM window, base byte offset 0x06f0, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 314 | 0x0164 | 4 | 0x00000634 | obj_data | SHM | SHM byte offset 0x06f0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 315 | 0x0164 | 4 | 0x01000000 | obj_data | SHM | SHM byte offset 0x06f4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 316 | 0x0164 | 4 | 0x000007c4 | obj_data | SHM | SHM byte offset 0x06f8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 317 | 0x0160 | 4 | 0x030101c1 | obj_sel | SHM | SHM window, base byte offset 0x0704, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 318 | 0x0164 | 4 | 0x000020d8 | obj_data | SHM | SHM byte offset 0x0704 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 319 | 0x0160 | 4 | 0x030101ca | obj_sel | SHM | SHM window, base byte offset 0x0728, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 320 | 0x0164 | 4 | 0xffffffff | obj_data | SHM | SHM byte offset 0x0728 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 321 | 0x0164 | 4 | 0xffffffff | obj_data | SHM | SHM byte offset 0x072c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 322 | 0x0164 | 4 | 0xffffffff | obj_data | SHM | SHM byte offset 0x0730 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 323 | 0x0164 | 4 | 0xffffffff | obj_data | SHM | SHM byte offset 0x0734 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 324 | 0x0164 | 4 | 0xffffffff | obj_data | SHM | SHM byte offset 0x0738 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 325 | 0x0164 | 4 | 0xffffffff | obj_data | SHM | SHM byte offset 0x073c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 326 | 0x0164 | 4 | 0xffffffff | obj_data | SHM | SHM byte offset 0x0740 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 327 | 0x0164 | 4 | 0xffffffff | obj_data | SHM | SHM byte offset 0x0744 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 328 | 0x0160 | 4 | 0x03010204 | obj_sel | SHM | SHM window, base byte offset 0x0810, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 329 | 0x0164 | 4 | 0x00000005 | obj_data | SHM | SHM byte offset 0x0810 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 330 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x0814 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 331 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x0818 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 332 | 0x0164 | 4 | 0x0003001d | obj_data | SHM | SHM byte offset 0x081c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 333 | 0x0164 | 4 | 0x147140e5 | obj_data | SHM | SHM byte offset 0x0820 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 334 | 0x0164 | 4 | 0x00008492 | obj_data | SHM | SHM byte offset 0x0824 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 335 | 0x0160 | 4 | 0x0301020d | obj_sel | SHM | SHM window, base byte offset 0x0834, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 336 | 0x0164 | 4 | 0x00000001 | obj_data | SHM | SHM byte offset 0x0834 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 337 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x0838 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 338 | 0x0164 | 4 | 0x0003001d | obj_data | SHM | SHM byte offset 0x083c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 339 | 0x0164 | 4 | 0x147140e5 | obj_data | SHM | SHM byte offset 0x0840 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 340 | 0x0164 | 4 | 0x00008492 | obj_data | SHM | SHM byte offset 0x0844 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 341 | 0x0160 | 4 | 0x03010215 | obj_sel | SHM | SHM window, base byte offset 0x0854, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 342 | 0x0164 | 4 | 0x00000002 | obj_data | SHM | SHM byte offset 0x0854 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 343 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x0858 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 344 | 0x0164 | 4 | 0x0003001d | obj_data | SHM | SHM byte offset 0x085c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 345 | 0x0164 | 4 | 0x147140e5 | obj_data | SHM | SHM byte offset 0x0860 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 346 | 0x0164 | 4 | 0x00008492 | obj_data | SHM | SHM byte offset 0x0864 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 347 | 0x0160 | 4 | 0x0301021d | obj_sel | SHM | SHM window, base byte offset 0x0874, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 348 | 0x0164 | 4 | 0x00000003 | obj_data | SHM | SHM byte offset 0x0874 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 349 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x0878 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 350 | 0x0164 | 4 | 0x0003001d | obj_data | SHM | SHM byte offset 0x087c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 351 | 0x0164 | 4 | 0x147140e5 | obj_data | SHM | SHM byte offset 0x0880 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 352 | 0x0164 | 4 | 0x00008492 | obj_data | SHM | SHM byte offset 0x0884 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 353 | 0x0160 | 4 | 0x03010225 | obj_sel | SHM | SHM window, base byte offset 0x0894, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 354 | 0x0164 | 4 | 0x00000004 | obj_data | SHM | SHM byte offset 0x0894 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 355 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x0898 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 356 | 0x0164 | 4 | 0x0003001d | obj_data | SHM | SHM byte offset 0x089c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 357 | 0x0164 | 4 | 0x147140e5 | obj_data | SHM | SHM byte offset 0x08a0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 358 | 0x0164 | 4 | 0x00008492 | obj_data | SHM | SHM byte offset 0x08a4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 359 | 0x0160 | 4 | 0x0301022d | obj_sel | SHM | SHM window, base byte offset 0x08b4, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 360 | 0x0164 | 4 | 0x00000005 | obj_data | SHM | SHM byte offset 0x08b4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 361 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x08b8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 362 | 0x0164 | 4 | 0x0003001d | obj_data | SHM | SHM byte offset 0x08bc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 363 | 0x0164 | 4 | 0x147140e5 | obj_data | SHM | SHM byte offset 0x08c0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 364 | 0x0164 | 4 | 0x00008492 | obj_data | SHM | SHM byte offset 0x08c4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 365 | 0x0160 | 4 | 0x03010235 | obj_sel | SHM | SHM window, base byte offset 0x08d4, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 366 | 0x0164 | 4 | 0x00000006 | obj_data | SHM | SHM byte offset 0x08d4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 367 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x08d8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 368 | 0x0164 | 4 | 0x0003001d | obj_data | SHM | SHM byte offset 0x08dc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 369 | 0x0164 | 4 | 0x147140e5 | obj_data | SHM | SHM byte offset 0x08e0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 370 | 0x0164 | 4 | 0x00008492 | obj_data | SHM | SHM byte offset 0x08e4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 371 | 0x0160 | 4 | 0x0301023d | obj_sel | SHM | SHM window, base byte offset 0x08f4, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 372 | 0x0164 | 4 | 0x0002ffff | obj_data | SHM | SHM byte offset 0x08f4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 373 | 0x0164 | 4 | 0x00000002 | obj_data | SHM | SHM byte offset 0x08f8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 374 | 0x0164 | 4 | 0x000040c2 | obj_data | SHM | SHM byte offset 0x08fc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 375 | 0x0164 | 4 | 0x018b000f | obj_data | SHM | SHM byte offset 0x0900 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 376 | 0x0164 | 4 | 0x0400008f | obj_data | SHM | SHM byte offset 0x0904 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 377 | 0x0164 | 4 | 0x41c20000 | obj_data | SHM | SHM byte offset 0x0908 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 378 | 0x0164 | 4 | 0x00170000 | obj_data | SHM | SHM byte offset 0x090c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 379 | 0x0164 | 4 | 0x0097024b | obj_data | SHM | SHM byte offset 0x0910 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 380 | 0x0164 | 4 | 0x00000400 | obj_data | SHM | SHM byte offset 0x0914 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 381 | 0x0164 | 4 | 0x000080c3 | obj_data | SHM | SHM byte offset 0x0918 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 382 | 0x0164 | 4 | 0x01eb0015 | obj_data | SHM | SHM byte offset 0x091c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 383 | 0x0164 | 4 | 0x50c007f6 | obj_data | SHM | SHM byte offset 0x0920 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 384 | 0x0164 | 4 | 0x81c30002 | obj_data | SHM | SHM byte offset 0x0924 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 385 | 0x0164 | 4 | 0x00250000 | obj_data | SHM | SHM byte offset 0x0928 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 386 | 0x0164 | 4 | 0x0bf602ab | obj_data | SHM | SHM byte offset 0x092c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 387 | 0x0164 | 4 | 0x000250c0 | obj_data | SHM | SHM byte offset 0x0930 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 388 | 0x0164 | 4 | 0x38733873 | obj_data | SHM | SHM byte offset 0x0934 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 389 | 0x0164 | 4 | 0x237b3a72 | obj_data | SHM | SHM byte offset 0x0938 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 390 | 0x0164 | 4 | 0x237b2a79 | obj_data | SHM | SHM byte offset 0x093c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 391 | 0x0164 | 4 | 0x257a257a | obj_data | SHM | SHM byte offset 0x0940 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 392 | 0x0164 | 4 | 0x34753475 | obj_data | SHM | SHM byte offset 0x0944 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 393 | 0x0164 | 4 | 0x167e3873 | obj_data | SHM | SHM byte offset 0x0948 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 394 | 0x0164 | 4 | 0x127f257a | obj_data | SHM | SHM byte offset 0x094c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 395 | 0x0164 | 4 | 0x237b1f7c | obj_data | SHM | SHM byte offset 0x0950 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 396 | 0x0164 | 4 | 0x0000001d | obj_data | SHM | SHM byte offset 0x0954 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 397 | 0x0164 | 4 | 0x002000e0 | obj_data | SHM | SHM byte offset 0x0958 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 398 | 0x0164 | 4 | 0x03020100 | obj_data | SHM | SHM byte offset 0x095c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 399 | 0x0164 | 4 | 0x08000504 | obj_data | SHM | SHM byte offset 0x0960 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 400 | 0x0164 | 4 | 0xa5a6a3a4 | obj_data | SHM | SHM byte offset 0x0964 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 401 | 0x0164 | 4 | 0xb3b40900 | obj_data | SHM | SHM byte offset 0x0968 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 402 | 0x0164 | 4 | 0x1110b5b6 | obj_data | SHM | SHM byte offset 0x096c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 403 | 0x0164 | 4 | 0xbeef0015 | obj_data | SHM | SHM byte offset 0x0970 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 404 | 0x0164 | 4 | 0x0000ff00 | obj_data | SHM | SHM byte offset 0x0974 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 405 | 0x0160 | 4 | 0x03010263 | obj_sel | SHM | SHM window, base byte offset 0x098c, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 406 | 0x0164 | 4 | 0x01cb0020 | obj_data | SHM | SHM byte offset 0x098c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 407 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x0990 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 408 | 0x0164 | 4 | 0x000008ab | obj_data | SHM | SHM byte offset 0x0994 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 409 | 0x0164 | 4 | 0x00000410 | obj_data | SHM | SHM byte offset 0x0998 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 410 | 0x0160 | 4 | 0x0301026a | obj_sel | SHM | SHM window, base byte offset 0x09a8, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 411 | 0x0164 | 4 | 0x000208af | obj_data | SHM | SHM byte offset 0x09a8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 412 | 0x0164 | 4 | 0x00000064 | obj_data | SHM | SHM byte offset 0x09ac (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 413 | 0x0160 | 4 | 0x0301026d | obj_sel | SHM | SHM window, base byte offset 0x09b4, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 414 | 0x0164 | 4 | 0x01ca0010 | obj_data | SHM | SHM byte offset 0x09b4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 415 | 0x0164 | 4 | 0x00000002 | obj_data | SHM | SHM byte offset 0x09b8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 416 | 0x0164 | 4 | 0x000208aa | obj_data | SHM | SHM byte offset 0x09bc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 417 | 0x0164 | 4 | 0x00000054 | obj_data | SHM | SHM byte offset 0x09c0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 418 | 0x0160 | 4 | 0x03010272 | obj_sel | SHM | SHM window, base byte offset 0x09c8, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 419 | 0x0164 | 4 | 0x01ce0008 | obj_data | SHM | SHM byte offset 0x09c8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 420 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x09cc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 421 | 0x0164 | 4 | 0x000008ae | obj_data | SHM | SHM byte offset 0x09d0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 422 | 0x0164 | 4 | 0x00000044 | obj_data | SHM | SHM byte offset 0x09d4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 423 | 0x0160 | 4 | 0x03010277 | obj_sel | SHM | SHM window, base byte offset 0x09dc, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 424 | 0x0164 | 4 | 0x01c90008 | obj_data | SHM | SHM byte offset 0x09dc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 425 | 0x0164 | 4 | 0x00000002 | obj_data | SHM | SHM byte offset 0x09e0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 426 | 0x0164 | 4 | 0x000208a9 | obj_data | SHM | SHM byte offset 0x09e4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 427 | 0x0164 | 4 | 0x0000003c | obj_data | SHM | SHM byte offset 0x09e8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 428 | 0x0160 | 4 | 0x0301027c | obj_sel | SHM | SHM window, base byte offset 0x09f0, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 429 | 0x0164 | 4 | 0x01cd0004 | obj_data | SHM | SHM byte offset 0x09f0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 430 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x09f4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 431 | 0x0164 | 4 | 0x000008ad | obj_data | SHM | SHM byte offset 0x09f8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 432 | 0x0164 | 4 | 0x00000034 | obj_data | SHM | SHM byte offset 0x09fc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 433 | 0x0160 | 4 | 0x03010281 | obj_sel | SHM | SHM window, base byte offset 0x0a04, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 434 | 0x0164 | 4 | 0x01c80004 | obj_data | SHM | SHM byte offset 0x0a04 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 435 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x0a08 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 436 | 0x0164 | 4 | 0x000008a8 | obj_data | SHM | SHM byte offset 0x0a0c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 437 | 0x0164 | 4 | 0x00000030 | obj_data | SHM | SHM byte offset 0x0a10 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 438 | 0x0160 | 4 | 0x03010286 | obj_sel | SHM | SHM window, base byte offset 0x0a18, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 439 | 0x0164 | 4 | 0x01cc0000 | obj_data | SHM | SHM byte offset 0x0a18 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 440 | 0x0164 | 4 | 0x00000002 | obj_data | SHM | SHM byte offset 0x0a1c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 441 | 0x0164 | 4 | 0x000208ac | obj_data | SHM | SHM byte offset 0x0a20 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 442 | 0x0164 | 4 | 0x00000030 | obj_data | SHM | SHM byte offset 0x0a24 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 443 | 0x0160 | 4 | 0x0301028b | obj_sel | SHM | SHM window, base byte offset 0x0a2c, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 444 | 0x0164 | 4 | 0x040a00c0 | obj_data | SHM | SHM byte offset 0x0a2c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 445 | 0x0164 | 4 | 0x013a0070 | obj_data | SHM | SHM byte offset 0x0a30 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 446 | 0x0164 | 4 | 0x0228040a | obj_data | SHM | SHM byte offset 0x0a34 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 447 | 0x0164 | 4 | 0x000002f2 | obj_data | SHM | SHM byte offset 0x0a38 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 448 | 0x0164 | 4 | 0x016a01ca | obj_data | SHM | SHM byte offset 0x0a3c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 449 | 0x0164 | 4 | 0x0100040a | obj_data | SHM | SHM byte offset 0x0a40 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 450 | 0x0164 | 4 | 0x00a0040a | obj_data | SHM | SHM byte offset 0x0a44 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 451 | 0x0164 | 4 | 0x04140060 | obj_data | SHM | SHM byte offset 0x0a48 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 452 | 0x0164 | 4 | 0x01020038 | obj_data | SHM | SHM byte offset 0x0a4c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 453 | 0x0164 | 4 | 0x01140414 | obj_data | SHM | SHM byte offset 0x0a50 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 454 | 0x0164 | 4 | 0x000001de | obj_data | SHM | SHM byte offset 0x0a54 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 455 | 0x0164 | 4 | 0x011a014a | obj_data | SHM | SHM byte offset 0x0a58 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 456 | 0x0164 | 4 | 0x00800414 | obj_data | SHM | SHM byte offset 0x0a5c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 457 | 0x0164 | 4 | 0x00500414 | obj_data | SHM | SHM byte offset 0x0a60 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 458 | 0x0164 | 4 | 0x04370022 | obj_data | SHM | SHM byte offset 0x0a64 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 459 | 0x0164 | 4 | 0x00df0015 | obj_data | SHM | SHM byte offset 0x0a68 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 460 | 0x0164 | 4 | 0x00650437 | obj_data | SHM | SHM byte offset 0x0a6c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 461 | 0x0164 | 4 | 0x0000012e | obj_data | SHM | SHM byte offset 0x0a70 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 462 | 0x0164 | 4 | 0x00e800f9 | obj_data | SHM | SHM byte offset 0x0a74 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 463 | 0x0164 | 4 | 0x002f0437 | obj_data | SHM | SHM byte offset 0x0a78 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 464 | 0x0164 | 4 | 0x001e0437 | obj_data | SHM | SHM byte offset 0x0a7c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 465 | 0x0164 | 4 | 0x846e0011 | obj_data | SHM | SHM byte offset 0x0a80 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 466 | 0x0164 | 4 | 0x00d4000b | obj_data | SHM | SHM byte offset 0x0a84 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 467 | 0x0164 | 4 | 0x0033846e | obj_data | SHM | SHM byte offset 0x0a88 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 468 | 0x0164 | 4 | 0x000000fc | obj_data | SHM | SHM byte offset 0x0a8c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 469 | 0x0164 | 4 | 0x00d800e1 | obj_data | SHM | SHM byte offset 0x0a90 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 470 | 0x0164 | 4 | 0x0018846e | obj_data | SHM | SHM byte offset 0x0a94 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 471 | 0x0164 | 4 | 0x000f046e | obj_data | SHM | SHM byte offset 0x0a98 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 472 | 0x0164 | 4 | 0x9d8a0000 | obj_data | SHM | SHM byte offset 0x0a9c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 473 | 0x0164 | 4 | 0x000000fb | obj_data | SHM | SHM byte offset 0x0aa0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 474 | 0x0164 | 4 | 0x00fa4ec5 | obj_data | SHM | SHM byte offset 0x0aa4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 475 | 0x0164 | 4 | 0x34830000 | obj_data | SHM | SHM byte offset 0x0aa8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 476 | 0x0164 | 4 | 0x000000fe | obj_data | SHM | SHM byte offset 0x0aac (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 477 | 0x0164 | 4 | 0x00f92762 | obj_data | SHM | SHM byte offset 0x0ab0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 478 | 0x0164 | 4 | 0x1a420000 | obj_data | SHM | SHM byte offset 0x0ab4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 479 | 0x0164 | 4 | 0x000000fd | obj_data | SHM | SHM byte offset 0x0ab8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 480 | 0x0164 | 4 | 0x00f813b1 | obj_data | SHM | SHM byte offset 0x0abc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 481 | 0x0164 | 4 | 0x11810000 | obj_data | SHM | SHM byte offset 0x0ac0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 482 | 0x0164 | 4 | 0x000000fc | obj_data | SHM | SHM byte offset 0x0ac4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 483 | 0x0164 | 4 | 0x00fc0fc1 | obj_data | SHM | SHM byte offset 0x0ac8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 484 | 0x0164 | 4 | 0x0fc10000 | obj_data | SHM | SHM byte offset 0x0acc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 485 | 0x0164 | 4 | 0x000000fc | obj_data | SHM | SHM byte offset 0x0ad0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 486 | 0x0164 | 4 | 0x00fc0fc1 | obj_data | SHM | SHM byte offset 0x0ad4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 487 | 0x0164 | 4 | 0x00040006 | obj_data | SHM | SHM byte offset 0x0ad8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 488 | 0x0164 | 4 | 0x00000002 | obj_data | SHM | SHM byte offset 0x0adc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 489 | 0x0164 | 4 | 0x00050007 | obj_data | SHM | SHM byte offset 0x0ae0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 490 | 0x0164 | 4 | 0x00010003 | obj_data | SHM | SHM byte offset 0x0ae4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 491 | 0x0160 | 4 | 0x030103cf | obj_sel | SHM | SHM window, base byte offset 0x0f3c, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 492 | 0x0164 | 4 | 0x000a0000 | obj_data | SHM | SHM byte offset 0x0f3c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 493 | 0x0164 | 4 | 0xffffffff | obj_data | SHM | SHM byte offset 0x0f40 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 494 | 0x0164 | 4 | 0x07a407a4 | obj_data | SHM | SHM byte offset 0x0f44 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 495 | 0x0160 | 4 | 0x03010422 | obj_sel | SHM | SHM window, base byte offset 0x1088, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 496 | 0x0164 | 4 | 0x00640054 | obj_data | SHM | SHM byte offset 0x1088 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 497 | 0x0164 | 4 | 0x03020100 | obj_data | SHM | SHM byte offset 0x108c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 498 | 0x0164 | 4 | 0x0a000504 | obj_data | SHM | SHM byte offset 0x1090 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 499 | 0x0164 | 4 | 0x0e0d0c0b | obj_data | SHM | SHM byte offset 0x1094 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 500 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x1098 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 501 | 0x0160 | 4 | 0x03010428 | obj_sel | SHM | SHM window, base byte offset 0x10a0, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 502 | 0x0164 | 4 | 0x00960074 | obj_data | SHM | SHM byte offset 0x10a0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 503 | 0x0164 | 4 | 0x03020100 | obj_data | SHM | SHM byte offset 0x10a4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 504 | 0x0164 | 4 | 0x00b40504 | obj_data | SHM | SHM byte offset 0x10a8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 505 | 0x0164 | 4 | 0x01c00000 | obj_data | SHM | SHM byte offset 0x10ac (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 506 | 0x0164 | 4 | 0x0c0b0a00 | obj_data | SHM | SHM byte offset 0x10b0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 507 | 0x0164 | 4 | 0x00000e0d | obj_data | SHM | SHM byte offset 0x10b4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 508 | 0x0160 | 4 | 0x03010431 | obj_sel | SHM | SHM window, base byte offset 0x10c4, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 509 | 0x0164 | 4 | 0x00000001 | obj_data | SHM | SHM byte offset 0x10c4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 510 | 0x0164 | 4 | 0x000c0000 | obj_data | SHM | SHM byte offset 0x10c8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 511 | 0x0160 | 4 | 0x03010565 | obj_sel | SHM | SHM window, base byte offset 0x1594, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 512 | 0x0164 | 4 | 0x00000018 | obj_data | SHM | SHM byte offset 0x1594 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 513 | 0x0164 | 4 | 0x61a80bb8 | obj_data | SHM | SHM byte offset 0x1598 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 514 | 0x0164 | 4 | 0x00640ea6 | obj_data | SHM | SHM byte offset 0x159c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 515 | 0x0164 | 4 | 0x01f40000 | obj_data | SHM | SHM byte offset 0x15a0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 516 | 0x0164 | 4 | 0x00000005 | obj_data | SHM | SHM byte offset 0x15a4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 517 | 0x0164 | 4 | 0x61a87530 | obj_data | SHM | SHM byte offset 0x15a8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 518 | 0x0164 | 4 | 0x001e7530 | obj_data | SHM | SHM byte offset 0x15ac (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 519 | 0x0160 | 4 | 0x0301056e | obj_sel | SHM | SHM window, base byte offset 0x15b8, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 520 | 0x0164 | 4 | 0x0000c350 | obj_data | SHM | SHM byte offset 0x15b8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 521 | 0x0160 | 4 | 0x03010570 | obj_sel | SHM | SHM window, base byte offset 0x15c0, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 522 | 0x0164 | 4 | 0x05140000 | obj_data | SHM | SHM byte offset 0x15c0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 523 | 0x0164 | 4 | 0x00000753 | obj_data | SHM | SHM byte offset 0x15c4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 524 | 0x0160 | 4 | 0x03010574 | obj_sel | SHM | SHM window, base byte offset 0x15d0, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 525 | 0x0164 | 4 | 0x00004e20 | obj_data | SHM | SHM byte offset 0x15d0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 526 | 0x0164 | 4 | 0x000f0000 | obj_data | SHM | SHM byte offset 0x15d4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 527 | 0x0164 | 4 | 0x000401f4 | obj_data | SHM | SHM byte offset 0x15d8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 528 | 0x0160 | 4 | 0x0301057a | obj_sel | SHM | SHM window, base byte offset 0x15e8, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 529 | 0x0164 | 4 | 0x00130001 | obj_data | SHM | SHM byte offset 0x15e8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 530 | 0x0164 | 4 | 0x00030000 | obj_data | SHM | SHM byte offset 0x15ec (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 531 | 0x0164 | 4 | 0x00070001 | obj_data | SHM | SHM byte offset 0x15f0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 532 | 0x0164 | 4 | 0x0000afc8 | obj_data | SHM | SHM byte offset 0x15f4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 533 | 0x0164 | 4 | 0x00001388 | obj_data | SHM | SHM byte offset 0x15f8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 534 | 0x0164 | 4 | 0x00ff172c | obj_data | SHM | SHM byte offset 0x15fc (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 535 | 0x0160 | 4 | 0x03010581 | obj_sel | SHM | SHM window, base byte offset 0x1604, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 536 | 0x0164 | 4 | 0x012c0000 | obj_data | SHM | SHM byte offset 0x1604 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 537 | 0x0164 | 4 | 0x0fa00000 | obj_data | SHM | SHM byte offset 0x1608 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 538 | 0x0160 | 4 | 0x03010584 | obj_sel | SHM | SHM window, base byte offset 0x1610, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 539 | 0x0164 | 4 | 0x00030000 | obj_data | SHM | SHM byte offset 0x1610 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 540 | 0x0164 | 4 | 0x012c0000 | obj_data | SHM | SHM byte offset 0x1614 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 541 | 0x0164 | 4 | 0x000000c0 | obj_data | SHM | SHM byte offset 0x1618 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 542 | 0x0164 | 4 | 0x00001388 | obj_data | SHM | SHM byte offset 0x161c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 543 | 0x0164 | 4 | 0x00000064 | obj_data | SHM | SHM byte offset 0x1620 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 544 | 0x0164 | 4 | 0x1f4005dc | obj_data | SHM | SHM byte offset 0x1624 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 545 | 0x0164 | 4 | 0x00000000 | obj_data | SHM | SHM byte offset 0x1628 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 546 | 0x0164 | 4 | 0x00000050 | obj_data | SHM | SHM byte offset 0x162c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 547 | 0x0164 | 4 | 0x00000002 | obj_data | SHM | SHM byte offset 0x1630 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 548 | 0x0160 | 4 | 0x0301058e | obj_sel | SHM | SHM window, base byte offset 0x1638, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 549 | 0x0164 | 4 | 0x00000002 | obj_data | SHM | SHM byte offset 0x1638 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 550 | 0x0164 | 4 | 0x9c400000 | obj_data | SHM | SHM byte offset 0x163c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 551 | 0x0164 | 4 | 0x00004e20 | obj_data | SHM | SHM byte offset 0x1640 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 552 | 0x0164 | 4 | 0x00000bb8 | obj_data | SHM | SHM byte offset 0x1644 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 553 | 0x0160 | 4 | 0x03010593 | obj_sel | SHM | SHM window, base byte offset 0x164c, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 554 | 0x0164 | 4 | 0x4e200000 | obj_data | SHM | SHM byte offset 0x164c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 555 | 0x0160 | 4 | 0x03010595 | obj_sel | SHM | SHM window, base byte offset 0x1654, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 556 | 0x0164 | 4 | 0x000005dc | obj_data | SHM | SHM byte offset 0x1654 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 557 | 0x0164 | 4 | 0x00000271 | obj_data | SHM | SHM byte offset 0x1658 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 558 | 0x0164 | 4 | 0x00007530 | obj_data | SHM | SHM byte offset 0x165c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 559 | 0x0160 | 4 | 0x030105f9 | obj_sel | SHM | SHM window, base byte offset 0x17e4, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 560 | 0x0164 | 4 | 0x00040000 | obj_data | SHM | SHM byte offset 0x17e4 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 561 | 0x0164 | 4 | 0x00040804 | obj_data | SHM | SHM byte offset 0x17e8 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 562 | 0x0164 | 4 | 0x08080000 | obj_data | SHM | SHM byte offset 0x17ec (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 563 | 0x0164 | 4 | 0x00100000 | obj_data | SHM | SHM byte offset 0x17f0 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 564 | 0x0160 | 4 | 0x03010605 | obj_sel | SHM | SHM window, base byte offset 0x1814, auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 565 | 0x0164 | 4 | 0x0000003c | obj_data | SHM | SHM byte offset 0x1814 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 566 | 0x0164 | 4 | 0x00000600 | obj_data | SHM | SHM byte offset 0x1818 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 567 | 0x0164 | 4 | 0x03810000 | obj_data | SHM | SHM byte offset 0x181c (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 568 | 0x0164 | 4 | 0x1c001fff | obj_data | SHM | SHM byte offset 0x1820 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 569 | 0x0164 | 4 | 0x02810002 | obj_data | SHM | SHM byte offset 0x1824 (low half) | OBJ | OBJDATA | SHM state (config) | C2/C3 |
| 570 | 0x0160 | 4 | 0x00020003 | obj_sel | SCR | SCR window, base byte offset 0x000c, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 571 | 0x0164 | 4 | 0x0000001f | obj_data | SCR | SCR index 3 (byte 0x000c) | OBJ | SCR[3] S_DOT11_CWMIN | PSM scratch config | C2/C3 |
| 572 | 0x0160 | 4 | 0x00020004 | obj_sel | SCR | SCR window, base byte offset 0x0010, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 573 | 0x0164 | 4 | 0x000003ff | obj_data | SCR | SCR index 4 (byte 0x0010) | OBJ | SCR[4] S_DOT11_CWMAX | PSM scratch config | C2/C3 |
| 574 | 0x0160 | 4 | 0x00020005 | obj_sel | SCR | SCR window, base byte offset 0x0014, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 575 | 0x0164 | 4 | 0x0000001f | obj_data | SCR | SCR index 5 (byte 0x0014) | OBJ | SCR[5] S_DOT11_CWCUR | PSM scratch config | C2/C3 |
| 576 | 0x0160 | 4 | 0x00020006 | obj_sel | SCR | SCR window, base byte offset 0x0018, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 577 | 0x0164 | 4 | 0x00000007 | obj_data | SCR | SCR index 6 (byte 0x0018) | OBJ | SCR[6] S_DOT11_SRC_LMT | PSM scratch config | C2/C3 |
| 578 | 0x0160 | 4 | 0x00020007 | obj_sel | SCR | SCR window, base byte offset 0x001c, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 579 | 0x0164 | 4 | 0x00000004 | obj_data | SCR | SCR index 7 (byte 0x001c) | OBJ | SCR[7] S_DOT11_LRC_LMT | PSM scratch config | C2/C3 |
| 580 | 0x0160 | 4 | 0x00020008 | obj_sel | SCR | SCR window, base byte offset 0x0020, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 581 | 0x0164 | 4 | 0x0000ffff | obj_data | SCR | SCR index 8 (byte 0x0020) | OBJ | SCR[8] S_DOT11_DTIMCOUNT | PSM scratch config | C2/C3 |
| 582 | 0x0160 | 4 | 0x00020018 | obj_sel | SCR | SCR window, base byte offset 0x0060, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 583 | 0x0164 | 4 | 0x00000007 | obj_data | SCR | SCR index 24 (byte 0x0060) | OBJ | SCR[24] S_THIS_AGG | PSM scratch config | C2/C3 |
| 584 | 0x0160 | 4 | 0x00020009 | obj_sel | SCR | SCR window, base byte offset 0x0024, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 585 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 9 (byte 0x0024) | OBJ | SCR[9] S_SEQ_NUM | PSM scratch config | C2/C3 |
| 586 | 0x0160 | 4 | 0x0002000a | obj_sel | SCR | SCR window, base byte offset 0x0028, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 587 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 10 (byte 0x0028) | OBJ | SCR[10] S_SEQ_NUM_FRAG | PSM scratch config | C2/C3 |
| 588 | 0x0160 | 4 | 0x0002000b | obj_sel | SCR | SCR window, base byte offset 0x002c, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 589 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 11 (byte 0x002c) | OBJ | SCR[11] S_FRMRETX_CNT | PSM scratch config | C2/C3 |
| 590 | 0x0160 | 4 | 0x0002000c | obj_sel | SCR | SCR window, base byte offset 0x0030, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 591 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 12 (byte 0x0030) | OBJ | SCR[12] S_SSRC | PSM scratch config | C2/C3 |
| 592 | 0x0160 | 4 | 0x0002000d | obj_sel | SCR | SCR window, base byte offset 0x0034, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 593 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 13 (byte 0x0034) | OBJ | SCR[13] S_SLRC | PSM scratch config | C2/C3 |
| 594 | 0x0160 | 4 | 0x0002000e | obj_sel | SCR | SCR window, base byte offset 0x0038, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 595 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 14 (byte 0x0038) | OBJ | SCR[14] S_EXP_RSP | PSM scratch config | C2/C3 |
| 596 | 0x0160 | 4 | 0x0002000f | obj_sel | SCR | SCR window, base byte offset 0x003c, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 597 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 15 (byte 0x003c) | OBJ | SCR[15] S_OLD_BREM | PSM scratch config | C2/C3 |
| 598 | 0x0160 | 4 | 0x00020010 | obj_sel | SCR | SCR window, base byte offset 0x0040, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 599 | 0x0164 | 4 | 0x0000001f | obj_data | SCR | SCR index 16 (byte 0x0040) | OBJ | SCR[16] S_OLD_CWWIN | PSM scratch config | C2/C3 |
| 600 | 0x0160 | 4 | 0x00020011 | obj_sel | SCR | SCR window, base byte offset 0x0044, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 601 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 17 (byte 0x0044) | OBJ | SCR[17] S_TXECTL | PSM scratch config | C2/C3 |
| 602 | 0x0160 | 4 | 0x00020012 | obj_sel | SCR | SCR window, base byte offset 0x0048, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 603 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 18 (byte 0x0048) | OBJ | SCR[18] S_CTXTST | PSM scratch config | C2/C3 |
| 604 | 0x0160 | 4 | 0x00020013 | obj_sel | SCR | SCR window, base byte offset 0x004c, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 605 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 19 (byte 0x004c) | OBJ | SCR[19] S_RXTST | PSM scratch config | C2/C3 |
| 606 | 0x0160 | 4 | 0x00020015 | obj_sel | SCR | SCR window, base byte offset 0x0054, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 607 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 21 (byte 0x0054) | OBJ | SCR[21] S_TXPWR_SUM | PSM scratch config | C2/C3 |
| 608 | 0x0160 | 4 | 0x00020016 | obj_sel | SCR | SCR window, base byte offset 0x0058, no auto-inc | OBJ | OBJADDR selector | object-memory selector | C2/C3 |
| 609 | 0x0164 | 4 | 0x00000000 | obj_data | SCR | SCR index 22 (byte 0x0058) | OBJ | SCR[22] S_TXPWR_ITER | PSM scratch config | C2/C3 |

---

# d11ac1bsinitvals42 (band-switch) — machine-generated record classification

- source: `/lib/firmware/brcm/bcm4352-d11ac1bsinitvals42.bin`
- size: 592 bytes, sha256 `e81a645c79f55557c87f4662702d7c57599918c9b2340bc4e440ce1bdbd014da`
- data records: **73** (terminator at index 73)
- status: **ANALYSIS ONLY** (no hardware access)

## Summary by kind

| kind | count |
|---|---|
| direct | 5 |
| obj_sel | 34 |
| obj_data | 34 |

## Summary by category

| category | count |
|---|---|
| IHR | 5 |
| OBJ | 68 |

## Summary by target space/region

| space/region | count |
|---|---|
| IHR/IFS | 4 |
| IHR/NAV | 1 |
| SHM | 68 |

## Summary by side-effect class

| side effect | count |
|---|---|
| SHM state (config) | 34 |
| object-memory selector | 34 |
| timing/IFS configuration | 4 |
| unknown side effect | 1 |
| **TOTAL** | **73** |

## Full per-record classification

| # | offset | w | value | kind | space | target | category | name | side effect | conf |
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
