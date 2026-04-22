#include "video_decoder.h"
#include <iostream>

VideoDecoder::VideoDecoder(SafeQueue<AVPacket*>& pkt_queue,
                           SafeQueue<AVFrame*>& frame_queue)
    : pkt_queue_(pkt_queue), frame_queue_(frame_queue) {}

VideoDecoder::~VideoDecoder() {
    stop();
    join();
    // 注意：codec_ctx_ 由 Demuxer 管理，不在这里释放
}

void VideoDecoder::setCodecContext(AVCodecContext* ctx) {
    codec_ctx_ = ctx;
}

void VideoDecoder::setTimeBase(AVRational tb) {video_time_base_ = tb;}

void VideoDecoder::start() {
    running_ = true;
    decode_thread_ = std::thread(&VideoDecoder::decodeLoop, this);
}

void VideoDecoder::stop() {
    running_ = false;
    pkt_queue_.stop();
    frame_queue_.stop();
}

void VideoDecoder::join() {
    if (decode_thread_.joinable())
        decode_thread_.join();
}

void VideoDecoder::decodeLoop() {
    AVPacket* pkt = nullptr;
    AVFrame* frame = av_frame_alloc();
    bool need_keyframe = false;
    if (!frame) return;

    std::cout << "Video decoder loop started" << std::endl;

    while (running_) {
        pkt = pkt_queue_.pop();
        if (!pkt) break;  

        if (pkt == flush_pkt_sentinel()){
            frame_queue_.clearAndfree([](AVFrame* frm){av_frame_free(&frm);});
            avcodec_flush_buffers(codec_ctx_);
            need_keyframe = true; 
            std::cout << "Video decoder flush" << std::endl;
            continue;
        }
        // 如果需要关键帧，但当前包不是关键帧，丢弃
        if (need_keyframe && !(pkt->flags & AV_PKT_FLAG_KEY)) {
            av_packet_free(&pkt);
            continue;
        }

        // 成功收到关键帧后，清除标志
        if (pkt->flags & AV_PKT_FLAG_KEY) {
            need_keyframe = false;
        }

        if (avcodec_send_packet(codec_ctx_, pkt) != 0) {
            av_packet_free(&pkt);
            continue;
        }
        av_packet_free(&pkt);

        while (true) {
            int ret = avcodec_receive_frame(codec_ctx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0) {
                std::cerr << "Video decode error" << std::endl;
                break;
            }
            
            AVFrame* out_frame = av_frame_alloc();
            av_frame_move_ref(out_frame, frame);
            if (out_frame) {
                frame_queue_.push(out_frame);
            }
        }
    }

    // 刷新解码器
    avcodec_send_packet(codec_ctx_, nullptr);
    while (true) {
        int ret = avcodec_receive_frame(codec_ctx_, frame);
        if (ret != 0) break;
        AVFrame* out_frame = av_frame_clone(frame);
        if (out_frame) frame_queue_.push(out_frame);
        av_frame_unref(frame);
    }

    av_frame_free(&frame);
    std::cout << "Video decoder thread exiting" << std::endl;
}