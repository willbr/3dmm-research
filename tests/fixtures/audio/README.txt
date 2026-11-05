These are generated test tones, not clips from community movies or retail data.
FFmpeg was used once to encode the fixtures and produce independent reference
PCM. Running the tests does not require FFmpeg or installed audio codecs.

ffmpeg -f lavfi -i sine=frequency=440:sample_rate=8000:duration=0.2 -c:a libgsm_ms -bitexact gsm.wav
ffmpeg -f lavfi -i sine=frequency=659:sample_rate=22050:duration=0.2 -ac 2 -c:a libmp3lame -b:a 64k -bitexact mp3.wav
ffmpeg -c:a libgsm_ms -i gsm.wav -f s16le -c:a pcm_s16le gsm-reference.s16
ffmpeg -i mp3.wav -f s16le -c:a pcm_s16le mp3-reference.s16

gsm.wav: 8 kHz mono WAV49, 1,600 samples, five 65-byte paired GSM blocks.
mp3.wav: 22.05 kHz stereo MP3-in-WAV, 5,760 decoded frames including encoder padding.
GSM decoding must match the reference exactly; MP3 allows two signed-16-bit
sample levels of decoder rounding difference.
