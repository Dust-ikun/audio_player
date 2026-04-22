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
    void doseek(int64_t seek_target);

    std::atomic<bool> seek_req_{false};
    std::atomic<int64_t> seek_target_{0};

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

    bool is_seekable() const;
    bool hasVideo() const { return video_stream_index_ >= 0; }
    bool hasAudio() const { return audio_stream_index_ >= 0; }

    void requestSeek(double percent);
    int64_t getDuration()const { return fmt_ctx_ ? fmt_ctx_->duration : 0;}


};
#endif