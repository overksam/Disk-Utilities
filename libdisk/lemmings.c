/******************************************************************************
 * disk/lemmings.c
 * 
 * Custom format as used by Lemmings.
 * 
 * Written in 2011 by Keir Fraser
 * 
 * RAW TRACK LAYOUT:
 *  u16 0x4489,0x552a,0xaaaaa :: Track header
 *  6 back-to-back sectors (no gaps)
 * Decoded sector:
 *  u16 csum       :: sum of all 16-bit data words
 *  u16 data[512]
 * MFM encoding of sectors:
 *  u16 data -> u16 mfm_even,mfm_odd (i.e., sequence of interleaved e/o words)
 * Timings:
 *  Despite storing 6kB of data, minimal metadata means this is not stored
 *  on a long track. Cell timing is 2us as usual.
 * 
 * TRKTYP_lemmings data layout:
 *  u8 sector_data[6][1024]
 */

#include <libdisk/util.h>
#include "private.h"

#include <arpa/inet.h>

static void *lemmings_write_mfm(
    unsigned int tracknr, struct track_header *th, struct stream *s)
{
    char *block;
    unsigned int i, j, k, valid_blocks = 0, bad;

    block = memalloc(1024 * 6);
    for ( i = 0; i < 6 * 1024 / 4; i++ )
        memcpy((uint32_t *)block + i, "NLEM", 4);

    while ( (stream_next_bit(s) != -1) &&
            (valid_blocks != ((1u<<6)-1)) )
    {
        uint16_t raw_dat[6*513];
        uint32_t idx_off, nr_valid = 0;

        if ( (uint16_t)s->word != 0x4489 )
            continue;

        idx_off = s->index_offset - 15;

        if ( stream_next_bits(s, 32) == -1 )
            goto done;

        if ( s->word != 0x552aaaaa )
            continue;

        for ( j = 0; j < sizeof(raw_dat)/2; j++ )
        {
            uint16_t e, o;
            if ( stream_next_bits(s, 32) == -1 )
                goto done;
            e = s->word >> 16;
            o = s->word;
            raw_dat[j] = htons(((e & 0x5555u) << 1) | (o & 0x5555u));
        }

        for ( j = 0; j < 6; j++ )
        {
            uint16_t *sec = &raw_dat[j*513];
            uint16_t csum = ntohs(*sec++), c = 0;
            for ( k = 0; k < 512; k++ )
                c += ntohs(sec[k]);
            if ( c == csum )
            {
                memcpy(&block[j*1024], sec, 1024);
                valid_blocks |= 1u << j;
                nr_valid++;
            }
        }

        if ( nr_valid )
            th->data_bitoff = idx_off;
    }

done:
    if ( valid_blocks == 0 )
    {
        free(block);
        return NULL;
    }

    th->bytes_per_sector = 1024;
    th->nr_sectors = 6;
    th->len = th->nr_sectors * th->bytes_per_sector;
    write_valid_sector_map(th, valid_blocks);

    return block;
}

static void lemmings_read_mfm(
    unsigned int tracknr, struct track_buffer *tbuf,
    struct track_header *th, void *data)
{
    uint32_t valid_sectors = track_valid_sector_map(th);
    uint16_t *dat = data;
    uint16_t *mfm = memalloc(6 + 6*513*2*2);
    unsigned int i, j;

    tbuf->start = th->data_bitoff;
    tbuf->len = th->total_bits;
    tbuf_init(tbuf);

    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_raw, 16, 0x4489);
    tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_all, 16, 0xf000);

    for ( i = 0; i < 6; i++ )
    {
        uint16_t csum = 0;
        for ( j = 0; j < 512; j++ )
            csum += ntohs(dat[j]);
        if ( !(valid_sectors & (1u << i)) )
            csum = ~csum; /* bad checksum for an invalid sector */
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 16, csum);
        tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 16, csum);
        for ( j = 0; j < 512; j++ )
        {
            tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_even, 16, ntohs(*dat));
            tbuf_bits(tbuf, DEFAULT_SPEED, TBUFDAT_odd, 16, ntohs(*dat));
            dat++;
        }
    }

    tbuf_finalise(tbuf);
}

struct track_handler lemmings_handler = {
    .name = "Lemmings",
    .type = TRKTYP_lemmings,
    .write_mfm = lemmings_write_mfm,
    .read_mfm = lemmings_read_mfm
};