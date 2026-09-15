#include "pulse_control.hpp"
#include "proc_exec.hpp"

#include <iostream>
#include <sstream>

namespace {

    std::string trim(const std::string& s)
    {
        size_t start = s.find_first_not_of(" \t\r\n");
        size_t end = s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos)
            return "";
        return s.substr(start, end - start + 1);
    }

    bool tool_missing(const std::string& tool)
    {
        return exec_capture({tool, "--version"}).exit_code == 127;
    }

    std::vector<AudioSource> parse_sources(const std::string& pactl_output)
    {
        std::vector<AudioSource> sources;
        std::istringstream stream(pactl_output);
        std::string line;
        AudioSource current;
        bool in_block = false;

        auto flush = [&]() {
            if (in_block && !current.name.empty())
                sources.push_back(current);
            current = AudioSource{};
        };

        while (std::getline(stream, line)) {
            std::string trimmed = trim(line);
            if (trimmed.rfind("Source #", 0) == 0) {
                flush();
                in_block = true;
                current.index = trim(trimmed.substr(8));
            } else if (in_block && trimmed.rfind("Name:", 0) == 0) {
                current.name = trim(trimmed.substr(5));
            } else if (in_block && trimmed.rfind("Description:", 0) == 0) {
                current.description = trim(trimmed.substr(13));
            }
        }
        flush();
        return sources;
    }

    std::vector<std::vector<std::string>> parse_short_rows(
        const std::string& pactl_output)
    {
        std::vector<std::vector<std::string>> rows;
        std::istringstream stream(pactl_output);
        std::string line;
        while (std::getline(stream, line)) {
            line = trim(line);
            if (line.empty())
                continue;
            std::vector<std::string> fields;
            std::istringstream cols(line);
            std::string field;
            while (std::getline(cols, field, '\t'))
                fields.push_back(field);
            rows.push_back(std::move(fields));
        }
        return rows;
    }

} // namespace

std::vector<AudioSource> PulseControl::list_microphones()
{
    ExecResult result = exec_capture({"pactl", "list", "sources"});
    if (result.exit_code != 0)
        return {};

    std::vector<AudioSource> mics;
    for (auto& source : parse_sources(result.stdout_text)) {
        if (source.name.size() >= 8 &&
            source.name.compare(source.name.size() - 8, 8, ".monitor") == 0)
            continue;
        mics.push_back(std::move(source));
    }
    return mics;
}

bool PulseControl::tools_available(std::string& missing_tool)
{
    for (const char* tool : {"pactl", "pw-link", "pw-play"}) {
        if (tool_missing(tool)) {
            missing_tool = tool;
            return false;
        }
    }
    return true;
}

bool PulseControl::setup(const AudioSource& mic)
{
    mic_index_ = mic.index;

    ExecResult load = exec_capture(
        {"pactl", "load-module", "module-null-sink",
         "media.class=Audio/Source/Virtual", "sink_name=" + node_name_,
         "channel_map=front-left,front-right",
         "sink_properties=device.description=UnixBoard_Mic "
         "node.always-process=true"});
    if (load.exit_code != 0 || trim(load.stdout_text).empty()) {
        std::cerr << "Failed to create the virtual mic (is PipeWire/PulseAudio "
                     "running?)\n";
        return false;
    }
    module_id_ = trim(load.stdout_text);

    ExecResult link = exec_capture({"pw-link", mic.name, node_name_});
    if (link.exit_code != 0) {
        std::cerr << "Failed to wire '" << mic.description
                  << "' into the virtual mic\n";
        teardown();
        return false;
    }
    exec_capture(
        {"pw-link", mic.name + ":capture_MONO", node_name_ + ":input_FR"});

    previous_default_source_ =
        trim(exec_capture({"pactl", "get-default-source"}).stdout_text);
    default_source_changed_ =
        exec_capture({"pactl", "set-default-source", node_name_}).exit_code ==
        0;

    return true;
}

void PulseControl::redirect_recording_apps()
{
    ExecResult outputs =
        exec_capture({"pactl", "list", "short", "source-outputs"});
    for (const auto& row : parse_short_rows(outputs.stdout_text)) {
        if (row.size() < 2 || row[1] != mic_index_)
            continue;
        exec_capture({"pactl", "move-source-output", row[0], node_name_});
    }
}

void PulseControl::teardown()
{
    if (default_source_changed_ && !previous_default_source_.empty()) {
        exec_capture({"pactl", "set-default-source", previous_default_source_});
        default_source_changed_ = false;
    }
    if (!module_id_.empty()) {
        exec_capture({"pactl", "unload-module", module_id_});
        module_id_.clear();
    }
}
