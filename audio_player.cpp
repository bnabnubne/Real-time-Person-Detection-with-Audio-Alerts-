#include "audio_player.hpp"

#include <sys/time.h>
#include <cstdlib>
#include <thread>

AudioPlayer::AudioPlayer(const std::string& file_path,
                         int throttle_ms)
    : file(file_path)
    , throttle(throttle_ms)
    , last_ms(0)
{
}

long long AudioPlayer::now_ms() const
{
    timeval tv;
    gettimeofday(&tv, nullptr);
    return (long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

void AudioPlayer::play_blocking()
{
    // dùng aplay như bên Python
    std::string cmd = "aplay -q \"" + file + "\"";
    std::system(cmd.c_str());
}

void AudioPlayer::play()
{
    long long now = now_ms();
    if (now - last_ms < throttle)
        return;

    last_ms = now;

    std::thread t([this]()
    {
        play_blocking();
    });
    t.detach();
}