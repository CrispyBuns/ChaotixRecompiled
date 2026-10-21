// Best-effort host utilization metrics for the developer overlay.
// CPU usage is sampled from Linux /proc/stat. GPU usage is read only when
// the device driver exposes a recognizable sysfs utilization/load counter.
#pragma once

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__linux__)
namespace chaotix {
class SystemMetrics {
public:
    void update() {
        using clock = std::chrono::steady_clock;
        const auto now = clock::now();
        if (last_sample_.time_since_epoch().count() != 0 &&
            now - last_sample_ < std::chrono::milliseconds(500)) return;
        last_sample_ = now;
        sample_cpu();
        sample_gpu();
    }

    double cpu_percent() const { return cpu_percent_; }
    double gpu_percent() const { return gpu_percent_; }

private:
    void sample_cpu() {
        std::ifstream in("/proc/stat");
        std::string label;
        unsigned long long user = 0, nice = 0, system = 0, idle = 0;
        unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
        if (!(in >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) || label != "cpu") {
            cpu_percent_ = -1.0;
            return;
        }
        const unsigned long long idle_all = idle + iowait;
        const unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
        if (have_cpu_sample_) {
            const auto total_delta = total - previous_total_;
            const auto idle_delta = idle_all - previous_idle_;
            if (total_delta > 0)
                cpu_percent_ = std::clamp(100.0 * double(total_delta - std::min(total_delta, idle_delta)) /
                                          double(total_delta), 0.0, 100.0);
        }
        previous_total_ = total;
        previous_idle_ = idle_all;
        have_cpu_sample_ = true;
    }

    static double parse_percent(const std::filesystem::path& path) {
        std::ifstream in(path);
        if (!in) return -1.0;
        std::string value;
        if (!(in >> value)) return -1.0;
        // Some Mali devfreq drivers return "busy_percent@frequency".
        const auto at = value.find('@');
        if (at != std::string::npos) value.resize(at);
        try {
            size_t used = 0;
            const double n = std::stod(value, &used);
            if (used != value.size() || n < 0.0 || n > 100.0) return -1.0;
            return n;
        } catch (...) {
            return -1.0;
        }
    }

    void sample_gpu() {
        gpu_percent_ = -1.0;
        // Common Linux GPU devfreq interfaces, including Mali load counters.
        std::error_code ec;
        const std::filesystem::path root("/sys/class/devfreq");
        for (std::filesystem::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            std::string name = it->path().filename().string();
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (lower.find("gpu") == std::string::npos && lower.find("mali") == std::string::npos)
                continue;
            for (const char* counter : {"load", "utilization", "gpu_utilization"}) {
                const double value = parse_percent(it->path() / counter);
                if (value >= 0.0) { gpu_percent_ = value; return; }
            }
        }
        // A few drivers publish a direct utilization value outside devfreq.
        for (const char* path : {
                 "/sys/class/misc/mali0/device/utilization",
                 "/sys/kernel/debug/mali0/gpu_utilization",
                 "/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage"}) {
            const double value = parse_percent(path);
            if (value >= 0.0) { gpu_percent_ = value; return; }
        }
    }

    std::chrono::steady_clock::time_point last_sample_{};
    unsigned long long previous_total_ = 0;
    unsigned long long previous_idle_ = 0;
    bool have_cpu_sample_ = false;
    double cpu_percent_ = -1.0;
    double gpu_percent_ = -1.0;
};
} // namespace chaotix
#else
namespace chaotix {
class SystemMetrics {
public:
    void update() {}
    double cpu_percent() const { return -1.0; }
    double gpu_percent() const { return -1.0; }
};
} // namespace chaotix
#endif
