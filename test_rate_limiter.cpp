#include "rate_limiter.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

class RateLimiterTest : public ::testing::Test {
protected:
    void SetUp() override {
        rateLimiter = std::make_unique<RateLimiter>();
    }

    std::unique_ptr<RateLimiter> rateLimiter;
};

TEST_F(RateLimiterTest, BasicRateLimiting) {
    std::string clientId = "test_client";
    std::string endpoint = "/api/auth/login";  // Use configured endpoint with 5 requests per minute

    // Initialize with default rules
    rateLimiter->initializeDefaultRules();

    // First 5 requests should be allowed
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(rateLimiter->isAllowed(clientId, endpoint)) << "Request " << i + 1 << " should be allowed";
    }

    // 6th request should be denied
    EXPECT_FALSE(rateLimiter->isAllowed(clientId, endpoint)) << "6th request should be rate limited";
}

TEST_F(RateLimiterTest, DifferentClients) {
    std::string clientId1 = "client1";
    std::string clientId2 = "client2";
    std::string endpoint = "/api/auth/login";

    // Initialize with default rules
    rateLimiter->initializeDefaultRules();

    // Both clients should be allowed initially
    EXPECT_TRUE(rateLimiter->isAllowed(clientId1, endpoint));
    EXPECT_TRUE(rateLimiter->isAllowed(clientId2, endpoint));
}

TEST_F(RateLimiterTest, GetRateLimitInfo) {
    std::string clientId = "test_client";
    std::string endpoint = "/api/test";

    auto info = rateLimiter->getRateLimitInfo(clientId, endpoint);

    EXPECT_GE(info.limit, 0);
    EXPECT_GE(info.remainingRequests, 0);
    EXPECT_LE(info.remainingRequests, info.limit);
}

TEST_F(RateLimiterTest, ResetClient) {
    std::string clientId = "test_client";
    std::string endpoint = "/api/test";

    // Make some requests
    for (int i = 0; i < 10; ++i) {
        rateLimiter->isAllowed(clientId, endpoint);
    }

    // Reset the client
    rateLimiter->resetClient(clientId);

    // Should be allowed again
    EXPECT_TRUE(rateLimiter->isAllowed(clientId, endpoint));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
