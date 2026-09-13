//
#include "include/tinyipc/tinyipc.hpp"
//
#include "include/tinyipc/types.hpp"
//
#include <iostream>
//
#include <chrono>
//
#include <thread>


int main() {
    Publisher<UInt32Type, 10> publisher("/test");
    uint32_t counter = 0;
    while (true) {
        //
        uint64_t time_us_publish =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        UInt32Type test{time_us_publish, counter};

        //
        publisher.publish(test);

        //
        std::cout << "Published: " << counter << '\n';
        counter ++;

        //
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}
