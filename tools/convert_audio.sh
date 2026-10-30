#!/bin/sh
# Converts the original mp3 sound effects to ogg (decoded natively by SDL2_mixer on PS Vita)
# and points assets/objects.json at the ogg files.   usage: tools/convert_audio.sh [assets-dir]
set -e
A="${1:-$(dirname "$0")/../assets}"
find "$A/sounds" -name '*.mp3' | while read -r f; do
  ffmpeg -nostdin -loglevel error -y -i "$f" -c:a libvorbis -q:a 4 "${f%.mp3}.ogg" && rm "$f"
done
sed -i 's/\.mp3"/.ogg"/g' "$A/objects.json"
echo "converted: $(find "$A/sounds" -name '*.ogg' | wc -l) ogg files"
