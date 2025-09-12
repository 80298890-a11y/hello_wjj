#ifndef HELLOWORLD_HPP
#define HELLOWORLD_HPP

#include <string>
#include <fastrtps/TopicDataType.h>
#include <fastcdr/Cdr.h>

class HelloWorld
{
public:
    HelloWorld() = default;
    ~HelloWorld() = default;
    HelloWorld(const HelloWorld& x) = default;
    HelloWorld(HelloWorld&& x) = default;
    HelloWorld& operator=(const HelloWorld& x) = default;
    HelloWorld& operator=(HelloWorld&& x) = default;

    void index(uint32_t _index) { m_index = _index; }
    uint32_t index() const { return m_index; }
    uint32_t& index() { return m_index; }

    void message(const std::string& _message) { m_message = _message; }
    void message(std::string&& _message) { m_message = std::move(_message); }
    const std::string& message() const { return m_message; }
    std::string& message() { return m_message; }

private:
    uint32_t m_index{0};
    std::string m_message;
};

class HelloWorldPubSubType : public eprosima::fastrtps::TopicDataType
{
public:
    typedef HelloWorld type;

    HelloWorldPubSubType()
    {
        setName("HelloWorld");
        auto type_size = 4 + 4 + 255;  // index + string_length + max_string_size
        m_typeSize = static_cast<uint32_t>(type_size) + 4;  // extra padding
        m_isGetKeyDefined = false;
    }

    ~HelloWorldPubSubType() override = default;

    bool serialize(void* data, eprosima::fastrtps::rtps::SerializedPayload_t* payload) override
    {
        HelloWorld* p_type = static_cast<HelloWorld*>(data);
        eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(payload->data), payload->max_size);
        eprosima::fastcdr::Cdr ser(fastbuffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::Cdr::DDS_CDR);
        payload->encapsulation = ser.endianness() == eprosima::fastcdr::Cdr::BIG_ENDIANNESS ? CDR_BE : CDR_LE;
        ser.serialize_encapsulation();

        try {
            ser << p_type->index();
            ser << p_type->message();
        } catch (eprosima::fastcdr::exception::Exception& /*exception*/) {
            return false;
        }

        payload->length = static_cast<uint32_t>(ser.getSerializedDataLength());
        return true;
    }

    bool deserialize(eprosima::fastrtps::rtps::SerializedPayload_t* payload, void* data) override
    {
        try {
            HelloWorld* p_type = static_cast<HelloWorld*>(data);
            eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(payload->data), payload->length);
            eprosima::fastcdr::Cdr deser(fastbuffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::Cdr::DDS_CDR);
            deser.read_encapsulation();
            payload->encapsulation = deser.endianness() == eprosima::fastcdr::Cdr::BIG_ENDIANNESS ? CDR_BE : CDR_LE;

            deser >> p_type->index();
            deser >> p_type->message();
        } catch (eprosima::fastcdr::exception::Exception& /*exception*/) {
            return false;
        }

        return true;
    }

    std::function<uint32_t()> getSerializedSizeProvider(void* data) override
    {
        return [data]() -> uint32_t {
            HelloWorld* p_type = static_cast<HelloWorld*>(data);
            uint32_t size = 0;
            size += 4; // index
            size += 4 + static_cast<uint32_t>(p_type->message().size()); // string
            return size;
        };
    }

    void* createData() override
    {
        return reinterpret_cast<void*>(new HelloWorld());
    }

    void deleteData(void* data) override
    {
        delete(reinterpret_cast<HelloWorld*>(data));
    }

    bool getKey(void* /*data*/, eprosima::fastrtps::rtps::InstanceHandle_t* /*handle*/, bool /*force_md5*/) override
    {
        return false;
    }
};

#endif // HELLOWORLD_HPP
