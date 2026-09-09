#pragma once

// int8_t, int16_t, int32_t, int64_t
// uint8_t, uint16_t, uint32_t, uint64_t
#include <cstdint>


/**
 * @brief Base type for all IPC messages.
 *
 * Contains metadata that is common to all message types.
 */
struct BaseType {
    /**
     * @brief Timestamp of published message in microseconds.
     */
    uint64_t timestamp_us{0};
};


/**
 * @brief 8-bit signed integer IPC message.
 */
struct Int8Type : public BaseType {
    int8_t data{0};
};


/**
 * @brief 16-bit signed integer IPC message.
 */
struct Int16Type : public BaseType {
    int16_t data{0};
};


/**
 * @brief 32-bit signed integer IPC message.
 */
struct Int32Type : public BaseType {
    int32_t data{0};
};


/**
 * @brief 64-bit signed integer IPC message.
 */
struct Int64Type : public BaseType {
    int64_t data{0};
};


/**
 * @brief 8-bit unsigned integer IPC message.
 */
struct UInt8Type : public BaseType {
    uint8_t data{0};
};


/**
 * @brief 16-bit unsigned integer IPC message.
 */
struct UInt16Type : public BaseType {
    uint16_t data{0};
};


/**
 * @brief 32-bit unsigned integer IPC message.
 */
struct UInt32Type : public BaseType {
    uint32_t data{0};
};


/**
 * @brief 64-bit unsigned integer IPC message.
 */
struct UInt64Type : public BaseType {
    uint64_t data{0};
};


/**
 * @brief 32-bit floating-point IPC message.
 */
struct Float32Type : public BaseType {
    float data{0.0f};
};


/**
 * @brief 64-bit floating-point IPC message.
 */
struct Float64Type : public BaseType {
    double data{0.0};
};


/**
 * @brief Boolean IPC message.
 */
struct BoolType : public BaseType {
    bool data{false};
};
