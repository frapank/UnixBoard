#include "pulse_control.hpp"
#include "soundboard.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

using namespace ftxui;

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

    std::string label_of(const AudioSource& mic)
    {
        return mic.description.empty() ? mic.name : mic.description;
    }

    // Returns the index into `mics`, or -1 if the user quit.
    int choose_microphone(const std::vector<AudioSource>& mics)
    {
        std::vector<std::string> labels;
        for (const auto& mic : mics)
            labels.push_back(label_of(mic));

        int selected = 0;
        bool quit = false;
        auto screen = ScreenInteractive::TerminalOutput();
        auto menu = Menu(&labels, &selected);

        auto root = Renderer(menu, [&] {
            return vbox({
                       text("UnixBoard") | bold,
                       text("Which microphone should we intercept?") | dim,
                       separator(),
                       menu->Render() | vscroll_indicator | frame |
                           size(HEIGHT, LESS_THAN, 12),
                       separator(),
                       text("enter = select   q = quit") | dim,
                   }) |
                   border;
        });

        root |= CatchEvent([&](Event event) {
            if (event == Event::Return) {
                screen.Exit();
                return true;
            }
            if (event == Event::Character('q') || event == Event::Escape) {
                quit = true;
                screen.Exit();
                return true;
            }
            return false;
        });

        screen.Loop(root);
        return quit ? -1 : selected;
    }

    void run_board(Soundboard& board, const std::string& mic_label)
    {
        std::vector<SoundClip> clips;
        std::vector<std::string> names;
        std::string status;

        auto rescan = [&] {
            clips = board.scan("sounds");
            names.clear();
            for (const auto& clip : clips)
                names.push_back(clip.name);
            status = std::to_string(clips.size()) + " clip(s)";
        };
        rescan();

        int selected = 0;
        auto screen = ScreenInteractive::Fullscreen();
        auto menu = Menu(&names, &selected);

        auto root = Renderer(menu, [&] {
            Element list =
                names.empty()
                    ? text(
                          "no .mp3 in ./sounds -- drop some in, then press r") |
                          dim | center
                    : menu->Render() | vscroll_indicator | frame;
            return vbox({
                       hbox({text(" UnixBoard ") | bold | inverted,
                             text("  mic: " + mic_label) | dim}),
                       separator(),
                       list | flex,
                       separator(),
                       hbox({text(status) | flex,
                             text("enter = play   s = stop   r = rescan   q = "
                                  "quit") |
                                 dim}),
                   }) |
                   border;
        });

        root |= CatchEvent([&](Event event) {
            if (event == Event::Return) {
                if (selected >= 0 &&
                    static_cast<size_t>(selected) < clips.size()) {
                    board.play(clips[selected]);
                    status = "playing " + clips[selected].name;
                }
                return true;
            }
            if (event == Event::Character('s')) {
                board.stop_all();
                status = "stopped";
                return true;
            }
            if (event == Event::Character('r')) {
                rescan();
                return true;
            }
            if (event == Event::Character('q') || event == Event::Escape) {
                screen.Exit();
                return true;
            }
            return false;
        });

        screen.Loop(root);
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

    int mic_index = choose_microphone(mics);
    if (mic_index < 0)
        return 0;
    const AudioSource& mic = mics[mic_index];

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
    run_board(board, label_of(mic));

    g_running = false;
    redirector.join();
    board.stop_all();
    pulse.teardown();
    return 0;
}
