#!/bin/bash
# test_api_key.sh — Verify your Anthropic Admin API key works
#
# Usage:
#   ./scripts/test_api_key.sh <your-admin-api-key>
#   ./scripts/test_api_key.sh sk-ant-admin-...
#
# This calls the same endpoint the ESP32 will use and shows today's token usage.

set -e

API_KEY="${1:-}"

if [ -z "$API_KEY" ]; then
    echo "Usage: $0 <anthropic-admin-api-key>"
    echo ""
    echo "Get your key from: https://console.anthropic.com/settings/admin-keys"
    exit 1
fi

TODAY=$(date -u +%Y-%m-%d)
echo "Testing Anthropic Admin API key..."
echo "Date: $TODAY"
echo ""

RESPONSE=$(curl -s -w "\n%{http_code}" \
    "https://api.anthropic.com/v1/organizations/usage?start_date=${TODAY}&end_date=${TODAY}" \
    -H "x-api-key: ${API_KEY}" \
    -H "anthropic-version: 2023-06-01")

HTTP_CODE=$(echo "$RESPONSE" | tail -1)
BODY=$(echo "$RESPONSE" | sed '$d')

if [ "$HTTP_CODE" -eq 200 ]; then
    echo "API key is valid! (HTTP $HTTP_CODE)"
    echo ""
    echo "Response:"
    echo "$BODY" | python3 -m json.tool 2>/dev/null || echo "$BODY"
else
    echo "API key FAILED (HTTP $HTTP_CODE)"
    echo ""
    echo "Response:"
    echo "$BODY" | python3 -m json.tool 2>/dev/null || echo "$BODY"
    echo ""
    echo "Common issues:"
    echo "  - 401: Invalid API key. Make sure it starts with 'sk-ant-admin-'"
    echo "  - 403: Key doesn't have admin permissions"
    echo "  - 404: Wrong endpoint (key may be a regular API key, not admin)"
    exit 1
fi
