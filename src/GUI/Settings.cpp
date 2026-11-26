#include "GUI/Settings.hpp"

#include <algorithm>
#include <cstdint>
#include <thread>
#include <charconv>
#include <fstream>
#include <type_traits>
#include <unordered_map>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#elif defined(__APPLE__)
    #include <sys/sysctl.h>
#else
    #include <unistd.h>
#endif

namespace {
    template <class Self, class Visitor>
    void VisitFields(Self& s, Visitor&& visit) {
        visit("LimitByDepth", s.LimitByDepth);
        visit("ThinkTimeMs", s.ThinkTimeMs);
        visit("SearchDepth", s.SearchDepth);
        visit("EvalDepth", s.EvalDepth);
        visit("OwnBook", s.OwnBook);
        visit("Ponder", s.Ponder);
        visit("CatchAll", s.CatchAll);
        visit("HashMB", s.HashMB);
        visit("Threads", s.Threads);
        visit("Theme", s.Theme);
        visit("PieceSet", s.PieceSet);
        visit("ShowEvalBar", s.ShowEvalBar);
        visit("ShowMoveList", s.ShowMoveList);
        visit("ShowCoordinates", s.ShowCoordinates);
        visit("SoundEnabled", s.SoundEnabled);
        visit("Volume", s.Volume);
        visit("WindowWidth", s.WindowWidth);
        visit("WindowHeight", s.WindowHeight);
        visit("WindowMaximized", s.WindowMaximized);
    }

    template <class T>
    void FromText(const std::string& text, T& value) {
        if constexpr (std::is_same_v<T, std::string>) {
            value = text;
        } else if constexpr (std::is_same_v<T, bool>) {
            if (text == "1" || text == "0") value = text == "1";
        } else {
            using Integer = std::conditional_t<std::is_enum_v<T>, int, T>;

            Integer parsed{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);

            if (error == std::errc() && end == text.data() + text.size()) value = static_cast<T>(parsed);
        }
    }

    template <class T>
    std::string ToText(const T& value) {
        if constexpr (std::is_same_v<T, std::string>) return value;
        else if constexpr (std::is_same_v<T, bool>) return value ? "1" : "0";
        else return std::to_string(static_cast<int>(value));
    }
}

void Settings::Load(const std::filesystem::path& path) {
    std::ifstream file(path);

    std::unordered_map<std::string, std::string> entries;

    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const std::size_t split = line.find('=');

        if (split != std::string::npos) entries[line.substr(0, split)] = line.substr(split + 1);
    }

    VisitFields(*this, [&entries](const char* key, auto& field) {
        const auto it = entries.find(key);

        if (it != entries.end()) FromText(it->second, field);
    });

    clamp();
}

void Settings::Save(const std::filesystem::path& path) const {
    std::ofstream file(path, std::ios::trunc);

    if (!file) return;

    VisitFields(*this, [&file](const char* key, const auto& field) {
        file << key << '=' << ToText(field) << '\n';
    });
}

void Settings::clamp() {
    ThinkTimeMs = std::clamp(ThinkTimeMs, Limits::ThinkTimeMin, Limits::ThinkTimeMax);
    ThinkTimeMs -= ThinkTimeMs % Limits::ThinkTimeStep;
    ThinkTimeMs = std::max(ThinkTimeMs, Limits::ThinkTimeMin);

    SearchDepth = std::clamp(SearchDepth, Limits::DepthMin, Limits::DepthMax);
    EvalDepth = std::clamp(EvalDepth, Limits::DepthMin, Limits::DepthMax);
    Volume = std::clamp(Volume, Limits::VolumeMin, Limits::VolumeMax);

    HashMB = std::clamp(HashMB, 1, static_cast<int>(Platform::MaxHashMB()));
    Threads = std::clamp(Threads, 1, static_cast<int>(Platform::TotalThreads()));

    WindowWidth = std::max(0, WindowWidth);
    WindowHeight = std::max(0, WindowHeight);
}

#pragma region Platform

namespace {
    std::uint64_t PhysicalMemoryMB() noexcept {
#if defined(_WIN32)
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status)) return status.ullTotalPhys / (1024ull * 1024ull);
#elif defined(__APPLE__)
        std::uint64_t bytes = 0;
        std::size_t size = sizeof(bytes);
        if (sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0) return bytes / (1024ull * 1024ull);
#else
        const long pages = sysconf(_SC_PHYS_PAGES);
        const long pageSize = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && pageSize > 0) {
            return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize) / (1024ull * 1024ull);
        }
#endif
        return 2048;
    }
}

unsigned int Platform::TotalThreads() noexcept {
    return std::max(1u, std::thread::hardware_concurrency());
}

unsigned int Platform::DefaultThreads() noexcept {
    return std::max(1u, TotalThreads() / 2);
}

unsigned int Platform::MaxHashMB() noexcept {
    return static_cast<unsigned int>(std::clamp<std::uint64_t>(PhysicalMemoryMB() / 2, 16, 32768));
}
