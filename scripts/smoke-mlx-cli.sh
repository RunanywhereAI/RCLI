#!/usr/bin/env bash
# Compatibility name for the SDK-era rcli/scripts/smoke-mlx-cli.sh.
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/smoke-mlx.sh" "$@"
