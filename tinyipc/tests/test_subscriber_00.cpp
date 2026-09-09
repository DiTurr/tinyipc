//
#include "include/tinyipc/tinyipc.hpp"
//
#include "include/tinyipc/types.hpp"
//
#include <iostream>
//
#include <chrono>


int main() {
    Subscriber<UInt32Type> subscriber("/test");
    while (true) {
        //
        UInt32Type data = subscriber.wait();

        //
        uint64_t receive_time_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        uint64_t latency_us = receive_time_us - data.timestamp_us;

        //
        std::cout << "Received: " << data.data
                  << ", latency: " << latency_us << " us\n";
    }
}
