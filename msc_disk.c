/* ==========================================================================
 * MSC (Mass Storage Class) — read-only FAT16 virtual disk
 *
 * Presents a 3 MB read-only USB drive with two files:
 *   - parameters.txt   "version=0.1\n"
 *   - index.html        minimal Hello World page
 *
 * No RAM image — sectors are generated on the fly in the read callback.
 *
 * FAT16 disk layout (512 bytes/sector, 6144 sectors = 3 MB):
 *   Sector  0       : Boot sector (BPB)
 *   Sectors 1–6     : FAT #1  (6 sectors)
 *   Sectors 7–12    : FAT #2  (6 sectors, mirror)
 *   Sector  13      : Root directory (16 entries)
 *   Sectors 14+     : Data area (cluster 2 starts here)
 *
 *   Cluster 2 (sectors 14–17) : parameters.txt
 *   Cluster 3 (sectors 18–21) : index.html
 * ========================================================================== */

#include "tusb.h"
#include <string.h>

/* ---- Disk geometry ---- */
/* FAT16 requires >= 4085 data clusters. With 3 MB and 1 sector/cluster we get
 * ~6094 clusters, safely in FAT16 territory.  (4 sectors/cluster would give only
 * ~1532 clusters, which Windows interprets as FAT12.) */
#define DISK_BLOCK_SIZE     512
#define DISK_BLOCK_COUNT    6144   /* 3 MB */
#define SECTORS_PER_CLUSTER 1
#define CLUSTER_SIZE        (SECTORS_PER_CLUSTER * DISK_BLOCK_SIZE)  /* 512 */
#define RESERVED_SECTORS    1
#define NUM_FATS            2
#define FAT_SECTORS         24     /* ceil((6096 entries * 2) / 512) */
#define ROOT_ENTRY_COUNT    16
#define ROOT_DIR_SECTORS    1      /* 16 * 32 / 512 */
#define DATA_START_SECTOR   (RESERVED_SECTORS + NUM_FATS * FAT_SECTORS + ROOT_DIR_SECTORS)  /* 50 */

/* First data cluster is always cluster 2 in FAT */
#define PARAMS_START_CLUSTER 2

/* ---- File contents ---- */
static const char file_parameters[] = "version=" SERVO_VERSION "\n";
#define FILE_PARAMETERS_SIZE (sizeof(file_parameters) - 1)  /* 12 bytes */

/* Embedded from index.htm via CMake objcopy */
extern const uint8_t file_index_html[];
extern const uint8_t file_index_html_end[];
#define FILE_INDEX_HTML_SIZE ((uint32_t)(file_index_html_end - file_index_html))

/* ---- Boot sector (FAT16 BPB) ---- */
static const uint8_t boot_sector[DISK_BLOCK_SIZE] = {
    0xEB, 0x3C, 0x90,                          /* Jump boot code */
    'M','S','D','O','S','5','.','0',            /* OEM name */
    /* Bytes per sector (512) */
    (DISK_BLOCK_SIZE & 0xFF), (DISK_BLOCK_SIZE >> 8),
    SECTORS_PER_CLUSTER,                        /* Sectors per cluster */
    (RESERVED_SECTORS & 0xFF), (RESERVED_SECTORS >> 8), /* Reserved sectors */
    NUM_FATS,                                   /* Number of FATs */
    (ROOT_ENTRY_COUNT & 0xFF), (ROOT_ENTRY_COUNT >> 8), /* Root entry count */
    (DISK_BLOCK_COUNT & 0xFF), (DISK_BLOCK_COUNT >> 8), /* Total sectors 16 */
    0xF8,                                       /* Media type (fixed disk) */
    (FAT_SECTORS & 0xFF), (FAT_SECTORS >> 8),   /* Sectors per FAT */
    0x01, 0x00,                                 /* Sectors per track */
    0x01, 0x00,                                 /* Number of heads */
    0x00, 0x00, 0x00, 0x00,                     /* Hidden sectors */
    0x00, 0x00, 0x00, 0x00,                     /* Total sectors 32 (0 = use 16-bit field) */
    0x80,                                       /* Drive number */
    0x00,                                       /* Reserved */
    0x29,                                       /* Extended boot signature */
    0x78, 0x56, 0x34, 0x12,                     /* Volume serial number */
    'S','M','A','R','T','S','E','R','V','O',' ',/* Volume label (11 chars) */
    'F','A','T','1','6',' ',' ',' ',            /* Filesystem type */
    /* Boot code and padding — zeros until signature */
    [510] = 0x55, [511] = 0xAA
};

/* ---- FAT directory entry helper ---- */
typedef struct {
    char     name[11];     /* 8.3 space-padded */
    uint8_t  attr;         /* 0x01=read-only, 0x08=volume label, 0x20=archive */
    uint8_t  reserved[10];
    uint16_t time;         /* creation time */
    uint16_t date;         /* creation date: 2025-01-01 → ((45<<9)|(1<<5)|1) = 0x5A21 */
    uint16_t cluster;      /* first cluster */
    uint32_t size;         /* file size in bytes */
} __attribute__((packed)) fat_dir_entry_t;

_Static_assert(sizeof(fat_dir_entry_t) == 32, "dir entry must be 32 bytes");

/* Date encoding: 2025-01-01 = ((2025-1980)<<9)|(1<<5)|1 = (45<<9)|(1<<5)|1 */
#define FAT_DATE 0x5A21
#define FAT_TIME 0x0000

/* Helpers — computed at runtime since FILE_INDEX_HTML_SIZE is from extern symbols */
static uint32_t clusters_for(uint32_t file_size) {
    return (file_size + CLUSTER_SIZE - 1) / CLUSTER_SIZE;
}

static uint32_t cluster_to_lba(uint32_t cluster) {
    return DATA_START_SECTOR + (cluster - 2) * SECTORS_PER_CLUSTER;
}

/* ==========================================================================
 * Generate one 512-byte sector on the fly
 * ========================================================================== */
static void generate_sector(uint32_t lba, uint8_t *buf) {
    memset(buf, 0, DISK_BLOCK_SIZE);

    const uint32_t params_clusters = clusters_for(FILE_PARAMETERS_SIZE);
    const uint32_t index_start     = PARAMS_START_CLUSTER + params_clusters;
    const uint32_t index_clusters  = clusters_for(FILE_INDEX_HTML_SIZE);

    if (lba == 0) {
        /* ---- Boot sector ---- */
        memcpy(buf, boot_sector, DISK_BLOCK_SIZE);

    } else if ((lba >= 1 && lba <= FAT_SECTORS) ||
               (lba >= 1 + FAT_SECTORS && lba <= 2 * FAT_SECTORS)) {
        /* ---- FAT #1 or FAT #2 (identical) ---- */
        uint32_t fat_sector = (lba >= 1 + FAT_SECTORS)
                            ? lba - 1 - FAT_SECTORS   /* FAT #2 */
                            : lba - 1;                 /* FAT #1 */

        /* Each FAT sector holds 256 entries (512 / 2).
         * Compute which entry range this sector covers. */
        uint32_t entry_start = fat_sector * (DISK_BLOCK_SIZE / 2);
        uint32_t entry_end   = entry_start + (DISK_BLOCK_SIZE / 2);
        uint16_t *fat = (uint16_t *)buf;

        for (uint32_t e = entry_start; e < entry_end; e++) {
            uint16_t val = 0x0000;  /* free */

            if (e == 0) {
                val = 0xFFF8;  /* media type */
            } else if (e == 1) {
                val = 0xFFFF;  /* EOC marker */
            } else if (e >= PARAMS_START_CLUSTER &&
                       e < PARAMS_START_CLUSTER + params_clusters) {
                /* parameters.txt chain */
                val = (e == PARAMS_START_CLUSTER + params_clusters - 1)
                    ? 0xFFFF : (uint16_t)(e + 1);
            } else if (e >= index_start &&
                       e < index_start + index_clusters) {
                /* index.htm chain */
                val = (e == index_start + index_clusters - 1)
                    ? 0xFFFF : (uint16_t)(e + 1);
            }

            fat[e - entry_start] = val;
        }

    } else if (lba == RESERVED_SECTORS + NUM_FATS * FAT_SECTORS) {
        /* ---- Root directory (sector 13) ---- */
        fat_dir_entry_t *entries = (fat_dir_entry_t *)buf;

        /* Volume label */
        memcpy(entries[0].name, "SMARTSERVO ", 11);
        entries[0].attr = 0x08;
        entries[0].date = FAT_DATE;
        entries[0].time = FAT_TIME;

        /* parameters.txt — read-only */
        memcpy(entries[1].name, "VERSION TXT", 11);
        entries[1].attr = 0x01 | 0x20;
        entries[1].date = FAT_DATE;
        entries[1].time = FAT_TIME;
        entries[1].cluster = PARAMS_START_CLUSTER;
        entries[1].size = FILE_PARAMETERS_SIZE;

        /* index.htm — read-only */
        memcpy(entries[2].name, "INDEX   HTM", 11);
        entries[2].attr = 0x01 | 0x20;
        entries[2].date = FAT_DATE;
        entries[2].time = FAT_TIME;
        entries[2].cluster = index_start;
        entries[2].size = FILE_INDEX_HTML_SIZE;

    } else if (lba >= cluster_to_lba(PARAMS_START_CLUSTER) &&
               lba < cluster_to_lba(PARAMS_START_CLUSTER + params_clusters)) {
        /* ---- parameters.txt data ---- */
        uint32_t byte_offset = (lba - cluster_to_lba(PARAMS_START_CLUSTER)) * DISK_BLOCK_SIZE;
        if (byte_offset < FILE_PARAMETERS_SIZE) {
            uint32_t copy_len = FILE_PARAMETERS_SIZE - byte_offset;
            if (copy_len > DISK_BLOCK_SIZE) copy_len = DISK_BLOCK_SIZE;
            memcpy(buf, file_parameters + byte_offset, copy_len);
        }

    } else if (lba >= cluster_to_lba(index_start) &&
               lba < cluster_to_lba(index_start + index_clusters)) {
        /* ---- index.htm data ---- */
        uint32_t byte_offset = (lba - cluster_to_lba(index_start)) * DISK_BLOCK_SIZE;
        if (byte_offset < FILE_INDEX_HTML_SIZE) {
            uint32_t copy_len = FILE_INDEX_HTML_SIZE - byte_offset;
            if (copy_len > DISK_BLOCK_SIZE) copy_len = DISK_BLOCK_SIZE;
            memcpy(buf, file_index_html + byte_offset, copy_len);
        }
    }
    /* All other sectors: zeros (already memset) */
}

/* ==========================================================================
 * TinyUSB MSC Callbacks
 * ========================================================================== */

/* Invoked when the host queries device capacity */
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    (void)lun;
    *block_count = DISK_BLOCK_COUNT;
    *block_size  = DISK_BLOCK_SIZE;
}

/* Invoked when the host reads sectors */
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    (void)lun;

    /* offset is cumulative bytes already transferred for this SCSI READ command.
     * Advance the LBA accordingly so multi-sector reads return the correct sectors. */
    uint32_t actual_lba = lba + offset / DISK_BLOCK_SIZE;
    uint8_t *buf = (uint8_t *)buffer;
    uint32_t sectors = bufsize / DISK_BLOCK_SIZE;

    for (uint32_t i = 0; i < sectors; i++) {
        generate_sector(actual_lba + i, buf + i * DISK_BLOCK_SIZE);
    }

    return (int32_t)bufsize;
}

/* Invoked when the host writes sectors — reject all writes (read-only) */
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
    return -1;  /* Signal write error */
}

/* Invoked when the host performs SCSI commands we don't handle */
int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsi_cmd[16], void *buffer, uint16_t bufsize) {
    (void)lun; (void)buffer; (void)bufsize;

    /* Reject unknown commands with CHECK CONDITION */
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

/* Invoked on TEST_UNIT_READY — always ready */
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    return true;
}

/* Invoked to check if the medium is write-protected */
bool tud_msc_is_writable_cb(uint8_t lun) {
    (void)lun;
    return false;  /* Read-only */
}

/* Invoked on INQUIRY — return vendor/product identification */
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    (void)lun;
    memcpy(vendor_id,   "Elmot   ", 8);
    memcpy(product_id,  "Smart Servo     ", 16);
    snprintf((char*)product_rev, 5, "%-4s", SERVO_VERSION);
}
