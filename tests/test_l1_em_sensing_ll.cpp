#include <gtest/gtest.h>

#include "em_sensing_ll.h"

#include <array>
#include <cstring>

TEST(em_sensing_ll_stub, reports_sensing_capabilities)
{
    em_sensing_ll_stub_t stub;
    em_sensing_capability_snapshot_t capabilities;

    ASSERT_TRUE(stub.get_capabilities(capabilities));
    ASSERT_EQ(capabilities.layer3_transport_flags, 0x80U);
    ASSERT_EQ(capabilities.radios.size(), 1U);
    EXPECT_EQ(capabilities.radios[0].bss_flags, 0xe0U);
    EXPECT_EQ(capabilities.radios[0].sta_flags, 0xe0U);
    ASSERT_EQ(capabilities.radios[0].data_types.size(), 1U);
    EXPECT_EQ(capabilities.radios[0].data_types[0], EM_SENSING_DATA_TYPE_IEEE_CSI);
}

TEST(em_sensing_ll_stub, disabled_backend_rejects_operations)
{
    em_sensing_ll_stub_t stub(false);
    em_sensing_capability_snapshot_t capabilities;
    const mac_address_t mac = {0, 1, 2, 3, 4, 5};

    EXPECT_FALSE(stub.get_capabilities(capabilities));
    EXPECT_FALSE(stub.send_sensing_measurement_request(1U));
    EXPECT_FALSE(stub.send_sensing_measurement_query(mac, mac));
    EXPECT_FALSE(stub.start_qos_null_exchange(1U));
    EXPECT_FALSE(stub.send_probe_request(mac, {}));
}

TEST(em_sensing_ll_stub, creates_deterministic_agent_sta_addresses)
{
    em_sensing_ll_stub_t stub;
    const mac_address_t ruid = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60};
    std::vector<std::array<uint8_t, 6>> addresses;

    ASSERT_TRUE(stub.create_agent_sta_iface(ruid, 2U, addresses));
    ASSERT_EQ(addresses.size(), 2U);
    EXPECT_EQ(addresses[0][5], 0x61U);
    EXPECT_EQ(addresses[1][5], 0x62U);
}

TEST(em_sensing_ll_stub, agent_sta_is_unassociated_only)
{
    em_sensing_ll_stub_t stub;
    EXPECT_TRUE(stub.agent_sta_is_unassociated_only());
}
