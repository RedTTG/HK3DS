#!/bin/bash
# bot.sh Room seed frames
room=$1; seed=$2; frames=${3:-2400}
d=/tmp/bot/$room.$seed; rm -rf $d; mkdir -p $d; cd $d; ln -s ${HK3DS:-$(cd "$(dirname "$0")/../.." && pwd)}/romfs romfs
HK_FAST=1 HK_CHECK=1 HK_START="$room -1 -1" HK_SCRIPT="$(python3 ${HK3DS:-$(cd "$(dirname "$0")/../.." && pwd)}/tools/bot_script.py $seed $frames)" timeout 600 ${HK3DS:-$(cd "$(dirname "$0")/../.." && pwd)}/host/hk3ds_host > log 2>&1
echo "$room.$seed rc=$? checks=$(grep -c '^CHECK' log) rooms=$(grep -c enter_room log)"
