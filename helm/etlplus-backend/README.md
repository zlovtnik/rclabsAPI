# ETL Plus Backend Helm Chart

This Helm chart deploys the ETL Plus Backend application to a Kubernetes cluster.

## Prerequisites

- Kubernetes 1.19+
- Helm 3.0+
- A PostgreSQL database (or Oracle as configured)

## Installing the Chart

To install the chart with the release name `etlplus-backend`:

```bash
helm install etlplus-backend ./helm/etlplus-backend
```

## Configuration

The following table lists the configurable parameters of the ETL Plus Backend chart and their default values.

| Parameter | Description | Default |
|-----------|-------------|---------|
| `replicaCount` | Number of replicas | `3` |
| `image.repository` | Image repository | `ghcr.io/zlovtnik/rclabsapi` |
| `image.tag` | Image tag | `""` (uses Chart appVersion) |
| `service.type` | Service type | `ClusterIP` |
| `service.port` | Service port | `80` |
| `ingress.enabled` | Enable ingress | `true` |
| `ingress.hosts[0].host` | Ingress host | `etlplus-{environment}.yourdomain.com` |
| `resources.limits.cpu` | CPU limit | `500m` |
| `resources.limits.memory` | Memory limit | `512Mi` |
| `autoscaling.enabled` | Enable HPA | `true` |
| `autoscaling.minReplicas` | Minimum replicas | `2` |
| `autoscaling.maxReplicas` | Maximum replicas | `10` |
| `environment` | Deployment environment | `staging` |

## Secrets Configuration

Before deploying, create the required secrets:

```bash
kubectl create secret generic etlplus-backend-secrets \
  --from-literal=database-host=your-db-host \
  --from-literal=database-password=your-db-password \
  --from-literal=jwt-secret=your-jwt-secret
```

## Database Configuration

Update the `config.database` section in `values.yaml` to match your database setup:

```yaml
config:
  database:
    host: "${DATABASE_HOST}"
    port: 5432  # For PostgreSQL
    name: "etl_db"
    username: "etlplus"
    password: "${DATABASE_PASSWORD}"
    type: "postgresql"  # or "oracle"
```

## Ingress Configuration

Update the ingress host in `values.yaml`:

```yaml
ingress:
  hosts:
    - host: etlplus-prod.yourdomain.com
      paths:
        - path: /
          pathType: Prefix
```

## Upgrading the Chart

To upgrade the chart:

```bash
helm upgrade etlplus-backend ./helm/etlplus-backend
```

## Uninstalling the Chart

To uninstall the chart:

```bash
helm uninstall etlplus-backend
```

## Rolling Deployment

The chart is configured with a rolling update strategy:

- `maxSurge: 1` - Allows 1 extra pod during updates
- `maxUnavailable: 0` - Ensures no downtime during updates

## Health Checks

The deployment includes:

- **Liveness Probe**: Checks `/health` endpoint every 10 seconds
- **Readiness Probe**: Checks `/ready` endpoint every 5 seconds

## Security Features

- Non-root user execution
- Read-only root filesystem (except logs)
- Security context with restricted capabilities
- Service account with minimal permissions
