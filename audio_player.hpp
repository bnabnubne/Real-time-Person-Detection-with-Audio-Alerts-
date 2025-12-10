#ifndef AUDIO_PLAYER_HPP
#define AUDIO_PLAYER_HPP

#include <string>

class AudioPlayer
{
public:
    AudioPlayer(const std::string& file_path,
                int throttle_ms = 2000);

    void play();   // non-blocking, có chống spam

private:
    std::string file;
    int         throttle;
    long long   last_ms;

    void        play_blocking();
    long long   now_ms() const;
};

#endif // AUDIO_PLAYER_HPP