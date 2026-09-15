#include "pulse_control.hpp"
#include "soundboard.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace ftxui;

namespace {

    PulseControl* g_pulse = nullptr;
    Soundboard* g_board = nullptr;
    std::atomic<bool> g_running{true};

    const Color kAccent = Color::RGB(137, 180, 250);
    const Color kAccent2 = Color::RGB(203, 166, 247);
    const Color kOk = Color::RGB(166, 227, 161);
    const Color kMuted = Color::RGB(108, 112, 134);

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

    Element logo()
    {
        return hbox({
            text("UNIX") | bold | color(kAccent2),
            text("BOARD") | bold | color(kAccent),
        });
    }

    Element hints(std::vector<std::pair<std::string, std::string>> pairs)
    {
        Elements chips;
        for (const auto& [key, what] : pairs) {
            chips.push_back(hbox({
                text(" " + key) | bold | color(kAccent),
                text(" " + what + "  ") | color(kMuted),
            }));
        }
        return hbox(std::move(chips));
    }

    MenuOption pretty_menu(const char* icon)
    {
        MenuOption option = MenuOption::Vertical();
        option.entries_option.transform = [icon](const EntryState& state) {
            Element label = hbox({
                text(state.active ? " ▶ " : "   ") | color(kOk),
                text(icon + std::string(" ")) | color(kAccent2),
                text(state.label),
            });
            if (state.active)
                label = label | bold | bgcolor(Color::RGB(49, 50, 68));
            else
                label = label | color(Color::RGB(186, 194, 222));
            if (state.focused)
                label = label | color(kAccent);
            return label;
        };
        return option;
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
        auto menu = Menu(&labels, &selected, pretty_menu("🎙"));

        auto root = Renderer(menu, [&] {
            return vbox({
                       hbox({logo(), filler(), text("setup ") | color(kMuted)}),
                       separator() | color(kMuted),
                       text(" Which microphone should we intercept?") |
                           color(kMuted),
                       text(""),
                       menu->Render() | vscroll_indicator | frame |
                           size(HEIGHT, LESS_THAN, 12),
                       separator() | color(kMuted),
                       hints({{"↵", "select"}, {"↑↓", "move"}, {"q", "quit"}}),
                   }) |
                   borderRounded | color(Color::White) |
                   size(WIDTH, GREATER_THAN, 52);
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

    std::string human_size(const std::string& path)
    {
        std::error_code ec;
        auto bytes = std::filesystem::file_size(path, ec);
        if (ec)
            return "?";
        return std::to_string(bytes / 1024) + " KB";
    }

    void run_board(Soundboard& board, const std::string& mic_label)
    {
        std::vector<SoundClip> clips;
        std::vector<std::string> names;
        std::string status = "ready";

        auto rescan = [&] {
            clips = board.scan("sounds");
            names.clear();
            for (const auto& clip : clips)
                names.push_back(clip.name);
            status = "scanned " + std::to_string(clips.size()) + " clip(s)";
        };
        rescan();

        int selected = 0;
        auto screen = ScreenInteractive::Fullscreen();
        auto menu = Menu(&names, &selected, pretty_menu("♪"));

        auto play_selected = [&] {
            if (selected < 0 || static_cast<size_t>(selected) >= clips.size())
                return;
            board.play(clips[selected]);
            status = "playing " + clips[selected].name;
        };

        auto root = Renderer(menu, [&] {
            const size_t live = board.active();

            Element list =
                names.empty()
                    ? vbox({filler(),
                            text("no .mp3 in ./sounds") | bold | center,
                            text("drop some in, then press r") | color(kMuted) |
                                center,
                            filler()})
                    : menu->Render() | vscroll_indicator | frame;

            Element details =
                clips.empty()
                    ? text("—") | color(kMuted)
                    : vbox({
                          hbox({text("clip  ") | color(kMuted),
                                text(clips[selected].name) | bold |
                                    color(kAccent)}),
                          hbox({text("size  ") | color(kMuted),
                                text(human_size(clips[selected].path))}),
                          hbox({text("file  ") | color(kMuted),
                                text(clips[selected].path) | color(kMuted)}),
                      });

            return vbox({
                       hbox({
                           logo(),
                           text("  mic: ") | color(kMuted),
                           text(mic_label) | color(Color::White),
                           filler(),
                       }),
                       separator() | color(kMuted),
                       hbox({
                           window(text(" clips ") | bold | color(kAccent),
                                  list) |
                               flex,
                           window(text(" selected ") | bold | color(kAccent),
                                  details) |
                               size(WIDTH, GREATER_THAN, 34),
                       }) | flex,
                       hbox({
                           text(" " + status) | color(kOk) | flex,
                           text(std::to_string(clips.size()) + " clips  ") |
                               color(kMuted),
                           text(std::to_string(live) + " playing ") |
                               color(live > 0 ? kOk : kMuted),
                       }),
                       separator() | color(kMuted),
                       hints({{"↵", "play"},
                              {"1-9", "quick play"},
                              {"s", "stop"},
                              {"r", "rescan"},
                              {"q", "quit"}}),
                   }) |
                   borderRounded;
        });

        root |= CatchEvent([&](Event event) {
            if (event == Event::Return) {
                play_selected();
                return true;
            }
            if (event.is_character() && event.character().size() == 1) {
                const char c = event.character()[0];
                if (c >= '1' && c <= '9') {
                    const size_t index = static_cast<size_t>(c - '1');
                    if (index < clips.size()) {
                        selected = static_cast<int>(index);
                        play_selected();
                    }
                    return true;
                }
                if (c == 's') {
                    board.stop_all();
                    status = "stopped";
                    return true;
                }
                if (c == 'r') {
                    rescan();
                    return true;
                }
                if (c == 'q') {
                    screen.Exit();
                    return true;
                }
            }
            if (event == Event::Escape) {
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
