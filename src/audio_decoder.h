#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

extern "C"{
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

#include "queue.h"
#include <vector>
#include <atomic>
#include <thread>

class AudioDecoder{
private:
    void decodeloop();
    bool initSwr();
    void Swr_send_Frame(AVFrame*& frame);

    SafeQueue<AVPacket*>& pkt_queue_;
    SafeQueue<std::vector<uint8_t>>& frame_queue_;
    AVCodecContext* codec_ctx_ = nullptr;

    SwrContext* swr_ctx_ = nullptr;
    int out_sample_rate_ = 44100;
    int out_channels_ = 2;
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;

    std::thread decode_thread_;
    std::atomic<bool> running_{false};

public:
    AudioDecoder(SafeQueue<AVPacket*>& pkt_queue,SafeQueue<std::vector<uint8_t>>& frame_queue);
    ~AudioDecoder();

    void setCodecContext(AVCodecContext* codec_ctx);
    void start();
    void stop();
    void join();

    int getOutSampleRate() const { return out_sample_rate_; }
    int getOutChannels() const { return out_channels_; }
    AVSampleFormat getOutSampleFmt() const { return out_sample_fmt; }  
};

#endif