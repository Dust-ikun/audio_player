#include <iostream>
#include <SDL2/SDL.h>
#include <atomic>
#include <thread>
#include <chrono>

#include "queue.h"
#include "demuxer.h"
#include "audio_decoder.h"

SafeQueue<std::vector<uint8_t>> audio_frame_queue;
std::atomic<bool> audio_playing{true};

void audioCallback(void* userdata, Uint8* stream, int len){
    static std::vector<uint8_t> current_chunk;
    static size_t pos = 0;

    while (len > 0){
        if (current_chunk.empty() || pos >= current_chunk.size()){
            auto opt = audio_frame_queue.tryPopFor(std::chrono::milliseconds(10));
            if(opt.has_value()){
                current_chunk = std::move(opt.value());
                pos = 0;
            }else{
                SDL_memset(stream, 0, len);
                return;
            }
        }
        size_t to_copy = std::min((size_t)len, current_chunk.size() - pos);
        memcpy(stream, current_chunk.data() + pos, to_copy);
        stream += to_copy;
        len -= to_copy;
        pos += to_copy;
    }
}

int main(int argc, char* argv[]){
    if (argc < 2){
        std::cerr << "Usage: " << argv[0] << " <media_file" << std::endl;
        return -1;
    }
    std::string filename = argv[1];

    if(SDL_Init(SDL_INIT_AUDIO) < 0){
        std::cerr << "SDL_Init error: " << SDL_GetError() << std::endl;
        return -1;
    }

    SafeQueue<AVPacket*> audio_pkt_queue;
    Demuxer demuxer(audio_pkt_queue);
    if(!demuxer.open(filename)){
        return -1;
    }
    AudioDecoder decoder(audio_pkt_queue, audio_frame_queue);
    decoder.setCodecContext(demuxer.getAudioCodecContex());

    SDL_AudioSpec wanted_spec, obtained_spec;
    SDL_zero(wanted_spec);
    wanted_spec.freq = decoder.getOutSampleRate();
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = decoder.getOutChannels();
    wanted_spec.samples = 1024;
    wanted_spec.callback = audioCallback;
    wanted_spec.userdata = nullptr;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &obtained_spec, 0);
    if (dev == 0){
        std::cerr << "Open audio device error: " << SDL_GetError() << std::endl;
        return -1;
    }
    std::cout << "SDL audio open: freq=" << obtained_spec.freq << ", format=" << obtained_spec.format 
        << ", channels=" << (int)obtained_spec.channels << std::endl;
    
    demuxer.start();
    decoder.start();

    SDL_PauseAudioDevice(dev, 0);

    std::cout << "Playing... Press Enter to exit." << std::endl;
    std::cin.clear();
    std::cin.get();

    SDL_PauseAudioDevice(dev, 1);
    SDL_CloseAudioDevice(dev);

    demuxer.stop();
    decoder.stop();
    demuxer.join();
    decoder.join();

    while (auto pkt = audio_pkt_queue.pop()){
        av_packet_free(&pkt);
    }
    audio_frame_queue.clear();

    SDL_Quit();
    return 0;
}