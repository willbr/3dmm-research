/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#pragma once
#include <windows.h>
#include <cstdint>
namespace RetailOracle
{
constexpr DWORD Magic = 0x314f5233, Version = 1;
constexpr UINT JobMessage = WM_APP + 0x3d;
enum Operation : DWORD
{
    Hello = 0,
    State,
    Seek,
    Command,
    Capture
};
struct Config
{
    DWORD magic = Magic, version = Version, uiThread = 0;
    wchar_t contentRoot[260] = {};
};
struct Request
{
    DWORD magic = Magic, version = Version, operation = 0, sequence = 0;
    std::int32_t arguments[8] = {};
    wchar_t path[512] = {};
};
static_assert(sizeof(Request) == 1072, "Oracle wire layout changed");
// Exact retail ISO EXE only. Addresses are RVAs, never pointers to our refactored classes.
constexpr char ExeSHA256[] = "cc7c05cd4b63c0d1f2a0cee6fed86d85bd9f54e35952818f412f128425b2bca5";
constexpr DWORD ExeTimestamp = 0x3078d894;
constexpr DWORD MovieVtable = 0xe22c0, SceneVtable = 0xe24a0, ActorVtable = 0xe2b68, ExecutorVtable = 0xdf0c8;
constexpr DWORD ErrorVtable = 0xdeec0;
constexpr DWORD SwitchScene = 0x9e530, GotoFrame = 0xa8f90, RenderWorld = 0x74590;
} // namespace RetailOracle
