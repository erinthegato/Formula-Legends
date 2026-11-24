#!/bin/bash
# $1 wav, $2 out emu
python3 -W ignore - "$1" <<'P' | ffmpeg -loglevel error -f s16le -ar 44100 -ac 2 -i - -ar 22050 -ac ${3:-2} -f s16le - | "$(dirname "$0")/adpcm_enc" "$2" ${3:-2}
import sys, numpy as np, scipy.io.wavfile as w
sr,x=w.read(sys.argv[1])
x=x.astype(np.int16)
if x.ndim==1: x=np.stack([x,x],1)
sys.stdout.buffer.write(x.tobytes())
P
