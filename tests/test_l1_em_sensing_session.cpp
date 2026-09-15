#include <gtest/gtest.h>

#include "em_sensing_session.h"

TEST(em_sensing_session, creates_and_removes_session)
{
    em_sensing_session_manager_t manager;
    std::string path;
    ASSERT_TRUE(manager.create_session(path));
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(manager.session_count(), 1U);
    EXPECT_TRUE(manager.add_exchange(path, 42U));
    EXPECT_FALSE(manager.add_exchange(path, 42U));
    EXPECT_TRUE(manager.remove_exchange(path, 42U));
    EXPECT_TRUE(manager.delete_session(path));
    EXPECT_EQ(manager.session_count(), 0U);
}

TEST(em_sensing_session, rejects_unknown_sessions)
{
    em_sensing_session_manager_t manager;
    EXPECT_FALSE(manager.add_exchange("/tmp/unknown-sensing-session", 1U));
    EXPECT_FALSE(manager.remove_exchange("/tmp/unknown-sensing-session", 1U));
}

TEST(em_sensing_session, termination_removes_exchange_subscription)
{
    em_sensing_session_manager_t manager;
    std::string path;
    ASSERT_TRUE(manager.create_session(path));
    ASSERT_TRUE(manager.add_exchange(path, 77U));
    EXPECT_TRUE(manager.notify_exchange_terminated(77U, em_sensing_exchange_terminated));
    EXPECT_FALSE(manager.remove_exchange(path, 77U));
    EXPECT_TRUE(manager.delete_session(path));
}