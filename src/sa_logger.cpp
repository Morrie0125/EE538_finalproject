#include "../include/sa_logger.h"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

using namespace std;

namespace {

string sanitize_source(const string& source) {
    string out;
    out.reserve(source.size());
    for (char c : source) {
        if (isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "unknown";
    }
    return out;
}

string time_stamp_now() {
    auto now = chrono::system_clock::now();
    const time_t t = chrono::system_clock::to_time_t(now);

    tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &t);
#else
    localtime_r(&t, &local_tm);
#endif

    ostringstream oss;
    oss << put_time(&local_tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

string join_args(const vector<string>& args) {
    ostringstream oss;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            oss << ' ';
        }
        const string& arg = args[i];
        if (arg.find_first_of(" \t\"") != string::npos) {
            oss << '"';
            for (char c : arg) {
                if (c == '"') {
                    oss << '\\';
                }
                oss << c;
            }
            oss << '"';
        } else {
            oss << arg;
        }
    }
    return oss.str();
}

}  // namespace

SaRunLogger::SaRunLogger(const string& source,
                         const string& cost_mode,
                         const vector<string>& cli_args,
                         double temp_floor) {
    const string src = sanitize_source(source);
    const string ts = time_stamp_now();

    filesystem::create_directories("logs");

    csv_path_ = "logs/" + src + "_" + ts + ".csv";
    summary_path_ = "logs/" + src + "_" + ts + ".log";

    csv_.open(csv_path_);
    summary_.open(summary_path_);

    if (!ok()) {
        return;
    }

    csv_ << "stage_idx,temperature,runtime_sec,attempted_moves,accepted_moves,accepted_uphill_moves,best_hpwl_so_far,current_hpwl,cost_mode\n";

    summary_ << "SA Run Log\n";
    summary_ << "source: " << src << "\n";
    summary_ << "cost_mode: " << cost_mode << "\n\n";
    summary_ << "command_line: " << join_args(cli_args) << "\n";
    summary_ << "temp_floor: " << fixed << setprecision(12) << temp_floor << "\n\n";
    summary_ << left
             << setw(8) << "stage"
             << setw(14) << "temp"
             << setw(14) << "runtime_s"
             << setw(12) << "attempted"
             << setw(12) << "accepted"
             << setw(12) << "uphill"
             << setw(14) << "best_hpwl"
             << setw(14) << "curr_hpwl"
             << setw(8) << "mode"
             << "\n";

    cout << "SA log files:\n";
    cout << "  csv: " << csv_path_ << "\n";
    cout << "  txt: " << summary_path_ << "\n";
}

bool SaRunLogger::ok() const {
    return csv_.is_open() && summary_.is_open();
}

const string& SaRunLogger::csv_path() const {
    return csv_path_;
}

const string& SaRunLogger::summary_path() const {
    return summary_path_;
}

void SaRunLogger::log_stage(const SaStageLogEntry& e) {
    if (!ok()) {
        return;
    }

    csv_ << e.stage_idx << ","
         << fixed << setprecision(8) << e.temperature << ","
            << fixed << setprecision(8) << e.runtime_sec << ","
         << e.attempted_moves << ","
         << e.accepted_moves << ","
         << e.accepted_uphill_moves << ","
         << e.best_hpwl_so_far << ","
         << e.current_hpwl << ","
         << e.cost_mode << "\n";

    summary_ << left
             << setw(8) << e.stage_idx
             << setw(14) << fixed << setprecision(6) << e.temperature
             << setw(14) << fixed << setprecision(6) << e.runtime_sec
             << setw(12) << e.attempted_moves
             << setw(12) << e.accepted_moves
             << setw(12) << e.accepted_uphill_moves
             << setw(14) << e.best_hpwl_so_far
             << setw(14) << e.current_hpwl
             << setw(8) << e.cost_mode
             << "\n";

}

void SaRunLogger::log_summary(long long initial_hpwl,
                              long long final_hpwl,
                              long long best_hpwl,
                              double runtime_sec,
                              int accepted_moves,
                              int total_steps,
                              int total_proposals,
                              const string& output_path) {
    if (!ok()) {
        return;
    }

    summary_ << "\nSummary\n";
    summary_ << "initial_hpwl: " << initial_hpwl << "\n";
    summary_ << "final_hpwl: " << final_hpwl << "\n";
    summary_ << "best_hpwl: " << best_hpwl << "\n";
    summary_ << "runtime_sec: " << fixed << setprecision(6) << runtime_sec << "\n";
    summary_ << "accepted_moves: " << accepted_moves << "\n";
    summary_ << "steps: " << total_steps << "\n";
    summary_ << "proposals: " << total_proposals << "\n";
    summary_ << "output_file: " << output_path << "\n";
}
