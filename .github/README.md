# 🚀 ETL Plus CI/CD Documentation

This directory contains the GitHub Actions workflows for the ETL Plus project, providing comprehensive CI/CD capabilities.

## 🎯 Workflows Overview

### 📋 Main Workflows

#### 1. `ci-cd.yml` - Main CI/CD Pipeline
**Triggers:** Push to main/develop, PRs, manual dispatch
**Features:**
- 🔍 **Code Quality Analysis** - Static analysis, formatting checks, SonarCloud
- 🛡️ **Security Scanning** - CodeQL analysis for vulnerability detection
- 🏗️ **Multi-Platform Builds** - Ubuntu and macOS builds with caching
- 🧪 **Unit Testing** - Automated test execution with coverage reporting
- 🐳 **Docker Build & Push** - Multi-arch container images to GitHub Container Registry
- 🚀 **Kubernetes Deployment** - Automated deployment to staging/production
- 🔔 **Notifications** - Slack and email notifications

#### 2. `benchmarks.yml` - Performance Testing
**Triggers:** Push to main, PRs, weekly schedule
**Features:**
- 🔥 **Load Testing** - HTTP load testing with `wrk`
- ⚡ **Micro-benchmarks** - Response time and throughput measurements
- 📊 **Performance Reports** - Automated benchmark result reporting
- 💬 **PR Comments** - Performance comparison comments on pull requests

#### 3. `security.yml` - Security Auditing
**Triggers:** Push to main, PRs, weekly schedule
**Features:**
- 🔍 **Vulnerability Scanning** - Trivy security scanner for code and containers
- 📦 **Dependency Review** - Automated dependency vulnerability checks
- 🔐 **Secret Scanning** - TruffleHog for detecting leaked secrets
- 📋 **SBOM Generation** - Software Bill of Materials creation

#### 4. `integration-tests.yml` - End-to-End Testing
**Triggers:** Push to main/develop, PRs, manual dispatch
**Features:**
- 🗄️ **Real Database Testing** - Oracle Free container for realistic testing
- 🧪 **API Testing** - Complete REST API endpoint validation
- 🔐 **Authentication Testing** - User registration and login flows
- 📊 **ETL Job Testing** - Full ETL pipeline validation
- 🚀 **Load Testing** - Basic concurrent request testing

#### 5. `release.yml` - Automated Releases
**Triggers:** Git tags (`v*`)
**Features:**
- 📝 **Changelog Generation** - Automatic changelog from git commits
- 🏗️ **Multi-Platform Builds** - Release binaries for Linux and macOS
- 📦 **Asset Packaging** - Complete release packages with dependencies
- 🔔 **Release Notifications** - Slack notifications for new releases

## 🔧 Setup Requirements

### Required Secrets

Add these secrets to your GitHub repository settings:

```bash
# SonarCloud Integration
SONAR_TOKEN=your_sonarcloud_token

# Container Registry
GITHUB_TOKEN=automatic_token  # Already available

# Kubernetes Deployment
KUBE_CONFIG=base64_encoded_kubeconfig

# Notifications
SLACK_WEBHOOK_URL=your_slack_webhook_url
EMAIL_USERNAME=your_email@domain.com
EMAIL_PASSWORD=your_email_password
EMAIL_TO=notifications@yourcompany.com

# Code Coverage
CODECOV_TOKEN=your_codecov_token
```

### Environment Setup

1. **SonarCloud**: Create account and project at sonarcloud.io
2. **Codecov**: Create account and add repository at codecov.io
3. **Kubernetes**: Prepare cluster and obtain kubeconfig
4. **Slack**: Create webhook in your Slack workspace

## 🎯 Workflow Features

### 🚀 Automatic Features

- **Smart Caching**: Dependencies and build artifacts cached across runs
- **Parallel Execution**: Jobs run concurrently for faster feedback
- **Matrix Builds**: Multi-OS and multi-version testing
- **Conditional Deployment**: Automatic staging deployment, manual production
- **Security Integration**: Multiple security scanning tools
- **Performance Monitoring**: Automated performance regression detection

### 🔔 Notification Strategy

- **Success**: Slack notifications for successful deployments
- **Failure**: Email + Slack notifications for build failures
- **Security**: Immediate notifications for security issues
- **Performance**: PR comments for performance changes

## 📊 Quality Gates

### ✅ Required Checks for Merge

1. **Code Quality**: All static analysis checks pass
2. **Security**: No high-severity vulnerabilities
3. **Tests**: All unit and integration tests pass
4. **Coverage**: Minimum 80% code coverage maintained
5. **Performance**: No significant performance regression

### 🚫 Deployment Blockers

- Security vulnerabilities (high/critical)
- Test failures
- Docker build failures
- Failed health checks

## 🔄 Development Workflow

### Feature Development
1. Create feature branch
2. Push commits → triggers CI checks
3. Create PR → full test suite + security scans
4. Review + merge → automatic staging deployment

### Release Process
1. Create release tag (`git tag v1.0.0`)
2. Push tag → triggers release workflow
3. Automatic changelog generation
4. Multi-platform binary builds
5. GitHub release creation
6. Notifications sent

## 📈 Performance Monitoring

### Benchmarks Tracked
- **HTTP Response Time**: 95th percentile latency
- **Throughput**: Requests per second
- **Memory Usage**: Peak memory consumption
- **CPU Usage**: Average CPU utilization

### Performance Thresholds
- Response time: < 100ms (95th percentile)
- Throughput: > 1000 RPS
- Memory: < 512MB peak usage
- CPU: < 70% average utilization

## 🛠️ Customization

### Adding New Checks

1. **Code Quality**: Add tools to `ci-cd.yml` code-quality job
2. **Security**: Add scanners to `security.yml`
3. **Performance**: Add benchmarks to `benchmarks.yml`
4. **Integration**: Add tests to `integration-tests.yml`

### Environment-Specific Configuration

Edit `k8s/deployment.yaml` for:
- Resource limits
- Environment variables
- Scaling configuration
- Ingress settings

## 🐛 Troubleshooting

### Common Issues

1. **Build Failures**: Check dependency installation steps
2. **Test Timeouts**: Increase timeout values in workflow files
3. **Docker Issues**: Verify Dockerfile and registry access
4. **Deployment Failures**: Check Kubernetes configuration and secrets

### Debug Commands

```bash
# Local workflow testing
act -j build

# Check workflow syntax
yamllint .github/workflows/*.yml

# Test Docker build
docker build -t etlplus:test .

# Validate Kubernetes manifests
kubectl apply --dry-run=client -f k8s/
```

## 📚 Additional Resources

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Docker Best Practices](https://docs.docker.com/develop/dev-best-practices/)
- [Kubernetes Deployment Guide](https://kubernetes.io/docs/concepts/workloads/controllers/deployment/)
- [SonarCloud Integration](https://docs.sonarcloud.io/advanced-setup/ci-based-analysis/github-actions/)

---

**Last Updated:** August 8, 2025  
**Maintained by:** ETL Plus Development Team
