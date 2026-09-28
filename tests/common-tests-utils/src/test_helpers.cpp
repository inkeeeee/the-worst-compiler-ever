#include "test_helpers.hpp"

#include <chrono>
#include <fstream>
#include <string>
#include <system_error>

temporary_directory::temporary_directory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int i = 0;; ++i) {
        path = std::filesystem::temp_directory_path() /
               ("compiler_lab_" + std::to_string(stamp) + "_" + std::to_string(i));
        if (std::filesystem::create_directory(path)) {
            break;
        }
    }
}

temporary_directory::~temporary_directory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
}

void write_text_file(const std::filesystem::path &path, std::string_view text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::ios_base::failure("Cannot create test file: " + path.string());
    }
    output << text;
    output.close();
    if (!output) {
        throw std::ios_base::failure("Cannot save test file: " + path.string());
    }
}
