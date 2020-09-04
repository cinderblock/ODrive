/*
see protocol.md for the protocol specification
*/

#ifndef __PROTOCOL_HPP
#define __PROTOCOL_HPP

// TODO: resolve assert
#define assert(expr)

#include <functional>
#include <limits>
#include <math.h>
//#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <cstring>
#include "crc.hpp"
#include "cpp_utils.hpp"
#include "bufptr.hpp"
#include "simple_serdes.hpp"

// Note that this option cannot be used to debug UART because it prints on UART
//#define DEBUG_FIBRE
#ifdef DEBUG_FIBRE
#define LOG_FIBRE(...)  do { printf(__VA_ARGS__); } while (0)
#else
#define LOG_FIBRE(...)  ((void) 0)
#endif


// Default CRC-8 Polynomial: x^8 + x^5 + x^4 + x^2 + x + 1
// Can protect a 4 byte payload against toggling of up to 5 bits
//  source: https://users.ece.cmu.edu/~koopman/crc/index.html
constexpr uint8_t CANONICAL_CRC8_POLYNOMIAL = 0x37;
constexpr uint8_t CANONICAL_CRC8_INIT = 0x42;

constexpr size_t CRC8_BLOCKSIZE = 4;

// Default CRC-16 Polynomial: 0x9eb2 x^16 + x^13 + x^12 + x^11 + x^10 + x^8 + x^6 + x^5 + x^2 + 1
// Can protect a 135 byte payload against toggling of up to 5 bits
//  source: https://users.ece.cmu.edu/~koopman/crc/index.html
// Also known as CRC-16-DNP
constexpr uint16_t CANONICAL_CRC16_POLYNOMIAL = 0x3d65;
constexpr uint16_t CANONICAL_CRC16_INIT = 0x1337;

constexpr uint8_t CANONICAL_PREFIX = 0xAA;





/* move to fibre_config.h ******************************/

typedef size_t endpoint_id_t;

struct ReceiverState {
    endpoint_id_t endpoint_id;
    size_t length;
    uint16_t seqno_thread;
    uint16_t seqno;
    bool expect_ack;
    bool expect_response;
    bool enforce_ordering;
};

/*******************************************************/


constexpr uint16_t PROTOCOL_VERSION = 1;

// This value must not be larger than USB_TX_DATA_SIZE defined in usbd_cdc_if.h
constexpr uint16_t TX_BUF_SIZE = 32; // does not work with 64 for some reason
constexpr uint16_t RX_BUF_SIZE = 128; // larger values than 128 have currently no effect because of protocol limitations

// Maximum time we allocate for processing and responding to a request
constexpr uint32_t PROTOCOL_SERVER_TIMEOUT_MS = 10;


typedef struct {
    uint16_t json_crc = 0;
    uint16_t endpoint_id = 0;
} endpoint_ref_t;


namespace fibre {
// These symbols are defined in the autogenerated endpoints.hpp
extern const unsigned char embedded_json[];
extern const size_t embedded_json_length;
extern const uint16_t json_crc_;
extern const uint32_t json_version_id_;
bool endpoint_handler(int idx, cbufptr_t* input_buffer, bufptr_t* output_buffer);
bool endpoint0_handler(cbufptr_t* input_buffer, bufptr_t* output_buffer);
bool is_endpoint_ref_valid(endpoint_ref_t endpoint_ref);
bool set_endpoint_from_float(endpoint_ref_t endpoint_ref, float value);
}


template<typename T, typename = typename std::enable_if_t<!std::is_const<T>::value>>
inline size_t write_le(T value, uint8_t* buffer){
    //TODO: add static_assert that this is still a little endian machine
    std::memcpy(&buffer[0], &value, sizeof(value));
    return sizeof(value);
}

template<typename T>
typename std::enable_if_t<std::is_const<T>::value, size_t>
write_le(T value, uint8_t* buffer) {
    return write_le<std::remove_const_t<T>>(value, buffer);
}

template<>
inline size_t write_le<float>(float value, uint8_t* buffer) {
    static_assert(CHAR_BIT * sizeof(float) == 32, "32 bit floating point expected");
    static_assert(std::numeric_limits<float>::is_iec559, "IEEE 754 floating point expected");
    uint32_t value_as_uint32;
    std::memcpy(&value_as_uint32, &value, sizeof(uint32_t));
    return write_le<uint32_t>(value_as_uint32, buffer);
}

template<typename T>
inline size_t read_le(T* value, const uint8_t* buffer){
    // TODO: add static_assert that this is still a little endian machine
    std::memcpy(value, buffer, sizeof(*value));
    return sizeof(*value);
}

template<>
inline size_t read_le<float>(float* value, const uint8_t* buffer) {
    static_assert(CHAR_BIT * sizeof(float) == 32, "32 bit floating point expected");
    static_assert(std::numeric_limits<float>::is_iec559, "IEEE 754 floating point expected");
    return read_le(reinterpret_cast<uint32_t*>(value), buffer);
}

// @brief Reads a value of type T from the buffer.
// @param buffer    Pointer to the buffer to be read. The pointer is updated by the number of bytes that were read.
// @param length    The number of available bytes in buffer. This value is updated to subtract the bytes that were read.
template<typename T>
static inline T read_le(const uint8_t** buffer, size_t* length) {
    T result;
    size_t cnt = read_le(&result, *buffer);
    *buffer += cnt;
    *length -= cnt;
    return result;
}

class PacketSink {
public:
    // @brief Get the maximum packet length (aka maximum transmission unit)
    // A packet size shall take no action and return an error code if the
    // caller attempts to send an oversized packet.
    //virtual size_t get_mtu() = 0;

    // @brief Processes a packet.
    // The blocking behavior shall depend on the thread-local deadline_ms variable.
    // @return: 0 on success, otherwise a non-zero error code
    // TODO: define what happens when the packet is larger than what the implementation can handle.
    virtual int process_packet(const uint8_t* buffer, size_t length) = 0;
};

class StreamSink {
public:
    // @brief Processes a chunk of bytes that is part of a continuous stream.
    // The blocking behavior shall depend on the thread-local deadline_ms variable.
    // @param processed_bytes: if not NULL, shall be incremented by the number of
    //        bytes that were consumed.
    // @return: 0 on success, otherwise a non-zero error code
    virtual int process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) = 0;

    // @brief Returns the number of bytes that can still be written to the stream.
    // Shall return SIZE_MAX if the stream has unlimited lenght.
    // TODO: deprecate
    virtual size_t get_free_space() = 0;

    /*int process_bytes(const uint8_t* buffer, size_t length) {
        size_t processed_bytes = 0;
        return process_bytes(buffer, length, &processed_bytes);
    }*/
};

class StreamSource {
public:
    // @brief Generate a chunk of bytes that are part of a continuous stream.
    // The blocking behavior shall depend on the thread-local deadline_ms variable.
    // @param generated_bytes: if not NULL, shall be incremented by the number of
    //        bytes that were written to buffer.
    // @return: 0 on success, otherwise a non-zero error code
    virtual int get_bytes(uint8_t* buffer, size_t length, size_t* generated_bytes) = 0;

    // @brief Returns the number of bytes that can still be written to the stream.
    // Shall return SIZE_MAX if the stream has unlimited lenght.
    // TODO: deprecate
    //virtual size_t get_free_space() = 0;
};

class StreamToPacketSegmenter : public StreamSink {
public:
    explicit StreamToPacketSegmenter(PacketSink& output) :
        output_(output)
    {
    };

    int process_bytes(const uint8_t *buffer, size_t length, size_t* processed_bytes) override;
    
    size_t get_free_space() { return SIZE_MAX; }

private:
    uint8_t header_buffer_[3] = {0};
    size_t header_index_ = 0;
    uint8_t packet_buffer_[RX_BUF_SIZE] = {0};
    size_t packet_index_ = 0;
    size_t packet_length_ = 0;
    PacketSink& output_;
};


class StreamBasedPacketSink : public PacketSink {
public:
    explicit StreamBasedPacketSink(StreamSink& output) :
        output_(output)
    {
    };
    
    //size_t get_mtu() { return SIZE_MAX; }
    int process_packet(const uint8_t *buffer, size_t length) override;

private:
    StreamSink& output_;
};

// @brief: Represents a stream sink that's based on an underlying packet sink.
// A single call to process_bytes may result in multiple packets being sent.
class PacketBasedStreamSink : public StreamSink {
public:
    explicit PacketBasedStreamSink(PacketSink& packet_sink) : _packet_sink(packet_sink) {}
    ~PacketBasedStreamSink() {}

    int process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) override {
        // Loop to ensure all bytes get sent
        while (length) {
            size_t chunk = length;
            // send chunk as packet
            if (_packet_sink.process_packet(buffer, chunk))
                return -1;
            buffer += chunk;
            length -= chunk;
            if (processed_bytes)
                *processed_bytes += chunk;
        }
        return 0;
    }

    size_t get_free_space() { return SIZE_MAX; }

private:
    PacketSink& _packet_sink;
};

// Implements the StreamSink interface by writing into a fixed size
// memory buffer.
class MemoryStreamSink : public StreamSink {
public:
    MemoryStreamSink(uint8_t *buffer, size_t length) :
        buffer_(buffer),
        buffer_length_(length) {}

    // Returns 0 on success and -1 if the buffer could not accept everything because it became full
    int process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) override {
        size_t chunk = length < buffer_length_ ? length : buffer_length_;
        memcpy(buffer_, buffer, chunk);
        buffer_ += chunk;
        buffer_length_ -= chunk;
        if (processed_bytes)
            *processed_bytes += chunk;
        return chunk == length ? 0 : -1;
    }

    size_t get_free_space() { return buffer_length_; }

private:
    uint8_t * buffer_;
    size_t buffer_length_;
};

// Implements the StreamSink interface by discarding the first couple of bytes
// and then forwarding the rest to another stream.
class NullStreamSink : public StreamSink {
public:
    NullStreamSink(size_t skip, StreamSink& follow_up_stream) :
        skip_(skip),
        follow_up_stream_(follow_up_stream) {}

    // Returns 0 on success and -1 if the buffer could not accept everything because it became full
    int process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) override {
        if (skip_ < length) {
            buffer += skip_;
            length -= skip_;
            if (processed_bytes)
                *processed_bytes += skip_;
            skip_ = 0;
            return follow_up_stream_.process_bytes(buffer, length, processed_bytes);
        } else {
            skip_ -= length;
            if (processed_bytes)
                *processed_bytes += length;
            return 0;
        }
    }

    size_t get_free_space() override { return skip_ + follow_up_stream_.get_free_space(); }

private:
    size_t skip_;
    StreamSink& follow_up_stream_;
};



// Implements the StreamSink interface by calculating the CRC16 checksum
// on the data that is sent to it.
class CRC16Calculator : public StreamSink {
public:
    explicit CRC16Calculator(uint16_t crc16_init) :
        crc16_(crc16_init) {}

    int process_bytes(const uint8_t* buffer, size_t length, size_t* processed_bytes) override{
        crc16_ = calc_crc16<CANONICAL_CRC16_POLYNOMIAL>(crc16_, buffer, length);
        if (processed_bytes)
            *processed_bytes += length;
        return 0;
    }

    size_t get_free_space() override { return SIZE_MAX; }

    uint16_t get_crc16() { return crc16_; }
private:
    uint16_t crc16_;
};


namespace fibre {
template<typename T, typename = void>
struct Codec {
    static std::optional<T> decode(cbufptr_t* buffer) { return std::nullopt; }
};

template<> struct Codec<bool> {
    static std::optional<bool> decode(cbufptr_t* buffer) { return (buffer->begin() == buffer->end()) ? std::nullopt : std::make_optional((bool)*(buffer->begin()++)); }
    static bool encode(bool value, bufptr_t* buffer) { return SimpleSerializer<uint8_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<int8_t> {
    static std::optional<int8_t> decode(cbufptr_t* buffer) { return SimpleSerializer<int8_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(int8_t value, bufptr_t* buffer) { return SimpleSerializer<int8_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<uint8_t> {
    static std::optional<uint8_t> decode(cbufptr_t* buffer) { return SimpleSerializer<uint8_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(uint8_t value, bufptr_t* buffer) { return SimpleSerializer<uint8_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<int16_t> {
    static std::optional<int16_t> decode(cbufptr_t* buffer) { return SimpleSerializer<int16_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(int16_t value, bufptr_t* buffer) { return SimpleSerializer<int16_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<uint16_t> {
    static std::optional<uint16_t> decode(cbufptr_t* buffer) { return SimpleSerializer<uint16_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(uint16_t value, bufptr_t* buffer) { return SimpleSerializer<uint16_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<int32_t> {
    static std::optional<int32_t> decode(cbufptr_t* buffer) { return SimpleSerializer<int32_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(int32_t value, bufptr_t* buffer) { return SimpleSerializer<int32_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<uint32_t> {
    static std::optional<uint32_t> decode(cbufptr_t* buffer) { return SimpleSerializer<uint32_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(uint32_t value, bufptr_t* buffer) { return SimpleSerializer<uint32_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<int64_t> {
    static std::optional<int64_t> decode(cbufptr_t* buffer) { return SimpleSerializer<int64_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(int64_t value, bufptr_t* buffer) { return SimpleSerializer<int64_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<uint64_t> {
    static std::optional<uint64_t> decode(cbufptr_t* buffer) { return SimpleSerializer<uint64_t, false>::read(&(buffer->begin()), buffer->end()); }
    static bool encode(uint64_t value, bufptr_t* buffer) { return SimpleSerializer<uint64_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<float> {
    static std::optional<float> decode(cbufptr_t* buffer) {
        std::optional<uint32_t> int_val = Codec<uint32_t>::decode(buffer);
        return int_val.has_value() ? std::optional<float>(*reinterpret_cast<float*>(&int_val.value())) : std::nullopt;
    }
    static bool encode(float value, bufptr_t* buffer) {
        void* ptr = &value;
        return Codec<uint32_t>::encode(*reinterpret_cast<uint32_t*>(ptr), buffer);
    }
};
template<typename T>
struct Codec<T, std::enable_if_t<std::is_enum<T>::value>> {
    static std::optional<T> decode(cbufptr_t* buffer) {
        std::optional<int32_t> int_val = SimpleSerializer<int32_t, false>::read(&(buffer->begin()), buffer->end());
        return int_val.has_value() ? std::make_optional(static_cast<T>(int_val.value())) : std::nullopt;
    }
    static bool encode(T value, bufptr_t* buffer) { return SimpleSerializer<int32_t, false>::write(value, &(buffer->begin()), buffer->end()); }
};
template<> struct Codec<endpoint_ref_t> {
    static std::optional<endpoint_ref_t> decode(cbufptr_t* buffer) {
        std::optional<uint16_t> val0 = SimpleSerializer<uint16_t, false>::read(&(buffer->begin()), buffer->end());
        std::optional<uint16_t> val1 = SimpleSerializer<uint16_t, false>::read(&(buffer->begin()), buffer->end());
        return (val0.has_value() && val1.has_value()) ? std::make_optional(endpoint_ref_t{val1.value(), val0.value()}) : std::nullopt;
    }
    static bool encode(endpoint_ref_t value, bufptr_t* buffer) {
        return SimpleSerializer<uint16_t, false>::write(value.endpoint_id, &(buffer->begin()), buffer->end())
            && SimpleSerializer<uint16_t, false>::write(value.json_crc, &(buffer->begin()), buffer->end());
    }
};
}


/* @brief Handles the communication protocol on one channel.
*
* When instantiated with a list of endpoints and an output packet sink,
* objects of this class will handle packets passed into process_packet,
* pass the relevant data to the corresponding endpoints and dispatch response
* packets on the output.
*/
class BidirectionalPacketBasedChannel : public PacketSink {
public:
    explicit BidirectionalPacketBasedChannel(PacketSink& output) :
        output_(output)
    { }

    //size_t get_mtu() {
    //    return SIZE_MAX;
    //}
    int process_packet(const uint8_t* buffer, size_t length) override;
private:
    PacketSink& output_;
    uint8_t tx_buf_[TX_BUF_SIZE] = {0};
};


/* ToString / FromString functions -------------------------------------------*/
/*
* These functions are currently not used by Fibre and only here to
* support the ODrive ASCII protocol.
* TODO: find a general way for client code to augment endpoints with custom
* functions
*/

template<typename T>
struct format_traits_t;

// template<> struct format_traits_t<float> { using type = void;
//     static constexpr const char * fmt = "%f";
//     static constexpr const char * fmtp = "%f";
// };
template<> struct format_traits_t<int64_t> { using type = void;
    static constexpr const char * fmt = "%lld";
    static constexpr const char * fmtp = "%lld";
};
template<> struct format_traits_t<uint64_t> { using type = void;
    static constexpr const char * fmt = "%llu";
    static constexpr const char * fmtp = "%llu";
};
template<> struct format_traits_t<int32_t> { using type = void;
    static constexpr const char * fmt = "%ld";
    static constexpr const char * fmtp = "%ld";
};
template<> struct format_traits_t<uint32_t> { using type = void;
    static constexpr const char * fmt = "%lu";
    static constexpr const char * fmtp = "%lu";
};
// TODO: change all overloads to fundamental int type space
template<> struct format_traits_t<unsigned int> { using type = void;
    static constexpr const char * fmt = "%ud";
    static constexpr const char * fmtp = "%ud";
};
template<> struct format_traits_t<int16_t> { using type = void;
    static constexpr const char * fmt = "%hd";
    static constexpr const char * fmtp = "%hd";
};
template<> struct format_traits_t<uint16_t> { using type = void;
    static constexpr const char * fmt = "%hu";
    static constexpr const char * fmtp = "%hu";
};
template<> struct format_traits_t<int8_t> { using type = void;
    static constexpr const char * fmt = "%hhd";
    static constexpr const char * fmtp = "%d";
};
template<> struct format_traits_t<uint8_t> { using type = void;
    static constexpr const char * fmt = "%hhu";
    static constexpr const char * fmtp = "%u";
};

template<typename T, typename = typename format_traits_t<T>::type>
static bool to_string(const T& value, char * buffer, size_t length, int) {
    snprintf(buffer, length, format_traits_t<T>::fmtp, value);
    return true;
}
// Special case for float because printf promotes float to double, and we get warnings
template<typename T = float>
static bool to_string(const float& value, char * buffer, size_t length, int) {
    snprintf(buffer, length, "%f", (double)value);
    return true;
}
template<typename T = bool>
static bool to_string(const bool& value, char * buffer, size_t length, int) {
    buffer[0] = value ? '1' : '0';
    buffer[1] = 0;
    return true;
}
template<typename T>
static bool to_string(const T& value, char * buffer, size_t length, ...) {
    return false;
}

template<typename T, typename = typename format_traits_t<T>::type>
static bool from_string(const char * buffer, size_t length, T* property, int) {
    // Note for T == uint8_t: Even though we supposedly use the correct format
    // string sscanf treats our pointer as pointer-to-int instead of
    // pointer-to-uint8_t. To avoid an unexpected memory access we first read
    // into a union.
    union { T t; int i; } val;
    if (sscanf(buffer, format_traits_t<T>::fmt, &val.t) == 1) {
        *property = val.t;
        return true;
    } else {
        return false;
    }
}
// Special case for float because printf promotes float to double, and we get warnings
template<typename T = float>
static bool from_string(const char * buffer, size_t length, float* property, int) {
    return sscanf(buffer, "%f", property) == 1;
}
template<typename T = bool>
static bool from_string(const char * buffer, size_t length, bool* property, int) {
    int val;
    if (sscanf(buffer, "%d", &val) != 1)
        return false;
    *property = val;
    return true;
}
template<typename T>
static bool from_string(const char * buffer, size_t length, T* property, ...) {
    return false;
}


//template<typename T, typename = typename std>
//bool set_from_float_ex(float value, T* property) {
//    return false;
//}

namespace conversion {
//template<typename T>
template<typename T>
bool set_from_float_ex(float value, float* property, int) {
    return *property = value, true;
}
template<typename T>
bool set_from_float_ex(float value, bool* property, int) {
    return *property = (value >= 0.0f), true;
}
template<typename T, typename = std::enable_if_t<std::is_integral<T>::value && !std::is_const<T>::value>>
bool set_from_float_ex(float value, T* property, int) {
    return *property = static_cast<T>(std::round(value)), true;
}
template<typename T>
bool set_from_float_ex(float value, T* property, ...) {
    return false;
}
template<typename T>
bool set_from_float(float value, T* property) {
    return set_from_float_ex<T>(value, property, 0);
}
}


template<typename T>
struct Property {
    Property(void* ctx, T(*getter)(void*), void(*setter)(void*, T))
        : ctx_(ctx), getter_(getter), setter_(setter) {}
    Property(T* ctx)
        : ctx_(ctx), getter_([](void* ctx){ return *(T*)ctx; }), setter_([](void* ctx, T val){ *(T*)ctx = val; }) {}
    Property& operator*() { return *this; }
    Property* operator->() { return this; }

    T read() const {
        return (*getter_)(ctx_);
    }

    T exchange(std::optional<T> value) const {
        T old_value = (*getter_)(ctx_);
        if (value.has_value()) {
            (*setter_)(ctx_, value.value());
        }
        return old_value;
    }
    
    void* ctx_;
    T(*getter_)(void*);
    void(*setter_)(void*, T);
};

template<typename T>
struct Property<const T> {
    Property(void* ctx, T(*getter)(void*))
        : ctx_(ctx), getter_(getter) {}
    Property(const T* ctx)
        : ctx_(const_cast<T*>(ctx)), getter_([](void* ctx){ return *(const T*)ctx; }) {}
    Property& operator*() { return *this; }
    Property* operator->() { return this; }

    T read() const {
        return (*getter_)(ctx_);
    }

    void* ctx_;
    T(*getter_)(void*);
};


#endif
