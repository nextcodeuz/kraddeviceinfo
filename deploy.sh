#!/usr/bin/env bash
# krad.device.info - deploy site/ to Cloudflare Pages in one command.
# Usage:
#   ./deploy.sh                # uses existing wrangler login
#   ./deploy.sh --token TOKEN  # non-interactive with API token
set -euo pipefail
cd "$(dirname "$0")"

PROJECT="${CF_PROJECT:-kraddeviceinfo}"

if [[ "${1:-}" == "--token" && -n "${2:-}" ]]; then
    export CLOUDFLARE_API_TOKEN="$2"
    npx wrangler@latest pages deploy site --project-name "$PROJECT" --commit-dirty=true
else
    npx wrangler@latest pages deploy site --project-name "$PROJECT" --commit-dirty=true
fi

echo
echo "Deployed! Your URL will be:"
echo "  https://$PROJECT.pages.dev"
