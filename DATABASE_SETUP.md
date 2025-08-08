# ETL Plus Oracle Database Setup

This directory contains the Docker Compose configuration for running Oracle Database Free with ETL Plus.

## Prerequisites

1. Docker and Docker Compose installed
2. At least 4GB of available memory for Oracle container
3. Network access to Oracle Container Registry

## Setup Instructions

### 1. Pull Oracle Free Image (First Time Only)

Oracle requires authentication to pull their container images:

```bash
# Login to Oracle Container Registry
docker login container-registry.oracle.com

# Username: your-oracle-account-email
# Password: your-oracle-account-password
```

If you don't have an Oracle account, create one at <https://container-registry.oracle.com>

### 2. Start Oracle Database

```bash
# Start the database container
docker-compose up -d oracle-db

# Monitor the startup process (Oracle takes 2-3 minutes to initialize)
docker-compose logs -f oracle-db
```

### 3. Verify Database Connection

```bash
# Check if Oracle is running
docker-compose ps

# Test connection
docker exec -it etlplus-oracle-db sqlplus etlplus/etlplus123@localhost:1521/FREE
```

## Database Configuration

- **Host**: localhost
- **Port**: 1521
- **Service Name**: FREE
- **Username**: etlplus
- **Password**: etlplus123
- **SYS Password**: etlplus123

## Oracle Enterprise Manager

Access Oracle Enterprise Manager Express at: <http://localhost:5500/em>

- **Username**: etlplus
- **Password**: etlplus123

## Useful Commands

```bash
# Start database
docker-compose up -d oracle-db

# Stop database
docker-compose down

# View logs
docker-compose logs oracle-db

# Connect to database
docker exec -it etlplus-oracle-db sqlplus etlplus/etlplus123@localhost:1521/FREE

# Reset database (removes all data)
docker-compose down -v
docker volume rm cplus_oracle_data
```

## Database Schema

The initialization script creates:

1. **etl_jobs** table - Stores ETL job definitions and status
2. **etl_job_logs** table - Stores ETL job execution logs  
3. **users** table - Stores user authentication data

## Troubleshooting

### Container Won't Start

- Ensure you have enough memory (4GB minimum)
- Check Docker logs: `docker-compose logs oracle-db`

### Connection Refused

- Oracle takes 2-3 minutes to fully initialize
- Check health status: `docker-compose ps`
- Monitor logs for "DATABASE IS READY TO USE" message

### Authentication Issues

- Verify Oracle Container Registry login
- Check credentials in config.json match database setup

## Performance Notes

- Oracle Free has limitations: 2 CPUs, 2GB RAM, 12GB user data
- For production, consider Oracle Database Enterprise Edition
- Persistent data is stored in Docker volume `cplus_oracle_data`
