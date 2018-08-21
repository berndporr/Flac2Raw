#!/bin/sh
sox $1 -b 16 -c 1 -r 48k -t flac $2 trim 0 60
