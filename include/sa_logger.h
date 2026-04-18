#ifndef SA_LOGGER_H
#define SA_LOGGER_H

#include <fstream>
#include <string>
#include <vector>

struct SaStageLogEntry {
    int stage_idx = 0;
    double temperature = 0.0;
    double runtime_sec = 0.0;
    int attempted_moves = 0;
    int accepted_moves = 0;
    int accepted_uphill_moves = 0;
    long long best_hpwl_so_far = 0;
    long long current_hpwl = 0;
    std::string cost_mode;
};

class SaRunLogger {
public:
    SaRunLogger(const std::string& source,
                const std::string& cost_mode,
                const std::vector<std::string>& cli_args,
                double temp_floor);

    bool ok() const;
    const std::string& csv_path() const;
    const std::string& summary_path() const;

    void log_stage(const SaStageLogEntry& e);
    void log_summary(long long initial_hpwl,
                     long long final_hpwl,
                     long long best_hpwl,
                     double runtime_sec,
                     int accepted_moves,
                     int total_steps,
                     int total_proposals,
                     const std::string& output_path);

private:
    std::string csv_path_;
    std::string summary_path_;
    std::ofstream csv_;
    std::ofstream summary_;
};

#endif
