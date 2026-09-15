#include "pulse_control.hpp"
#include "soundboard.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {

    PulseControl* g_pulse = nullptr;
    Soundboard* g_board = nullptr;
    std::atomic<bool> g_running{true};

    void handle_signal(int)
    {
        g_running = false;
        if (g_board)
            g_board->stop_all();
        if (g_pulse)
            g_pulse->teardown();
        std::_Exit(0);
    }

    int choose_microphone(const std::vector<AudioSource>& mics)
    {
        std::cout << "Microphones:\n";
        for (size_t i = 0; i < mics.size(); ++i) {
            const std::string& label = mics[i].description.empty()
                                           ? mics[i].name
                                           : mics[i].description;
            std::cout << "  [" << i << "] " << label << "\n";
        }
        std::cout << "\nWhich one to intercept? [0]: ";
        std::string line;
        std::getline(std::cin, line);
        if (line.empty())
            return 0;
        try {
            int idx = std::stoi(line);
            if (idx >= 0 && static_cast<size_t>(idx) < mics.size())
                return idx;
        } catch (...) {
        }
        return 0;
    }

    void draw_menu(const std::string& mic_label,
                   const std::vector<SoundClip>& clips)
    {
        std::cout << "\033[2J\033[H";
        std::cout << "UnixBoard  |  intercepting: " << mic_label << "\n\n";
        if (clips.empty()) {
            std::cout
                << "  No .mp3 in ./sounds -- drop some in, then press r\n";
        } else {
            for (size_t i = 0; i < clips.size(); ++i) {
                std::cout << "  [" << i << "] " << clips[i].name << "\n";
            }
        }
        std::cout << "\n  number = play   r = rescan   q = quit\n> ";
    }

} // namespace

int main()
{
    std::string missing;
    if (!PulseControl::tools_available(missing)) {
        std::cerr << "Missing required tool: " << missing << "\n"
                  << "Install PipeWire's client tools (pipewire-utils / "
                     "pipewire-audio) "
                     "and pulseaudio-utils.\n";
        return 1;
    }

    auto mics = PulseControl::list_microphones();
    if (mics.empty()) {
        std::cerr << "No microphones found. Is PipeWire/PulseAudio running?\n";
        return 1;
    }
    const AudioSource& mic = mics[choose_microphone(mics)];
    const std::string mic_label =
        mic.description.empty() ? mic.name : mic.description;

    PulseControl pulse;
    g_pulse = &pulse;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    if (!pulse.setup(mic))
        return 1;

    pulse.redirect_recording_apps();
    std::thread redirector([&pulse] {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (g_running)
                pulse.redirect_recording_apps();
        }
    });

    Soundboard board(pulse.node_name());
    g_board = &board;
    auto clips = board.scan("sounds");

    while (true) {
        draw_menu(mic_label, clips);

        std::string line;
        if (!std::getline(std::cin, line))
            break;
        if (line == "q")
            break;
        if (line == "r") {
            clips = board.scan("sounds");
            continue;
        }
        try {
            size_t idx = static_cast<size_t>(std::stoul(line));
            if (idx < clips.size())
                board.play(clips[idx]);
        } catch (...) {
        }
    }

    g_running = false;
    redirector.join();
    board.stop_all();
    pulse.teardown();
    return 0;
}
