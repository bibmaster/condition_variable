#pragma once
#include <condition_variable>

namespace univang {

// condition variable with CLOCK_MONOTONIC init/timedwait
class condition_variable {
public:
    using native_handle_type = pthread_cond_t*;
    condition_variable() {
        pthread_condattr_t cvattr;
        int ec = pthread_condattr_init(&cvattr);
        if(ec == 0) {
            ec = pthread_condattr_setclock(&cvattr, CLOCK_MONOTONIC);
            if(ec == 0)
                ec = pthread_cond_init(&cv_, &cvattr);
            pthread_condattr_destroy(&cvattr);
        }
        if(ec != 0)
            throw std::system_error(ec, std::generic_category());
    }
    condition_variable(const condition_variable&) = delete;
    condition_variable& operator=(const condition_variable&) = delete;
    ~condition_variable() {
        pthread_cond_destroy(&cv_);
    }

    native_handle_type native_handle() {
        return &cv_;
    }
    void notify_one() {
        int ec = pthread_cond_signal(&cv_);
        if(ec != 0)
            throw std::system_error(ec, std::generic_category());
    }
    void notify_all() {
        int ec = pthread_cond_broadcast(&cv_);
        if(ec != 0)
            throw std::system_error(ec, std::generic_category());
    }
    void wait(std::unique_lock<std::mutex>& lock) {
        if(!lock.owns_lock())
            throw std::system_error(
                std::make_error_code(std::errc::operation_not_permitted),
                "condition_variable: mutex not locked");
        auto* mtx = lock.mutex()->native_handle();
        int ec = pthread_cond_wait(&cv_, mtx);
        if(ec != 0)
            throw std::system_error(ec, std::generic_category());
    }
    template<class Predicate>
    void wait(std::unique_lock<std::mutex>& lock, Predicate pred) {
        while(!pred())
            wait(lock);
    }

    template<class Rep, class Period>
    std::cv_status wait_for(
        std::unique_lock<std::mutex>& lock,
        const std::chrono::duration<Rep, Period>& rel_time) {
        auto ts = to_abs_time_(rel_time);
        return wait_until_(lock, ts);
    }

    template<class Rep, class Period, class Predicate>
    bool wait_for(
        std::unique_lock<std::mutex>& lock,
        const std::chrono::duration<Rep, Period>& rel_time, Predicate pred) {
        auto ts = to_abs_time_(rel_time);
        while(!pred())
            if(wait_until_(lock, ts) == std::cv_status::timeout)
                return pred();
        return true;
    }

    template<class Clock, class Duration>
    std::cv_status wait_until(
        std::unique_lock<std::mutex>& lock,
        const std::chrono::time_point<Clock, Duration>& abs_time) {
        auto ts = to_abs_time_(abs_time - Clock::now());
        return wait_until_(lock, ts);
    }

    template<class Clock, class Duration, class Predicate>
    bool wait_until(
        std::unique_lock<std::mutex>& lock,
        const std::chrono::time_point<Clock, Duration>& abs_time,
        Predicate pred) {
        auto ts = to_abs_time_(abs_time - Clock::now());
        while(!pred())
            if(wait_until_(lock, ts) == std::cv_status::timeout)
                return pred();
        return true;
    }

private:
    template<class Rep, class Period>
    timespec to_abs_time_(const std::chrono::duration<Rep, Period>& rel_time) {
        timespec res;
        if(rel_time <= std::chrono::duration<Rep, Period>::zero()) {
            res.tv_sec = 0;
            res.tv_nsec = 0;
        }
        else {
            clock_gettime(CLOCK_MONOTONIC, &res);
            auto s = std::chrono::duration_cast<std::chrono::seconds>(rel_time);
            res.tv_sec += (time_t)s.count();
            auto ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(rel_time)
                - std::chrono::nanoseconds(s);
            res.tv_nsec += (long)ns.count();
            if(res.tv_nsec >= std::nano::den) {
                res.tv_nsec -= std::nano::den;
                ++res.tv_sec;
            }
        }
        return res;
    }

    std::cv_status wait_until_(
        std::unique_lock<std::mutex>& lock, timespec& ts) {
        auto* mtx = lock.mutex()->native_handle();
        if(!lock.owns_lock())
            throw std::system_error(
                std::make_error_code(std::errc::operation_not_permitted),
                "condition_variable: mutex not locked");
        int ec = pthread_cond_timedwait(&cv_, mtx, &ts);
        switch(ec) {
        case 0:
            return std::cv_status::no_timeout;
        case ETIMEDOUT:
            return std::cv_status::timeout;
        default:
            throw std::system_error(ec, std::generic_category());
        }
    }

private:
    pthread_cond_t cv_;
};

} // namespace univang
