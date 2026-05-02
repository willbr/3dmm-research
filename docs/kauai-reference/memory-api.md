# Memory management API

> Markdown port of [`kauai/doc/mem.txt`](../../kauai/doc/mem.txt). The
> plain-text source is authoritative; this copy adds formatting.

## Types

```cpp
HQ
```
An opaque handle to a generic memory block. Implementation is private.

```cpp
HN
```
An operating system handle. On Mac, these are `Handle`s. On Windows,
they are `HGLOBAL`.

## Constants

```cpp
const byte kbGarbage;
```
New blocks are filled with this (if they are not zeroed).

```cpp
const long kcbMax;
```
Maximum size of an `HQ`.

```cpp
HQ hqNil;
pNil;
hNil;
```
It is legal to assume these are zero. Use `hqNil` to compare against
an explicit hq, `pNil` for pointers and `hNil` for any other abstract
handle.

- `fhqNil` — default options
- `fhqClear` — zero newly allocated memory

## Routines

```cpp
HQ HqAlloc(long cb, ushort grfhq);
```
Allocates an hq and optionally clears it.

```cpp
void FreeHq(HQ hq);
```
Frees an hq. Accepts `hqNil`.

```cpp
void FreePhq(HQ *phq);
```
Calls `FreeHq` on `*phq` and sets `*phq` to `hqNil`.

```cpp
long CbOfHq(HQ hq);
```
Returns the size of the hq. This is guaranteed to be the same as the
`cb` passed to `HqAlloc` (or `FResizePhq`).

```cpp
HQ HqCopyHq(HQ hq);
```
Duplicates the hq.

```cpp
bool FResizePhq(HQ *phq, long cb, ushort grfhq);
```
Resizes `*phq`. The value of `*phq` may change.

```cpp
void *PvLockHq(HQ hq);
```
Lock the hq and return a pointer to the data.

```cpp
void UnlockHq(HQ hq);
```
Unlock the hq. Must balance a call to `PvLockHq` or `LockHq`.

```cpp
void *QvFromHq(HQ hq);
```
Return a volatile pointer to the hq block.

## Generic pointer arithmetic

```cpp
void *PvAddBv(void *pv, long bv);
```
Add an offset to a pointer.

```cpp
void *PvSubBv(void *pv, long bv);
```
Subtract an offset from a pointer.

```cpp
long BvSubPvs(void *pv1, void *pv2);
```
Subtract two pointers to get the number of bytes between them.

## Debug-only API

```cpp
void UnmarkAllHqs(void);
```
Clear the marks on all hq's.

```cpp
void MarkHq(HQ hq);
```
Marks an hq.

```cpp
void AssertUnmarkedHqs(void);
```
Asserts on any unmarked hqs.

```cpp
void AssertHq(HQ hq);
```
Validate an hq. `hq` should not be nil.
