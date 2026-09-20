#pragma once

#include <chrono>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>

template <typename Tnode_iterator> auto in_count(Tnode_iterator node) {
    return std::distance(node->in_begin(), node->in_end());
}

template <typename Tnode_iterator> auto out_count(Tnode_iterator node) {
    return std::distance(node->out_begin(), node->out_end());
}

struct temporary_directory {
    std::filesystem::path path;

    temporary_directory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int i = 0;; ++i) {
            path = std::filesystem::temp_directory_path() /
                   ("graph_lab_" + std::to_string(stamp) + "_" + std::to_string(i));
            if (std::filesystem::create_directory(path)) {
                break;
            }
        }
    }

    ~temporary_directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
