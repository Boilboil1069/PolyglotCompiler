// ============================================================================
// config_store.cpp — C++ configuration store for attribute access demo
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <string>
#include <unordered_map>
#include <vector>

class ConfigStore {
public:
    std::string name;
    int max_retries;
    double timeout;
    bool verbose;

    ConfigStore(const std::string& config_name)
        : name(config_name), max_retries(3), timeout(30.0), verbose(false) {}

    void set_option(const std::string& key, double value) {
        options_[key] = value;
    }

    double get_option(const std::string& key) const {
        auto it = options_.find(key);
        if (it != options_.end()) return it->second;
        return 0.0;
    }

    int option_count() const {
        return static_cast<int>(options_.size());
    }

private:
    std::unordered_map<std::string, double> options_;
};

class Sensor {
public:
    double value;
    double min_threshold;
    double max_threshold;
    bool active;

    Sensor(double initial_value)
        : value(initial_value), min_threshold(0.0),
          max_threshold(100.0), active(true) {}

    bool in_range() const {
        return value >= min_threshold && value <= max_threshold;
    }

    void calibrate(double offset) {
        value += offset;
    }
};
