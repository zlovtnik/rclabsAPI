#include "component_logger.hpp"
#include "logger.hpp"

namespace etl {

// Template method implementations for ComponentLogger

template<typename Component>
template<typename... Args>
void ComponentLogger<Component>::debug(const std::string& message, Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
        Logger::getInstance().debug(getComponentName(), message);
    } else {
        Logger::getInstance().debug(getComponentName(), message, std::forward<Args>(args)...);
    }
}

template<typename Component>
template<typename... Args>
void ComponentLogger<Component>::info(const std::string& message, Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
        Logger::getInstance().info(getComponentName(), message);
    } else {
        Logger::getInstance().info(getComponentName(), message, std::forward<Args>(args)...);
    }
}

template<typename Component>
template<typename... Args>
void ComponentLogger<Component>::warn(const std::string& message, Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
        Logger::getInstance().warn(getComponentName(), message);
    } else {
        Logger::getInstance().warn(getComponentName(), message, std::forward<Args>(args)...);
    }
}

template<typename Component>
template<typename... Args>
void ComponentLogger<Component>::error(const std::string& message, Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
        Logger::getInstance().error(getComponentName(), message);
    } else {
        Logger::getInstance().error(getComponentName(), message, std::forward<Args>(args)...);
    }
}

template<typename Component>
template<typename... Args>
void ComponentLogger<Component>::fatal(const std::string& message, Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
        Logger::getInstance().fatal(getComponentName(), message);
    } else {
        Logger::getInstance().fatal(getComponentName(), message, std::forward<Args>(args)...);
    }
}

template<typename Component>
template<typename... Args>
void ComponentLogger<Component>::debugForJob(const std::string& message, const std::string& jobId, Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
        Logger::getInstance().debugForJob(getComponentName(), message, jobId);
    } else {
        Logger::getInstance().debugForJob(getComponentName(), message, jobId, std::forward<Args>(args)...);
    }
}

template<typename Component>
template<typename... Args>
void ComponentLogger<Component>::infoForJob(const std::string& message, const std::string& jobId, Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
        Logger::getInstance().infoForJob(getComponentName(), message, jobId);
    } else {
        Logger::getInstance().infoForJob(getComponentName(), message, jobId, std::forward<Args>(args)...);
    }
}

template<typename Component>
template<typename... Args>
void ComponentLogger<Component>::warnForJob(const std::string& message, const std::string& jobId, Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
        Logger::getInstance().warnForJob(getComponentName(), message, jobId);
    } else {
        Logger::getInstance().warnForJob(getComponentName(), message, jobId, std::forward<Args>(args)...);
    }
}

template<typename Component>
template<typename... Args>
void ComponentLogger<Component>::errorForJob(const std::string& message, const std::string& jobId, Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
        Logger::getInstance().errorForJob(getComponentName(), message, jobId);
    } else {
        Logger::getInstance().errorForJob(getComponentName(), message, jobId, std::forward<Args>(args)...);
    }
}

template<typename Component>
template<typename... Args>
void ComponentLogger<Component>::fatalForJob(const std::string& message, const std::string& jobId, Args&&... args) {
    if constexpr (sizeof...(args) == 0) {
        Logger::getInstance().fatalForJob(getComponentName(), message, jobId);
    } else {
        Logger::getInstance().fatalForJob(getComponentName(), message, jobId, std::forward<Args>(args)...);
    }
}

// Explicit template instantiations for the component types we use
template class ComponentLogger<AuthManager>;
template class ComponentLogger<ConfigManager>;
template class ComponentLogger<DatabaseManager>;
template class ComponentLogger<DataTransformer>;
template class ComponentLogger<ETLJobManager>;
template class ComponentLogger<HttpServer>;
template class ComponentLogger<JobMonitorService>;
template class ComponentLogger<NotificationService>;
template class ComponentLogger<RequestHandler>;
template class ComponentLogger<WebSocketManager>;
template class ComponentLogger<WebSocketFilterManager>;

} // namespace etl
