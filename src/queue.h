#ifndef SAFE_QUEUE_H
#define SAFE_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>

template <typename T>
class SafeQueue
{

private:
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cond_empty;
    std::condition_variable cond_full_;
    size_t capacity_;
    bool stop_ = false;

public:
    SafeQueue(size_t capacity = 100) :capacity_(capacity) {}
    ~SafeQueue() = default;

    SafeQueue(const SafeQueue&) = delete;
    SafeQueue& operator=(const SafeQueue&) = delete;

    void push(T item){
        std::unique_lock<std::mutex> lock(mtx_);
        // 背压控制：如果队列满且未停止，阻塞生产者
        cond_full_.wait(lock, [this]{ return queue_.size() < capacity_ || stop_; });
        if(stop_) return;

        queue_.push(std::move(item));
        cond_empty.notify_one();
    }

    T pop(){
        std::unique_lock<std::mutex> lock(mtx_);
        cond_empty.wait(lock,[this]{ return !queue_.empty() || stop_;});
        if(stop_ && queue_.empty()){
            return T{}; // 修复：使用 T{} 替代 nullptr，兼容非指针类型(如 Audiochunk)
        }
        T item = std::move(queue_.front());
        queue_.pop();
        cond_full_.notify_one(); // 消费后，唤醒可能阻塞的生产者
        return item;
    }
    std::optional<T> tryPopFor(std::chrono::milliseconds timeout){
        std::unique_lock<std::mutex> lock(mtx_);
        if(cond_empty.wait_for(lock,timeout,[this]{ return !queue_.empty() || stop_;})){
            if (stop_ && queue_.empty()) return std::nullopt;
            T item = std::move(queue_.front());
            queue_.pop();
            cond_full_.notify_one();
            return item;
        }
        return std::nullopt;
    }

    void clear(){
        std::unique_lock<std::mutex> lock(mtx_);
        while(!queue_.empty()){
            queue_.pop();
        }
    }

    void stop(){
        std::unique_lock<std::mutex> lock(mtx_);
        stop_ = true;
        cond_empty.notify_all();
        cond_full_.notify_all();  
    }
    bool empty() const{
        std::unique_lock<std::mutex> lock(mtx_);
        return queue_.empty();
    }
};

struct Audiochunk
{
    std::vector<uint8_t> data;
    double pts;
};



#endif