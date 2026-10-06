#pragma once
#include "NetworkConfig.h"
#include "Statistics.h"
#include <string>
#include <utility>

class Logger {
public:
    explicit Logger(std::string directory = "results") : directory_(std::move(directory)) {}
    bool save(const NetworkConfig& config, const Statistics& stats, std::string& savedPath,
              const std::string& extraText = "") const;

private:
    std::string directory_;
};
