#include "core/dom.h"
#include "core/html_parser.h"
#include "core/html_tokenizer.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

constexpr std::size_t kMaxInputBytes = 512 * 1024;

std::string sample_html() {
    return "<!doctype html>"
           "<html lang='en'>"
           "<head><title>JellyFrame</title><style>.hidden { display: none; }</style></head>"
           "<body>"
           "<main id='app' data-screen=round>"
           "<h1>JellyFrame</h1>"
           "<p>DOM &amp; tokenizer demo<div class='card'>implicit close</div>"
           "<ul><li>steps<li>limits<li>portable</ul>"
           "<script>if (a < b) { mount('<div></div>'); }</script>"
           "</main>"
           "</body>"
           "</html>";
}

std::string read_file_limited(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open input file");
    }

    std::ostringstream output;
    char buffer[4096];
    std::size_t total = 0;
    while (file && total < kMaxInputBytes) {
        const std::size_t remaining = kMaxInputBytes - total;
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

std::string clipped(std::string value, std::size_t max_bytes) {
    if (value.size() <= max_bytes) {
        return value;
    }
    value.resize(max_bytes);
    value += "...";
    return value;
}

std::string escaped(std::string value) {
    std::string output;
    output.reserve(value.size());
    for (const char ch : value) {
        if (ch == '\n') {
            output += "\\n";
        } else if (ch == '\t') {
            output += "\\t";
        } else if (ch == '"') {
            output += "\\\"";
        } else {
            output.push_back(ch);
        }
    }
    return output;
}

const char* token_type_name(HtmlTokenType type) {
    switch (type) {
    case HtmlTokenType::Doctype:
        return "doctype";
    case HtmlTokenType::StartTag:
        return "start";
    case HtmlTokenType::EndTag:
        return "end";
    case HtmlTokenType::Comment:
        return "comment";
    case HtmlTokenType::Text:
        return "text";
    case HtmlTokenType::EndOfFile:
        return "eof";
    }
    return "unknown";
}

void print_token_summary(const std::vector<HtmlToken>& tokens) {
    std::cout << "Tokens\n";
    std::size_t index = 0;
    for (const HtmlToken& token : tokens) {
        std::cout << "  [" << index++ << "] " << token_type_name(token.type);
        if (!token.name.empty()) {
            std::cout << " " << token.name;
        }
        if (token.type == HtmlTokenType::Text || token.type == HtmlTokenType::Comment) {
            std::cout << " \"" << escaped(clipped(token.data, 60)) << "\"";
        }
        if (token.self_closing) {
            std::cout << " /";
        }
        std::cout << '\n';
        if (token.type == HtmlTokenType::EndOfFile || index >= 80) {
            break;
        }
    }
    if (tokens.size() > 80) {
        std::cout << "  ... clipped token output ...\n";
    }
}

void print_attributes(const Node& node) {
    std::size_t count = 0;
    for (const auto& entry : node.attributes) {
        if (count >= 6) {
            std::cout << " ...";
            break;
        }
        std::cout << " " << entry.first << "=\"" << escaped(clipped(entry.second, 40)) << "\"";
        ++count;
    }
}

void print_dom(const Node& node, std::size_t depth = 0) {
    for (std::size_t i = 0; i < depth; ++i) {
        std::cout << "  ";
    }

    if (node.type == NodeType::Text) {
        std::cout << "#text \"" << escaped(clipped(node.text, 80)) << "\"\n";
        return;
    }

    std::cout << '<' << node.tag_name;
    print_attributes(node);
    std::cout << ">\n";

    for (const auto& child : node.children) {
        print_dom(*child, depth + 1);
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string input = argc > 1 ? read_file_limited(argv[1]) : sample_html();

        HtmlTokenizer tokenizer;
        const std::vector<HtmlToken> tokens = tokenizer.tokenize(input);
        print_token_summary(tokens);

        HtmlParser parser;
        auto document = parser.parse(input);
        std::cout << "\nDOM\n";
        print_dom(*document);
    } catch (const std::exception& error) {
        std::cerr << "dom dump failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}

