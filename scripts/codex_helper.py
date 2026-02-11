#!/usr/bin/env python3
"""
Helper script to interact with gpt-5.3-codex via OpenRouter API.
Used for decompilation analysis coordination.
"""

import os
import sys
import json
import argparse
from pathlib import Path

try:
    import requests
except ImportError:
    print("Error: requests library not installed. Run: pip install requests", file=sys.stderr)
    sys.exit(1)


def load_env():
    """Load environment variables from .env file"""
    env_file = Path(__file__).parent.parent / ".env"
    if env_file.exists():
        with open(env_file) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#'):
                    key, _, value = line.partition('=')
                    os.environ[key.strip()] = value.strip()


def call_codex(prompt: str, system_prompt: str = None, model: str = "openai/gpt-5-codex", reasoning_effort: str = "high") -> dict:
    """
    Call gpt-5.3-codex via OpenRouter API

    Args:
        prompt: The user prompt to send
        system_prompt: Optional system prompt for context
        model: Model identifier (default: openai/gpt-5-codex)
        reasoning_effort: Reasoning effort level (low/medium/high)

    Returns:
        dict with 'content' (response text) and 'raw' (full API response)
    """
    load_env()

    api_key = os.environ.get('OPENROUTER_API_KEY')
    if not api_key:
        raise ValueError("OPENROUTER_API_KEY not found in environment")

    url = "https://openrouter.ai/api/v1/chat/completions"

    messages = []
    if system_prompt:
        messages.append({"role": "system", "content": system_prompt})
    messages.append({"role": "user", "content": prompt})

    headers = {
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json",
        "HTTP-Referer": "https://github.com/milohax/dc3-decomp",
        "X-Title": "DC3 Decomp Codex Helper"
    }

    payload = {
        "model": model,
        "messages": messages,
    }

    # Add reasoning effort if using gpt-5 codex
    if "gpt-5" in model and reasoning_effort:
        payload["reasoning"] = {"effort": reasoning_effort}

    response = requests.post(url, headers=headers, json=payload, timeout=120)
    response.raise_for_status()

    data = response.json()

    # Extract the response content
    content = data['choices'][0]['message']['content']

    return {
        'content': content,
        'raw': data,
        'model': data.get('model', model),
        'usage': data.get('usage', {})
    }


def main():
    parser = argparse.ArgumentParser(description="Call gpt-5.3-codex via OpenRouter for decompilation analysis")
    parser.add_argument('prompt', nargs='?', help='Prompt to send (or read from stdin)')
    parser.add_argument('--system', help='System prompt for context')
    parser.add_argument('--model', default='openai/gpt-5-codex', help='Model to use')
    parser.add_argument('--reasoning', default='high', choices=['low', 'medium', 'high'], help='Reasoning effort')
    parser.add_argument('--json', action='store_true', help='Output full JSON response')
    parser.add_argument('--file', help='Read prompt from file')

    args = parser.parse_args()

    # Get prompt from various sources
    if args.file:
        with open(args.file) as f:
            prompt = f.read()
    elif args.prompt:
        prompt = args.prompt
    elif not sys.stdin.isatty():
        prompt = sys.stdin.read()
    else:
        parser.print_help()
        sys.exit(1)

    try:
        result = call_codex(prompt, system_prompt=args.system, model=args.model, reasoning_effort=args.reasoning)

        if args.json:
            print(json.dumps(result, indent=2))
        else:
            print(result['content'])

            # Print usage stats to stderr
            usage = result.get('usage', {})
            if usage:
                print(f"\n[Usage: {usage.get('prompt_tokens', 0)} in, {usage.get('completion_tokens', 0)} out]", file=sys.stderr)

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
