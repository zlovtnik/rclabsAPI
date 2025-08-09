#include "exceptions.hpp"
#include <iostream>

using namespace ETLPlus::Exceptions;

int main() {
    std::cout << "ETL Plus Exception Handling System Demo" << std::endl;
    std::cout << "=======================================" << std::endl;
    
    // Test basic exception creation and JSON serialization
    std::cout << "\n=== Basic Exception Demo ===" << std::endl;
    
    try {
        throw ValidationException(ErrorCode::INVALID_INPUT, "Invalid user input provided");
    } catch (const BaseException& ex) {
        std::cout << "ValidationException caught:" << std::endl;
        std::cout << "  Code: " << static_cast<int>(ex.getErrorCode()) << std::endl;
        std::cout << "  Category: " << static_cast<int>(ex.getCategory()) << std::endl;
        std::cout << "  Message: " << ex.getMessage() << std::endl;
        std::cout << "  JSON: " << ex.toJsonString() << std::endl;
    }
    
    std::cout << "\n=== Different Exception Types ===" << std::endl;
    
    // Test different exception types
    try {
        throw DatabaseException(ErrorCode::CONNECTION_FAILED, "Database connection timeout");
    } catch (const BaseException& ex) {
        std::cout << "DatabaseException: " << ex.toJsonString() << std::endl;
    }
    
    try {
        throw NetworkException(ErrorCode::REQUEST_TIMEOUT, "HTTP request timeout", 408);
    } catch (const BaseException& ex) {
        std::cout << "NetworkException: " << ex.toJsonString() << std::endl;
    }
    
    try {
        throw AuthException(ErrorCode::INVALID_CREDENTIALS, "Authentication failed");
    } catch (const BaseException& ex) {
        std::cout << "AuthException: " << ex.toJsonString() << std::endl;
    }
    
    try {
        throw ETLException(ErrorCode::JOB_EXECUTION_FAILED, "ETL job processing failed");
    } catch (const BaseException& ex) {
        std::cout << "ETLException: " << ex.toJsonString() << std::endl;
    }
    
    std::cout << "\n=== Exception Chaining Demo ===" << std::endl;
    
    try {
        try {
            // First exception
            throw DatabaseException(ErrorCode::CONNECTION_FAILED, "Primary database connection failed");
        } catch (const BaseException& primary) {
            // Chain with a higher-level exception
            auto system_ex = SystemException(ErrorCode::INTERNAL_ERROR, "System initialization failed");
            auto primary_copy = std::make_shared<DatabaseException>(dynamic_cast<const DatabaseException&>(primary));
            system_ex.setCause(std::static_pointer_cast<BaseException>(primary_copy));
            throw system_ex;
        }
    } catch (const BaseException& ex) {
        std::cout << "Chained Exception:" << std::endl;
        std::cout << "  Main: " << ex.toJsonString() << std::endl;
        auto cause = ex.getCause();
        if (cause != nullptr) {
            std::cout << "  Cause: " << cause->toJsonString() << std::endl;
        }
    }
    
    std::cout << "\nDemo completed successfully!" << std::endl;
    std::cout << "All exception types working properly with structured JSON output!" << std::endl;
    
    return 0;
}
