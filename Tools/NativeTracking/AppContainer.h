// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <vector>
#include <cstdint>
using CaptureFrame = std::function<bool(std::vector<uint8_t>&, int&, int&)>;
bool ReadIsolatedFrame(uintptr_t mapping, uintptr_t event, uintptr_t mutex,
    std::vector<uint8_t>& rgb, int& width, int& height, int64_t& timestamp);
void RequireOfflineAppContainer();
int LaunchOfflineAppContainer(int argc, wchar_t** argv, const std::filesystem::path& output,
    const std::filesystem::path& stopFile, const std::filesystem::path& dll,
    const std::filesystem::path& model, const std::filesystem::path& image, std::atomic<bool>& stop,
    CaptureFrame capture = {});
