/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef STUDIO_PACK_HASH_H
#define STUDIO_PACK_HASH_H
#include <cstddef>
#include <filesystem>
#include <string>
namespace StudioPacks
{
std::string Hash(const void *bytes, std::size_t size);
std::string HashFile(const std::filesystem::path &path);
} // namespace StudioPacks
#endif
