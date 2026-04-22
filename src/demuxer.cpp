#include "demuxer.h"
#include <iostream>


Demuxer::Demuxer(SafeQueue<AVPacket*>& video_pkt_queue, SafeQueue<AVPacket*>& audio_pkt_queue)
    : video_pkt_queue_(video_pkt_queue), audio_pkt_queue_(audio_pkt_queue){}

Demuxer::~Demuxer(){
    stop();
    join();
    if(video_ctx_) avcodec_free_context(&video_ctx_);
    if(audio_ctx_) avcodec_free_context(&audio_ctx_);
    if(fmt_ctx_) avformat_close_input(&fmt_ctx_);
}

bool Demuxer::open(const std::string& filename) {
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "timeout","5000000", 0);
    int ret = avformat_open_input(&fmt_ctx_, filename.c_str(), nullptr, &opts);
    if (ret < 0) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        std::cerr << "Cannot open file: " << err << std::endl;
        return false;
    }

    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to find stream info" << std::endl;
        return false;
    }

    // 查找视频流（可选）
    const AVCodec* video_codec = nullptr;
    video_stream_index_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &video_codec, 0);
    if (video_stream_index_ >= 0) {
        video_ctx_ = avcodec_alloc_context3(video_codec);
        ret = avcodec_parameters_to_context(video_ctx_, fmt_ctx_->streams[video_stream_index_]->codecpar);
        if (ret < 0) {
            std::cerr << "Failed to copy video codec parameters" << std::endl;
            return false;
        }
        ret = avcodec_open2(video_ctx_, video_codec, nullptr);
        if (ret < 0) {
            std::cerr << "Cannot open video codec" << std::endl;
            return false;
        }
        // 保存视频流的时间基
        video_time_base_ = fmt_ctx_->streams[video_stream_index_]->time_base;
        std::cout << "Video stream found: " << video_ctx_->width << "x" << video_ctx_->height << std::endl;
    } else {
        std::cout << "No video stream found, audio only." << std::endl;
    }

    // 查找音频流（必须）
    const AVCodec* audio_codec = nullptr;
    audio_stream_index_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, &audio_codec, 0);
    if (audio_stream_index_ < 0) {
        std::cerr << "Failed to find audio stream" << std::endl;
        return false;   // 音频流必须存在
    }

    audio_ctx_ = avcodec_alloc_context3(audio_codec);
    ret = avcodec_parameters_to_context(audio_ctx_, fmt_ctx_->streams[audio_stream_index_]->codecpar);
    if (ret < 0) {
        std::cerr << "Failed to copy audio codec parameters" << std::endl;
        return false;
    }
    ret = avcodec_open2(audio_ctx_, audio_codec, nullptr);
    if (ret < 0) {
        std::cerr << "Cannot open audio codec" << std::endl;
        return false;
    }
    // 保存音频流的时间基
    audio_time_base_ = fmt_ctx_->streams[audio_stream_index_]->time_base;

    std::cout << "Audio stream found: sample_rate=" << audio_ctx_->sample_rate 
              << ", channels=" << audio_ctx_->ch_layout.nb_channels
              << ", format=" << audio_ctx_->sample_fmt << std::endl;
    return true;
}

void Demuxer::start(){
    running_ = true;
    demux_thread_ = std::thread(&Demuxer::demuxloop,this);
}

void Demuxer::stop(){
    running_ = false;
    audio_pkt_queue_.stop();
    video_pkt_queue_.stop();
}

void Demuxer::join(){
    if(demux_thread_.joinable())
       demux_thread_.join();
}

void Demuxer::demuxloop(){
    std::cout << "demuxloop start " << std::endl;
    int error_count = 0;
    const int MAX_ERRORS = 10;
    while (running_) {
        if (seek_req_.exchange(false)){
            int64_t target = seek_target_;
            doseek(target);
            continue;
        }
        AVPacket* pkt = av_packet_alloc();
        int ret = av_read_frame(fmt_ctx_,pkt);
        if(ret < 0){
            if(ret == AVERROR_EOF){
                std::cout << "End of file" << std::endl;
                av_packet_free(&pkt);
                break;
            }
            av_packet_free(&pkt);
            error_count++;
            if (error_count > MAX_ERRORS){
                std::cerr << "Too many read errors, aborting demuxer." << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;     
        }
        error_count = 0;
         
        if(pkt->stream_index == audio_stream_index_){
            audio_pkt_queue_.push(pkt);
        }else if(pkt->stream_index == video_stream_index_){
            video_pkt_queue_.push(pkt);
        }
    }
    std::cout << "pushing nullptr to signal end" << std::endl;
    video_pkt_queue_.push(nullptr);
    audio_pkt_queue_.push(nullptr);
    std::cout << "Demux thread exiting " << std::endl;
}

void Demuxer::requestSeek(double percent){
    if (!fmt_ctx_ || percent < 0.0 || percent > 1.0) return;
    int64_t target = static_cast<int64_t>(percent * fmt_ctx_->duration);
    seek_target_.store(target);
    seek_req_.store(true);
}

void Demuxer::doseek(int64_t seek_target){

    audio_pkt_queue_.clearAndfree([](AVPacket* pkt){ av_packet_free(&pkt); });
    video_pkt_queue_.clearAndfree([](AVPacket* pkt){ av_packet_free(&pkt); });

    int ret = av_seek_frame(fmt_ctx_,-1, seek_target, AVSEEK_FLAG_BACKWARD);
    if (ret < 0){
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "av_seek_frame failed: " << errbuf << std::endl;
    }

    AVPacket* flush_pkt = flush_pkt_sentinel();
    video_pkt_queue_.push(flush_pkt);
    audio_pkt_queue_.push(flush_pkt);
  

    std::cout << "Seek executed to " << seek_target / 1000000.0 << "s" << std::endl;
}

bool Demuxer::is_seekable() const {return (fmt_ctx_->duration > 0) && (fmt_ctx_->pb->seekable & AVIO_SEEKABLE_NORMAL); }