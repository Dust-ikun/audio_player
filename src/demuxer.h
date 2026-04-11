#ifndef DEMUXER_H
#define DEMUXER_H

extern "C"{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

#include "queue.h"
#include <atomic>
#include <thread>

class Demuxer{
private:
    void demuxloop();

    AVFormatContext* fmt_ctx_ = nullptr;
    AVCodecContext* audio_ctx_ = nullptr;
    int audio_stream_index_ = -1;

    SafeQueue<AVPacket*>& audio_pkt_queue_;
    std::thread demux_thread_;
    std::atomic<bool> running_{false};

public:
    Demuxer(SafeQueue<AVPacket*>& audio_pkt_queue);
    ~Demuxer();

    bool open(const std::string& filename);
    void start();
    void stop();
    void join();

    AVCodecContext* getAudioCodecContex() const{ return audio_ctx_; }
    int getAudioStreamIndex() const{ return audio_stream_index_; }
};

#endif