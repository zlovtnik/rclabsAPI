# ETL Plus Deployment Tools

This directory contains tools and procedures for deploying the refactored ETL Plus system safely with rollback capabilities.

## Overview

The deployment toolkit provides:

- **Feature Flags**: Gradual rollout control for refactored components
- **Monitoring**: Real-time health monitoring and alerting
- **Rollback**: Automated rollback procedures for different scenarios
- **Checklist**: Comprehensive deployment checklist

## Directory Structure

```text
deployment/
├── rollback.sh              # Rollback automation script
├── deployment_checklist.md  # Comprehensive deployment checklist
└── README.md               # This file

monitoring/
├── monitor.sh              # Health monitoring and alerting script
└── ...

config/
├── feature_flags.json      # Feature flag configuration
└── ...
```

## Feature Flags System

### Configuration

Feature flags are configured in `config/feature_flags.json`:

```json
{
  "flags": {
    "new_logger_system": false,
    "new_exception_system": false,
    "new_request_handler": false,
    "new_websocket_manager": false,
    "new_concurrency_patterns": false,
    "new_type_system": false
  },
  "rollout_percentages": {
    "new_logger_system": 0.0,
    "new_exception_system": 0.0,
    "new_request_handler": 0.0,
    "new_websocket_manager": 0.0,
    "new_concurrency_patterns": 0.0,
    "new_type_system": 0.0
  }
}
```

### Available Features

- `new_logger_system`: New CoreLogger with handler pattern
- `new_exception_system`: Simplified ETLException hierarchy
- `new_request_handler`: Decomposed request handler with new components
- `new_websocket_manager`: Refactored WebSocket manager with connection pooling
- `new_concurrency_patterns`: New RAII lock helpers and standardized patterns
- `new_type_system`: Strong types and type aliases

### Usage in Code

```cpp
#include "feature_flags.hpp"

// Check if feature is enabled
if (FeatureFlags::getInstance().isEnabled(FeatureFlags::NEW_LOGGER_SYSTEM)) {
    // Use new logger system
} else {
    // Use legacy logger system
}

// Check rollout percentage for gradual rollout
if (FeatureFlags::getInstance().shouldEnableForUser(
    FeatureFlags::NEW_EXCEPTION_SYSTEM, userId)) {
    // Enable for this user
}
```

## Monitoring System

### Starting Monitoring

```bash
# Start continuous monitoring
./monitoring/monitor.sh start

# Single health check
./monitoring/monitor.sh check

# Generate report only
./monitoring/monitor.sh report
```

### Monitoring Configuration

Edit the monitoring script to configure:

- `MONITORING_INTERVAL`: Check frequency (default: 60 seconds)
- `ALERT_EMAIL`: Email address for alerts
- `SLACK_WEBHOOK_URL`: Slack webhook for alerts (optional)
- Thresholds for CPU, memory, disk usage

### Monitored Components

- **System Resources**: CPU, memory, disk usage
- **ETL Plus Service**: Process health and availability
- **WebSocket Service**: Connection status and port availability
- **Database**: Connection health
- **Log Files**: Error rate monitoring
- **Feature Flags**: Current rollout status

### Alert Types

- **WARNING**: High resource usage, service issues
- **CRITICAL**: Service down, disk full, database issues
- **INFO**: Status updates and informational alerts

## Rollback Procedures

### Full System Rollback

```bash
# Rollback to safe state (all features disabled)
./deployment/rollback.sh full
```

### Partial Rollback

```bash
# Disable specific feature
./deployment/rollback.sh partial logger
./deployment/rollback.sh partial exception
./deployment/rollback.sh partial request_handler
./deployment/rollback.sh partial websocket
./deployment/rollback.sh partial concurrency
./deployment/rollback.sh partial types
```

### Backup Management

```bash
# Create backup without rollback
./deployment/rollback.sh backup

# Check system health
./deployment/rollback.sh health
```

### What Gets Rolled Back

1. **Feature Flags**: All flags disabled, rollout percentages set to 0%
2. **Configuration**: Restore from backup if needed
3. **Binary**: Restore previous version if available
4. **Services**: Restart ETL Plus service

## Deployment Checklist

See `deployment_checklist.md` for the comprehensive deployment checklist covering:

- Pre-deployment preparation
- Phased deployment approach
- Post-deployment validation
- Rollback triggers and procedures
- Success metrics
- Communication plan

## Quick Start

1. **Setup Feature Flags:**

   ```bash
   cp config/feature_flags.json config/feature_flags.json.backup
   ```

2. **Start Monitoring:**

   ```bash
   ./monitoring/monitor.sh start &
   ```

3. **Deploy Gradually:**

   ```bash
   # Enable features gradually using feature flags
   # Monitor system health throughout
   ```

4. **Rollback if Needed:**

   ```bash
   ./deployment/rollback.sh full
   ```

## Best Practices

### Deployment Strategy

- Start with low-risk infrastructure changes
- Enable features gradually (25% → 50% → 100%)
- Monitor system health continuously
- Have rollback plan ready before each phase

### Monitoring

- Set up alerts for critical thresholds
- Monitor both system and application metrics
- Keep detailed logs for troubleshooting
- Test alert system before deployment

### Rollback Readiness

- Test rollback procedures in staging
- Keep multiple backup points
- Document rollback triggers clearly
- Practice rollback scenarios

## Troubleshooting

### Common Issues

**Feature flags not loading:**

- Check JSON syntax in `config/feature_flags.json`
- Verify file permissions
- Check application logs for parsing errors

**Monitoring not starting:**

- Ensure script has execute permissions
- Check required tools (curl, mail, jq)
- Verify log directory exists

**Rollback fails:**

- Check backup integrity
- Verify file permissions
- Ensure ETL Plus service can be stopped/restarted

### Log Files

- Monitoring logs: `logs/monitoring_*.log`
- Alert logs: `logs/alerts_*.log`
- Rollback logs: `logs/rollback_*.log`
- ETL Plus logs: `logs/etlplus.log`

## Support

For issues with deployment tools:

1. Check log files for error details
2. Test in staging environment first
3. Review deployment checklist for missed steps
4. Contact deployment team for assistance

## Version History

- **v1.0**: Initial deployment toolkit
  - Feature flags system
  - Monitoring and alerting
  - Rollback automation
  - Deployment checklist
