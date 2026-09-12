#!/bin/sh
set -eu

font='/Library/Fonts/Arial Unicode.ttf'
out='UV-SAR-TEAM-stable-release-zh-en.png'

magick sar-web-qr.png -resize 128x128 qr-render.png
magick background.png \
  -fill '#03182df7' -stroke '#19b9ed' -strokewidth 2 -draw 'roundrectangle 42,42 1080,802 24,24' \
  -fill '#f36a13' -stroke none -draw 'roundrectangle 70,70 375,120 25,25' \
  -font "$font" -fill white -pointsize 24 -gravity northwest -annotate +94+79 '正式发布 · STABLE RELEASE' \
  -font "$font" -fill white -pointsize 75 -annotate +70+135 'UV-SAR-TEAM' \
  -font "$font" -fill '#55d5ff' -pointsize 31 -annotate +72+225 'K1 v1.0  ·  K5/K6 v1.1' \
  -stroke '#f36a13' -strokewidth 3 -draw 'line 70,292 1028,292' -stroke none \
  -font "$font" -fill white -pointsize 31 \
  -annotate +95+320 '●  语音通信 + Yaesu ARTS' \
  -annotate +95+375 '●  SAR 测向 + Bandscope' \
  -annotate +95+430 '●  F+7 独立测向训练信标' \
  -annotate +95+485 '●  低电保护 · 失联声光告警 · PTT 超时' \
  -annotate +95+540 '●  Web 刷机、呼号与频道配置' \
  -font "$font" -fill '#9edff5' -pointsize 19 \
  -annotate +735+334 'VOICE + ARTS' \
  -annotate +735+389 'DIRECTION FINDING' \
  -annotate +735+444 'TRAINING BEACON' \
  -annotate +735+499 'FIELD SAFETY' \
  -annotate +735+554 'WEB FLASH & CONFIG' \
  -fill white -draw 'roundrectangle 70,625 212,767 8,8' \
  qr-render.png -geometry +77+632 -composite \
  -font "$font" -fill white -pointsize 28 -annotate +240+641 '扫码使用 · 免费开源' \
  -font "$font" -fill '#55d5ff' -pointsize 24 -annotate +240+687 'sar.trailspud.com' \
  -font "$font" -fill '#b7c9d6' -pointsize 17 -annotate +240+729 'BH1JID · oldpotatoes66@gmail.com' \
  -fill '#031522e0' -stroke '#f36a13' -strokewidth 1 -draw 'roundrectangle 42,1322 1080,1367 10,10' \
  -font "$font" -fill white -stroke none -pointsize 17 -gravity south -annotate +0+38 '本项目不能作为唯一救援通信或定位设备 · NOT A SOLE LIFE-SAFETY SYSTEM' \
  "$out"

rm -f qr-render.png
echo "$out"
