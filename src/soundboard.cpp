#include "soundboard.hpp"
#include "proc_exec.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <sstream>
#include <sys/wait.h>
#include <thread>

namespace fs = std::filesystem;

namespace {

    std::vector<std::pair<std::string, std::string>> parse_ports(
        const std::string& pw_link_output)
    {
        std::vector<std::pair<std::string, std::string>> ports;
        std::istringstream stream(pw_link_output);
        std::string line;
        while (std::getline(stream, line)) {
            std::istringstream fields(line);
            std::string id, name;
            if (fields >> id >> name)
                ports.emplace_back(id, name);
        }
        return ports;
    }

} // namespace

std::vector<SoundClip> Soundboard::scan(const std::string& sounds_dir)
{
    std::vector<SoundClip> clips;
    if (!fs::exists(sounds_dir))
        fs::create_directories(sounds_dir);

    for (const auto& entry : fs::directory_iterator(sounds_dir)) {
        if (!entry.is_regular_file())
            continue;
        if (entry.path().extension() != ".mp3")
            continue;
        clips.push_back({entry.path().stem().string(), entry.path().string()});
    }
    std::sort(
        clips.begin(), clips.end(),
        [](const SoundClip& a, const SoundClip& b) { return a.name < b.name; });
    return clips;
}

void Soundboard::play(const SoundClip& clip)
{
    const std::string clip_node =
        "unixboard_clip_" + std::to_string(next_clip_id_++);
    reap_finished();
    players_.push_back(
        exec_spawn({"pw-play", "--target", "0", "-P",
                    "{ node.name = \"" + clip_node + "\" }", clip.path}));
    link_into_mic(clip_node);
}

void Soundboard::reap_finished()
{
    players_.erase(std::remove_if(players_.begin(), players_.end(),
                                  [](pid_t pid) {
                                      return waitpid(pid, nullptr, WNOHANG) !=
                                             0;
                                  }),
                   players_.end());
}

void Soundboard::link_into_mic(const std::string& clip_node)
{
    const std::string prefix = clip_node + ":output_";
    for (int attempt = 0; attempt < 75; ++attempt) {
        bool linked = false;
        for (const auto& [id, name] :
             parse_ports(exec_capture({"pw-link", "-I", "-o"}).stdout_text)) {
            if (name.rfind(prefix, 0) != 0)
                continue;
            const std::string channel = name.substr(prefix.size());
            if (channel == "MONO") {
                exec_capture({"pw-link", id, mic_node_ + ":input_FL"});
                exec_capture({"pw-link", id, mic_node_ + ":input_FR"});
            } else {
                exec_capture({"pw-link", id, mic_node_ + ":input_" + channel});
            }
            linked = true;
        }
        if (linked)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void Soundboard::stop_all()
{
    for (pid_t pid : players_) {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
    }
    players_.clear();
}
