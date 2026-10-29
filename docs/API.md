# API contracts

[Русский](API.ru.md)

The complete library is in `include/vardiff/vardiff.hpp`. The watched object and
the watcher must remain alive during method calls and callback dispatch.

## Fields and supported types

`field(name, member)` uses `operator==`, or recursive comparison for registered
structures and supported containers. `field(name, member, compare)` uses the
supplied equality predicate. It must be copyable, callable as const, and accept
two `const T&` arguments; `true` means equal.

Supported values include copy-constructible types with equality, registered
structures, `std::vector<T>`, `std::optional<T>`, `std::array<T, N>`, and their
combinations. Copy-only types with a deleted move constructor are supported.
A type with a custom field comparator does not need `operator==`.

Comparators must be pure and deterministic for the same arguments: a method
may compare a value more than once. A custom comparator makes its whole field
atomic, so a container or structure produces one change instead of child paths.

Names are copied. Use nonempty names without dots or square brackets for
unambiguous paths; these characters are not escaped. Null member pointers throw
`std::invalid_argument`. Bit-fields, static members, volatile fields, and
noncopyable values are outside the field API.

## Registration and object lifetime

Specialize `vardiff::schema<T>` with a static `fields()` function returning a
nonempty tuple of descriptors. Declare the specialization before first use.
Register nested types before their containing structures. Recursive value trees
such as a Node containing `std::vector<Node>` are supported.

A schema must return consistent field descriptions. Nested schema metadata is
cached on first use. Registered types compare only registered fields, even
when the type also defines `operator==`.

`track(object, fields...)` accepts an lvalue and captures its initial state.
`track(object, options, fields...)` supplies `watch_options`.
Registered roots also support `track(object)` and `track(object, options)`.
Temporaries are rejected.

The watcher borrows the object's address. Container storage inside the object
may reallocate, but the watched object itself must stay alive at the same address.
Snapshots and diffs own copies using each type's normal copy semantics; pointers
or shared resources inside custom types keep their normal semantics.

Watcher copies observe the same object and have independent history, cursors,
options, and callback objects. Immutable saved states may share internal storage
between watcher copies. Live object edits cannot alter those saved values.

## Reading and dispatch

| Method | Result | Baseline effect | Dispatches callbacks |
| --- | --- | --- | --- |
| `changed() const` | bool | None | No |
| `diff() const` | vector of changes | None | No |
| `poll()` | vector of changes | Saves observed state when changed and auto-update is enabled | When changed |
| `snapshot()` | void | Appends current values | No |
| `undo()`, `rollback()` | bool | Restores previous saved state | No |
| `redo()` | bool | Restores next saved state | No |
| `rollback(position)` | bool | Restores chosen retained state | No |

Checks happen at method-call time. An edit followed by a return to the baseline
value leaves no difference.

Diff ordering follows descriptor order, recursively followed by schema order
and increasing container indices. `change.name` is the full path. `before`
and `after` own their values and can outlive both watcher and object.

`value.get<T>()` returns a const reference; a wrong type or an absent value throws
`std::bad_variant_access`. `value.has_value()` distinguishes a missing side from
a present default-valued item. `value.type()` exposes the stored type information.

Stream output uses a type's own operator when available. Otherwise, containers
print as `[a, b]`, registered structures as `{field: value}`, empty optionals as
`<missing>`, and other unprintable types as `<value>`. Stream formatting settings
are respected.

Equality between `value` objects first compares stored types. A comparator-only
custom type has no general equality available in the value wrapper: comparing
two such wrappers throws `std::logic_error`. The field-specific comparator is
not stored inside each diff value.

## poll() sequencing

1. Check for differences from the current baseline. If unchanged, return an empty
   vector without copying fields, adding history, or dispatching callbacks.
2. Capture selected fields into an observed state and build the event list.
3. Dispatch callbacks in registration order.
4. If automatic snapshots were enabled at entry, save that observed state.
   Return the event list.

`on_change(callback)` receives `const std::vector<change>&` once per changed poll.
`on_change(path, callback)` receives `const change&` for each matching event,
in diff order. A path subscription includes descendants: `items` matches
`items[0].price`, while `hp` does not match `hp_max` and `items[1]` does not
match `items[10]`. Empty callbacks or subscription paths are rejected.

Callbacks run only inside `poll()`. Editing a field, calling `changed()` or
`diff()`, or restoring history does not dispatch them.

Callback edits to the object remain pending for the next poll: the automatic
snapshot saves the state captured before dispatch. New registrations and changes
to ignore/auto-update settings affect later checks; the current event list and
auto-update decision have already been fixed. Mutable callbacks retain their
internal state between calls.

Reading diffs/history, registering callbacks, and changing ignore/auto-update
settings during a callback are allowed. Recursive `poll()`, `snapshot()`,
undo/redo/rollback, history-limit changes, and copying/moving/assigning the
active watcher throw `std::logic_error`.

If a callback throws, later callbacks are skipped and the baseline/history are
not committed. The exception propagates. Object edits and external effects of
callbacks already run remain in place, so the next poll may report changes again.
Copy a callback's event list if you need to retain it after dispatch.

## Options and ignored paths

`watch_options` defaults to `auto_snapshot = true` and `history_limit = 64`.
Use `set_auto_snapshot(bool)` and `auto_snapshot_enabled()` to change/read the
mode. With auto-update disabled, repeated polls compare with the old baseline
until an explicit snapshot.

Unchanged polls never save a new baseline. This also avoids gradual baseline
drift with tolerance-based comparisons.

`ignore(path)` excludes a path and descendants from detection, dispatch, and
restoration. Unknown paths are allowed because future container entries may
match them. Duplicate exclusions are ignored; empty paths are rejected.
Snapshots still copy all selected top-level values.

Restoration restores container size and optional presence from the saved state.
Ignored leaves are preserved when their containing values exist in both states.
When an element is created or removed as a whole, its shape comes from the
snapshot. For example, ignoring `items[2].price` does not prevent restoration
from removing `items[2]`. A custom-comparator field is atomic, so child-path
exclusions do not affect its internal values.

## Container events

Vector and array elements match by index; there is no identity matching or move
detection. Inserting in the middle of a vector may change later indices.

Shared indices are compared recursively. An added or removed element produces
one event containing the whole element. `kind` is `modified`, `added`, or
`removed`. The absent side has an empty `vardiff::value`.

Arrays have fixed size. Vectors may grow/shrink; `vector<bool>`, empty arrays,
and elements without default constructors are supported.

Two engaged optionals recurse into their contents at the same path. A presence
transition produces one added/removed event containing the underlying value.
When adding an item to `vector<optional<T>>`, the vector slot exists even if
its optional is empty: `after.has_value()` is true while
`after.get<std::optional<T>>()` may be empty.

## History and restoration

`history()` returns independent copies in a vector. Each snapshot is a
`watcher::snapshot_type` tuple in descriptor order. Modifying this result does
not alter the watcher. Use `history_size()` for a count without copying.

`history_position()` is the current index in that list; the initial snapshot
starts at zero. `can_undo()` and `can_redo()` test neighboring availability.

An explicit `snapshot()` always appends, including equal states. Automatic
entries are appended only after changed polls. Appending from the middle of
history discards the redo branch; without a new snapshot, redo remains available.

Undo restores the previous saved state; redo restores the next. At either
boundary they return false without touching the object. `rollback()` is undo.
`rollback(position)` restores a chosen retained state, including the current one,
so it can discard pending edits without moving the cursor. An invalid position
throws `std::out_of_range`. Restoration never automatically saves pending edits.

Only selected fields are restored. Within a registered structure, only its
registered fields are assigned; unregistered data remains intact. Atomic
custom-comparator fields are restored as a whole.

Restoration methods require a mutable object and assignable selected fields.
Nested restored leaves must also permit assignment. Read-only objects and const
members still support observation, callbacks, and history.

The history limit includes the current state and must be at least one.
`set_history_limit(N)` changes it. Normal appends evict the oldest states.
When reducing the limit after undo, older states are removed up to the current
one, then the farthest redo states are removed. The current state always remains;
list indices can change after eviction.

Snapshot capture and history commit prepare new data before replacing the
baseline. Copy/allocation failure preserves the prior history and baseline.
Restoration performs user-defined assignments: if one throws, the object may
be partially restored while the history cursor stays unchanged.

## Floating-point values and scope

Default comparison is exact equality: positive and negative zero compare equal,
while NaN remains changed immediately after a snapshot. Supply a custom equality
predicate for tolerances or NaN handling.

Pointer values compare as pointers, without traversing their pointees.
Cyclic pointer graphs, automatic reflection, JSON, and thread synchronization
are not implemented.
