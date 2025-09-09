# ETL Plus Migration Tools

This directory contains automated migration tools and utilities for migrating from the legacy ETL Plus system to the refactored version 2.0.

## Overview

The migration tools provide:

- **Analysis tools** to identify what needs migration
- **Automated migration scripts** for common patterns
- **Configuration migration utilities**
- **Validation tools** to ensure migration completeness
- **Compatibility layer** for gradual migration

## Quick Start

### Run Complete Migration

```bash
# Run all migration steps
./migrate.sh all

# Or run individual steps
./migrate.sh analyze
./migrate.sh config
./migrate.sh code
./migrate.sh validate
./migrate.sh compatibility
```

### Dry Run First

```bash
# Test migration without making changes
./migrate.sh all --dry-run

# Test individual components
./migrate.sh config --dry-run
./migrate.sh code --dry-run
```

## Available Tools

### Main Migration Script (`migrate.sh`)

Orchestrates all migration activities.

**Usage:**

```bash
./migrate.sh [COMMAND] [OPTIONS]
```

**Commands:**

- `analyze` - Analyze codebase for migration opportunities
- `config` - Migrate configuration files
- `code` - Migrate code patterns
- `validate` - Validate migration completeness
- `compatibility` - Install compatibility layer
- `all` - Run all migration steps
- `help` - Show help message

**Options:**

- `--dry-run` - Show what would be changed without making changes
- `--backup` - Create backups before making changes (default: enabled)
- `--no-backup` - Don't create backups
- `--verbose` - Enable verbose output
- `--target-dir DIR` - Target directory for migration (default: project root)

**Examples:**

```bash
# Analyze what needs migration
./migrate.sh analyze

# Migrate configuration with backups
./migrate.sh config --backup

# Migrate code in dry-run mode
./migrate.sh code --dry-run --verbose

# Run complete migration
./migrate.sh all --target-dir /path/to/project
```

### Analysis Tool (`analyze_migration.sh`)

Analyzes the codebase to identify deprecated patterns and migration opportunities.

**What it checks:**

- Old exception classes (DatabaseException, NetworkException, etc.)
- Old logging macros (LOG_INFO, LOG_ERROR, etc.)
- Old configuration access patterns
- Old WebSocket handler patterns
- Custom extensions that may need updates

**Output:**

- Migration analysis report
- Priority recommendations
- File counts for each pattern type

### Configuration Migration (`migrate_config.sh`)

Migrates configuration files from old format to new format.

**Features:**

- Updates main configuration file with new sections
- Migrates environment-specific configurations
- Creates new configuration files for new features
- Validates configuration migration

**Migrates:**

- WebSocket configuration section
- Enhanced logging configuration
- Security settings (CORS, rate limiting)
- Database connection pooling
- Environment-specific overrides

### Code Migration (`migrate_code.sh`)

Automatically migrates common code patterns from old to new system.

**Migration patterns:**

- Exception patterns: `throw DatabaseException()` → `throw ETLException()`
- Logging patterns: `LOG_INFO()` → `logger_->info()`
- Configuration access: `config->getString()` → `config->get<std::string>()`
- WebSocket handlers: `WebSocketHandler` → `WebSocketManager::Handler`
- Correlation ID tracking: Adds correlation ID support

**Safety features:**

- Creates backups before making changes
- Dry-run mode to preview changes
- Selective migration (only migrates files that need it)

### Validation Tool (`validate_migration.sh`)

Validates that migration has been completed successfully.

**Validation checks:**

- Exception migration completeness
- Logging migration completeness
- Configuration migration completeness
- WebSocket migration completeness
- Correlation ID usage
- Compilation validation
- Deprecated include removal

**Output:**

- Detailed validation report
- List of any remaining issues
- Migration completeness percentage
- Recommendations for next steps

### Compatibility Layer (`install_compatibility.sh`)

Installs a compatibility layer to allow legacy code to work with the new system.

**Provides compatibility for:**

- Old exception types (DatabaseException, NetworkException, etc.)
- Old logging macros (LOG_INFO, LOG_ERROR, etc.)
- Old configuration access patterns
- Old WebSocket handler interface

**Benefits:**

- Allows gradual migration
- Prevents breaking existing functionality
- Provides time to migrate complex components

## Migration Workflow

### Phase 1: Analysis and Planning

```bash
# Analyze current state
./migrate.sh analyze

# Review analysis report
cat migration_analysis_*.txt

# Plan migration approach based on findings
```

### Phase 2: Configuration Migration

```bash
# Backup configuration files
cp -r config config.backup

# Migrate configuration
./migrate.sh config

# Test configuration
./validate_migration.sh
```

### Phase 3: Code Migration

```bash
# Dry run first
./migrate.sh code --dry-run

# Review proposed changes
# Make any necessary adjustments

# Run actual migration
./migrate.sh code --backup

# Validate migration
./migrate.sh validate
```

### Phase 4: Compatibility and Testing

```bash
# Install compatibility layer if needed
./migrate.sh compatibility

# Run tests
make test

# Test compilation
make clean && make

# Validate again
./migrate.sh validate
```

### Phase 5: Cleanup

```bash
# Remove compatibility layer when no longer needed
rm -rf include/compatibility
rm -rf src/compatibility

# Update CMakeLists.txt
# Remove compatibility references

# Final validation
./migrate.sh validate
```

## Migration Checklist

### Pre-Migration

- [ ] Backup entire codebase
- [ ] Run analysis to understand scope
- [ ] Plan migration in phases
- [ ] Setup test environment
- [ ] Review migration guide documentation

### Configuration Migration

- [ ] Run configuration migration
- [ ] Update environment-specific configs
- [ ] Test configuration loading
- [ ] Validate new configuration options

### Code Migration

- [ ] Run code migration scripts
- [ ] Review automated changes
- [ ] Fix any compilation errors
- [ ] Update custom extensions manually
- [ ] Test all functionality

### Validation and Testing

- [ ] Run validation tools
- [ ] Execute unit tests
- [ ] Run integration tests
- [ ] Test performance
- [ ] Validate in staging environment

### Post-Migration

- [ ] Remove compatibility layer
- [ ] Update documentation
- [ ] Train development team
- [ ] Monitor production deployment

## Troubleshooting

### Common Issues

**Compilation Errors After Migration:**

```bash
# Check for migration issues
./migrate.sh validate

# Review compilation errors
make 2>&1 | head -20

# Check if compatibility layer is needed
./migrate.sh compatibility
```

**Missing Configuration Sections:**

```bash
# Re-run configuration migration
./migrate.sh config

# Check configuration file
cat config/config.json
```

**Old Patterns Still Present:**

```bash
# Re-run code migration
./migrate.sh code

# Or migrate specific files manually
# Edit files according to migration guide
```

### Troubleshooting Tips

1. **Check the migration guide:** `docs/migration_guide.md`
2. **Review validation reports:** Look for detailed error messages
3. **Run with verbose output:** Use `--verbose` flag for more details
4. **Check logs:** Look for any error messages in tool output

---

**Note:** These tools are designed to automate common migration patterns. Complex custom extensions may require manual migration following the patterns documented in the migration guide.

### Getting Help

1. **Check the migration guide:** `docs/migration_guide.md`
2. **Review validation reports:** Look for detailed error messages
3. **Run with verbose output:** Use `--verbose` flag for more details
4. **Check logs:** Look for any error messages in tool output

## Advanced Usage

### Custom Migration Rules

You can extend the migration tools by modifying the scripts:

1. **Add new patterns** to `migrate_code.sh`
2. **Customize validation** in `validate_migration.sh`
3. **Add configuration sections** to `migrate_config.sh`

### Integration with CI/CD

Add migration validation to your CI pipeline:

```yaml
# .github/workflows/migration.yml
name: Migration Validation
on: [push, pull_request]

jobs:
  validate-migration:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Run migration validation
        run: |
          cd scripts/migration
          ./migrate.sh validate
```

### Batch Processing

For large codebases, process files in batches:

```bash
# Migrate specific directories
./migrate.sh code --target-dir src/components

# Migrate specific file types
find . -name "*.cpp" -exec ./migrate_code.sh {} \;
```

## Support and Resources

### Documentation

- Migration Guide: `docs/migration_guide.md`
- Configuration Guide: `docs/configuration_guide.md`
- API Documentation: `docs/api/overview.md`

### Support Resources

- Check validation output for specific error messages
- Review the analysis report for migration scope
- Use dry-run mode to preview changes
- Check the troubleshooting section above

### Contributing to Migration Tools

To improve the migration tools:

1. Test changes with dry-run mode
2. Validate with the validation script
3. Update this documentation
4. Test on sample legacy code

---

**Note:** These tools are designed to automate common migration patterns. Complex custom extensions may require manual migration following the patterns documented in the migration guide.
