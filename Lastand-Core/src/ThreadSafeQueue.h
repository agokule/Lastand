#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class ThreadSafeQueue {
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable cv;
public:
    void push(T item) {
        std::lock_guard lock(mutex);
        queue.push(std::move(item));
        cv.notify_one();
    }

    // Non-blocking: returns false if empty
    std::optional<T> try_pop() {
        std::lock_guard lock(mutex);

        if (queue.empty())
            return std::nullopt;

        std::optional<T> item = std::move(queue.front());
        queue.pop();
        return item;
    }

    void reset() {
        while (!queue.empty())
            queue.pop();
    }

    bool empty() const {
        return queue.empty();
    }
};

