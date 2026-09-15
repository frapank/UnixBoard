#pragma once
#include <string>
#include <sys/types.h>
#include <vector>

struct SoundClip {
    std::string name; // filename without extension, shown in the menu
    std::string path;
};

class Soundboard {
public:
    explicit Soundboard(std::string mic_node)
        : mic_node_(std::move(mic_node))
    {
    }

    std::vector<SoundClip> scan(const std::string& sounds_dir);

    void play(const SoundClip& clip);

    void stop_all();

    size_t active();

private:
    void link_into_mic(const std::string& clip_node);
    void reap_finished();

    std::string mic_node_;
    int next_clip_id_ = 0;
    std::vector<pid_t> players_;
};
