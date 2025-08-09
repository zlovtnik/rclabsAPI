#!/bin/bash
# Test script for Oracle Docker setup

echo "🚀 ETL Plus Oracle Database Setup Test"
echo "======================================"

# Check if Docker is running
if ! docker info > /dev/null 2>&1; then
    echo "❌ Docker is not running. Please start Docker first."
    exit 1
fi

echo "✅ Docker is running"

# Check if Docker Compose file exists
if [ ! -f "docker-compose.yml" ]; then
    echo "❌ docker-compose.yml not found in current directory"
    exit 1
fi

echo "✅ docker-compose.yml found"

# Start Oracle database
echo "🔄 Starting Oracle database container..."
docker-compose up -d oracle-db

# Wait for database to be ready
echo "⏳ Waiting for Oracle database to initialize (this may take 2-3 minutes)..."
timeout=180
counter=0

while [ $counter -lt $timeout ]; do
    if docker-compose exec -T oracle-db sqlplus -s etlplus/etlplus123@localhost:1521/FREE <<< "SELECT 'ready' FROM dual;" > /dev/null 2>&1; then
        echo "✅ Oracle database is ready!"
        break
    fi
    
    if [ $((counter % 30)) -eq 0 ]; then
        echo "⏳ Still waiting... ($counter seconds elapsed)"
    fi
    
    sleep 5
    counter=$((counter + 5))
done

if [ $counter -ge $timeout ]; then
    echo "❌ Timeout waiting for Oracle database to start"
    echo "Check logs with: docker-compose logs oracle-db"
    exit 1
fi

# Test connection
echo "🔍 Testing database connection..."
if docker-compose exec -T oracle-db sqlplus -s etlplus/etlplus123@localhost:1521/FREE <<EOF
SELECT 'Connection successful!' AS result FROM dual;
SELECT username, created FROM user_users WHERE username = 'ETLPLUS';
EXIT;
EOF
then
    echo "✅ Database connection test passed!"
else
    echo "❌ Database connection test failed"
    exit 1
fi

# Show container status
echo ""
echo "📊 Container Status:"
docker-compose ps

echo ""
echo "🎉 Oracle database setup complete!"
echo ""
echo "Connection details:"
echo "  Host: localhost"
echo "  Port: 1521"
echo "  Service: FREE" 
echo "  Username: etlplus"
echo "  Password: etlplus123"
echo ""
echo "Oracle Enterprise Manager: http://localhost:5500/em"
echo ""
echo "To stop: docker-compose down"
echo "To view logs: docker-compose logs oracle-db"
