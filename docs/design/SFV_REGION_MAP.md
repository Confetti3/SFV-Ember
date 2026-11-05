# SFV rollback snapshot region map (consolidated)

Sources: the registrations analysis (R), the run-time walk analysis (W), and the adversarial verification (V). V's corrections are applied and the claims it refuted are dropped. All addresses are image VAs at base 0x140000000.

Confidence uses two letters per owner: **L** is layout (H = V confirmed it from the decompile, M = both R and W report it but V did not check it, L = only one analysis reports it), and **C** is classification (H = synctest or reader evidence, M = role inferred with some reader evidence, L = inferred from the type name only).

---

## 0. Conventions and structural reconciliations

- **Offset convention.** Every offset in this document is **relative to the object start**. The slot-8 registration functions receive `this = obj+8` (the secondary vtable's slot 0 is a `sub rcx,8` thunk), so decompiled `a1+N` becomes object `+N+8`. That conversion is already applied everywhere here. There are two exceptions:
  - HUD parts (AnnouncePart, CockpitBasePart, KunaiGaugePart, TutorialPart, Z30GaugePart) receive `this = part` directly. Their offsets are part-relative with no +8 adjustment. The handle is at part+0xB0 (176).
  - Cockpit elements (type ids 0x200, 0x1000, 0x2000) have no type object, because vslot 0 returns a constant id. Their offsets are element-relative.
- **Snapshot context (handle).** It is at unit+232 (0xE8) and part+176 (0xB0).
- **Battle vs root (R corrected).** R said the battle *is* the ModeUnit. That is wrong.
  - `battle` is a separate object. It holds the root unit as a shared_ptr at battle+24 (object) and battle+32 (control block).
  - The unit multimap at battle+344 is filled by `sub_1404C8F80`.
  - ModeUnit's `sub_140427380` registers into `this+566264` = obj+0x8A400, and the manager is root+0x8A400. So **the ModeUnit object is the root unit**.
  - Its type chain is ModeUnit 0x143A69CF0 → … → RootUnit 0x1439DA2D8 → Unit 0x1439DA2A0. This is unverified; check it at run time by walking type+8.
- **Resolved gaps from W.**
  - The registrar of the BattleData snapshot fields is ModeUnit `sub_140427380`: root+6448 len 228 = BattleData+6176..+6404.
  - `sub_1403C4220` appends to the **post-restore** list at manager+0x38 (count at +0x40).
- **Dropped claim.** W's row "root+272, size 566000, BattleData FIGHT" is not a registered region. BattleData is used below only as a naming alias.
- **Registration gate.** Registration happens only when `*(BattleData+44) == 1` (`sub_1402FBE50`). BattleData = root+272.
- **Insert helpers.** There are 12 byte-identical copies of the `std::map<u64 addr, u64 size>` insert, all funnelling into `sub_1402E9ED0`:
  - 1402E8C30, 140315930, 140342840, 140367AB0, 14038BE90, 1403A6B20, 1403C4120, 1403E8C40, 14041B850, 14044AE20, 140477EE0, 1404A1BE0.
  - Inserting a duplicate address does nothing; the first size registered wins.
  - The xref sweep of every caller was exhaustive, so **no registration function is missing** from section 2.

---

## 1. Run-time naming recipe

**When to run it:** once per table build, immediately after `sub_14047C4D0` (the manager+0x10 table is populated) and before the mod appends its own regions (the Bind / AddGameRegions point). The build is triggered by message code 1 in the rollback unit's handler `sub_140481EA0`; that mapping to BattleStart is presumed. Message 15 (`sub_140303A30`) clears the table and the frame buffers but not the map.

### Step 0: locate battle, root and manager

1. From the mod's unit U, read `ctx = U+232` and `owner = *(ctx+8)`.
2. Read `battle = *(owner+96)`, with the control block at owner+104. Use it only if the control block's use count at ctrl+8 is non-zero.
3. If battle is valid, `root = *(battle+24)`.
4. Robust derivation and cross-check: `mgr = GameGetManager(ctx)`.
   - If `mgr != 0x143A64610` (the static fallback), set `root = mgr - 0x8A400` and assert it equals step 3.
   - If they disagree, trust `mgr` and log the mismatch.
5. The game's own fallback chain (`sub_1402F8710` / `sub_1402F8C30`) is, in order:
   - the battle weak_ptr at owner+96;
   - owner+80/+88, typed against 0x1439DA230 or else 0x1439DA1F8;
   - a climb of weak parent links at +152 until an object of type RootUnit 0x1439DA2D8 is reached;
   - the global shared_ptr `qword_1439D9CB8` / `qword_1439D9CC0`;
   - the statics (manager 0x143A64610, BattleData 0x1439DA310).
6. If `mgr == 0x143A64610`, there is no battle: **do not bind**.
7. If root is valid but battle is null, enumerate units by walking the tree from root (Step 1b) instead of using the multimap.
8. Aliases:
   - `BattleData = root+272`
   - `manager = root+0x8A400`, 152 bytes: map at +0x00, region vector at +0x10/+0x18, post-store list at +0x28, post-restore list at +0x38, total bytes at +0x48, frame deque at +0x50..+0x70, last FrameId at +0x78, 10 copy chunks at +0x80.

### Step 1: enumerate units

- **1a. Multimap.** battle+344 is a `std::multimap<const Type*, weak_ptr<Unit>>`. The head node pointer is at +344 and the count at +352.
  - Node layout: +0 left, +8 parent, +16 right, +24 color, +25 isNil, +32 key (the concrete Type*), +40 Unit*, +48 control block.
  - Walk it in order: start at `head->left`. The successor is the leftmost node of the right subtree if `right->isNil == 0`; otherwise climb parents while the current node is its parent's right child.
  - Equal keys keep insertion order (upper-bound insert, depth-first from root). Index **k** is a unit's position among units of the same concrete type; it equals what `FindUnit(type, k)` returns.
  - FindUnit (`sub_1404CE5D0`) matches the exact concrete type only, so always do the full walk. Never look up by a base type.
- **1b. Tree cross-check.** Recurse from root through the children vector at unit+168..+176 (16-byte shared_ptrs). It must produce the same set as 1a.
- **Player-slot naming.** For CharaUnit, name by player slot instead of k: `CharaUnit[P<slot>]`, with the slot at unit+256 (also +1880). This is unverified; assert that the two values agree and that the slots are {0,1}.
  - R's claim that +252 is the "player/side index" is corrected: +252 is a sequential chara counter.
- Liveness: skip any node whose control block use count (ctrl+8) is 0.

### Step 2: enumerate parts

- Parts live at unit+192 (begin) to unit+200 (end): a vector of 16-byte shared_ptrs (object at +0, control at +8).
- A part's index j is its position among parts of the same concrete type within that unit.
- A part holds a weak_ptr to its owner unit at part+152.

### Step 3: type and name

- `type = vslot0(obj)()`. Calling it is safe. Decoding `48 8D 05 rel32 C3` (`type = fn+7+rel32`) is an optimisation and is unverified, so call the function if the byte pattern does not match.
- Type object layout (confirmed from `sub_1402EC620`):
  - +0: name pointer, which points 26 bytes into a node of the global type-name tree (the +26 is confirmed);
  - +8: parent type;
  - +16: 0x14305E758;
  - +24: handler;
  - +32: copy function `sub_1413FA3B0`;
  - +40: factory;
  - +48: `sub_1403133C0`.
- The string format is unverified. Accept the name only if it is printable ASCII, NUL-terminated and at most 64 bytes. Otherwise fall back to `T_<typeaddr>`.
- is-a test: walk type+8 until it reaches the target type.

### Step 4: object sizes

- Read the factory at type+40. In its first 32 bytes, find `B9 imm32` followed directly by `E8 rel32` whose target is 0x1404EA630 (GameAlloc). `imm32` is the class size.
  - Known sizes: CharaUnit 0xE50, CharaStatusPart 0x570, StatsUnit 0x130E8, BonusStageModeUnit 0x8A4D0.
- Abstract types have the factory 0x14246B130 (`xor eax,eax; ret`), which gives no size. Live objects are always of a concrete type, so this does not matter.
- Fallback: the mod's existing GameAlloc hook records `{ptr, size}` during battle setup; look up each unit or part pointer there.
- Last resort: bound the object by the next known object's start, and log it.

### Step 5: owned sub-objects (separate allocations)

Each row gives the owner (concrete type), the pointer field, the registered range inside the pointee, and the name suffix.

| Owner | Pointer at | Registered in pointee | Size known | Name suffix |
|---|---|---|---|---|
| CharaUnit | obj+3160 (0xC58), if non-null | +16..+48 (32): qwords +16, +24 (**pointers**), dwords +32..+44 | no, at least 48 | `->C58+0x10` |
| CharaActionPart | part+16072 (0x3EC8) | +48..+272 (224); 272-byte object, ctor `sub_1402ECAD0`, vtable 0x143231618 | 272 | `->3EC8+0x30` |
| CharaActionPart | part+16088 (0x3ED8) | same | 272 | `->3ED8+0x30` |
| ProjectileActionPart | part+16080 (0x3ED0) | same | 272 | `->3ED0+0x30` |
| CharaCommandPart | part+216 (0xD8) | +16..+1708 (1692); 1760-byte object, ctor `sub_1404C68E0` | 1760 | `->D8+0x10` |
| ProjectileUnit | obj+672 (0x2A0) | +16..+36 (20), via `sub_14044AF20` | unknown | `->2A0+0x10` |

Unverified containers:
- HUD part elements in the TSparseArray at part+0x118 (`sub_140303470`). Elements with vtable 0x142C4E598 register elem+40, +136 and +140; elements with vtable 0x142C4E568 register elem+40.
- Cockpit elements: vtables 0x142C64300 / 0x142C647F0, built by `sub_1403F03C0`.

Name both kinds as `<part>.Elem@<vtable>[i]+off`.

### Step 6: attribute each table entry

1. Build a sorted interval index of `[base, base+size)` for the root, every unit, every part and every sub-object. Inside the root, BattleData `[root+272, …)` and the manager `[root+0x8A400, +152)` are nested aliases; prefer the innermost interval.
2. For each table entry `{ptr, len}`, binary-search for the containing interval and build the name:
   - Units: `<UnitType>[k]+0x<off>/<len>`
   - Parts: `<UnitType>[k].<PartType>[j]+0x<off>/<len>`
   - Sub-objects: `<parentname>-><ptrOff>+0x<off>/<len>`
   - Root: `<ModeType>[0]+0x1930/228` (alias `BattleData+0x1820`)
3. Check that `ptr+len <= base+objSize`. If it fails, the entry merged across objects. Split the name at the boundary and log it. This is possible because the build coalesces purely by byte adjacency across the whole map.
4. If nothing contains the entry, name it `unowned[i]@<ptr>/<len>`. If the GameAlloc hook has the containing block, also log the block's base, size and allocation return address. Unowned regions go to the advisory hash (section 3), never silently into the fight hash.

**Expected-region self-check (recommended).** From the section 2 table, simulate the build for every live owner:
- sort the fields by address;
- merge a field only when `addr == previous end`;
- drop a field when `addr < previous end`, but still reset end to `addr+size`;
- apply the conditionals:
  - Camera +368: included when `sub_14032D350()` is true;
  - InputUnit: included when `BattleData+40 != 1`;
  - ProjectileUnit: skipped when `+928 != 0`, and +60 bytes when `sub_1402FC000(...) == 0`.

Then compare the result with the live table. Every live entry should equal one expected run exactly. Log each missing, extra or size-mismatched entry. This is also how the open attributions (112 / 528 / 300) get settled by address.

---

## 2. Owner types

FIGHT = decides the fight; PRESENTATION = visual; STATS = statistics; ENGINE = rollback/replay machinery; UNKNOWN = not established.

### Units

| Type | Type object | Registration fn (vtable slot-8 location) | Fields (obj offset, size: meaning) | Class | Conf. |
|---|---|---|---|---|---|
| **ActionUnit** (abstract base of CharaUnit and ProjectileUnit) | 0x143A64788 (parent Unit 0x1439DA2A0) | `sub_1402FED60`, called first by `sub_1403785A0` and `sub_14045A100` | +248 (0xF8), 40: dwords +248, +252 (sequential chara counter), +256 (player slot), then +260 (12), +272 (16)<br>+376 (0x178), 68: 17 dwords of action counters/ids (ctor pattern -1/0/0x10000) | FIGHT | L:H C:M |
| **CharaUnit** (size 0xE50) | 0x143A647C0 | `sub_1403785A0`; slot at 0x142C522D0 (secondary vtable 0x142C52290, primary 0x142C521F8); post-restore callback `sub_14037B670` | +1892 (0x764), 300: 7 dwords (+1892 index into another unit; +1908 gate flag read by CharaManager Prepare) + 17×16-byte vectors rebuilt in the CharaManager SyncPreMain task (`sub_140383820`; +2160 sinf bob) | **UNKNOWN (mixed)** | L:H C:L |
| | | | +2196 (0x894), 208: core scalars (16-byte vector, 3 dwords, 2×32 bytes, 26 dwords, **qword +2392**, dword) | FIGHT | L:H C:M |
| | | | +2416, 28 and +2448, 28: vector + 3 dwords each | FIGHT | L:H C:M |
| | | | +2480 (0x9B0), 652: 3×160 bytes, 28 dwords, 12 bytes, 12 dwords | FIGHT | L:H C:M |
| | | | +3136 (0xC40), 24 | FIGHT | L:H C:M |
| | | | sub-object `->C58+0x10`, 32 (two **pointers** + 4 dwords; pointee is polymorphic, kind 19/24) | FIGHT, pointers | L:H C:M |
| | | | +3488 (0xDA0), 48: 32 bytes + **qwords +3520, +3528** | UNKNOWN | L:H C:L |
| | | | +3548, 24; +3584, 12; +3604, 16 | UNKNOWN | L:H C:L |
| | | | inherits ActionUnit +248 and +376 | | |
| **ProjectileUnit** | 0x143A64C20 (parent ActionUnit) | `sub_14045A100`, only if `*(obj+928) == 0`; callback `sub_140469370` | +464, 208: 13×16-byte vectors (position, velocity, boxes)<br>+688, 104, or 164 when `sub_1402FC000(...) == 0` (adds +792 (8) and +800 (52))<br>`->2A0+0x10`, 20<br>plus ActionUnit fields | FIGHT | L:M C:M |
| **ProjectileGroupUnit** | 0x143A65758 | `sub_140480670` | +640, 600: 3×200 bytes, each 24 **ProjectileUnit\*** plus a count qword (+832, +1032, +1232)<br>+1280, 1408: group state; contents unknown, may hold pointers<br>Also writes each projectile's +928 / +448 / +248 / +252 / +256 | FIGHT, pointers | L:H C:M |
| **CharaManagerUnit** | 0x143A647F8 | `sub_1403D13F0`; slot at 0x142C5F4D0; callback `sub_1403D73A0` | +440, 4: dword | UNKNOWN | L:H C:L |
| | | | +448 (0x1C0), 112: centre point +448/+452 and +464/+468 (computed in the CharaManager Prepare task, clamped to FieldUnit stage bounds); two 8-int player-index lists +480 and +520 with count qwords +512 and +552 (values are counts, not pointers) | **UNKNOWN** (likely fight-relevant) | L:H C:L |
| | | | +600, 8: two dwords | UNKNOWN | L:H C:L |
| **CameraUnit** | 0x143A648A0 | `sub_14032EBD0`; callback `sub_140330DB0` | +256 (0x100): 528 when `sub_14032D350()` is true, else 112 + 376<br>+792, 20; +832, 76; +928, 36; +976, 120; +1104, 36; +1152, 128; +1360, 60 | PRESENTATION (no simulation readers were searched) | L:H C:L |
| **ModeUnit** (root unit; the BonusStage variant is 0x8A4D0 bytes) | 0x143A69CF0 | `sub_140427380`; also called directly by mode builders such as `sub_140423DD0` and `sub_1404257F0` (R only) | +6448 (0x1930), 228: BattleData+6176..+6404, battle/round state (176 + 52 bytes) | FIGHT | L:H C:M |
| **InputUnit** | 0x143A64C90 | `sub_140427270` | +376, 8192: input history. Gate is **BattleData+40 != 1** (R's "handle-info dword" was wrong) | FIGHT | L:H C:M |
| **VersusRuleUnit** | 0x143A723E0 | `sub_140480FB0` | +252, 64: rule block (`sub_140488820`)<br>+69828, 120: round/win state | FIGHT | L:L C:L |
| **ComboObserverUnit** | 0x143A692C8 | `sub_140425770` | +248, 32: 16 bytes + **two qwords**; read by UI (`sub_140445F50`) | STATS | L:H C:M |
| **StatusObserverUnit** | 0x143A657A0 | `sub_140427440` | +248, 240: two 120-byte blocks | UNKNOWN | L:L C:L |
| **StatsUnit** (size 0x130E8) | 0x143A665F0 | `sub_1404B0570`; fn pointer stored at 0x142C72430 (base 0x142C723F0 *if* slot 8, unchecked) | +248, 77804: statistics | STATS | L:H C:H (synctest; not every getter caller read) |
| **VfxManagerUnit** | 0x143A64BE8 | `sub_1404B0800`; fn pointer stored at 0x142C72838 (base 0x142C727F8 if slot 8) | +248, 496 | PRESENTATION | L:L C:L |
| **ReplayUnit** | 0x143A69C48 | `sub_140480D20`; fn pointer stored at 0x142C6FFD8 (base 0x142C6FF98 if slot 8) | +248, 16: replay cursor | ENGINE | L:L C:L |
| TimeUnit / SoundUnit | 0x143A64830 / 0x143A64D38 | `sub_1404B0500` / `sub_1404D4970` | none (post-restore callbacks `sub_1404B6230` / `sub_1404D5820` only) | n/a | H |
| FieldUnit | 0x143A64B78 | none | none. Simulation reads its +672..+760 stage-bounds matrix | n/a (unregistered) | H |

### Parts

| Type | Type object | Registration fn | Fields | Class | Conf. |
|---|---|---|---|---|---|
| **ActionPart** (base) | 0x143A64A60 (parent Part 0x1439DA268) | `sub_1402FED10` → `sub_1402E8D30(part+192)` | +216 (0xD8), 15808: action/script state (**qword +216**, then 32, 160, 80, 5360, 6712, 2392, 1056, 4, 4)<br>+16048 (0x3EB0), 4 | FIGHT | L:H C:M |
| **CharaActionPart** | 0x143A64A98 | `sub_14032F570`; slot of vtable 0x142C4F190 | ActionPart fields, plus:<br>`->3EC8+0x30` 224 and `->3ED8+0x30` 224 (action timeline players; **qword at pointee +64**)<br>**+16104 (0x3EE8), 4** and **+16112 (0x3EF0), 8**: two separate regions (W's merged 16-byte run was wrong) | FIGHT | L:H C:M |
| **ProjectileActionPart** | 0x143A64D70 | `sub_140459FA0` | ActionPart fields + `->3ED0+0x30`, 224 | FIGHT | L:H C:M |
| **CharaStatusPart** (size 0x570) | 0x143A64B08 | `sub_140354260`; slot at 0x142C516C8 | +192, 372: vitality, stun, gauges, status counters<br>+568 (0x238), 204, minus the masked words +0x2C0 and +0x2C4 (region +0x88)<br>+812, 12<br>loop, stride 72: +876 (8), +900 (4), +948 (8), +972 (4), +1020 (8), +1044 (4), +1092 (8)<br>**+1116 (0x45C), 132** (one merged run)<br>+1288, 76 | FIGHT | L:H C:M |
| ↳ masked words | | | part+0x2C0, 8: combo copied-move count and opponent move starts; read only by the stats listener | STATS | H (in the mod) |
| **CharaAbilityPart** | 0x143A65850 | `sub_14032F3D0` | +192, 96: ability params copied from chara data at bind<br>+708, 32 | FIGHT | L:L C:L |
| **CharaCommandPart** | 0x143A65C18 | `sub_140353740` | `->D8+0x10`, 1692: command/input parser<br>+280, 28<br>+312, 4 | FIGHT | L:L C:L |
| **CharaItemPart** | 0x143A663A0 | `sub_140353F40` | +192, 24: 12 bytes, a dword, **qword +200** | FIGHT | L:L C:L |
| CharaEndPart | 0x143A66368 | `sub_140353F10` | none | n/a | H |
| **CharaMotionPart** | 0x143A64750 | `sub_140354130` | +192, 13092: motion/animation state (12960 + 120 + 8 + 4)<br>+13288, 192 | UNKNOWN (may feed root motion) | L:L C:L |
| **CharaMaterialPart** | 0x143A65C50 | `sub_140354030`; callback `sub_1403587D0` | +216, 724; +944, 1624 | PRESENTATION | L:L C:L |
| **CharaVisualPart** | 0x143A64AD0 | `sub_140395320`; callback `sub_1403999B0` | +1752, 392: **qword +1752** + 384 bytes | PRESENTATION | L:L C:L |
| **AttackDataPart** | 0x143A69338 | `sub_1403F0310`; callback `sub_140418830` | +192, 632: hit/damage data (112 + 520) | FIGHT | L:L C:L |
| **ProjectileVisualPart** | 0x143A66D00 (R only) | `sub_140480C00`; callback `sub_140482D10` | +192, 44; +240, 136 | PRESENTATION | L:M C:L |
| CharaUniquePart base | 0x143A64B40 | `sub_140378530` | none (callback `sub_1403A8CBC`) | n/a | M |
| **CharaUniquePart_NNN** | R's list: _021 0x143A67338 … _005 0x143A68950 | R's per-type function list. _029/_035/_041 (`sub_1403B00D0`) register nothing | Mostly 1–3 state dwords at +232..+244. Larger ones:<br>_008 +240, 208<br>_010 +232 (52) + +288 (8)<br>_019 +232, 80<br>_042 +240, 880<br>_043 +1328, 304<br>_040 +264, +288, +324<br>other sizes as R lists them<br>Bone-name strings are not registered | FIGHT | L:L C:L |
| HUD parts (Announce, CockpitBase, KunaiGauge, Tutorial, Z30Gauge) | 0x143A64868, 0x143A657D8, 0x143A67760, 0x143A64CC8, 0x143A68090 | `sub_1403F4550`, `sub_1403F9260`, `sub_1403FEAE0` / `sub_140400320`, `sub_14042F760`, `sub_140430960` (this = part) | container elements at +0x118 (see Step 5); KunaiGauge +432, 8 | PRESENTATION | L:L C:L |
| Cockpit elements (id 0x200; 0x1000 / 0x2000 register callbacks only) | none | `sub_14042D220` (vslot 5) | +0x90, 1056; +0x4B0, 16 (two qwords); +0x540, 128 | PRESENTATION | L:L C:L |

---

## 3. Proposed peer-hash policy

Key rules on `(concrete owner type, object offset, length) → treatment`, never on region size alone. Treatments:

- **RAW**: hash the bytes.
- **CANON**: replace pointer words with a canonical id before hashing, `<OwnerType>[k]` resolved through the interval index, or 0 for null; unresolvable pointers are masked.
- **MASK**: zero the listed words.
- **LEARN**: hash, but run a word-level drift learn. Drifting words are investigated, and masked only once the cause is understood.

Use **two hashes**:
1. **Fight hash (authoritative)**: tier A and tier B only. A mismatch here is a desync.
2. **Advisory hash (diagnostic)**: tier C, every UNKNOWN region, and every `unowned[i]` region. It is logged per region and never triggers desync handling until the region is promoted.

**Why synctest is not enough.** Synctest only finds re-simulation nondeterminism on one machine. A cross-peer hash also sees heap pointers and uninitialised padding inside registered blocks. Restore copies those bytes faithfully, so synctest never flags them, but they differ between machines. Two mechanisms close the gap:
- **Pointer detector (at bind).** For every 8-aligned qword in a fight-hash region, if its value lies inside a GameAlloc block the mod has recorded, or inside the image, mark it CANON. Candidates already known: CharaUnit +2392, +3520, +3528; the sub-object at `->C58` +16/+24; ActionPart +216; timeline object +64; CharaItemPart +200; ComboObserver's two qwords; ProjectileGroupUnit +640..+1240; possibly ProjectileGroupUnit +1280.
- **Frame-0 cross-peer diff.** At the first synchronised frame, peers exchange per-word hashes or values for fight regions. Words that differ before any divergent input are padding or pointer candidates. Log them and mask them after review.

### Tier A: fight hash, RAW plus pointer detection

- ActionUnit +248/40, +376/68, for CharaUnit and ProjectileUnit.
- CharaUnit +2196/208, +2416/28, +2448/28, +2480/652, +3136/24.
- ActionPart +216/15808, +16048/4.
- CharaActionPart +16104/4, +16112/8, and timeline `->3EC8+0x30`/224, `->3ED8+0x30`/224.
- ProjectileActionPart timeline `->3ED0+0x30`/224.
- CharaStatusPart: all runs, with **MASK** on part+0x2C0..+0x2C8 (existing kMaskedWords {204, 2, 0x88}).
- ModeUnit +6448/228.
- ProjectileUnit +464/208, +688/104 (or 164), `->2A0+0x10`/20.
- InputUnit +376/8192 when registered.
- AttackDataPart +192/632.
- CharaCommandPart `->D8+0x10`/1692, +280/28, +312/4.
- CharaAbilityPart, CharaItemPart, VersusRuleUnit, CharaUniquePart_NNN: all runs.

The last group's layouts are single-source, but the regions are small and their roles are fight-shaped. Promote them to tier A once the expected-region self-check passes for them.

### Tier B: fight hash with CANON

- ProjectileGroupUnit +640/600: 3 × (24 pointers + count). CANON each pointer to `ProjectileUnit[k]`; keep the counts RAW.
- ProjectileGroupUnit +1280/1408: RAW, plus the pointer detector.
- CharaUnit sub-object `->C58+0x10`/32: CANON +16 and +24 (polymorphic pointee; kind 19/24 from its vtable+32 is also usable as the canonical value); dwords +32..+44 RAW.

### Tier C: provisional (advisory hash plus LEARN; promote or exclude on evidence)

| Region | Why it is not excluded |
|---|---|
| CharaUnit +1892/300 | V refuted the PRESENTATION label. The 7 dwords (+1892..+1916) are control state (an index into another unit, a gate flag read by CharaManager Prepare): **hash them in the fight hash now** unless word-level learn shows they drift. The vectors +1920..+2192 are rebuilt in the SyncPreMain task and feed chara+2176 and mgr+448. Synctest drift there may be a **rollback bug**, not noise. Find the cause before masking. |
| CharaManagerUnit +448/112 | It contains the stage-clamped centre point and the player-index lists. If the centre drives screen-edge or wall pushback, it is fight state. The owner attribution to CharaManagerUnit was made by elimination; confirm it by address. |
| CharaManagerUnit +440/4, +600/8 | Roles are unknown. |
| CameraUnit (all) | The layout is confirmed; no simulation readers were searched (`sub_14030F4F0` "Pan", `sub_1403BACA0`, second FindUnit `sub_1404CE710`). If the screen edge comes from the camera, it is fight state. Until shown, keep it in the advisory hash, not the fight hash. |
| CharaMotionPart (both runs) | Animation state may drive root motion or box timing. |
| CharaUnit +3488/48, +3548/24, +3584/12, +3604/16 | Roles are unknown; +3520/+3528 are pointer candidates. |
| StatusObserverUnit +248/240 | An observer (likely UI) whose role is unknown. |

### Excluded from the peer hash (still saved and restored)

| Region | Reason |
|---|---|
| StatsUnit +248/77804 | STATS: synctest-confirmed `.stats` region. Not every getter caller was read, so trial or challenge modes could read it in simulation. |
| CharaStatusPart +0x2C0, 8 | STATS: read only by the statistics listener. |
| ComboObserverUnit +248/32 | STATS: drives combo UI widgets; also contains pointer-candidate qwords. |
| CharaMaterialPart, CharaVisualPart, ProjectileVisualPart, VfxManagerUnit | PRESENTATION by type role; layouts are single-source and readers were not searched. A later pass could move them to the advisory hash. |
| HUD part elements, KunaiGauge +432, cockpit elements | PRESENTATION (HUD). |
| ReplayUnit +248/16 | ENGINE: replay cursor. |
| Manager internals (root+0x8A400..) | Not registered; ENGINE. |

**Replace the size-keyed rules** (`kUncheckedSizes {300, 2}`, the 112 and 528 entries) with the owner rules above.

---

## 4. Open gaps and unverified items

**Attribution and layout**
1. The 112-byte drifting region is attributed to CharaManagerUnit +448 only by elimination. CameraUnit also produces a 112-byte run when `sub_14032D350()` is false. Which owner it belongs to, and whether the 528-byte region is CameraUnit +256, both depend on that function's value in versus. Settle it by address with Step 6.
2. Layouts reported by only one analysis (R): CharaUniquePart_NNN (all type objects and fields), CharaAbilityPart, CharaCommandPart and its 1760-byte object, CharaItemPart, CharaMaterialPart, CharaMotionPart, CharaVisualPart, AttackDataPart, VersusRuleUnit, ProjectileUnit's conditionals, StatusObserverUnit, VfxManagerUnit, ReplayUnit, HUD and cockpit layouts, and the ProjectileVisualPart type object.
3. The vtable bases for StatsUnit, VfxManagerUnit and ReplayUnit are unchecked; only the location of each function pointer is known.
4. Not established:
   - ModeUnit's parent chain to RootUnit;
   - which concrete mode types versus and training use, and their sizes;
   - the meaning of ModeUnit 0x143A69CF0 vs BonusStageModeUnit 0x143A71780.
5. Not established:
   - the type names of the bases 0x1439DA2A0 / 0x1439DA268 (read at run time);
   - the HUD element vtables 0x142C4E598 / 0x142C4E568;
   - the cockpit element classes.
6. The size and type of the CharaUnit sub-object at *(+3160) are unknown, as is the size of the ProjectileUnit sub-object at *(+672).
7. The player slot at CharaUnit +256 / +1880 is unverified. Check it before naming by slot.
8. The string format of the type-name pointer, the `lea; ret` form of the slot-0 getters, and the liveness rule in `sub_140313450` (use count at ctrl+8) are unverified.

**Classification**
9. None of the FIGHT / PRESENTATION / UNKNOWN labels for the owners in item 2 has had a simulation-reader search. The STATS label for StatsUnit is not exhaustive (getter callers `sub_1403458C0`, `sub_140346440`, `sub_140361310`, `sub_1403635C0`, `sub_140494FE0` were not read). For ComboObserver, `sub_140420900` was not inspected.
10. Not established:
    - whether the CharaManager SyncPreMain / Prepare tasks run inside the rollback-stepped frame;
    - whether mgr+448 or the camera drives screen-edge constraints.

    This decides whether the 300 / 112 / 528 drift is cosmetic or a real rollback defect.

**Rollback correctness risks (not only hashing)**
11. **Unregistered state written every simulation tick:** CharaManagerUnit obj+560..+600 (a third 8-int list plus count, written in SyncPostMain `sub_1403DD1B0`) and CharaUnit obj+3632 (written in `sub_140386710` from another unit's +2334). Neither is saved or restored. Confirm they are fully recomputed before every read, or else add them as mod-owned regions.
12. FieldUnit's stage-bounds matrix (+672..+760) is presumed static per stage. Assert at run time that it never changes during a fight.
13. Map entries are never erased (no erase path was found). An object destroyed before the table build leaves stale addresses in the table. Step 6 must check that each owner is alive.
14. The table is rebuilt only on message 1 (presumed BattleStart; the type check against 0x143A651C0 was not mapped to the mod's flow messages). The rollback depth (200 / 60 / 1 frames, via `sub_14047C4D0`) is unverified.
15. Two uncertainties about registrations during battle:
    - It is unverified whether `sub_1404D2160`'s vslot-5 calls register objects spawned mid-battle. If they do, those regions enter the map but not the table until a rebuild, so they would be neither snapshotted nor hashed.
    - ProjectileGroupUnit sets ProjectileUnit+928, the flag that makes a projectile skip its own registration. Whether that happens before or after the projectiles register is unknown. If the projectiles are a preallocated pool, it is likely before.
16. It is not verified that every object a registration covers is reachable from battle+344 or unit+192. Objects held only in other containers will show up as `unowned`; log them and extend the Step 5 table.
17. The other battle containers (battle+312, keyed by unit+112; battle+328, built from unit+116..+148) are not used by this recipe and are uncharacterised.

**Cross-peer specifics (not covered by synctest)**
18. Pointer words and uninitialised padding inside registered blocks have not been listed exhaustively. Rely on the run-time pointer detector and the frame-0 cross-peer diff (section 3), and review what they log before treating fight-hash mismatches as desyncs.