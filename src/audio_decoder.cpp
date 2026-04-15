#include "audio_decoder.h"
#include <iostream>
#include <cstring>

AudioDecoder::AudioDecoder(SafeQueue<AVPacket*>& pkt_queue,SafeQueue<Audiochunk>& frame_queue, SafeQueue<std::vector<uint8_t>>& free_buffers)
    : pkt_queue_(pkt_queue), frame_queue_(frame_queue), free_buffers_(free_buffers){}

AudioDecoder::~AudioDecoder(){
    stop();
    join();
    if(swr_ctx_) swr_free(&swr_ctx_);
}

void AudioDecoder::setCodecContext(AVCodecContext* codec_ctx){
    codec_ctx_ = codec_ctx;
    initSwr();
}

void AudioDecoder::setTimeBase(AVRational tb){
    audio_time_base_ = tb;
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
        << ") -> out(S16, " << out_channels_ << ")" << std::endl;
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

void AudioDecoder::Swr_send_Frame(AVFrame*& frame, double pts_sec) {
    // 1. 计算需要输出的样本数（swr_get_out_samples 会返回转换后可能的样本数）
    int out_nb_samples = swr_get_out_samples(swr_ctx_, frame->nb_samples);
    
    // 2. 计算输出缓冲区总字节数
    int bytes_per_sample = av_get_bytes_per_sample(out_sample_fmt);
    int out_buffer_size = out_nb_samples * out_channels_ * bytes_per_sample;
    
    std::vector<uint8_t> pcm_data;

    // 核心优化：尝试从空闲池获取 buffer，避免 malloc
    // 使用 0ms 超时，非阻塞获取。如果没有空闲的，再创建新的。
    auto opt_buf = free_buffers_.tryPopFor(std::chrono::milliseconds(0));
    if (opt_buf.has_value()) {
        pcm_data = std::move(opt_buf.value());
    }
    
    // resize 不会缩减底层 capacity，如果复用的 buffer 足够大，这里就是零开销
    pcm_data.resize(out_buffer_size);
    
    // 4. 准备输出平面指针（对于打包格式，只需要一个平面；对于 planar 格式需要多个平面）
    //    但 swr_convert 支持打包格式的单指针，我们统一使用单指针方式。
    uint8_t* out_planes[1] = { pcm_data.data() };
    
    // 5. 准备输入平面指针（从 AVFrame 中获取）
    const uint8_t* in_planes[AV_NUM_DATA_POINTERS];
    for (int i = 0; i < frame->ch_layout.nb_channels; ++i) {
        in_planes[i] = frame->data[i];
    }
    
    // 6. 执行重采样，结果直接写入 pcm_data
    int samples_converted = swr_convert(swr_ctx_, out_planes, out_nb_samples, in_planes, frame->nb_samples);
    if (samples_converted > 0) {
        // 7. 调整 vector 大小为实际转换得到的字节数
        int actual_size = samples_converted * out_channels_ * bytes_per_sample;
        pcm_data.resize(actual_size);
        
        // 8. 构造 AudioChunk 并移动数据
        Audiochunk chunk;
        chunk.data = std::move(pcm_data);   // 移动 vector，无拷贝
        chunk.pts = pts_sec;
        
        frame_queue_.push(std::move(chunk));
    } else {
        // 转换失败或为空，将 buffer 还给资源池
        pcm_data.clear();
        free_buffers_.push(std::move(pcm_data));
    }
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
            double pts_sec = frame->best_effort_timestamp * av_q2d(audio_time_base_);
            Swr_send_Frame(frame, pts_sec);
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
            double pts_sec = frame->best_effort_timestamp * av_q2d(audio_time_base_);
            Swr_send_Frame(frame, pts_sec);
            av_frame_unref(frame);
    }
    av_frame_free(&frame);
    std::cout << "Audio decoder thread exiting" << std::endl;
}