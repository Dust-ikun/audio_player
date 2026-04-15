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
    AVCodecContext* video_ctx_ = nullptr;
    AVCodecContext* audio_ctx_ = nullptr;
    int video_stream_index_ = -1;
    int audio_stream_index_ = -1;

    SafeQueue<AVPacket*>& video_pkt_queue_;
    SafeQueue<AVPacket*>& audio_pkt_queue_;
    AVRational audio_time_base_{0, 1};
    AVRational video_time_base_{0, 1};
    std::thread demux_thread_;
    std::atomic<bool> running_{false};

public:
    Demuxer(SafeQueue<AVPacket*>& video_pkt_queue, SafeQueue<AVPacket*>& audio_pkt_queue);
    ~Demuxer();

    bool open(const std::string& filename);
    void start();
    void stop();
    void join();

    AVCodecContext* getVideoCodecContex() const{ return video_ctx_; }
    AVCodecContext* getAudioCodecContex() const{ return audio_ctx_; }
    int getVideoStreamIndex() const{ return video_stream_index_; }
    int getAudioStreamIndex() const{ return audio_stream_index_; }
    AVRational getAudioTimeBase() const { return audio_time_base_; }
    AVRational getVideoTimeBase() const { return video_time_base_; }
    bool hasVideo() const { return video_stream_index_ >= 0; }
    bool hasAudio() const { return audio_stream_index_ >= 0; }
};

#endif