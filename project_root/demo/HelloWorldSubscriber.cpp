#include "HelloWorld.hpp"

#include <fastrtps/participant/Participant.h>
#include <fastrtps/attributes/ParticipantAttributes.h>
#include <fastrtps/subscriber/Subscriber.h>
#include <fastrtps/attributes/SubscriberAttributes.h>
#include <fastrtps/subscriber/SubscriberListener.h>
#include <fastrtps/subscriber/SampleInfo.h>
#include <fastrtps/Domain.h>

#include <chrono>
#include <thread>
#include <iostream>

using namespace eprosima::fastrtps;
using namespace eprosima::fastrtps::rtps;

class HelloWorldSubscriber
{
private:
    Participant* mp_participant;
    Subscriber* mp_subscriber;
    HelloWorldPubSubType m_type;

    class SubListener : public SubscriberListener
    {
    public:
        SubListener() : n_matched(0), n_samples(0) {}
        ~SubListener() override {}

        void onSubscriptionMatched(Subscriber* sub, MatchingInfo& info) override
        {
            if (info.status == MATCHED_MATCHING)
            {
                n_matched++;
                std::cout << "[SUCCESS] 订阅者匹配到发布者! 总连接数: " << n_matched << std::endl;
            }
            else
            {
                n_matched--;
                std::cout << "[INFO] 发布者断开连接! 剩余连接数: " << n_matched << std::endl;
            }
        }

        void onNewDataMessage(Subscriber* sub) override
        {
            HelloWorld msg;
            if (sub->takeNextData(&msg, &m_info))
            {
                if (m_info.sampleKind == ALIVE)
                {
                    n_samples++;
                    std::cout << "[" << n_samples << "] 接收到消息 - Index: " << msg.index() 
                             << ", Message: " << msg.message() << std::endl;
                }
            }
        }

        SampleInfo_t m_info;
        int n_matched;
        int n_samples;
    } m_listener;

public:
    HelloWorldSubscriber() : mp_participant(nullptr), mp_subscriber(nullptr) {}

    virtual ~HelloWorldSubscriber()
    {
        if (mp_subscriber != nullptr)
        {
            Domain::removeSubscriber(mp_subscriber);
        }
        if (mp_participant != nullptr)
        {
            Domain::removeParticipant(mp_participant);
        }
    }

    bool init()
    {
        std::cout << "=== FastRTPS 2.6.10 HelloWorld 订阅者 ===" << std::endl;
        std::cout << "开始启动订阅者..." << std::endl;

        ParticipantAttributes PParam;
        PParam.rtps.builtin.discovery_config.discoveryProtocol = DiscoveryProtocol_t::SIMPLE;
        PParam.rtps.builtin.discovery_config.use_SIMPLE_EndpointDiscoveryProtocol = true;
        PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationReaderANDSubscriptionWriter = true;
        PParam.rtps.builtin.discovery_config.m_simpleEDP.use_PublicationWriterANDSubscriptionReader = true;
        PParam.rtps.setName("HelloWorld_Subscriber_A117");

        mp_participant = Domain::createParticipant(PParam);
        if (mp_participant == nullptr)
        {
            std::cout << "[ERROR] 创建参与者失败!" << std::endl;
            return false;
        }

        Domain::registerType(mp_participant, &m_type);

        SubscriberAttributes Rparam;
        Rparam.topic.topicKind = NO_KEY;
        Rparam.topic.topicDataType = "HelloWorld";
        Rparam.topic.topicName = "HelloWorldTopic";
        Rparam.topic.historyQos.kind = KEEP_LAST_HISTORY_QOS;
        Rparam.topic.historyQos.depth = 30;
        Rparam.topic.resourceLimitsQos.max_samples = 50;
        Rparam.topic.resourceLimitsQos.allocated_samples = 20;
        Rparam.qos.m_reliability.kind = RELIABLE_RELIABILITY_QOS;

        mp_subscriber = Domain::createSubscriber(mp_participant, Rparam, &m_listener);
        if (mp_subscriber == nullptr)
        {
            std::cout << "[ERROR] 创建订阅者失败!" << std::endl;
            return false;
        }

        std::cout << "[SUCCESS] HelloWorld订阅者初始化完成!" << std::endl;
        std::cout << "[INFO] 等待发布者数据..." << std::endl;
        return true;
    }

    void run()
    {
        while (true)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

int main()
{
    HelloWorldSubscriber sub;
    if (sub.init())
    {
        sub.run();
    }

    return 0;
}
