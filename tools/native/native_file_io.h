#pragma once

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace jellyframe::native {

inline std::string read_file_limited(const std::string& path, std::size_t max_input_bytes = 512 * 1024) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open input file");
    }

    std::ostringstream output;
    char buffer[4096];
    std::size_t total = 0;
    while (file && total < max_input_bytes) {
        const std::size_t remaining = max_input_bytes - total;
        const std::size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        file.read(buffer, static_cast<std::streamsize>(chunk));
        const std::streamsize read = file.gcount();
        if (read <= 0) {
            break;
        }
        output.write(buffer, read);
        total += static_cast<std::size_t>(read);
    }
    return output.str();
}

} // namespace jellyframe::native
