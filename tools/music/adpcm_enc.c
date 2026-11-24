/* Endurance Motor Racing 3D - music encoder.
 * Reads 16-bit stereo PCM (22050 Hz) from stdin and writes an .emu file: IMA ADPCM, 4 bits per
 * sample, in blocks the PSP can stream and decode cheaply while the menus run.
 *
 *   ffmpeg -i song.ogg -ac 2 -ar 22050 -f s16le - | adpcm_enc out.emu
 *
 * .emu: 'EMU1' u32 rate, u32 channels (2), u32 frames, u32 blockFrames (2048), u32 pad[3]
 *       blocks: per channel { s16 predictor, u8 index, u8 0 }, then per channel blockFrames/2 bytes
 *       (low nibble first). Block = 8 + 2048 bytes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const int STEP[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int IDX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

typedef struct { int pred, index; } St;

static int enc(St* s, int x) {
    int step = STEP[s->index], diff = x - s->pred, code = 0;
    if (diff < 0) { code = 8; diff = -diff; }
    int vd = step >> 3;
    if (diff >= step) { code |= 4; diff -= step; vd += step; }
    step >>= 1; if (diff >= step) { code |= 2; diff -= step; vd += step; }
    step >>= 1; if (diff >= step) { code |= 1; vd += step; }
    s->pred += (code & 8) ? -vd : vd;
    if (s->pred > 32767) s->pred = 32767; if (s->pred < -32768) s->pred = -32768;
    s->index += IDX[code]; if (s->index < 0) s->index = 0; if (s->index > 88) s->index = 88;
    return code;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: adpcm_enc out.emu [channels 1|2] < pcm (s16le 22050 Hz)\n"); return 1; }
    int CH = argc > 2 ? atoi(argv[2]) : 2;
    if (CH != 1 && CH != 2) CH = 2;
    size_t cap = 1 << 24, n = 0;
    int16_t* pcm = malloc(cap * 2);
    size_t r;
    while ((r = fread(pcm + n, 2, cap - n, stdin)) > 0) { n += r; if (n == cap) { cap *= 2; pcm = realloc(pcm, cap * 2); } }
    uint32_t frames = (uint32_t)(n / CH), BF = 2048;
    FILE* f = fopen(argv[1], "wb");
    uint32_t hdr[8] = {0x31554D45u, 22050, (uint32_t)CH, frames, BF, 0, 0, 0};
    fwrite(hdr, 4, 8, f);
    St st[2] = {{0, 0}, {0, 0}};
    uint8_t buf[8 + 2048];
    size_t bsz = (size_t)CH * (4 + 1024);
    for (uint32_t b = 0; b < frames; b += BF) {
        for (int c = 0; c < CH; c++) {
            int16_t p = (int16_t)st[c].pred;
            memcpy(buf + c * 4, &p, 2); buf[c * 4 + 2] = (uint8_t)st[c].index; buf[c * 4 + 3] = 0;
        }
        for (int c = 0; c < CH; c++) {
            uint8_t* o = buf + CH * 4 + c * (BF / 2);
            for (uint32_t i = 0; i < BF; i += 2) {
                int a = b + i < frames ? pcm[(b + i) * CH + c] : 0;
                int d = b + i + 1 < frames ? pcm[(b + i + 1) * CH + c] : 0;
                int lo = enc(&st[c], a), hi = enc(&st[c], d);
                o[i / 2] = (uint8_t)(lo | (hi << 4));
            }
        }
        fwrite(buf, 1, bsz, f);
    }
    fclose(f);
    fprintf(stderr, "%s: %u frames (%.1f s)\n", argv[1], frames, frames / 22050.0);
    return 0;
}
