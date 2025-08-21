#include <fastrtps/participant/Participant.h>
#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/publisher/Publisher.h>
#include <fastrtps/attributes/PublisherAttributes.h>
#include <fastrtps/Domain.h>
#include <fastrtps/TopicDataType.h>
#include <fastcdr/Cdr.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <string>

using namespace eprosima::fastrtps;
using namespace eprosima::fastrtps::rtps;

// 与B机器完全相同的消息结构
struct HelloWorldMsg
{
    uint32_t index;
    std::string message;

    HelloWorldMsg() : index(0) {}
};

// 与B机器完全相同的类型支持类
class HelloWorldMsgPubSubType : public TopicDataType
{
public:
    HelloWorldMsgPubSubType()
    {
        setName("HelloWorld");
        m_typeSize = 4 + 256; // uint32_t + max string
        m_isGetKeyDefined = false;
    }

    bool serialize(void* data, SerializedPayload_t* payload) override
    {
        HelloWorldMsg* msg = static_cast<HelloWorldMsg*>(data);
        
        // 简单的序列化
        eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(payload->data), payload->max_size);
        eprosima::fastcdr::Cdr ser(fastbuffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::Cdr::DDS_CDR);
        
        ser << msg->index;
        ser << msg->message;
        
        payload->length = static_cast<uint32_t>(ser.getSerializedDataLength());
        return true;
    }

    bool deserialize(SerializedPayload_t* payload, void* data) override
    {
        HelloWorldMsg* msg = static_cast<HelloWorldMsg*>(data);
        
        eprosima::fastcdr::FastBuffer fastbuffer(reinterpret_cast<char*>(payload->data), payload->length);
        eprosima::fastcdr::Cdr deser(fastbuffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::Cdr::DDS_CDR);
        
        deser >> msg->index;
        deser >> msg->message;
        
        return true;
    }

    std::function<uint32_t()> getSerializedSizeProvider(void* data) override
    {
        return [data]() -> uint32_t
        {
            HelloWorldMsg* msg = static_cast<HelloWorldMsg*>(data);
            return 4 + 4 + static_cast<uint32_t>(msg->message.length());
        };
    }

    void* createData() override
    {
        return new HelloWorldMsg();
    }

    void deleteData(void* data) override
    {
        delete static_cast<HelloWorldMsg*>(data);
    }

    bool getKey(void*, InstanceHandle_t*, bool) override
    {
        return false;
    }
};

class HelloWorldPublisher
{
private:
    Participant* mp_participant;
    Publisher* mp_publisher;
    HelloWorldMsgPubSubType m_type;

public:
    HelloWorldPublisher() : mp_participant(nullptr), mp_publisher(nullptr) {}

    ~HelloWorldPublisher()
    {
        if (mp_publisher != nullptr)
        {
            Domain::removePublisher(mp_publisher);
        }
        if (mp_participant != nullptr)
        {
            Domain::removeParticipant(mp_participant);
        }
    }

    bool init()
    {
        std::cout << "=== FastRTPS 2.6.10 HelloWorld 发布者 ===" << std::endl;
        std::cout << "初始化发布者..." << std::endl;

        ParticipantAttributes PParam;
        PParam.rtps.builtin.discovery_config.discoveryProtocol = DiscoveryProtocol_t::SIMPLE;
        PParam.rtps.builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = true;
        PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationReaderANDSubscriptionWriter = true;
        PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationWriterANDSubscriptionReader = true;
        PParam.rtps.setName("HelloWorld_Publisher_A117");

        mp_participant = Domain::createParticipant(PParam);
        if (mp_participant == nullptr)
        {
            std::cout << "[ERROR] 创建参与者失败!" << std::endl;
            return false;
        }

        Domain::registerType(mp_participant, &m_type);

        PublisherAttributes Wparam;
        Wparam.topic.topicKind = NO_KEY;
        Wparam.topic.topicDataType = "HelloWorld";
        Wparam.topic.topicName = "HelloWorldTopic";
        Wparam.topic.historyQos.kind = KEEP_LAST_HISTORY_QOS;
        Wparam.topic.historyQos.depth = 30;
        Wparam.topic.resourceLimitsQos.max_samples = 50;
        Wparam.topic.resourceLimitsQos.allocated_samples = 20;
        Wparam.times.heartbeatPeriod.seconds = 2;
        Wparam.times.heartbeatPeriod.nanosec = 200 * 1000 * 1000;
        Wparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;

        mp_publisher = Domain::createPublisher(mp_participant, Wparam, nullptr);
        if (mp_publisher == nullptr)
        {
            std::cout << "[ERROR] 创建发布者失败!" << std::endl;
            return false;
        }

        std::cout << "[SUCCESS] FastRTPS发布者初始化完成!" << std::endl;
        return true;
    }

    void runPublisher()
    {
        int count = 0;
        std::cout << "[INFO] 开始发布消息..." << std::endl;
        
        while (true)
        {
            HelloWorldMsg msg;
            msg.index = count;
            msg.message = "Hello from A机器(192.168.9.117) FastRTPS Publisher! Count: " + std::to_string(count);

            if (mp_publisher->write(&msg))
            {
                std::cout << "[" << count << "] 发送成功 - Index: " << msg.index 
                         << ", Message: " << msg.message << std::endl;
            }
            else
            {
                std::cout << "[" << count << "] 发送失败!" << std::endl;
            }

            ++count;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};

int main()
{
    std::cout << "A机器 FastRTPS Publisher - 与B机器兼容版本" << std::endl;
    std::cout << "本机IP: 192.168.9.117" << std::endl;
    std::cout << "目标: 与FastDDS Subscriber和B机器Publisher兼容" << std::endl;

    HelloWorldPublisher pub;
    if (pub.init())
    {
        pub.runPublisher();
    }

    return 0;
}

