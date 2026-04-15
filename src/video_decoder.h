#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

#include "queue.h"
#include <atomic>
#include <thread>

class VideoDecoder {
private:
    void decodeLoop();

    SafeQueue<AVPacket*>& pkt_queue_;
    SafeQueue<AVFrame*>& frame_queue_;   // 存储解码后的 AVFrame*
    AVCodecContext* codec_ctx_ = nullptr;
    AVRational video_time_base_ = {0, 1};

    std::thread decode_thread_;
    std::atomic<bool> running_{false};

public:
    VideoDecoder(SafeQueue<AVPacket*>& pkt_queue, SafeQueue<AVFrame*>& frame_queue);
    ~VideoDecoder();

    void setCodecContext(AVCodecContext* ctx);
    void setTimeBase(AVRational tb);
    void start();
    void stop();
    void join();
};

#endif