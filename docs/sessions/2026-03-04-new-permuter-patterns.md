# Session: New Permuter Patterns from Git/DB Mining (2026-03-04)

## Context

Mined git history (dev branch diffs) and the decomp.db attempt notes to identify recurring mechanical source-level fixes that could be automated as permuter patterns. Created 8 new patterns this session, identified 5 more for future implementation.

## Implemented Patterns

### 1. `temp_elimination` — Inline single-use temp variables

**Source**: Manual fixes for ClipPredict::Predict and RndOverlay::CurrentLine.

**What it does**: When a local variable is declared, initialized, and used exactly once, substitute the initializer expression at the use site. This changes register allocation — the value comes from a volatile return register instead of a callee-saved spill.

```cpp
// Before:
float norm1 = LimitAng(mLastAng - locf);
mAng = LimitAng(norm1 + mAng);

// After:
mAng = LimitAng(LimitAng(mLastAng - locf) + mAng);
```

Also includes **iterator helper substitution** (Milo-specific):
```cpp
// Before:
std::list<String>::iterator it = mLines.end();
mLine = --it;

// After:
mLine = PrevItr(mLines.end());
```

**Strategies**: Single-use elimination, PrevItr/NextItr substitution, multi-temp batch elimination.

**Triggers**: Commutative operand swaps (fadds, fmuls), callee-saved register swaps, clusters.

### 2. `member_ref_bind` — Hoist member/param accesses into local references

**Source**: DB notes for SetTransform (r30/r31 fix), Print (r29/r30 fix).

**What it does**: When a member variable (this->mFoo or plain mFoo) is used multiple times, bind it to a local reference. This shifts the live-range start point earlier, changing callee-saved register assignment.

```cpp
// Before:
mLines.end(); ... mLines.begin(); ...

// After:
auto& _ref0 = mLines;
_ref0.end(); ... _ref0.begin(); ...
```

Also extracts ObjOwnerPtr smart pointer members to raw pointers, which changes `cmpwi cr6` (signed) to `cmplwi cr0` (unsigned) for null checks.

**Triggers**: Callee-saved GPR swaps (r13-r31), cmpwi/cmplwi replace mismatches, CR field differences.

### 3. `fabs_variant` — Swap fabs/fabsf/std::fabs

**Source**: Git history — 2+ commits with fabs variant swaps.

**What it does**: The compiler generates different instructions for `fabs()` (double) vs `fabsf()` (float) vs `std::fabs()` (overloaded). Swapping between them can fix instruction width mismatches.

```cpp
fabs(x)       -> fabsf(x)      // double to float
fabsf(x)      -> std::fabs(x)  // float to C++ overload
std::fabs(x)  -> fabs(x)       // C++ overload to C double
```

**Triggers**: lfd vs lfs mismatches, fabs instruction differences, replace mismatches.

### 4. `milo_log_swap` — Swap MILO_WARN/NOTIFY/LOG/FAIL macros

**Source**: Git history + DB notes — 2+ commits with log macro swaps.

**What it does**: Milo logging macros generate different code sizes and branch patterns. MILO_WARN includes file/line metadata, MILO_NOTIFY is lighter, MILO_LOG is simplest, MILO_FAIL triggers assertion. Also handles MILO_NOTIFY_ONCE (has static guard variable).

```cpp
MILO_WARN("x is %d", x)   -> MILO_NOTIFY("x is %d", x)
MILO_NOTIFY(...)           -> MILO_LOG(...)
MILO_NOTIFY_ONCE(...)      -> MILO_NOTIFY(...)
```

**Triggers**: Insert/delete clusters (log macros vary in generated code size), store/load mismatches.

### 5. `float_double_literal` — Swap `0.001` vs `0.001f` literal suffixes

**Source**: Git history + documented in CLAUDE.md as known pattern.

**What it does**: In MSVC for PowerPC, `0.001` (double) generates `lfd` (64-bit float load), while `0.001f` (float) generates `lfs` (32-bit load). Wrong literal type cascades into register width mismatches.

```cpp
float y = x + 6.0;   -> float y = x + 6.0f;   // double to float
float y = x + 6.0f;  -> float y = x + 6.0;     // float to double
```

Also generates a bulk variant that flips ALL literals in a function at once.

**Triggers**: lfd vs lfs mismatches, frsp (round-to-single) differences.

### 6. `objptr_bool_extract` — Extract ObjPtr to raw pointer before && chains

**Source**: DB notes — 3 proven manual fixes (TexMovie::Enter, TexMovie::DrawToTexture, SfxInst::IsRunning).

**What it does**: When an ObjOwnerPtr<T> smart pointer is used in a `&&` chain, the compiler generates `cmpwi cr6` (signed, deferred CR field). Extracting into a raw `T*` local causes `cmplwi cr0` (unsigned, immediate branch).

```cpp
// Before:
bool b = (mTex && mTex->Width() && mTex->Height());

// After:
auto *_ptr0 = mTex.Ptr();
bool b = (_ptr0 && _ptr0->Width() && _ptr0->Height());
```

Handles both `if`-statement conditions and bool declaration initializers. Generates variants with `auto*`, `.Ptr()` extraction.

**Known limitation**: `auto*` doesn't compile for ObjOwnerPtr (no implicit pointer conversion). The `.Ptr()` variant works but may change codegen differently than using the explicit type (`RndTex*`). Ideally needs type inference or a type hint from the user.

**Triggers**: cmplwi vs cmpwi mismatches, CR field differences, beq/bne differences.

### 7. `iterator_deref_style` — `(*it).member` vs `it->member`

**Source**: Git history — 6 instances in one CharBlendBone commit.

**What it does**: The compiler may generate different code for `(*it).member` vs `it->member` due to different operator overload paths (operator* returns reference, operator-> returns pointer).

```cpp
(*it).mWeight  -> it->mWeight
it->mTarget    -> (*it).mTarget
```

Generates individual swap variants plus a bulk "convert all" variant. Only targets iterator-like variable names (it, iter, itr, etc.).

**Triggers**: Replace mismatches, register swaps, clusters.

### 8. `assignment_reorder` — Reorder consecutive assignment statements

**Source**: DB notes — PlayBack::Reset field assignment order was critical for matching.

**What it does**: Unlike `declaration_reorder` which permutes variable declarations, this reorders consecutive assignment statements to the same base object. MSVC emits stores in source order, so reordering fixes offset swap and instruction ordering mismatches.

```cpp
// Before:
w.unk18 = 0;
w.unk1c = 0;
w.unk14 = 0;

// After (pairwise swap):
w.unk14 = 0;
w.unk1c = 0;
w.unk18 = 0;
```

Generates pairwise swaps for runs of 2-4, plus a reverse-all variant for runs of 3+.

**Triggers**: Offset swap patterns, store instruction mismatches (stw, stfs, stfd), clusters.

## Deferred Patterns (Not Yet Implemented)

### A. `static_symbol_reorder` — Permute static Symbol declaration order

**Source**: Git history — appears in 8+ commits, very high frequency.

**What it does**: MSVC assigns addresses to `static` local variables in declaration order, affecting register allocation and code layout. Reordering blocks of consecutive `static Symbol` declarations can fix regswaps.

```cpp
// Before:
static Symbol double_xp_weekend("double_xp_weekend");
static Symbol completed_song_with_1_star("completed_song_with_1_star");

// After (reordered):
static Symbol completed_song_with_1_star("completed_song_with_1_star");
static Symbol double_xp_weekend("double_xp_weekend");
```

**Why deferred**: Large permutation space for blocks of 5+ symbols. Needs smart sampling or BSF-guided approach like `declaration_reorder`. High impact but medium complexity.

**Implementation plan**: Find consecutive `static Symbol name("name")` declarations. Generate pairwise swaps and random permutations (cap at 20). Could also use the declaration_reorder infrastructure since the approach is similar.

### B. `modulo_to_mul_sub` — Replace `%` with multiply-subtract

**Source**: Git history — 3 instances in Utl.cpp time formatting.

**What it does**: The compiler generates different code for `seconds % 60` vs `seconds - mins * 60`. The manual multiply-subtract form sometimes matches the target.

```cpp
// Before:
int secs = seconds % 60;

// After (when mins = seconds / 60 already exists):
int secs = seconds - mins * 60;
```

**Why deferred**: Only 3 instances found, requires detecting that the divisor variable already exists. Low frequency.

**Implementation plan**: Find `expr % constant` where the same expression was previously divided by the same constant in the same function. Replace with `expr - quotient * constant`.

### C. `bool_intermediate` — Hoist if-condition into bool local

**Source**: Git history — 2 instances in ByteGrinder.cpp.

**What it does**: Extracting a comparison result into a `bool` variable before the `if` changes register allocation timing.

```cpp
// Before:
if (da->Size() > 2) { ... }

// After:
bool moreThanTwo = da->Size() > 2;
if (moreThanTwo) { ... }
```

**Why deferred**: Inverse of some `temp_elimination` cases. Low frequency (2 instances). Could conflict with existing patterns.

**Implementation plan**: Find `if (expr)` where expr contains a function call or complex expression. Hoist into `bool _b = expr; if (_b)`.

### D. `for_to_do_while` — Convert for-loop to do-while

**Source**: Git history — instances in ResourceDirPtr.cpp, BustAMovePanel.cpp, CharBones.cpp.

**What it does**: Converting a `for` loop to an equivalent `do-while` with manual increment and explicit `break` changes branch structure.

```cpp
// Before:
for (int i = 0; i < n; i++) { body; }

// After:
unsigned int i = 0;
do { if (i >= n) break; body; i++; } while (true);
```

**Why deferred**: Structural transformation, hard to automate safely without introducing bugs. Medium complexity, medium frequency.

### E. `loop_bound_direction` — Swap loop bound comparison style

**Source**: Git history — 3+ instances.

**What it does**: Swap equivalent comparison forms that generate different branch instructions.

```cpp
h > 0    -> h >= 1      // bgt vs bge
size > 1 -> size >= 2   // same
i < n    -> n > i       // operand swap
```

**Why deferred**: Partially covered by existing `signed_unsigned` and `comparison_flip` patterns. The `> 0` to `>= 1` transform is new but low frequency.

**Implementation plan**: Find comparisons against integer literals. Generate equivalent forms: `> N` ↔ `>= N+1`, `< N` ↔ `<= N-1`.

## Test Coverage

All 8 implemented patterns have unit tests in `scripts/permuter/tests/test_patterns.py`:

| Test ID | Pattern | Validates |
|---------|---------|-----------|
| `tmpelim_inline_single_use` | temp_elimination | Single-use temp inlining |
| `ptrext_bool_decl_chain` | objptr_bool_extract | Bool decl && chain extraction |
| `fltlit_add_f_suffix` | float_double_literal | Double to float literal |
| `fltlit_remove_f_suffix` | float_double_literal | Float to double literal |
| `fabs_to_fabsf` | fabs_variant | fabs → fabsf |
| `fabsf_to_stdfabs` | fabs_variant | fabsf → std::fabs |
| `logswap_warn_to_notify` | milo_log_swap | MILO_WARN → MILO_NOTIFY |
| `logswap_notify_to_log` | milo_log_swap | MILO_NOTIFY → MILO_LOG |
| `asgnreorder_swap_pair` | assignment_reorder | Pairwise assignment swap |
| `itderef_star_to_arrow` | iterator_deref_style | (*it).member → it->member |

157 total tests, all passing.

## Files Changed

| File | Change |
|------|--------|
| `scripts/permuter/patterns/temp_elimination.py` | New pattern |
| `scripts/permuter/patterns/member_ref_bind.py` | New pattern |
| `scripts/permuter/patterns/fabs_variant.py` | New pattern |
| `scripts/permuter/patterns/milo_log_swap.py` | New pattern |
| `scripts/permuter/patterns/float_double_literal.py` | New pattern |
| `scripts/permuter/patterns/objptr_bool_extract.py` | New pattern |
| `scripts/permuter/patterns/iterator_deref_style.py` | New pattern |
| `scripts/permuter/patterns/assignment_reorder.py` | New pattern |
| `scripts/permuter/patterns/__init__.py` | Register all 8 new patterns |
| `scripts/permuter/tests/test_patterns.py` | 10 new test fixtures + 5 diagnosis helpers |

## Numbers

- **Before**: 31 patterns
- **After**: 39 patterns
- **Target populations** (from DB):
  - 33 AT_LIMIT with BOOL_MASK (objptr_bool_extract)
  - 29 AT_LIMIT with OFFSET_SWAP only (assignment_reorder)
  - 142 AT_LIMIT with REGISTER_SWAP only (temp_elimination, member_ref_bind)
  - 14 AT_LIMIT with COMMUTATIVE_OP_ORDER only (temp_elimination)
  - 124 AT_LIMIT tagged `has_fixable_*` (various patterns)
