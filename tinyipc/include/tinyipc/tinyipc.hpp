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
// std::chrono::steady_clock
#include <chrono>
// std::runtime_error
#include <stdexcept>
// flock()
#include <sys/file.h>
// std::strerror
#include <cstring>
// shared memory buffer
#include "shm_buffer.hpp"
// base type
#include "types.hpp"


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
        shm_fd_ = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
        if (shm_fd_ == -1) {
            const int error = errno;
            throw std::runtime_error("Failed to open shared memory '" + shm_name + "': " + std::strerror(error));
        }

        // Ensure that no other publisher is active.
        if (flock(shm_fd_, LOCK_EX | LOCK_NB) == -1) {
            const int error = errno;
            close(shm_fd_);
            shm_fd_ = -1;
            if (error == EWOULDBLOCK) {
                throw std::runtime_error("Another publisher is already active for '" + shm_name + "'");
            } else {
                throw std::runtime_error("Failed to lock shared memory '" + shm_name + "': " + std::strerror(error));
            }
        }

        // Set the shared-memory object to the required size.
        // Is this the correct way if the buffer header has a mismatch? It should not be a
        // problem, specially because the publisher is not active anymore. But what about
        // possible subscribers? --> subscribers should check in every wakeup callback that no
        // modification happened to buffer configuration.
        if (ftruncate(shm_fd_, sizeof(Buffer<T, BufferSize>)) == -1) {
            const int error = errno;
            flock(shm_fd_, LOCK_UN);
            close(shm_fd_);
            shm_fd_ = -1;
            throw std::runtime_error("Failed to resize shared memory '" + shm_name + "': " + std::strerror(error));
        }

        // Map the shared memory into the process.
        void* ptr = static_cast<Buffer<T, BufferSize>*>(
            mmap(nullptr,
                 sizeof(Buffer<T, BufferSize>),
                 PROT_READ | PROT_WRITE,
                 MAP_SHARED,
                 shm_fd_,
                 0));
        if (ptr == MAP_FAILED) {
            const int error = errno;
            close(shm_fd_);
            shm_fd_ = -1;
            throw std::runtime_error("Failed to map shared memory '" + shm_name + "': " + std::strerror(error));
        }

        // Pointer to the first element of the buffer
        buffer_ = static_cast<Buffer<T, BufferSize>*>(ptr);

        // Update header information
        buffer_->header.buffer_size = BufferSize;
        buffer_->header.id_msg = BaseType::typeId<T>();
    }

    /**
     * @brief Destroys the publisher.
     */
    ~Publisher() {
        if (buffer_ != MAP_FAILED) {
            munmap(buffer_, sizeof(Buffer<T, BufferSize>));
        }
        if (shm_fd_ != -1) {
            close(shm_fd_);
        }
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
        const uint32_t sequence = buffer_->header.sequence.load(std::memory_order_acquire);
        const std::size_t idx_slot = sequence % BufferSize;
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
    // shared memory file descriptor
    int shm_fd_{-1};
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
        // Open the shared-memory object.
        const int fd = shm_open(shm_name.c_str(), O_RDWR, 0666);
        if (fd == -1) {
            const int error = errno;
            throw std::runtime_error("Failed to open shared memory '" + shm_name + "': " + std::strerror(error));
        }

        // Get the size of the shared-memory object.
        struct stat st{};
        if (fstat(fd, &st) == -1) {
            const int error = errno;
            close(fd);
            throw std::runtime_error("Failed to get size of shared memory '" + shm_name + "': " + std::strerror(error));
        }
        mapping_size_ = static_cast<std::size_t>(st.st_size);

        // Map the shared memory into the process.
        mapping_ = mmap(nullptr,
                        mapping_size_,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        fd,
                        0);
        close(fd);
        if (mapping_ == MAP_FAILED) {
            const int error = errno;
            mapping_ = nullptr;
            mapping_size_ = 0;
            throw std::runtime_error("Failed to map shared memory '" + shm_name + "': " + std::strerror(error));
        }

        // Get the shared-memory header.
        if (mapping_size_ < sizeof(BufferHeader)) {
            munmap(mapping_, mapping_size_);
            mapping_ = nullptr;
            mapping_size_ = 0;
            throw std::runtime_error("Shared memory '" + shm_name + "' is too small");
        }
        header_ = static_cast<BufferHeader*>(mapping_);

        // Store the expected configuration.
        id_msg_ = BaseType::typeId<T>();
        buffer_size_ = header_->buffer_size;

        // Check that the buffer configuration matches this subscriber.
        if (header_->id_msg != id_msg_) {
            munmap(mapping_, mapping_size_);
            mapping_ = nullptr;
            mapping_size_ = 0;
            throw std::runtime_error("Shared-memory message type mismatch for '" + shm_name + "'");
        }

        // Check that the buffer size is valid before using it for indexing.
        if (buffer_size_ == 0) {
            throw std::runtime_error("Shared-memory buffer size is zero for '" + shm_name + "'");
        }

        // Check that the mapping contains the complete buffer.
        const std::size_t expected_size = sizeof(BufferHeader) + buffer_size_ * sizeof(Slot<T>);
        if (mapping_size_ < expected_size) {
            throw std::runtime_error("Shared memory '" + shm_name + "' has an invalid size");
        }

        // Initialize the local sequence number.
        sequence_ = header_->sequence.load(std::memory_order_acquire);

        // Slot 0 immediately follows the header.
        slot_ = reinterpret_cast<Slot<T>*>(static_cast<std::byte*>(mapping_) + sizeof(BufferHeader));
    }

    /**
     * @brief Destroys the subscriber.
     */
    ~Subscriber() {
        if (mapping_ != nullptr && mapping_ != MAP_FAILED) {
            munmap(mapping_, mapping_size_);
        }
    }

    /**
     * @brief Waits for the next published value.
     *
     * @return Newly published data.
     */
    T wait() {
        while (true) {
            const uint32_t sequence_header = header_->sequence.load(std::memory_order_acquire);
            if (sequence_header != sequence_) {
                // Read slot sequence
                // Write procedure from publisher:
                //  - idx_slot = sequence % buffer_size
                //  - Write to that slot index
                //  - Increase sequence by 1 to wake up subscribers --> therefore (sequence_ - 1)
                //    used to calculate slot index.
                const std::size_t idx_slot = (sequence_header - 1) % buffer_size_;
                const uint32_t sequence_slot = slot_[idx_slot].sequence.load(std::memory_order_acquire);

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
        // Check that the buffer configuration is still valid after waking.
        if (header_->id_msg != id_msg_ ||
            header_->buffer_size != buffer_size_) {
            throw std::runtime_error("Shared-memory configuration changed");
        }
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

    /**
     * @brief Expected message type ID.
     */
    uint64_t id_msg_{0};

    /**
     * @brief Expected number of buffer slots.
     */
    std::size_t buffer_size_{0};
};
