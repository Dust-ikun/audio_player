#include "demuxer.h"
#include <iostream>


Demuxer::Demuxer(SafeQueue<AVPacket*>& audio_pkt_queue)
    : audio_pkt_queue_(audio_pkt_queue){}

Demuxer::~Demuxer(){
    stop();
    join();
    if(audio_ctx_) avcodec_free_context(&audio_ctx_);
    if(fmt_ctx_) avformat_close_input(&fmt_ctx_);
}

bool Demuxer::open(const std::string& filename){
    int ret = avformat_open_input(&fmt_ctx_,filename.c_str(),nullptr,nullptr);
    if (ret < 0){
        char err[256];
        av_strerror(ret,err,sizeof(err));
        std::cerr << "Cannot open file:" << err << std::endl;
        return false;
    }
    ret = avformat_find_stream_info(fmt_ctx_,nullptr);
    if (ret < 0){
        std::cerr << "No audio stream found" << std::endl;
        return false;
    }

    const AVCodec* codec = nullptr;
    audio_stream_index_ = av_find_best_stream(fmt_ctx_,AVMEDIA_TYPE_AUDIO,-1,-1,&codec,0);
    if (audio_stream_index_ < 0){
        std::cerr << "Failed to find stream info" << std::endl;
        return false;
    }

    audio_ctx_ = avcodec_alloc_context3(codec);
    ret = avcodec_parameters_to_context(audio_ctx_,fmt_ctx_->streams[audio_stream_index_]->codecpar);
    if (ret < 0){
        std::cerr << "Failed to copy codec parameters" << std::endl;
        return false;
    }
    ret = avcodec_open2(audio_ctx_,codec,nullptr);
    if (ret < 0){
        std::cerr << "Cannot open audio codec" << std::endl;
        return false;
    }

    std::cout << "Audio stream found: sample_rate=" << audio_ctx_->sample_rate 
              << ", channels=" << audio_ctx_->ch_layout.nb_channels
              << ", format="   << audio_ctx_->sample_fmt << std::endl;
    return true;    
}

void Demuxer::start(){
    running_ = true;
    demux_thread_ = std::thread(&Demuxer::demuxloop,this);
}

void Demuxer::stop(){
    running_ = false;
    audio_pkt_queue_.stop();
}

void Demuxer::join(){
    if(demux_thread_.joinable())
       demux_thread_.join();
}

void Demuxer::demuxloop(){
    std::cout << "demuxloop start " << std::endl;
    while (running_) {
        AVPacket* pkt = av_packet_alloc();
        int ret = av_read_frame(fmt_ctx_,pkt);
        if(ret < 0){
            if(ret == AVERROR_EOF){
                std::cout << "End of file" << std::endl;
                av_packet_free(&pkt);
                break;
            }
            av_packet_free(&pkt);
            continue;
        } 
        if(pkt->stream_index == audio_stream_index_){
            audio_pkt_queue_.push(pkt);
        }else{
            av_packet_free(&pkt);
        }
    }
    std::cout << "pushing nullptr to signal end" << std::endl;
    audio_pkt_queue_.push(nullptr);
    std::cout << "Demux thread exiting " << std::endl;
}