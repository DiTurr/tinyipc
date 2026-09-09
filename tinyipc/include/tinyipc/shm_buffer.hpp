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
 * Contains metadata required to synchronize and inspect the buffer.
 */
struct BufferHeader {
    /**
     * @brief Sequence number of the latest published message.
     *
     * Incremented by the publisher after writing new data.
     * Used by the subscriber to detect new data.
     */
    std::atomic<uint32_t> sequence{0};

    /**
     * @brief Number of data slots in the buffer.
     */
    std::size_t buffer_size{0};
};


/**
 * @brief Shared-memory buffer.
 *
 * This structure is shared between the publisher and subscriber processes.
 *
 * Memory layout:
 *
 *   +-------------------------+
 *   | BufferHeader            |
 *   |  - sequence             |
 *   |  - buffer_size          |
 *   +-------------------------+
 *   | slot[0]                 |
 *   | slot[1]                 |
 *   | ...                     |
 *   | slot[BufferSize - 1]    |
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
     */
    std::array<T, BufferSize> slot{};
};
