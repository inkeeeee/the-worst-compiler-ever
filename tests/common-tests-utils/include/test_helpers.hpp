#pragma once

#include <filesystem>
#include <iterator>
#include <string_view>

template <typename Tnode_iterator> auto in_count(Tnode_iterator node) {
    return std::distance(node->in_begin(), node->in_end());
}

template <typename Tnode_iterator> auto out_count(Tnode_iterator node) {
    return std::distance(node->out_begin(), node->out_end());
}

struct temporary_directory {
    std::filesystem::path path;

    temporary_directory();
    ~temporary_directory();
};

void write_text_file(const std::filesystem::path &path, std::string_view text);
