#include "meta.h"
#include "../coding/coding.h"

#define get_id32be(s) (\
(uint32_t)((s)[0]) << 24 | \
(uint8_t)((s)[1]) << 16 | \
(uint8_t)((s)[2]) << 8  | \
(uint8_t)((s)[3])       \
)

typedef struct {
    uint32_t id;
    uint32_t magic_id;
    const char* extension;
    VGMSTREAM* (*init_vgmstream)(STREAMFILE*);
} srcd_container_t;

static const srcd_container_t srcd_containers[] = {
    {get_id32be("wav "), get_id32be("RIFF"), "wav",  init_vgmstream_riff},
    {get_id32be("ogg "), get_id32be("OggS"), "ogg",  init_vgmstream_ogg_vorbis}
};
#undef get_id32be

/* srcd - Capcom RE Engine */
VGMSTREAM* init_vgmstream_srcd(STREAMFILE* sf) {
    VGMSTREAM* vgmstream = NULL;
    STREAMFILE* subfile = NULL;
    off_t start_offset = 0;
    int loop_flag = 0;
    int32_t loop_start_sample = 0, loop_end_sample = 0;

    if (!is_id32be(0x00, sf, "srcd"))
        return NULL;

    if (!check_extensions(sf, "srcd,asrc,14,21,26,31"))
        return NULL;

    {
        enum versions { VERSION_31, VERSION_21_26, VERSION_14, VERSION_UNKNOWN };
        enum versions ver = VERSION_UNKNOWN;

        //v31 - AJ_AAT
        if (read_u32le(0x18, sf) > 0x02) {
            ver = VERSION_31;
        }
        //v21 - CAS2 - 0x41,0x49
        //v26 - GTPD - 0x46,0x4E
        else if (read_u32le(0x41, sf) == 0x49 || read_u32le(0x46, sf) == 0x4E) {
            ver = VERSION_21_26;
        }
        //v14 - CAS
        else if (read_u8(0x3A, sf) == 0x42) {
            ver = VERSION_14;
        }

        switch (ver) {
            case VERSION_31:
                loop_flag         = read_u8(0x34, sf);
                loop_start_sample = read_u32le(0x35, sf);
                loop_end_sample   = read_u32le(0x39, sf);
                break;

            case VERSION_21_26:
                loop_flag         = read_u8(0x2C, sf);
                loop_start_sample = read_u32le(0x2D, sf);
                loop_end_sample   = read_u32le(0x31, sf);
                break;

            case VERSION_14:
                loop_flag         = read_u8(0x28, sf);
                loop_start_sample = read_u32le(0x29, sf);
                loop_end_sample   = read_u32le(0x2D, sf);
                break;

            default:
                VGM_LOG("SRCD: Unknown version, disabling loop\n");
                loop_flag = 0;
                break;
        }
    }

    /* Find container info from the table */
    const srcd_container_t* container = NULL;
    uint32_t container_type_id = read_u32be(0x0C, sf);
    for (int i = 0; i < (sizeof(srcd_containers) / sizeof(srcd_container_t)); i++) {
        if (srcd_containers[i].id == container_type_id) {
            container = &srcd_containers[i];
            break;
        }
    }

    if (!container) {
        VGM_LOG("SRCD: Unrecognized container type 0x%08X\n", container_type_id);
        goto fail;
    }

    {
        const off_t scan_start = 0x40;
        const size_t scan_size = 0x100; //Should be small
        off_t offset;

        for (offset = scan_start; offset < scan_start + scan_size; offset++) {
            if (read_u32be(offset, sf) == container->magic_id) {
                start_offset = offset;
                break;
            }
        }

        if (offset == scan_start + scan_size)
            goto fail;
    }

    subfile = setup_subfile_streamfile(sf, start_offset, get_streamfile_size(sf) - start_offset, container->extension);
    if (!subfile) goto fail;

    vgmstream = container->init_vgmstream(subfile);
    if (!vgmstream) goto fail;

    vgmstream->meta_type = meta_SRCD;

    vgmstream_force_loop(vgmstream, loop_flag, loop_start_sample, loop_end_sample);

    close_streamfile(subfile);
    return vgmstream;

fail:
    close_streamfile(subfile);
    close_vgmstream(vgmstream);
    return NULL;
}
