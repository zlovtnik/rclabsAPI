# Render.com Deployment Guide for ETL Plus

This guide helps you deploy the ETL Plus C++ backend to Render.com with minimal disk space usage.

## Prerequisites

1. **Render.com Account**: Sign up at [render.com](https://render.com)
2. **GitHub Repository**: Your code should be in a GitHub repository
3. **Database Consideration**: The original config uses Oracle, but Render.com only supports PostgreSQL. You'll need to adapt your database code.

## Files Created for Render Deployment

1. **`render.yaml`**: Main deployment configuration
2. **`Dockerfile.render`**: Optimized Dockerfile for minimal image size
3. **`config/config.render.json`**: Environment-aware configuration

## Deployment Steps

### 1. Update Database Code (Required)

Since your application currently uses Oracle but Render.com only supports PostgreSQL, you need to:

1. Update `database_manager.cpp` to use PostgreSQL instead of Oracle
2. Replace Oracle-specific SQL with PostgreSQL-compatible queries
3. Update connection strings and drivers

### 2. Update Configuration Loading

Modify your `config_manager.cpp` to support environment variable substitution:

```cpp
// Example: Replace ${VAR_NAME} with environment variable values
std::string expandEnvVars(const std::string& str) {
    // Implementation to replace ${VAR_NAME} with getenv("VAR_NAME")
}
```

### 3. Deploy to Render

1. **Connect Repository**:
   - Go to Render Dashboard
   - Click "New +" → "Blueprint"
   - Connect your GitHub repository
   - Select the repository containing your code

2. **Configure Blueprint**:
   - Render will automatically detect the `render.yaml` file
   - Review the configuration
   - Click "Apply"

3. **Monitor Deployment**:
   - Watch the build logs for any issues
   - The PostgreSQL database will be created automatically
   - The web service will start after successful build

## Cost Optimization Features

### Minimal Disk Usage
- **1GB disk** for application logs
- **1GB database** (expandable if needed)
- **Starter plans** for both web service and database

### Build Optimizations
- Multi-stage Docker build
- Stripped binaries (`-s` flag)
- Minimal runtime dependencies
- Size-optimized compilation (`-Os`)

### Resource Limits
- **2 threads** instead of 4 (reduced for starter plan)
- **2 max concurrent jobs** instead of 5
- **Auto-scaling** between 1-2 instances

## Environment Variables

The following environment variables are automatically configured:

- `PORT`: Service port (set by Render)
- `DATABASE_HOST`: PostgreSQL host
- `DATABASE_PORT`: PostgreSQL port  
- `DATABASE_NAME`: Database name
- `DATABASE_USERNAME`: Database username
- `DATABASE_PASSWORD`: Database password
- `JWT_SECRET`: Auto-generated JWT secret

## Monitoring

- **Health Check**: `/api/health` endpoint
- **Logs**: Available in Render dashboard
- **Metrics**: Built into Render platform

## Troubleshooting

### Build Failures
1. Check that all PostgreSQL-related dependencies are properly linked
2. Verify CMakeLists.txt includes all necessary libraries
3. Review build logs for missing packages

### Runtime Issues
1. Check environment variables are properly set
2. Verify database connection
3. Monitor application logs in Render dashboard

### Database Migration
If migrating from Oracle:
1. Export your Oracle schema
2. Convert Oracle-specific syntax to PostgreSQL
3. Import data using PostgreSQL tools

## Scaling Up

To increase resources later:
- Upgrade to higher Render plans
- Increase disk sizes in `render.yaml`
- Adjust thread counts and job limits
- Add more instances for load balancing

## Security Notes

- JWT secrets are auto-generated
- Database credentials are managed by Render
- All traffic is HTTPS by default
- Private networking between services
