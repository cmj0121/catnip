/* host_png.c - see host_png.h. */
#include "host_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t crc32_of(const uint8_t *p, size_t n, uint32_t crc)
{
    static uint32_t table[256];
    static int built;
    size_t i;

    if (!built) {
        for (uint32_t k = 0; k < 256; k++) {
            uint32_t c = k;
            for (int b = 0; b < 8; b++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[k] = c;
        }
        built = 1;
    }
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void be32(uint8_t *out, uint32_t v)
{
    out[0] = (uint8_t)(v >> 24);
    out[1] = (uint8_t)(v >> 16);
    out[2] = (uint8_t)(v >> 8);
    out[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *tag, const uint8_t *data, size_t n)
{
    uint8_t hdr[8];
    uint8_t crc[4];
    uint32_t c;

    be32(hdr, (uint32_t)n);
    memcpy(hdr + 4, tag, 4);
    fwrite(hdr, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    c = crc32_of((const uint8_t *)tag, 4, 0);
    if (n) c = crc32_of(data, n, c);
    be32(crc, c);
    fwrite(crc, 1, 4, f);
}

bool catnip_host_png_write(const char *path, int w, int h, const uint16_t *px)
{
    FILE *f;
    uint8_t ihdr[13];
    uint8_t *raw; /* one filter byte then RGB per row */
    uint8_t *z;   /* the zlib stream: header, stored blocks, adler */
    size_t raw_n, z_n, off, i;
    uint32_t a = 1, b = 0;

    if (!path || !px || w <= 0 || h <= 0) return false;
    f = fopen(path, "wb");
    if (!f) return false;

    raw_n = (size_t)h * (1 + (size_t)w * 3);
    raw = (uint8_t *)malloc(raw_n);
    if (!raw) {
        fclose(f);
        return false;
    }
    off = 0;
    for (int y = 0; y < h; y++) {
        raw[off++] = 0; /* filter: none. Nothing here is being compressed. */
        for (int x = 0; x < w; x++) {
            uint16_t c = px[(size_t)y * w + x];
            /* RGB565 out to eight bits a channel, with the top bits repeated
             * into the bottom so that full-scale stays full-scale. */
            uint8_t r = (uint8_t)((c >> 11) & 0x1F);
            uint8_t g = (uint8_t)((c >> 5) & 0x3F);
            uint8_t bl = (uint8_t)(c & 0x1F);
            raw[off++] = (uint8_t)((r << 3) | (r >> 2));
            raw[off++] = (uint8_t)((g << 2) | (g >> 4));
            raw[off++] = (uint8_t)((bl << 3) | (bl >> 2));
        }
    }

    /* Deflate, stored: 2 bytes of zlib header, then 65535-byte blocks each with
     * a 5-byte header, then the Adler-32 of the raw bytes. */
    {
        size_t blocks = (raw_n + 65534) / 65535;
        z_n = 2 + blocks * 5 + raw_n + 4;
        z = (uint8_t *)malloc(z_n);
        if (!z) {
            free(raw);
            fclose(f);
            return false;
        }
        z[0] = 0x78;
        z[1] = 0x01;
        off = 2;
        for (size_t done = 0; done < raw_n;) {
            size_t n = raw_n - done > 65535 ? 65535 : raw_n - done;
            z[off++] = (done + n >= raw_n) ? 1 : 0;
            z[off++] = (uint8_t)(n & 0xFF);
            z[off++] = (uint8_t)(n >> 8);
            z[off++] = (uint8_t)(~n & 0xFF);
            z[off++] = (uint8_t)((~n >> 8) & 0xFF);
            memcpy(z + off, raw + done, n);
            off += n;
            done += n;
        }
        for (i = 0; i < raw_n; i++) {
            a = (a + raw[i]) % 65521;
            b = (b + a) % 65521;
        }
        be32(z + off, (b << 16) | a);
        off += 4;
        z_n = off;
    }

    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  /* bits per channel */
    ihdr[9] = 2;  /* truecolour */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* filter method */
    ihdr[12] = 0; /* no interlace */
    chunk(f, "IHDR", ihdr, sizeof(ihdr));
    chunk(f, "IDAT", z, z_n);
    chunk(f, "IEND", NULL, 0);

    free(z);
    free(raw);
    fclose(f);
    return true;
}
