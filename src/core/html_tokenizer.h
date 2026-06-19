#pragma once

#include "core/diagnostics.h"

#include <string>
#include <vector>

namespace jellyframe {

enum class HtmlTokenType {
    Doctype,
    StartTag,
    EndTag,
    Comment,
    Text,
    EndOfFile,
};

struct HtmlAttribute {
    std::string name;
    std::string value;
};

struct HtmlToken {
    HtmlTokenType type = HtmlTokenType::Text;
    std::string name;
    std::string data;
    std::vector<HtmlAttribute> attributes;
    bool self_closing = false;
};

struct HtmlTokenizerOptions {
    bool emit_comments = false;
    DiagnosticSink* diagnostics = nullptr;
};

class HtmlTokenSink {
public:
    virtual ~HtmlTokenSink() = default;
    virtual void consume(const HtmlToken& token) = 0;
};

class HtmlTokenizer {
public:
    std::vector<HtmlToken> tokenize(const std::string& source,
                                    const HtmlTokenizerOptions& options = {}) const;
    void tokenize_to_sink(const std::string& source,
                          HtmlTokenSink& sink,
                          const HtmlTokenizerOptions& options = {}) const;
};

} // namespace jellyframe
