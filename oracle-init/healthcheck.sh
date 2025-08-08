#!/bin/bash
# Quick test to verify Oracle database connectivity

# Test connection to Oracle database
sqlplus -s etlplus/etlplus123@localhost:1521/FREE <<EOF
SELECT 'Oracle database is healthy' AS status FROM dual;
EXIT;
EOF

if [ $? -eq 0 ]; then
    echo "✅ Oracle database connection successful"
    exit 0
else
    echo "❌ Oracle database connection failed"
    exit 1
fi
