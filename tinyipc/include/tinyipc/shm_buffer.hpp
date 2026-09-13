#pragma once

// std::array
#include <array>
// std::atomic
#include <atomic>
// std::size_t
#include <cstddef>
// uint32_t
#include <cstdint>


/**
 * @brief Header of a shared-memory buffer.
 *
 * Contains metadata and synchronization information shared between
 * the publisher and all subscribers.
 */
struct BufferHeader {
    /**
     * @brief Sequence number of the next message to be published.
     *
     * The publisher increments this value after writing a message.
     * Subscribers use it to detect newly published messages.
     *
     * The sequence number is also used to determine which slot contains
     * the latest message:
     *
     *   slot_index = sequence % buffer_size
     *
     * A monotonically increasing sequence number allows subscribers to
     * detect if they have fallen behind and a slot has been overwritten.
     */
    std::atomic<uint32_t> sequence{0};

    /**
     * @brief Number of slots in the shared-memory buffer.
     *
     * This value is initialized by the publisher when the shared-memory
     * buffer is created and is read by subscribers.
     */
    std::size_t buffer_size{0};
};


/**
 * @brief Data slot in the shared-memory buffer.
 *
 * Each slot contains its own sequence number in addition to the timestamp
 * and data. The sequence number identifies which publication the data
 * belongs to.
 *
 * The publisher invalidates the slot before modifying its contents and
 * commits the new sequence number after the timestamp and data have been
 * completely written.
 *
 * This allows a subscriber to verify that a slot has not been overwritten
 * while it is being read.
 *
 * @tparam T Data type stored in the slot.
 */
template <typename T>
struct Slot {
    /**
     * @brief Sequence number associated with the data in this slot.
     *
     * The publisher changes this value before writing to invalidate the
     * current contents and updates it to the new publication sequence
     * after the timestamp and data have been completely written.
     *
     * A subscriber compares the sequence before and after reading the
     * slot to detect whether the slot was modified while it was being read.
     */
    std::atomic<uint32_t> sequence{0};

    /**
     * @brief Timestamp when the data was published.
     *
     * Stored in microseconds.
     */
    uint64_t timestamp_us{0};

    /**
     * @brief Data stored in the slot.
     */
    T data{};
};


/**
 * @brief Shared-memory buffer.
 *
 * This structure is shared between the publisher and subscriber processes.
 *
 * The buffer uses a fixed-size ring-buffer layout. When the publisher
 * reaches the end of the buffer, it reuses the oldest slot.
 *
 * The global sequence number in the header is used to detect new messages,
 * while each slot sequence number is used to detect slot overwrites.
 *
 * Memory layout:
 *
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
 *
 * @tparam T Element type stored in each slot.
 * @tparam BufferSize Number of slots in the buffer.
 */
template <typename T, std::size_t BufferSize>
struct Buffer {
    /**
     * @brief Buffer metadata and synchronization information.
     */
    BufferHeader header{0, BufferSize};

    /**
     * @brief Data slots stored in shared memory.
     *
     * Slots are reused cyclically as the publisher produces messages.
     */
    std::array<Slot<T>, BufferSize> slot{};
};
