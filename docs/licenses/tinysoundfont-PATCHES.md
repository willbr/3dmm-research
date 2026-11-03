TinySoundFont is pinned to commit `853a0a171759f1ddba0de1442133a75912bbeffa`.
Its MIT license is in `tinysoundfont-LICENSE.txt`.

The build generates a private copy of `tsf.h` with one integration correction:
`GEN_UINT_ADD15` multiplies a signed coarse sample offset by 32768 before
converting to unsigned, instead of left-shifting a potentially negative value.
The multiplication fits in signed 32 bits for every 16-bit generator value and
preserves the intended unsigned sample-offset addition. This removes undefined
behavior encountered while loading GeneralUser GS under Linux runtime checks.
The downloaded source remains unchanged.
