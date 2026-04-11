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
    std::condition_variable cond_;
    bool stop_ = false;

public:
    SafeQueue() = default;
    ~SafeQueue() = default;

    SafeQueue(const SafeQueue&) = delete;
    SafeQueue& operator=(const SafeQueue&) = delete;

    void push(T item){
        std::unique_lock<std::mutex> lock(mtx_);
        queue_.push(std::move(item));
        cond_.notify_one();
    }

    T pop(){
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock,[this]{ return !queue_.empty() || stop_;});
        if(stop_ && queue_.empty()){
            return nullptr;
        }
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }
    std::optional<T> tryPopFor(std::chrono::milliseconds timeout){
        std::unique_lock<std::mutex> lock(mtx_);
        if(cond_.wait_for(lock,timeout,[this]{ return !queue_.empty() || stop_;})){
            if (stop_ && queue_.empty()) return std::nullopt;
            T item = std::move(queue_.front());
            queue_.pop();
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
        cond_.notify_all();  
    }
    bool empty() const{
        std::unique_lock<std::mutex> lock(mtx_);
        return queue_.empty();
    }
};


#endif