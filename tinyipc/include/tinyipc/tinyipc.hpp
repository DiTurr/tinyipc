#pragma once

// std::byte
#include <cstddef>
// INT_MAX
#include <climits>
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
// shared memory buffer
#include "shm_buffer.hpp"
// std::chrono::steady_clock
#include <chrono>


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
        // Create a fresh shared-memory object.
        int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
        // TODO: error handling
        ftruncate(fd, sizeof(Buffer<T, BufferSize>));
        buffer_ = static_cast<Buffer<T, BufferSize>*>(
            mmap(nullptr,
                 sizeof(Buffer<T, BufferSize>),
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 fd,
                 0));
        close(fd);
        buffer_->header.buffer_size = BufferSize;
    }

    /**
     * @brief Destroys the publisher.
     */
    ~Publisher() {
        // TODO: if process is killed, how to delete the allocated shared memory.
        munmap(buffer_, sizeof(Buffer<T, BufferSize>));
    }

    /**
     * @brief Publishes data to buffer.
     *
     *   Before publish:
     *       header.sequence = N
     *       slot.sequence   = N
     *
     *   Start writing:
     *       slot.sequence   = N + 1   ← invalidate slot
     *
     *   Write:
     *       slot.data = value
     *
     *   Publish:
     *       header.sequence = N + 1   ← subscriber is notified
     *
     * @param value Data to publish.
     */
    void publish(const T& value) {
        uint32_t sequence = buffer_->header.sequence.load(std::memory_order_acquire);
        std::size_t idx_slot = sequence % BufferSize;
        // Update sequence in the slot --> fixed data validity in reference to subscribers notification
        // Invalidate slot while it is being modified.
        buffer_->slot[idx_slot].sequence.store(sequence + 1, std::memory_order_release);
        // Update timestamp in the slot
        buffer_->slot[idx_slot].timestamp_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        // Update data in the slot
        buffer_->slot[idx_slot].data = value;
        // Update sequence in the header --> subscribers notified
        buffer_->header.sequence.fetch_add(1, std::memory_order_release);
        wakeup();
    }

private:
    /**
     * @brief Wakes waiting subscribers.
     */
    void wakeup() {
        syscall(SYS_futex,
                reinterpret_cast<uint32_t*>(&buffer_->header.sequence),
                FUTEX_WAKE,
                INT_MAX,
                nullptr,
                nullptr,
                0);
    }

    /**
     * @brief Pointer to buffer struct.
     *   +-------------------------+
     *   | BufferHeader            |
     *   |  - sequence             |
     *   |  - buffer_size          |
     *   +-------------------------+
     *   | Slot<T>                 |
     *   |  - sequence             |
     *   |  - data                 |
     *   +-------------------------+
     *   | Slot<T>                 |
     *   |  - sequence             |
     *   |  - data                 |
     *   +-------------------------+
     *   | ...                     |
     *   +-------------------------+
     *   | Slot<T>                 |
     *   |  - sequence             |
     *   |  - data                 |
     *   +-------------------------+
     */
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
        sequence_ = header_->sequence.load(std::memory_order_acquire);
        // Slot 0 immediately follows the header.
        slot_ = reinterpret_cast<Slot<T>*>(
            static_cast<std::byte*>(mapping_) + sizeof(BufferHeader));
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
     * @return Newly published data.
     */
    T wait() {
        while (true) {
            uint32_t sequence_header = header_->sequence.load(std::memory_order_acquire);
            if (sequence_header != sequence_) {
                // Read slot sequence
                // Write procedure from publisher:
                //  - idx_slot = sequence % buffer_size
                //  - Write to that slot index
                //  - Increase sequence by 1 to wake up subscribers --> therefore (sequence_ - 1)
                //    used to calculate slot index.
                std::size_t idx_slot = (sequence_header - 1) % header_->buffer_size;
                uint32_t sequence_slot =
                    slot_[idx_slot].sequence.load(std::memory_order_acquire);

                // The slot has already been overwritten by a newer
                // message. Retry using the latest sequence number.
                if (sequence_slot != sequence_header) {
                    sequence_ = sequence_header;
                    continue;
                }

                // Read data of the slot
                T value = slot_[idx_slot].data;

                // Check again after copying. If the sequence changed,
                // the publisher overwrote the slot while it was being read.
                if (slot_[idx_slot].sequence.load(std::memory_order_acquire) != sequence_slot) {
                    sequence_ = header_->sequence.load(std::memory_order_acquire);
                    continue;
                }

                sequence_ = sequence_header;
                return value;
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
    Slot<T>* slot_{nullptr};

    /**
     * @brief Size of the shared-memory mapping.
     */
    std::size_t mapping_size_{0};

    /**
     * @brief Last sequence number received by this subscriber.
     */
    uint32_t sequence_{0};
};
