# Groups API

> Markdown port of [`kauai/doc/groups.txt`](../../kauai/doc/groups.txt).
> The plain-text source is authoritative; this copy adds formatting and
> uses the current modern type names.

Collection classes:

- `DynamicArray` (general list, GL)
- `AllocatedArray` (allocated list, AL)
- `GeneralGroup` (GG)
- `AllocatedGroup` (AG)
- `StringTable_GST` (general string table)
- `AllocatedStringTable` (allocated string table)

## Types

```cpp
class DynamicArray;
typedef class DynamicArray *PDynamicArray;
```
A general purpose dynamically allocated array class. Data elements
must be all the same size. Clients may assume the data elements are
contiguous. I.e., if you have a pointer to the iv'th element,
incrementing the pointer will give you a pointer to the (iv+1)th
element.

```cpp
class AllocatedArray;
typedef class AllocatedArray *PAllocatedArray;
```
A list where indices don't change when items are added or deleted.
Clients may assume data elements are contiguous. Note, however, that
an element may be free. Call `FFree` to determine if a particular item
is free.

```cpp
class GeneralGroup;
typedef class GeneralGroup *PGeneralGroup;
```
A group: a collection of data objects of possibly different sizes. If
all your elements are the same size, use a `DynamicArray`.

```cpp
class AllocatedGroup;
typedef class AllocatedGroup *PAllocatedGroup;
```
A group where indices don't change when items are added or deleted.
Entries can be free. Call `FFree` to determine if an entry is free.

```cpp
class StringTable_GST;
typedef class StringTable_GST *PStringTable_GST;
```
A string table. Can also store a fixed size piece of "extra" data for
each string.

```cpp
class AllocatedStringTable;
typedef class AllocatedStringTable *PAllocatedStringTable;
```
An allocated string table. Like a string table, but indices don't
change when strings are added or deleted. Entries can be free. Call
`FFree` to determine if an entry is free.

## Constants

- `fgrpNil` — default options
- `fgrpShrink` — shrink space allocated if it's more than needed

## Common methods

```cpp
PDynamicArray         DynamicArray::PglNew(long cb, long cvInit = 0);
PAllocatedArray       AllocatedArray::PalNew(long cb, long cvInit = 0);
PGeneralGroup         GeneralGroup::PggNew(long cvInit = 0, long cbInit = 0);
PAllocatedGroup       AllocatedGroup::PagNew(long cvInit = 0, long cbInit = 0);
PStringTable_GST      StringTable_GST::PgstNew(long cbExtra, long cstzInit = 0, long cchInit = 0);
PAllocatedStringTable AllocatedStringTable::PastNew(long cbExtra, long cstzInit = 0, long cchInit = 0);
```
These are static methods (not invoked from an object) that allocate a
new object of the appropriate class. For `DynamicArray` and
`AllocatedArray` classes, `cb` is the size of the elements and `cvInit`
is the number of elements to reserve space for. For `StringTable_GST`
and `AllocatedStringTable`, `cbExtra` is the amount of extra data
associated with each string. For `GeneralGroup`, `AllocatedGroup`,
`StringTable_GST` and `AllocatedStringTable`, `cvInit`/`cstzInit` are
how many entries to make room for and `cbInit`/`cchInit` are the total
number of bytes to allow for these entries. These values are as in
`FEnsureSpace`.

```cpp
PDynamicArray         DynamicArray::PglRead(PFileObject pfil, FilePosition fp, long cb, short *pbo, short *posk);
PAllocatedArray       AllocatedArray::PalRead(PFileObject pfil, FilePosition fp, long cb, short *pbo, short *posk);
PGeneralGroup         GeneralGroup::PggRead(PFileObject pfil, FilePosition fp, long cb, short *pbo, short *posk);
PAllocatedGroup       AllocatedGroup::PagRead(PFileObject pfil, FilePosition fp, long cb, short *pbo, short *posk);
PStringTable_GST      StringTable_GST::PgstRead(PFileObject pfil, FilePosition fp, long cb, short *pbo, short *posk);
PAllocatedStringTable AllocatedStringTable::PastRead(PFileObject pfil, FilePosition fp, long cb, short *pbo, short *posk);
```
Static methods to read an object from disk. `pbo` and `posk` may be
`nil`. `*pbo` is set to the byte order of the object and `*posk` is
set to the osk (Operating System Kind) that wrote the object. Use
`*pbo` to do byte swapping after reading and `*posk` to do string
translation after reading.

```cpp
bool DynamicArray::FWrite(PFileObject pfil, FilePosition fp, long *pcb = pvNil,
        short bo = kboCur, short osk = koskCur);
// (and FWrite for AllocatedArray, GeneralGroup, AllocatedGroup,
//  StringTable_GST, AllocatedStringTable — same signature.)
```
Write the object to disk. If `pcb` is not `nil`, sets `*pcb` to the
amount of space used. If `bo` and `osk` are specified as something
different from `kboCur` and `koskCur`, these will adjust the byte
order and/or osk before writing the data. Obviously, client managed
data is not adjusted. *(REVIEW shonk: should strings in a
StringTable_GST/AllocatedStringTable be handled automatically.)*

```cpp
long DynamicArray::CbOnFile(void);
// (and CbOnFile for the other five containers.)
```
Returns the amount of space required to write the object to disk.

```cpp
long DynamicArray::IvMac(void);
long AllocatedArray::IvMac(void);
long GeneralGroup::IvMac(void);
long AllocatedGroup::IvMac(void);
long StringTable_GST::IstzMac(void);
long AllocatedStringTable::IstzMac(void);
```
Returns the number of active items. For `AllocatedArray`,
`AllocatedGroup` and `AllocatedStringTable`, this is the lim of legal
indices, not the number of non-free entries.

```cpp
void DynamicArray::Delete(long iv);
// (and Delete for the other five containers.)
```
Deletes the indicated element. For `AllocatedArray`, `AllocatedGroup`
and `AllocatedStringTable`, indices of remaining items don't change
(ie, a hole is created). For the other classes, items with larger
indices "slide down" to fill the hole.

```cpp
void *DynamicArray::QvGet(long iv);
void *AllocatedArray::QvGet(long iv);
void *GeneralGroup::QvGet(long iv);
void *AllocatedGroup::QvGet(long iv);
```
Returns a volatile pointer to the element. For GPs, `iv` must be less
than `IvMac()`. For `DynamicArray` and `AllocatedArray`, `iv` must be
`<= IMac()` (`==` is allowed for convenience; don't access it!). For
`AllocatedArray` and `AllocatedGroup`, the element may be free (call
`FFree` to check).

```cpp
void *DynamicArray::PvLock(long iv);
// (and PvLock for AllocatedArray, GeneralGroup, AllocatedGroup.)
```
Locks the data and returns a pointer to the iv'th element. Same
restrictions on `iv` as for `QvGet(iv)`.

```cpp
void DynamicArray::Lock(void);
// (and Lock for AllocatedArray, GeneralGroup, AllocatedGroup.)
void DynamicArray::Unlock(void);
// (and Unlock for the same set.)
```
`Lock` locks the data. `Unlock` unlocks the data; balances a call to
`PvLock()` or `Lock()`. Locking is nestable.

```cpp
void DynamicArray::Get(long iv, void *pv);
// (and Get for AllocatedArray, GeneralGroup, AllocatedGroup.)
```
Copies the iv'th item to `*pv`. Assumes `pv` points to a block large
enough to hold the item. `iv` must be less than `IvMac()`. For
`GeneralGroup` and `AllocatedGroup`, `GetRgb` is more useful.

```cpp
void DynamicArray::Put(long iv, void *pv);
// (and Put for AllocatedArray, GeneralGroup, AllocatedGroup.)
```
The opposite of `Get(iv, pv)`. Copies data from `*pv` to the iv'th
element. For `GeneralGroup` and `AllocatedGroup`, see `FPut` and
`PutRgb`.

```cpp
bool DynamicArray::FAdd(void *pv, long *piv);
bool AllocatedArray::FAdd(void *pv, long *piv);
bool GeneralGroup::FAdd(long cb, void *pv, long *piv);
bool AllocatedGroup::FAdd(long cb, void *pv, long *piv);
```
Add a new element to the class. If `piv` is not `nil`, `*piv` is set
to the index of the new item. For `DynamicArray` and `GeneralGroup`,
the new item is always the last. For `AllocatedArray` and
`AllocatedGroup`, the first free slot is used.

## DynamicArray-specific methods

```cpp
long DynamicArray::CbEntry(void);
```
Returns the size of each element of the list (as specified in `PglNew`
or as read from file in `PglRead`).

```cpp
bool DynamicArray::FSetIvMac(long ivMacNew);
```
Changes the number of items active in the list. Rarely used.

```cpp
bool DynamicArray::FEnsureSpace(long cvAdd, short grfgrp);
```
Ensure that there is room for at least `cvAdd` additional items. To
close up the list, use `FEnsureSpace(0, fgrpShrink)`.

```cpp
bool DynamicArray::FInsert(long iv, void *pv);
```
Inserts a new item into the list at location `iv`. Parameter `iv` must
be less than or equal to `IvMac()`. Moves later items up to make room.

```cpp
void Move(long ivSrc, long ivDst);
```
Moves the element at `ivSrc` to the `ivDst` position, shifting
everything in between.

```cpp
bool DynamicArray::FPush(void *pv);
```
Appends the item to the end of the list. Equivalent to
`FAdd(pv, pvNil)`.

```cpp
bool DynamicArray::FPop(void *pv = pvNil);
```
If the list is empty, returns false (indicating stack underflow).
Otherwise, fetches the last item (if `pv` is not `nil`), deletes the
item, and returns true.

```cpp
bool DynamicArray::FEnqueue(void *pv);
```
Equivalent to `FInsert(0, pv)`.

```cpp
bool DynamicArray::FDequeue(void *pv = pvNil);
```
Equivalent to `FPop(pv)`.

## AllocatedArray-specific methods

```cpp
long AllocatedArray::CbEntry(void);
```
Returns the size of each element of the list (as specified in `PalNew`
or as read from file in `PalRead`).

```cpp
bool AllocatedArray::FEnsureSpace(long cvAdd, short grfgrp);
```
Ensure that there is room for at least `cvAdd` additional items. To
close up the allocated list use `FEnsureSpace(0, fgrpShrink)`.

```cpp
bool AllocatedArray::FFree(long iv);
```
Indicates whether item `iv` is free. (This is fast.) `iv` should be
less than `IvMac()`.

## Group-specific methods

```cpp
bool GeneralGroup::FEnsureSpace(long cvAdd, long cbAdd, short grfgrp);
bool AllocatedGroup::FEnsureSpace(long cvAdd, long cbAdd, short grfgrp);
```
Make sure there is room for at least `cvAdd` additional items, using
`cbAdd` bytes of space. To close up the group use
`FEnsureSpace(0, 0, fgrpShrink)`.

```cpp
bool GeneralGroup::FInsert(long iv, long cb, void *pv);
```
Inserts an item of size `cb` into the group. `pv` may be `nil`.

```cpp
bool GeneralGroup::FPut(long iv, long cb, void *pv);
bool AllocatedGroup::FPut(long iv, long cb, void *pv);
```
Replaces the item with new data of the given length. `pv` may be
`nil`, in which case this effectively resizes the element.

```cpp
long GeneralGroup::Cb(long iv);
long AllocatedGroup::Cb(long iv);
```
Returns the length of the item.

```cpp
void GeneralGroup::GetRgb(long iv, long bv, long cb, void *pv);
void AllocatedGroup::GetRgb(long iv, long bv, long cb, void *pv);
```
Fetches a section of data from the given element. `bv` is the offset
of the data into the element, `cb` is the amount of data to copy and
`pv` is the destination.

```cpp
void GeneralGroup::PutRgb(long iv, long bv, long cb, void *pv);
void AllocatedGroup::PutRgb(long iv, long bv, long cb, void *pv);
```
The inverse of `GetRgb`.

```cpp
void GeneralGroup::DeleteRgb(long iv, long bv, long cb);
void AllocatedGroup::DeleteRgb(long iv, long bv, long cb);
```
Deletes a portion of the given element. Deletes bytes `[bv, bv+cb)`.

```cpp
bool GeneralGroup::FInsertRgb(long iv, long bv, long cb, void *pv);
bool AllocatedGroup::FInsertRgb(long iv, long bv, long cb, void *pv);
```
Inserts `cb` new bytes before byte `bv` of the element. `pv` may be
`nil`.

```cpp
bool AllocatedGroup::FFree(long iv);
```
Indicates whether item `iv` is free. (This is fast.) `iv` should be
less than `IvMac()`.

## String-table-specific methods

```cpp
bool StringTable_GST::FEnsureSpace(long cstzAdd, long cchAdd, short grfgrp);
bool AllocatedStringTable::FEnsureSpace(long cstzAdd, long cchAdd, short grfgrp);
```
Make sure there is room for at least `cstzAdd` additional strings,
using `cchAdd` bytes of space. To close up the string table use
`FEnsureSpace(0, 0, fgrpShrink)`.

```cpp
bool StringTable_GST::FInsertRgch(long istz, char *prgch, long cch, void *pvExtra);
bool StringTable_GST::FInsertStz(long istz, char *pstz, void *pvExtra);
bool StringTable_GST::FInsertSt(long istz, char *pst, void *pvExtra);
bool StringTable_GST::FInsertSz(long istz, char *psz, void *pvExtra);
```
Insert a new string at location `istz`. `pvExtra` may be `nil`.

```cpp
bool StringTable_GST::FAddRgch(char *prgch, long cch, void *pvExtra, long *pistz);
bool StringTable_GST::FAddStz(char *pstz, void *pvExtra, long *pistz);
bool StringTable_GST::FAddSt(char *pst, void *pvExtra, long *pistz);
bool StringTable_GST::FAddSz(char *psz, void *pvExtra, long *pistz);
// (and AllocatedStringTable variants.)
```
Append a string to the string table. `pistz` may be `nil`. If not,
`*pistz` is set the index of the new string. `pvExtra` may be `nil`.

```cpp
void StringTable_GST::GetExtra(long istz, void *pv);
void AllocatedStringTable::GetExtra(long istz, void *pv);
```
Fill `pv` with the extra data for the string. Asserts that `cbExtra`
is `> 0`.

```cpp
void StringTable_GST::PutExtra(long istz, void *pv);
void AllocatedStringTable::PutExtra(long istz, void *pv);
```
Set the extra data for the string. Asserts that `cbExtra` is `> 0`.

```cpp
void StringTable_GST::GetStz(long istz, char *pstz);
void StringTable_GST::GetSt(long istz, char *pst);
void StringTable_GST::GetSz(long istz, char *psz);
// (and AllocatedStringTable variants.)
```
Get the istz'th string. `pstz`/`pst`/`psz` must point to a buffer at
least `kcbMaxStz`/`kcbMaxSt`/`kcbMaxSz` bytes long.

```cpp
bool StringTable_GST::FPutRgch(long istz, char *prgch, long cch);
bool StringTable_GST::FPutStz(long istz, char *pstz);
bool StringTable_GST::FPutSt(long istz, char *pst);
bool StringTable_GST::FPutSz(long istz, char *psz);
// (and AllocatedStringTable variants.)
```
Replace the istz'th string.

```cpp
bool StringTable_GST::FFindStz(char *pstz, long *pistz, ulong grfstb = fstbNil);
bool StringTable_GST::FFindSt(char *pst, long *pistz, ulong grfstb = fstbNil);
bool StringTable_GST::FFindSz(char *psz, long *pistz, ulong grfstb = fstbNil);
// (and AllocatedStringTable variants.)
```
Finds the given string in the string table. If `fstbSorted` is
specified, and this is an `StringTable_GST`, a binary search is
performed. If the string is not found, false is returned. If the
string is not found and this is an `StringTable_GST` and `fstbSorted`
was specified, `*pistz` will be filled with where the string should be
inserted to maintain the sorting. In other cases, if the string is not
found, `*pistz` is filled with `IstzMac()`.

```cpp
bool AllocatedStringTable::FFree(long istz);
```
Indicates whether item `istz` is free. (This is fast.) `istz` should
be less than `IstzMac()`.
