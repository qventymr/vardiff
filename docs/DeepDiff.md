# Deep Diff Engine

[Русский](DeepDiff.ru.md)

`vardiff::deep_diff(before, after, options, keys...)` compares two const objects
without creating a watcher or snapshots. C++20, one header, no dependencies.
It uses the same schema, field comparators, recursive traversal and owned
`value` representation as `diff()` and `watcher`.

## Example

```cpp
#include <vardiff/vardiff.hpp>

struct Item { int id; int count; };
template <> struct vardiff::schema<Item> {
    static auto fields() {
        return std::make_tuple(vardiff::field("id", &Item::id),
                               vardiff::field("count", &Item::count));
    }
};

const std::vector<Item> before{{1, 10}, {2, 20}, {3, 30}};
const std::vector<Item> after{{2, 20}, {3, 30}, {1, 12}};
const auto changes = vardiff::deep_diff(before, after, vardiff::key(&Item::id));
// moved: from="[0]", name="[2]", old_index=0, new_index=2
// modified: name="[2].count", before=10, after=12
```

See [the complete example](../examples/deep_diff.cpp) for an inventory containing
both a vector and `map<string, optional<Item>>`. A registered root object gives
paths such as `inventory[2].count`. Root scalars and containers also work;
the path of a changed root scalar is an empty string.

## Results and paths

The result is `std::vector<vardiff::deep_change>`. `deep_change` extends the
existing `change` type, leaving its four-member aggregate intact.

| Member | Meaning |
| --- | --- |
| `name` | Full destination path; a removal uses its source path |
| `kind` | `modified`, `added`, `removed`, or `moved` |
| `before`, `after` | Owned copies for edits/additions/removals; missing side is empty |
| `from` | Original full path for a move; empty for other events |
| `old_index`, `new_index` | Original/destination sequence indices for a move; otherwise empty |

A move has empty `before` and `after`: it does not copy the moved object. A
moved element can additionally produce recursive leaf edits at its destination.
Added/removed entries produce one event containing the complete entry, rather
than expanding all of its leaves. `optional` presence changes use the contained
value, just as in the existing API. Added vector slots containing empty optionals
still have a nonempty `value` holding an empty `optional<T>`.

Registered fields use dot notation, sequences use zero-based indices, and maps
use keys: `groups[4][0].count`, `equipment["hand"].count`. String keys are quoted;
quotes, backslashes and control bytes are escaped (`\"`, `\\`, `\u000a`). Integral
and enum keys use decimal indices. Other keys need an unambiguous `operator<<`;
their classic-locale representation is quoted. Floating keys use `max_digits10`.
Schema field names retain the existing restrictions: use nonempty names without
dots or square brackets. Pointer keys need an application-defined wrapper with
an unambiguous stream representation if their identity must appear in a path.

The events describe the two complete snapshots; they are **not an executable
patch**. All indices refer to the original inputs, not intermediate mutation
steps. An element can belong to the minimal move set even if its original and
final numeric indices coincide: movement describes its ordering among survivors.

## Sequence identity and moves

```cpp
vardiff::deep_options options;
options.detect_moves = true; // Default.
options.duplicates = vardiff::duplicate_key_policy::reject;
auto changes = vardiff::deep_diff(before, after, options, vardiff::key(&Item::id));
```

`key(&Item::id)` applies to every visited vector/array whose element accepts
that member pointer, including derived elements. The member must support stable equivalence through `==`; a custom `std::hash`
must agree with that equality.
It can be outside `schema<Item>`: identity and compared fields are independent.
The selector borrows keys directly from the input; copy-only keys work.
Null member pointers throw `std::invalid_argument`.

`key("inventory", &Item::id)` limits a selector to that exact container path.
Path-specific selectors take precedence over type-wide selectors. If several
selectors match at the same specificity, the first wins. The options argument
can be omitted when passing selectors. Use `deep_diff(a, b)` for default options
without selectors, or `deep_diff(a, b, options)` for options alone.

Matching by key pairs the first occurrence in `before` with the first in `after`,
then the second with the second, and so on. This deterministic FIFO rule is the
default (`duplicate_key_policy::match_in_order`), including excess duplicates
that become additions/removals. `reject` validates both inputs, even when the
sequence is unchanged, and throws `std::invalid_argument` on duplicates. Excluded
containers are not visited or validated. Hash collisions still use key equality.

Without a selector, elements are matched by their complete registered/equality
values in FIFO order. Still-unmatched elements at the same index are compared
as edits; other unmatched elements are additions/removals. Identity of a changed
and reordered object is inherently ambiguous without a key: provide a selector
when the application knows that identity.

The largest increasing subsequence of matched original indices remains stable;
the other matches are reported as moves. This gives the minimum number of moves
for the chosen matching. Inserting or removing elements does not move all
survivors. Setting `detect_moves=false` suppresses move events while retaining
key matching; sequences without selectors then use the existing index semantics.

## Associative containers and restoration

`std::map` and `std::unordered_map` match by their own key comparison/hash/equality,
recursively comparing mapped values. Rehashing is not a change. Key selectors
affect sequences only. Equivalent map keys with different representations use
the destination key in a matched event's path.

Map support also works with existing `field()`, `diff()`, `changed()`, `track()`,
callbacks and history. Restoring maps recursively updates existing mapped values,
erases excess keys and copies missing entries. Unregistered nested fields and
ignored paths are preserved in existing entries. An ignored whole map entry is
neither erased nor created. A newly created entry comes from its saved value.

Traversal order follows schema registration. Sequences report removals in old
index order, then additions/matches in new index order; a move precedes its
element's edits. Maps visit old entries (removals/matches), then added new entries.
`unordered_map` event order follows native iteration and is not canonical; compare
events by path/kind if an application needs to ignore iteration order.

## Options, limits and exceptions

| Option | Default | Contract |
| --- | --- | --- |
| `detect_moves` | `true` | Detect sequence reordering |
| `duplicates` | `match_in_order` | FIFO pairing or rejection of duplicate explicit keys |
| `max_depth` | `256` | Maximum visited depth; the root has depth zero |
| `ignored_paths` | empty | Exclude a path and descendants, with the existing prefix boundaries |

Depth increases for registered fields, sequence elements, mapped values and
present optional contents. Exceeding the limit throws `std::length_error`.
The limit guards recursive comparison, including equality used during matching.
Whole added/removed values and user comparators/copy operations remain atomic;
their internal recursion is controlled by their own types, not this option.
An empty exclusion path throws `std::invalid_argument`.
Exclusions use destination paths for matched entries/moves and source paths for
removals. They filter events, not identity matching or the minimal move set.

Custom field comparators retain their existing atomic behavior: their field is
one value, without nested key matching or move detection. Comparators must be
pure, stable and callable as const. Inputs must stay alive and unmodified for the
call. No callbacks run. Inputs are unchanged on exceptions; comparator, copying,
hashing, key equality and allocation exceptions propagate to the caller.
Cycles through pointers are not traversed. Results own ordinary copies, with
the same shallow/deep semantics as the corresponding user types.

## Cost and compatibility

Hashable sequence keys use expected O(n+m) matching and O(k log k) move selection,
where k is the number of matches. Adversarial hash collisions can degrade lookup.
Equality-only keys and unhashable unkeyed values use an O(n*m) fallback plus
recursive comparison cost. Strict duplicate validation with equality-only keys
can additionally cost O(n²+m²). Supply a hashable ID for large structured sequences.
Auxiliary matching storage is O(n+m); aligned keyed sequences in FIFO mode do
not allocate a matching index. Matching never copies keys or elements. The
traversal reuses source/destination path buffers and copies only returned event values/paths.
Map lookups cost O((n+m) log(n+m)); unordered-map lookups are expected O(n+m).

The previous `diff()` and watcher sequence semantics stay index-based and do not
emit `moved`. Existing enum values retain their numeric values. Schema registration,
custom comparators, callbacks, options and history contracts remain available.
Build and run [the benchmark](performance.md#deep-diff-benchmark) to inspect
specific workloads; there are no timing thresholds in tests.
