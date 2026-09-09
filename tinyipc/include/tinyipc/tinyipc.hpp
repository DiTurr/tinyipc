#pragma once

// std::byte
#include <cstddef>
// std::string
#include <string>
// shm_open, O_CREAT, O_RDWR
#include <fcntl.h>
// FUTEX_WAIT, FUTEX_WAKE
#include <linux/futex.h>
// mmap, munmap, PROT_READ, PROT_WRITE, MAP_SHARED
#include <sys/mman.h>
// syscall, SYS_futex
#include <sys/syscall.h>
// close, ftruncate
#include <unistd.h>
// fstat, struct stat
#include <sys/stat.h>
// uint32_t
#include <cstdint>
//
#include "shm_buffer.hpp"


/**
 * @brief Publisher for shared-memory IPC.
 *
 * @tparam T Data type stored in the shared-memory buffer.
 * @tparam BufferSize Number of elements in the shared-memory buffer.
 */
template <typename T, std::size_t BufferSize>
class Publisher {
public:
    /**
     * @brief Creates a shared-memory buffer.
     *
     * @param shm_name Name of the shared-memory object.
     */
    explicit Publisher(const std::string& shm_name) {
        int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);

        ftruncate(fd, sizeof(Buffer<T, BufferSize>));

        buffer_ = static_cast<Buffer<T, BufferSize>*>(
            mmap(nullptr,
                 sizeof(Buffer<T, BufferSize>),
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 fd,
                 0));

        close(fd);
    }

    /**
     * @brief Destroys the publisher.
     */
    ~Publisher() {
        munmap(buffer_, sizeof(Buffer<T, BufferSize>));
    }

    /**
     * @brief Publishes data to slot 0.
     *
     * @param value Data to publish.
     */
    void publish(const T& value) {
        buffer_->slot[0] = value;
        buffer_->header.sequence.fetch_add(1, std::memory_order_release);
        wakeup();
    }

private:
    /**
     * @brief Wakes a waiting subscriber.
     */
    void wakeup() {
        syscall(SYS_futex,
                reinterpret_cast<uint32_t*>(&buffer_->header.sequence),
                FUTEX_WAKE,
                1,
                nullptr,
                nullptr,
                0);
    }

    Buffer<T, BufferSize>* buffer_{nullptr};
};


/**
 * @brief Subscriber for shared-memory IPC.
 *
 * The subscriber does not require the buffer size as a template parameter.
 * The buffer size is available through the shared-memory header.
 *
 * @tparam T Data type received from the shared-memory buffer.
 */
template <typename T>
class Subscriber {
public:
    /**
     * @brief Opens an existing shared-memory buffer.
     *
     * @param shm_name Name of the shared-memory object.
     */
    explicit Subscriber(const std::string& shm_name) {
        int fd = shm_open(shm_name.c_str(), O_RDWR, 0666);

        struct stat st{};
        fstat(fd, &st);

        mapping_size_ = static_cast<std::size_t>(st.st_size);

        mapping_ = mmap(nullptr,
                        mapping_size_,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        fd,
                        0);

        close(fd);

        header_ = static_cast<BufferHeader*>(mapping_);

        // Slot 0 immediately follows the header.
        slot_ = reinterpret_cast<T*>(
            static_cast<std::byte*>(mapping_) + sizeof(BufferHeader));

        sequence_ = header_->sequence.load(std::memory_order_acquire);
    }

    /**
     * @brief Destroys the subscriber.
     */
    ~Subscriber() {
        munmap(mapping_, mapping_size_);
    }

    /**
     * @brief Waits for the next published value.
     *
     * @return Newly published data from slot 0.
     */
    T wait() {
        while (true) {
            uint32_t current =
                header_->sequence.load(std::memory_order_acquire);

            if (current != sequence_) {
                sequence_ = current;
                return slot_[0];
            }

            wait_for_publish();
        }
    }

private:
    /**
     * @brief Waits for the publisher to signal new data.
     */
    void wait_for_publish() {
        syscall(SYS_futex,
                reinterpret_cast<uint32_t*>(&header_->sequence),
                FUTEX_WAIT,
                sequence_,
                nullptr,
                nullptr,
                0);
    }

    /**
     * @brief Pointer to the complete shared-memory mapping.
     */
    void* mapping_{nullptr};

    /**
     * @brief Pointer to the shared-memory header.
     */
    BufferHeader* header_{nullptr};

    /**
     * @brief Pointer to the first data slot.
     */
    T* slot_{nullptr};

    /**
     * @brief Size of the shared-memory mapping.
     */
    std::size_t mapping_size_{0};

    /**
     * @brief Last sequence number received by this subscriber.
     */
    uint32_t sequence_{0};
};
