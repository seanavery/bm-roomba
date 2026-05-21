#!/bin/bash
cd "$(dirname "$(realpath "$0")")"
exec python3 main.py "$@"
