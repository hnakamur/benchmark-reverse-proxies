#!/bin/bash
oha --no-tui --json -c 100 -z 15s --latency-correction --disable-keepalive http://127.0.0.1:3001
