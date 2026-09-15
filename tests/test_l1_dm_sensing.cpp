#include <gtest/gtest.h>

#include "dm_sensing_cap.h"
#include "dm_agent_sta_iface.h"
#include "dm_layer3_path.h"
#include "dm_sensing_exchange.h"

TEST(dm_sensing_t, records_initialize_and_copy)
{
    dm_sensing_cap_t cap;
    ASSERT_EQ(cap.init(), 0);
    cap.m_info.layer3_transport_flags = 0x80U;
    cap.m_info.num_data_types = 1U;
    cap.m_info.data_types[0] = EM_SENSING_DATA_TYPE_IEEE_CSI;
    dm_sensing_cap_t cap_copy;
    cap_copy = cap;
    EXPECT_TRUE(cap == cap_copy);

    dm_agent_sta_iface_t iface;
    ASSERT_EQ(iface.init(), 0);
    iface.m_info.num_sta = 1U;
    iface.m_info.agent_sta_mac[0][5] = 1U;
    dm_agent_sta_iface_t iface_copy = iface;
    EXPECT_TRUE(iface == iface_copy);

    dm_layer3_path_t path;
    ASSERT_EQ(path.init(), 0);
    path.m_info.active = true;
    dm_layer3_path_t path_copy = path;
    EXPECT_TRUE(path == path_copy);

    dm_sensing_exchange_t exchange;
    ASSERT_EQ(exchange.init(), 0);
    exchange.m_info.exchange_id = 42U;
    exchange.m_info.exchange_type = em_sensing_exchange_tb;
    dm_sensing_exchange_t exchange_copy = exchange;
    EXPECT_TRUE(exchange == exchange_copy);
}
