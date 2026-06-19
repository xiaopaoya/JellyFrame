#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace jellyframe {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

enum class DiagnosticStage {
    Package,
    Html,
    Css,
    Style,
    RenderTree,
    Layout,
    LayerTree,
    Paint,
    Script,
};

struct Diagnostic {
    DiagnosticStage stage = DiagnosticStage::Html;
    DiagnosticSeverity severity = DiagnosticSeverity::Warning;
    std::string code;
    std::string message;
    std::string detail;
};

class DiagnosticSink {
public:
    virtual ~DiagnosticSink() = default;
    virtual void report(DiagnosticStage stage,
                        DiagnosticSeverity severity,
                        std::string_view code,
                        std::string_view message,
                        std::string_view detail) = 0;
};

class VectorDiagnosticSink final : public DiagnosticSink {
public:
    void report(DiagnosticStage stage,
                DiagnosticSeverity severity,
                std::string_view code,
                std::string_view message,
                std::string_view detail) override {
        diagnostics_.push_back(Diagnostic{
            stage,
            severity,
            std::string(code),
            std::string(message),
            std::string(detail),
        });
    }

    const std::vector<Diagnostic>& diagnostics() const {
        return diagnostics_;
    }

    bool empty() const {
        return diagnostics_.empty();
    }

    std::size_t size() const {
        return diagnostics_.size();
    }

    void clear() {
        diagnostics_.clear();
    }

private:
    std::vector<Diagnostic> diagnostics_;
};

inline void report_diagnostic(DiagnosticSink* sink,
                              DiagnosticStage stage,
                              DiagnosticSeverity severity,
                              std::string_view code,
                              std::string_view message,
                              std::string_view detail = {}) {
    if (sink != nullptr) {
        sink->report(stage, severity, code, message, detail);
    }
}

inline const char* diagnostic_stage_name(DiagnosticStage stage) {
    switch (stage) {
    case DiagnosticStage::Package:
        return "package";
    case DiagnosticStage::Html:
        return "html";
    case DiagnosticStage::Css:
        return "css";
    case DiagnosticStage::Style:
        return "style";
    case DiagnosticStage::RenderTree:
        return "render-tree";
    case DiagnosticStage::Layout:
        return "layout";
    case DiagnosticStage::LayerTree:
        return "layer-tree";
    case DiagnosticStage::Paint:
        return "paint";
    case DiagnosticStage::Script:
        return "script";
    }
    return "unknown";
}

inline const char* diagnostic_severity_name(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Info:
        return "info";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Error:
        return "error";
    }
    return "unknown";
}

} // namespace jellyframe
