#include "exceptions.hpp"
#include "exception_handler.hpp"
#include "logger.hpp"
#include <iostream>

using namespace ETLPlus::Exceptions;
using namespace ETLPlus::ExceptionHandling;

void demonstrateExceptionChaining() {
    try {
        try {
            // Simulate a database connection failure
            throw DatabaseException(ErrorCode::CONNECTION_FAILED, "Database connection failed");
        } catch (const DatabaseException& db_ex) {
            // Wrap in a higher-level exception
            auto system_ex = SystemException(ErrorCode::INTERNAL_ERROR, "System initialization failed");
            system_ex.setCause(std::make_shared<DatabaseException>(db_ex));
            throw system_ex;
        }
    } catch (const BaseException& ex) {
        std::cout << "=== Exception Chaining Demo ===" << std::endl;
        std::cout << "Main Exception: " << ex.toJsonString() << std::endl;
        
        if (ex.getCause() != nullptr) {
            std::cout << "Caused by: " << ex.getCause()->toJsonString() << std::endl;
        }
    }
}

void demonstrateRetryLogic() {
    std::cout << "\n=== Retry Logic Demo ===" << std::endl;
    
    int attempts = 0;
    
    try {
        // For simplicity, let's manually implement retry since the template is complex
        for (int i = 0; i < 3; i++) {
            try {
                attempts++;
                std::cout << "Attempt " << attempts << std::endl;
                if (attempts < 3) {
                    throw NetworkException(ErrorCode::REQUEST_TIMEOUT, "Network timeout", 408);
                }
                std::cout << "Success on attempt " << attempts << std::endl;
                break;
            } catch (const BaseException& ex) {
                if (i == 2) throw; // Re-throw on final attempt
                std::cout << "Attempt failed, retrying..." << std::endl;
            }
        }
    } catch (const BaseException& ex) {
        std::cout << "Final failure: " << ex.getMessage() << std::endl;
    }
}

void demonstrateBasicExceptions() {
    std::cout << "\n=== Basic Exception Demo ===" << std::endl;
    
    // Test different exception types
    try {
        throw ValidationException(ErrorCode::INVALID_INPUT, "Invalid user input");
    } catch (const BaseException& ex) {
        std::cout << "ValidationException: " << ex.toJsonString() << std::endl;
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
}

int main() {
    std::cout << "ETL Plus Exception Handling System Demo" << std::endl;
    std::cout << "=======================================" << std::endl;
    
    demonstrateExceptionChaining();
    demonstrateRetryLogic();
    demonstrateBasicExceptions();
    
    std::cout << "\nDemo completed successfully!" << std::endl;
    return 0;
}
