#include "pulse_control.hpp"
#include "soundboard.hpp"

#include "unixboard.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
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

    std::string label_of(const AudioSource& mic)
    {
        return mic.description.empty() ? mic.name : mic.description;
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

    PulseControl pulse;
    Soundboard board(pulse.node_name());
    g_pulse = &pulse;
    g_board = &board;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::mutex pulse_mutex; // pulse is touched by the UI and the redirector
    std::atomic<int> active_mic{-1};
    std::vector<SoundClip> clips;

    auto ui = MainWindow::create();

    std::vector<slint::SharedString> mic_labels;
    for (const auto& mic : mics)
        mic_labels.emplace_back(label_of(mic));
    ui->set_mics(
        std::make_shared<slint::VectorModel<slint::SharedString>>(mic_labels));

    auto rescan = [&] {
        clips = board.scan("sounds");
        std::vector<Clip> rows;
        for (const auto& clip : clips) {
            Clip row;
            row.name = slint::SharedString(clip.name);
            rows.push_back(std::move(row));
        }
        ui->set_clips(std::make_shared<slint::VectorModel<Clip>>(rows));
        ui->set_status(slint::SharedString(
            "scanned " + std::to_string(clips.size()) + " clip(s)"));
    };
    rescan();

    // ponytail: play() blocks the UI thread while pw-link settles (~100ms,
    // 1.5s worst case). Move it to a worker thread if it ever feels sticky.
    ui->on_play([&](int index) {
        if (index < 0 || static_cast<size_t>(index) >= clips.size())
            return;
        if (active_mic < 0) {
            ui->set_status("pick a microphone first");
            return;
        }
        board.play(clips[index]);
        ui->set_playing_index(index);
        ui->set_status(slint::SharedString("playing " + clips[index].name));
    });

    ui->on_stop_all([&] {
        board.stop_all();
        ui->set_playing_index(-1);
        ui->set_status("stopped");
    });

    ui->on_rescan(rescan);

    ui->on_pick_mic([&](int index) {
        if (index < 0 || static_cast<size_t>(index) >= mics.size() ||
            index == active_mic)
            return;
        board.stop_all();

        std::lock_guard<std::mutex> lock(pulse_mutex);
        pulse.teardown();
        if (!pulse.setup(mics[index])) {
            active_mic = -1;
            ui->set_active_mic(-1);
            ui->set_status("failed to grab that mic");
            return;
        }
        pulse.redirect_recording_apps();
        active_mic = index;
        ui->set_active_mic(index);
        ui->set_status(slint::SharedString("mic → " + label_of(mics[index])));
    });

    std::thread redirector([&] {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!g_running || active_mic < 0)
                continue;
            std::lock_guard<std::mutex> lock(pulse_mutex);
            pulse.redirect_recording_apps();
        }
    });

    slint::Timer heartbeat;
    heartbeat.start(slint::TimerMode::Repeated, std::chrono::milliseconds(200),
                    [&] { ui->set_playing(static_cast<int>(board.active())); });

    ui->run();

    g_running = false;
    redirector.join();
    board.stop_all();
    std::lock_guard<std::mutex> lock(pulse_mutex);
    pulse.teardown();
    return 0;
}
