#pragma once
#include <string>
#include <vector>

struct AudioSource {
    std::string index;       // pactl numeric index
    std::string name;        // node name, e.g. "alsa_input.pci-0000...stereo"
    std::string description; // label, e.g. "Blue Microphones Analog Stereo"
};

class PulseControl {
public:
    static std::vector<AudioSource> list_microphones();

    static bool tools_available(std::string& missing_tool);

    bool setup(const AudioSource& mic);
    void teardown();

    void redirect_recording_apps();

    const std::string& node_name() const { return node_name_; }

private:
    std::string node_name_ = "unixboard_mic";
    std::string module_id_;
    std::string mic_index_;
    std::string previous_default_source_;
    bool default_source_changed_ = false;
};
