#include "audio_decoder.h"
#include <iostream>
#include <cstring>

AudioDecoder::AudioDecoder(SafeQueue<AVPacket*>& pkt_queue,SafeQueue<std::vector<uint8_t>>& frame_queue)
    : pkt_queue_(pkt_queue), frame_queue_(frame_queue){}

AudioDecoder::~AudioDecoder(){
    stop();
    join();
    if(swr_ctx_) swr_free(&swr_ctx_);
}

void AudioDecoder::setCodecContext(AVCodecContext* codec_ctx){
    codec_ctx_ = codec_ctx;
    initSwr();
}

bool AudioDecoder::initSwr(){
    if(!codec_ctx_) return false;

    swr_ctx_ = swr_alloc();
    if(!swr_ctx_) return false;

    AVChannelLayout in_ch_layout = codec_ctx_->ch_layout;
    AVSampleFormat in_sample_fmt = codec_ctx_->sample_fmt;
    int in_sample_rate = codec_ctx_->sample_rate;

    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    out_channels_ = 2;
    out_sample_rate_ = in_sample_rate;
    out_sample_fmt = AV_SAMPLE_FMT_S16;

    av_opt_set_chlayout(swr_ctx_,"in_chlayout",&in_ch_layout,0);
    av_opt_set_int(swr_ctx_,"in_sample_rate",in_sample_rate,0);
    av_opt_set_sample_fmt(swr_ctx_,"in_sample_fmt",in_sample_fmt,0);

    av_opt_set_chlayout(swr_ctx_,"out_chlayout",&out_ch_layout,0);
    av_opt_set_int(swr_ctx_,"out_sample_rate",out_sample_rate_,0);
    av_opt_set_sample_fmt(swr_ctx_,"out_sample_fmt",out_sample_fmt,0);

    int ret = swr_init(swr_ctx_);
    if(ret < 0){
        std::cerr << "swr_init failed" <<std::endl;
        return false;
    }
    std::cout << "Swr initialized: in(" << in_sample_fmt << ", " << in_ch_layout.nb_channels 
        << ", " << ") -> out(S16, " << out_channels_ << ")" << std::endl;
    return true;    
}

void AudioDecoder::start(){
    running_ = true;
    decode_thread_ = std::thread(&AudioDecoder::decodeloop,this);
}

void AudioDecoder::stop(){
    running_ = false;
    pkt_queue_.stop();
    frame_queue_.stop();
}

void AudioDecoder::join(){
    if(decode_thread_.joinable())
        decode_thread_.join();
}

void AudioDecoder::Swr_send_Frame(AVFrame*& frame){
    int out_nb_samples = swr_get_out_samples(swr_ctx_,frame->nb_samples);
    uint8_t* out_buffer = nullptr;
    int out_linesize = 0;
    int out_buffer_size = av_samples_alloc(&out_buffer,&out_linesize,out_channels_,out_nb_samples,out_sample_fmt,1);
    if (out_buffer_size < 0){
        std::cerr << "av_sample_alloc failed" << std::endl;
        return;
      }

    uint8_t* in_planes[AV_NUM_DATA_POINTERS];
    for (int i = 0; i < frame->ch_layout.nb_channels; ++i)
        in_planes[i] = frame->data[i];
    
    int samples_converted = swr_convert(swr_ctx_,&out_buffer,out_nb_samples,in_planes,frame->nb_samples);
    if (samples_converted > 0){
        int data_size = samples_converted * out_channels_ * av_get_bytes_per_sample(out_sample_fmt);
        std::vector<uint8_t> pcm_data(out_buffer, out_buffer + data_size);
        frame_queue_.push(std::move(pcm_data));
    }
    av_free(out_buffer);
}

void AudioDecoder::decodeloop(){
    AVPacket* pkt = nullptr;
    AVFrame* frame = av_frame_alloc();
    if (!frame) return;

    std::cout << "decodeloop start" << std::endl;

    while(running_){
        pkt = pkt_queue_.pop();
        if (!pkt){
            std::cout << "pkt is nullptr, breaking" << std::endl;
            break;
        }

        int ret = avcodec_send_packet(codec_ctx_,pkt);
        if (ret < 0){
            av_packet_free(&pkt);
            continue;
        }

        while (true){
            ret = avcodec_receive_frame(codec_ctx_,frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                break;
            }else if (ret < 0){
                std::cerr << "Decode error" << std::endl;
                break;
            }
           Swr_send_Frame(frame);
           av_frame_unref(frame);
        }
        av_packet_free(&pkt);
    }
    avcodec_send_packet(codec_ctx_,nullptr);
    while (true){
        int ret = avcodec_receive_frame(codec_ctx_,frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                break;
            }else if (ret < 0){
                std::cerr << "Decode error" << std::endl;
                break;
            }
            Swr_send_Frame(frame);
            av_frame_unref(frame);
    }
    av_frame_free(&frame);
    std::cout << "Audio decoder thread exiting" << std::endl;
}