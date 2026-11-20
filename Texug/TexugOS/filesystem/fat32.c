#include <stdint.h>

#include "ata.h"
#include "fat32.h"

#define SECTOR_SIZE 512u
#define FAT32_PARTITION 0x0bu
#define FAT32_LBA_PARTITION 0x0cu
#define FAT32_END_OF_CHAIN 0x0ffffff8u
#define FAT32_END_MARKER 0x0fffffffu
#define FAT32_UNKNOWN 0xffffffffu
#define DIRECTORY_ENTRY_SIZE 32u
#define ENTRIES_PER_SECTOR (SECTOR_SIZE / DIRECTORY_ENTRY_SIZE)
#define FAT32_LFN_MAX_LENGTH 255u
#define FAT32_LFN_MAX_ENTRIES 20u

struct directory_slot {
    uint32_t sector;
    uint16_t offset;
};

struct fat32_volume {
    uint32_t start_sector;
    uint32_t sector_count;
    uint32_t fat_start;
    uint32_t fat_sectors;
    uint32_t data_start;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t fsinfo_sector;
    uint32_t backup_sector;
    uint32_t free_cluster_hint;
    uint32_t next_cluster_hint;
    uint8_t fsinfo_valid;
    uint8_t backup_fsinfo_valid;
};

struct directory_location {
    uint32_t sector;
    uint16_t offset;
    uint8_t raw[DIRECTORY_ENTRY_SIZE];
    uint8_t lfn_count;
    struct directory_slot lfn_slots[FAT32_LFN_MAX_ENTRIES];
};

static struct fat32_volume volume;
static uint8_t sector_buffer[SECTOR_SIZE];
static int mounted;

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void fill_bytes(uint8_t *data, uint8_t value, uint32_t size)
{
    for (uint32_t index = 0; index < size; ++index) {
        data[index] = value;
    }
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       uint32_t size)
{
    for (uint32_t index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
}

static int equal_bytes(const uint8_t *left, const uint8_t *right,
                      uint32_t size)
{
    for (uint32_t index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

static int is_valid_cluster(uint32_t cluster)
{
    return cluster >= 2u && cluster < volume.cluster_count + 2u;
}

static uint32_t cluster_sector(uint32_t cluster)
{
    return volume.data_start +
           (cluster - 2u) * volume.sectors_per_cluster;
}

static int make_short_name(const char *name, uint8_t output[11])
{
    fill_bytes(output, ' ', 11u);
    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    if (name[0] == '.' && (name[1] == '\0' ||
                           (name[1] == '.' && name[2] == '\0'))) {
        output[0] = '.';
        if (name[1] == '.') {
            output[1] = '.';
        }
        return 1;
    }

    uint32_t base_length = 0;
    uint32_t extension_length = 0;
    int in_extension = 0;
    for (uint32_t index = 0; name[index] != '\0'; ++index) {
        uint8_t character = (uint8_t)name[index];
        if (character == '.') {
            if (in_extension || base_length == 0u) {
                return 0;
            }
            in_extension = 1;
            continue;
        }
        if (character <= ' ' || character >= 0x7fu ||
            character == '/' || character == '\\' || character == ':' ||
            character == '*' || character == '?' || character == '"' ||
            character == '<' || character == '>' || character == '|') {
            return 0;
        }

        if (in_extension) {
            if (extension_length == 3u) {
                return 0;
            }
            output[8u + extension_length++] =
                character >= 'a' && character <= 'z'
                    ? (uint8_t)(character - 'a' + 'A')
                    : character;
        } else {
            if (base_length == 8u) {
                return 0;
            }
            output[base_length++] =
                character >= 'a' && character <= 'z'
                    ? (uint8_t)(character - 'a' + 'A')
                    : character;
        }
    }
    return base_length != 0u;
}

static int valid_long_name(const char *name, uint32_t *length)
{
    if (name == 0 || name[0] == '\0') {
        return 0;
    }

    uint32_t index = 0u;
    while (name[index] != '\0') {
        uint8_t character = (uint8_t)name[index];
        if (index == FAT32_LFN_MAX_LENGTH || character < ' ' ||
            character >= 0x7fu || character == '"' || character == '*' ||
            character == '/' || character == ':' || character == '<' ||
            character == '>' || character == '?' || character == '\\' ||
            character == '|') {
            return 0;
        }
        ++index;
    }
    if (name[index - 1u] == '.' || name[index - 1u] == ' ') {
        return 0;
    }
    if (length != 0) {
        *length = index;
    }
    return 1;
}

static int ascii_name_equal(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        uint8_t a = (uint8_t)*left++;
        uint8_t b = (uint8_t)*right++;
        if (a >= 'a' && a <= 'z') {
            a = (uint8_t)(a - 'a' + 'A');
        }
        if (b >= 'a' && b <= 'z') {
            b = (uint8_t)(b - 'a' + 'A');
        }
        if (a != b) {
            return 0;
        }
    }
    return *left == '\0' && *right == '\0';
}

static void format_short_name(const uint8_t input[11], char output[13])
{
    uint32_t output_index = 0;
    for (uint32_t index = 0; index < 8u && input[index] != ' '; ++index) {
        output[output_index++] = (char)input[index];
    }

    if (input[8] != ' ') {
        output[output_index++] = '.';
        for (uint32_t index = 8; index < 11u && input[index] != ' '; ++index) {
            output[output_index++] = (char)input[index];
        }
    }
    output[output_index] = '\0';
}

static int fat_read(uint32_t cluster, uint32_t *value)
{
    if (!is_valid_cluster(cluster) || value == 0) {
        return 0;
    }

    uint32_t offset = cluster * 4u;
    uint32_t sector = volume.fat_start + offset / SECTOR_SIZE;
    if (!ata_read_sector(sector, sector_buffer)) {
        return 0;
    }
    *value = read_u32(&sector_buffer[offset % SECTOR_SIZE]) & 0x0fffffffu;
    return 1;
}

static int next_cluster(uint32_t cluster, uint32_t *next, int *at_end)
{
    if (next == 0 || at_end == 0 || !fat_read(cluster, next)) {
        return 0;
    }
    if (*next >= FAT32_END_OF_CHAIN) {
        *at_end = 1;
        return 1;
    }
    if (!is_valid_cluster(*next)) {
        return 0;
    }
    *at_end = 0;
    return 1;
}

static int valid_cluster_chain(uint32_t first_cluster)
{
    uint32_t slow = first_cluster;
    uint32_t fast = first_cluster;
    if (!is_valid_cluster(first_cluster)) {
        return 0;
    }

    for (uint32_t count = 0u; count < volume.cluster_count; ++count) {
        uint32_t next;
        int at_end;
        if (!next_cluster(slow, &next, &at_end)) {
            return 0;
        }
        if (at_end) {
            return 1;
        }
        slow = next;

        for (uint32_t step = 0u; step < 2u; ++step) {
            if (!next_cluster(fast, &next, &at_end)) {
                return 0;
            }
            if (at_end) {
                return 1;
            }
            fast = next;
        }
        if (slow == fast) {
            return 0;
        }
    }
    return 0;
}

static int fat_write(uint32_t cluster, uint32_t value)
{
    if (!is_valid_cluster(cluster)) {
        return 0;
    }

    uint32_t offset = cluster * 4u;
    uint32_t sector = volume.fat_start + offset / SECTOR_SIZE;
    if (!ata_read_sector(sector, sector_buffer)) {
        return 0;
    }
    value = (read_u32(&sector_buffer[offset % SECTOR_SIZE]) & 0xf0000000u) |
            (value & 0x0fffffffu);
    write_u32(&sector_buffer[offset % SECTOR_SIZE], value);

    for (uint32_t copy = 0; copy < volume.fat_count; ++copy) {
        if (!ata_write_sector(sector + copy * volume.fat_sectors,
                              sector_buffer)) {
            return 0;
        }
    }
    return 1;
}

static int update_fsinfo(void)
{
    if (volume.fsinfo_valid == 0u) {
        return 1;
    }

    uint32_t sectors[2] = {
        volume.start_sector + volume.fsinfo_sector,
        volume.start_sector + volume.backup_sector + volume.fsinfo_sector,
    };
    uint32_t copies = volume.backup_fsinfo_valid != 0u ? 2u : 1u;
    for (uint32_t index = 0; index < copies; ++index) {
        if (!ata_read_sector(sectors[index], sector_buffer)) {
            return 0;
        }
        if (read_u32(&sector_buffer[0]) != 0x41615252u ||
            read_u32(&sector_buffer[484]) != 0x61417272u) {
            return 0;
        }
        write_u32(&sector_buffer[488], volume.free_cluster_hint);
        write_u32(&sector_buffer[492], volume.next_cluster_hint);
        if (!ata_write_sector(sectors[index], sector_buffer)) {
            return 0;
        }
    }
    return 1;
}

static int allocate_cluster(uint32_t *allocated)
{
    if (allocated == 0 || volume.cluster_count == 0u) {
        return 0;
    }

    uint32_t start = volume.next_cluster_hint;
    if (!is_valid_cluster(start)) {
        start = 2u;
    }
    for (uint32_t offset = 0; offset < volume.cluster_count; ++offset) {
        uint32_t cluster =
            2u + ((start - 2u + offset) % volume.cluster_count);
        uint32_t value;
        if (!fat_read(cluster, &value)) {
            return 0;
        }
        if (value != 0u) {
            continue;
        }
        if (!fat_write(cluster, FAT32_END_MARKER)) {
            if (!fat_write(cluster, 0u)) {
                mounted = 0;
            }
            return 0;
        }

        uint32_t previous_free_hint = volume.free_cluster_hint;
        uint32_t previous_next_hint = volume.next_cluster_hint;
        if (volume.free_cluster_hint != FAT32_UNKNOWN) {
            volume.free_cluster_hint = volume.free_cluster_hint != 0u
                ? volume.free_cluster_hint - 1u : FAT32_UNKNOWN;
        }
        volume.next_cluster_hint =
            cluster + 1u < volume.cluster_count + 2u ? cluster + 1u : 2u;
        if (!update_fsinfo()) {
            int rollback_succeeded = fat_write(cluster, 0u);
            volume.free_cluster_hint = previous_free_hint;
            volume.next_cluster_hint = previous_next_hint;
            if (rollback_succeeded && !update_fsinfo()) {
                rollback_succeeded = 0;
            }
            if (!rollback_succeeded) {
                mounted = 0;
            }
            return 0;
        }
        *allocated = cluster;
        return 1;
    }
    return 0;
}

static int release_chain(uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;
    uint32_t chain_length = 0u;
    int at_end = 0;
    while (chain_length < volume.cluster_count) {
        uint32_t next;
        if (!next_cluster(cluster, &next, &at_end)) {
            return 0;
        }
        ++chain_length;
        if (at_end) {
            break;
        }
        cluster = next;
    }
    if (at_end == 0) {
        return 0;
    }

    cluster = first_cluster;
    for (uint32_t count = 0u; count < chain_length; ++count) {
        uint32_t next;
        if (!next_cluster(cluster, &next, &at_end) ||
            !fat_write(cluster, 0u)) {
            return 0;
        }
        if (volume.free_cluster_hint != FAT32_UNKNOWN) {
            if (volume.free_cluster_hint < volume.cluster_count) {
                ++volume.free_cluster_hint;
            } else {
                volume.free_cluster_hint = FAT32_UNKNOWN;
            }
        }
        if (at_end) {
            volume.next_cluster_hint = cluster;
            break;
        }
        cluster = next;
    }
    return update_fsinfo();
}

struct lfn_state {
    char name[FAT32_LFN_MAX_LENGTH + 1u];
    struct directory_slot slots[FAT32_LFN_MAX_ENTRIES];
    uint8_t slot_count;
    uint8_t expected_ordinal;
    uint8_t checksum;
    uint8_t active;
    uint8_t valid;
};

static uint8_t short_name_checksum(const uint8_t short_name[11])
{
    uint8_t checksum = 0u;
    for (uint32_t index = 0u; index < 11u; ++index) {
        checksum = (uint8_t)(((checksum & 1u) << 7) +
                             (checksum >> 1) + short_name[index]);
    }
    return checksum;
}

static void reset_lfn_state(struct lfn_state *state)
{
    state->slot_count = 0u;
    state->expected_ordinal = 0u;
    state->active = 0u;
    state->valid = 0u;
}

static void decode_lfn_slot(struct lfn_state *state,
                            const uint8_t raw[DIRECTORY_ENTRY_SIZE],
                            uint32_t sector, uint16_t offset)
{
    static const uint8_t character_offsets[13] = {
        1u, 3u, 5u, 7u, 9u, 14u, 16u,
        18u, 20u, 22u, 24u, 28u, 30u,
    };
    uint8_t order = raw[0] & 0x1fu;
    int last = (raw[0] & 0x40u) != 0u;
    if ((raw[0] & 0xa0u) != 0u || raw[11] != 0x0fu || raw[12] != 0u ||
        read_u16(&raw[26]) != 0u || order == 0u ||
        order > FAT32_LFN_MAX_ENTRIES) {
        reset_lfn_state(state);
        return;
    }

    if (last) {
        reset_lfn_state(state);
        state->active = 1u;
        state->valid = 1u;
        state->expected_ordinal = order;
        state->checksum = raw[13];
        for (uint32_t index = 0u; index <= FAT32_LFN_MAX_LENGTH; ++index) {
            state->name[index] = '\0';
        }
    }
    if (!state->active || state->expected_ordinal != order ||
        state->slot_count == FAT32_LFN_MAX_ENTRIES ||
        state->checksum != raw[13]) {
        reset_lfn_state(state);
        return;
    }

    state->slots[state->slot_count].sector = sector;
    state->slots[state->slot_count].offset = offset;
    ++state->slot_count;
    uint32_t base = ((uint32_t)order - 1u) * 13u;
    int terminated = 0;
    for (uint32_t index = 0u; index < 13u; ++index) {
        uint16_t character = read_u16(&raw[character_offsets[index]]);
        uint32_t position = base + index;
        if (character == 0u) {
            terminated = 1;
            if (position <= FAT32_LFN_MAX_LENGTH) {
                state->name[position] = '\0';
            }
        } else if (character == 0xffffu) {
            if (!terminated && position < FAT32_LFN_MAX_LENGTH) {
                state->valid = 0u;
            }
        } else if (terminated || character < ' ' ||
                   character >= 0x7fu ||
                   position >= FAT32_LFN_MAX_LENGTH) {
            state->valid = 0u;
        } else {
            state->name[position] = (char)character;
        }
    }
    --state->expected_ordinal;
}

static int lfn_matches(const struct lfn_state *state, const char *name,
                       uint8_t checksum)
{
    if (!state->active || !state->valid || state->expected_ordinal != 0u ||
        state->slot_count == 0u || state->checksum != checksum ||
        state->name[0] == '\0') {
        return 0;
    }
    return ascii_name_equal(state->name, name);
}

static void decode_entry(const uint8_t raw[DIRECTORY_ENTRY_SIZE],
                         const struct lfn_state *lfn,
                         struct fat32_entry *entry)
{
    if (lfn != 0 && lfn->active && lfn->valid &&
        lfn->expected_ordinal == 0u &&
        lfn->checksum == short_name_checksum(raw)) {
        uint32_t index = 0u;
        while (index < FAT32_LFN_MAX_LENGTH && lfn->name[index] != '\0') {
            entry->name[index] = lfn->name[index];
            ++index;
        }
        entry->name[index] = '\0';
    } else {
        format_short_name(raw, entry->name);
    }
    entry->attributes = raw[11];
    entry->first_cluster =
        ((uint32_t)read_u16(&raw[20]) << 16) | read_u16(&raw[26]);
    entry->first_cluster &= 0x0fffffffu;
    entry->size = read_u32(&raw[28]);
}

static int find_entry(uint32_t directory, const char *name,
                      struct fat32_entry *entry,
                      struct directory_location *location)
{
    uint8_t short_name[11];
    int has_short_name = make_short_name(name, short_name);
    if (!is_valid_cluster(directory) ||
        (!has_short_name && !valid_long_name(name, 0))) {
        return -1;
    }

    struct lfn_state lfn = {0};
    uint32_t cluster = directory;
    for (uint32_t chain = 0; chain < volume.cluster_count; ++chain) {
        if (!is_valid_cluster(cluster)) {
            return -1;
        }
        uint32_t first_sector = cluster_sector(cluster);
        for (uint32_t sector_index = 0;
             sector_index < volume.sectors_per_cluster; ++sector_index) {
            uint32_t lba = first_sector + sector_index;
            if (!ata_read_sector(lba, sector_buffer)) {
                return -1;
            }
            for (uint32_t index = 0; index < ENTRIES_PER_SECTOR; ++index) {
                uint32_t offset = index * DIRECTORY_ENTRY_SIZE;
                uint8_t first = sector_buffer[offset];
                if (first == 0u) {
                    return 0;
                }
                if (first == 0xe5u) {
                    reset_lfn_state(&lfn);
                    continue;
                }
                if (sector_buffer[offset + 11u] == 0x0fu) {
                    decode_lfn_slot(
                        &lfn, &sector_buffer[offset], lba,
                        (uint16_t)offset);
                    continue;
                }

                int matches_short =
                    has_short_name &&
                    equal_bytes(&sector_buffer[offset], short_name, 11u);
                int matches_long = lfn_matches(
                    &lfn, name, short_name_checksum(&sector_buffer[offset]));
                int has_valid_lfn =
                    lfn.active && lfn.valid && lfn.expected_ordinal == 0u &&
                    lfn.checksum ==
                        short_name_checksum(&sector_buffer[offset]);
                if ((sector_buffer[offset + 11u] & 0x08u) == 0u &&
                    (matches_short || matches_long)) {
                    if (entry != 0) {
                        decode_entry(&sector_buffer[offset], &lfn, entry);
                    }
                    if (location != 0) {
                        location->sector = lba;
                        location->offset = (uint16_t)offset;
                        copy_bytes(location->raw, &sector_buffer[offset],
                                   DIRECTORY_ENTRY_SIZE);
                        location->lfn_count =
                            has_valid_lfn ? lfn.slot_count : 0u;
                        for (uint32_t slot = 0u;
                             slot < location->lfn_count; ++slot) {
                            location->lfn_slots[slot] = lfn.slots[slot];
                        }
                    }
                    return 1;
                }
                reset_lfn_state(&lfn);
            }
        }

        uint32_t next;
        int at_end;
        if (!next_cluster(cluster, &next, &at_end)) {
            return -1;
        }
        if (at_end) {
            return 0;
        }
        cluster = next;
    }
    return -1;
}

static int find_free_slots(uint32_t directory, uint32_t needed,
                           struct directory_slot *slots)
{
    if (!is_valid_cluster(directory) || needed == 0u ||
        needed > FAT32_LFN_MAX_ENTRIES + 1u || slots == 0) {
        return 0;
    }
    if (!valid_cluster_chain(directory)) {
        return 0;
    }

    uint32_t found = 0u;
    int end_reached = 0;
    int cluster_scanned = 0;
    uint32_t cluster = directory;
    for (uint32_t chain = 0; chain < volume.cluster_count; ++chain) {
        if (!is_valid_cluster(cluster)) {
            return 0;
        }
        if (cluster_scanned == 0) {
            uint32_t first_sector = cluster_sector(cluster);
            for (uint32_t sector_index = 0;
                 sector_index < volume.sectors_per_cluster; ++sector_index) {
                uint32_t lba = first_sector + sector_index;
                if (!ata_read_sector(lba, sector_buffer)) {
                    return 0;
                }
                for (uint32_t index = 0; index < ENTRIES_PER_SECTOR; ++index) {
                    uint32_t offset = index * DIRECTORY_ENTRY_SIZE;
                    uint8_t first = sector_buffer[offset];
                    if (first == 0u) {
                        end_reached = 1;
                    }
                    if (end_reached || first == 0xe5u) {
                        slots[found].sector = lba;
                        slots[found].offset = (uint16_t)offset;
                        if (++found == needed) {
                            return 1;
                        }
                    } else {
                        found = 0u;
                    }
                }
            }
        }
        cluster_scanned = 0;

        uint32_t next;
        int at_end;
        if (!next_cluster(cluster, &next, &at_end)) {
            return 0;
        }
        if (at_end) {
            uint32_t extended;
            if (!allocate_cluster(&extended)) {
                return 0;
            }
            if (!fat_write(cluster, extended)) {
                if (!fat_write(cluster, FAT32_END_MARKER) ||
                    !release_chain(extended)) {
                    mounted = 0;
                }
                return 0;
            }
            uint8_t empty_sector[SECTOR_SIZE];
            fill_bytes(empty_sector, 0u, SECTOR_SIZE);
            for (uint32_t index = 0; index < volume.sectors_per_cluster; ++index) {
                if (!ata_write_sector(cluster_sector(extended) + index,
                                      empty_sector)) {
                    if (!fat_write(cluster, FAT32_END_MARKER) ||
                        !release_chain(extended)) {
                        mounted = 0;
                    }
                    return 0;
                }
            }
            uint32_t first_sector = cluster_sector(extended);
            for (uint32_t sector_index = 0u;
                 sector_index < volume.sectors_per_cluster; ++sector_index) {
                for (uint32_t index = 0u; index < ENTRIES_PER_SECTOR; ++index) {
                    if (found == needed) {
                        return 1;
                    }
                    slots[found].sector = first_sector + sector_index;
                    slots[found].offset =
                        (uint16_t)(index * DIRECTORY_ENTRY_SIZE);
                    ++found;
                }
            }
            if (found == needed) {
                return 1;
            }
            cluster = extended;
            cluster_scanned = 1;
            continue;
        }
        cluster = next;
    }
    return 0;
}

static int write_directory_entries(uint32_t directory,
                                   const uint8_t *raw_entries,
                                   uint32_t count)
{
    struct directory_slot slots[FAT32_LFN_MAX_ENTRIES + 1u];
    if (count == 0u || count > FAT32_LFN_MAX_ENTRIES + 1u ||
        !find_free_slots(directory, count, slots)) {
        return 0;
    }

    uint32_t attempted = 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        if (!ata_read_sector(slots[index].sector, sector_buffer)) {
            break;
        }
        copy_bytes(&sector_buffer[slots[index].offset],
                   &raw_entries[index * DIRECTORY_ENTRY_SIZE],
                   DIRECTORY_ENTRY_SIZE);
        attempted = index + 1u;
        if (!ata_write_sector(slots[index].sector, sector_buffer)) {
            break;
        }
    }
    if (attempted == count) {
        return 1;
    }

    int rollback_failed = 0;
    while (attempted != 0u) {
        --attempted;
        if (!ata_read_sector(slots[attempted].sector, sector_buffer)) {
            rollback_failed = 1;
            continue;
        }
        sector_buffer[slots[attempted].offset] = 0xe5u;
        if (!ata_write_sector(slots[attempted].sector, sector_buffer)) {
            rollback_failed = 1;
        }
    }
    if (rollback_failed) {
        mounted = 0;
    }
    return 0;
}

static int write_directory_entry(uint32_t directory,
                                 const uint8_t raw[DIRECTORY_ENTRY_SIZE])
{
    return write_directory_entries(directory, raw, 1u);
}

static void encode_entry(uint8_t raw[DIRECTORY_ENTRY_SIZE],
                         const uint8_t short_name[11], uint8_t attributes,
                         uint32_t first_cluster, uint32_t size)
{
    fill_bytes(raw, 0u, DIRECTORY_ENTRY_SIZE);
    copy_bytes(raw, short_name, 11u);
    raw[11] = attributes;
    raw[20] = (uint8_t)(first_cluster >> 16);
    raw[21] = (uint8_t)(first_cluster >> 24);
    raw[26] = (uint8_t)first_cluster;
    raw[27] = (uint8_t)(first_cluster >> 8);
    write_u32(&raw[28], size);
}

static int make_long_name_alias(uint32_t directory, const char *name,
                                uint32_t length, uint8_t short_name[11])
{
    uint32_t base_end = length;
    uint32_t extension_start = length;
    for (uint32_t index = 0u; index < length; ++index) {
        if (name[index] == '.') {
            base_end = index;
            extension_start = index + 1u;
        }
    }

    char base[7];
    char extension[4];
    uint32_t base_length = 0u;
    uint32_t extension_length = 0u;
    for (uint32_t index = 0u; index < base_end && base_length < 6u; ++index) {
        char character = name[index];
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9')) {
            base[base_length++] =
                character >= 'a' && character <= 'z'
                    ? (char)(character - 'a' + 'A')
                    : character;
        }
    }
    for (uint32_t index = extension_start;
         index < length && extension_length < 3u; ++index) {
        char character = name[index];
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9')) {
            extension[extension_length++] =
                character >= 'a' && character <= 'z'
                    ? (char)(character - 'a' + 'A')
                    : character;
        }
    }
    if (base_length == 0u) {
        base[base_length++] = 'F';
        base[base_length++] = 'I';
        base[base_length++] = 'L';
        base[base_length++] = 'E';
    }

    for (uint32_t number = 1u; number <= 999999u; ++number) {
        char suffix[7];
        uint32_t suffix_length = 0u;
        uint32_t value = number;
        do {
            suffix[suffix_length++] = (char)('0' + value % 10u);
            value /= 10u;
        } while (value != 0u);

        char alias[13];
        uint32_t output = 0u;
        uint32_t prefix_length =
            7u - suffix_length < base_length
                ? 7u - suffix_length
                : base_length;
        for (uint32_t index = 0u; index < prefix_length; ++index) {
            alias[output++] = base[index];
        }
        alias[output++] = '~';
        while (suffix_length != 0u) {
            alias[output++] = suffix[--suffix_length];
        }
        if (extension_length != 0u) {
            alias[output++] = '.';
            for (uint32_t index = 0u; index < extension_length; ++index) {
                alias[output++] = extension[index];
            }
        }
        alias[output] = '\0';
        if (!make_short_name(alias, short_name)) {
            return 0;
        }
        int match = find_entry(directory, alias, 0, 0);
        if (match < 0) {
            return 0;
        }
        if (match == 0) {
            return 1;
        }
    }
    return 0;
}

static int prepare_entry_name(uint32_t directory, const char *name,
                              uint8_t short_name[11], uint32_t *length,
                              int *needs_lfn)
{
    if (!valid_long_name(name, length) ||
        (name[0] == '.' &&
         (name[1] == '\0' ||
          (name[1] == '.' && name[2] == '\0')))) {
        return 0;
    }
    if (make_short_name(name, short_name)) {
        *needs_lfn = 0;
        return 1;
    }
    *needs_lfn = 1;
    return make_long_name_alias(directory, name, *length, short_name);
}

static void encode_lfn_entry(uint8_t raw[DIRECTORY_ENTRY_SIZE],
                             uint32_t ordinal, int last, const char *name,
                             uint32_t length, uint8_t checksum)
{
    static const uint8_t character_offsets[13] = {
        1u, 3u, 5u, 7u, 9u, 14u, 16u,
        18u, 20u, 22u, 24u, 28u, 30u,
    };
    fill_bytes(raw, 0xffu, DIRECTORY_ENTRY_SIZE);
    raw[0] = (uint8_t)ordinal | (last ? 0x40u : 0u);
    raw[11] = 0x0fu;
    raw[12] = 0u;
    raw[13] = checksum;
    raw[26] = 0u;
    raw[27] = 0u;

    uint32_t base = (ordinal - 1u) * 13u;
    for (uint32_t index = 0u; index < 13u; ++index) {
        uint32_t position = base + index;
        uint16_t character = 0xffffu;
        if (position < length) {
            character = (uint8_t)name[position];
        } else if (position == length) {
            character = 0u;
        }
        raw[character_offsets[index]] = (uint8_t)character;
        raw[character_offsets[index] + 1u] = (uint8_t)(character >> 8);
    }
}

static int write_named_directory_entry(uint32_t directory, const char *name,
                                       uint32_t name_length, int needs_lfn,
                                       const uint8_t short_name[11],
                                       const uint8_t short_entry[DIRECTORY_ENTRY_SIZE])
{
    if (!needs_lfn) {
        return write_directory_entry(directory, short_entry);
    }

    uint32_t lfn_count = (name_length + 1u + 12u) / 13u;
    if (lfn_count == 0u || lfn_count > FAT32_LFN_MAX_ENTRIES) {
        return 0;
    }
    uint8_t entries[FAT32_LFN_MAX_ENTRIES + 1u][DIRECTORY_ENTRY_SIZE];
    uint8_t checksum = short_name_checksum(short_name);
    for (uint32_t index = 0u; index < lfn_count; ++index) {
        uint32_t ordinal = lfn_count - index;
        encode_lfn_entry(entries[index], ordinal, index == 0u, name,
                         name_length, checksum);
    }
    copy_bytes(entries[lfn_count], short_entry, DIRECTORY_ENTRY_SIZE);
    return write_directory_entries(directory, &entries[0][0],
                                   lfn_count + 1u);
}

static int write_file_data(uint32_t first_cluster, const uint8_t *contents,
                           uint32_t size)
{
    uint32_t remaining = size;
    uint32_t source_offset = 0;
    uint32_t cluster = first_cluster;
    uint32_t cluster_bytes = volume.sectors_per_cluster * SECTOR_SIZE;
    uint32_t cluster_count = (size - 1u) / cluster_bytes + 1u;

    for (uint32_t current = 0; current < cluster_count; ++current) {
        if (!is_valid_cluster(cluster)) {
            return 0;
        }
        uint32_t first_sector = cluster_sector(cluster);
        for (uint32_t sector_index = 0;
             sector_index < volume.sectors_per_cluster; ++sector_index) {
            fill_bytes(sector_buffer, 0u, SECTOR_SIZE);
            uint32_t bytes = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
            copy_bytes(sector_buffer, &contents[source_offset], bytes);
            if (!ata_write_sector(first_sector + sector_index, sector_buffer)) {
                return 0;
            }
            source_offset += bytes;
            remaining -= bytes;
            if (remaining == 0u) {
                break;
            }
        }

        if (current + 1u < cluster_count) {
            uint32_t next;
            int at_end;
            if (!next_cluster(cluster, &next, &at_end) || at_end) {
                return 0;
            }
            cluster = next;
        }
    }
    return remaining == 0u;
}

int fat32_mount(void)
{
    uint8_t master_boot_record[SECTOR_SIZE];
    uint32_t partition_start = 0;
    uint32_t partition_size = 0;
    mounted = 0;
    volume = (struct fat32_volume){0};
    if (!ata_read_sector(0u, master_boot_record) ||
        master_boot_record[510] != 0x55u ||
        master_boot_record[511] != 0xaau) {
        return 0;
    }

    for (uint32_t index = 0; index < 4u; ++index) {
        uint32_t offset = 446u + index * 16u;
        uint8_t type = master_boot_record[offset + 4u];
        if (type == FAT32_PARTITION || type == FAT32_LBA_PARTITION) {
            partition_start = read_u32(&master_boot_record[offset + 8u]);
            partition_size = read_u32(&master_boot_record[offset + 12u]);
            break;
        }
    }
    uint32_t disk_sectors = ata_sector_count();
    if (partition_start == 0u || partition_size == 0u ||
        partition_start >= disk_sectors ||
        partition_size > disk_sectors - partition_start ||
        !ata_read_sector(partition_start, sector_buffer) ||
        sector_buffer[510] != 0x55u || sector_buffer[511] != 0xaau) {
        return 0;
    }

    uint32_t bytes_per_sector = read_u16(&sector_buffer[11]);
    uint32_t sectors_per_cluster = sector_buffer[13];
    uint32_t reserved_sectors = read_u16(&sector_buffer[14]);
    uint32_t fat_count = sector_buffer[16];
    uint32_t total_sectors = read_u16(&sector_buffer[19]);
    if (total_sectors == 0u) {
        total_sectors = read_u32(&sector_buffer[32]);
    }
    uint32_t fat_sectors = read_u32(&sector_buffer[36]);
    uint32_t root_cluster = read_u32(&sector_buffer[44]) & 0x0fffffffu;
    uint32_t fsinfo_sector = read_u16(&sector_buffer[48]);
    uint32_t backup_sector = read_u16(&sector_buffer[50]);

    if (bytes_per_sector != SECTOR_SIZE || sectors_per_cluster == 0u ||
        sectors_per_cluster > 64u ||
        (sectors_per_cluster & (sectors_per_cluster - 1u)) != 0u ||
        reserved_sectors == 0u || fat_count == 0u || fat_count > 2u ||
        fat_sectors == 0u || total_sectors == 0u ||
        total_sectors > partition_size || reserved_sectors >= total_sectors ||
        fat_sectors > (total_sectors - reserved_sectors) / fat_count) {
        return 0;
    }

    uint32_t fat_area_sectors = fat_count * fat_sectors;
    volume.start_sector = partition_start;
    volume.sector_count = total_sectors;
    volume.fat_start = partition_start + reserved_sectors;
    volume.fat_sectors = fat_sectors;
    volume.data_start = partition_start + reserved_sectors +
                        fat_area_sectors;
    volume.cluster_count =
        (total_sectors - reserved_sectors - fat_area_sectors) /
        sectors_per_cluster;
    volume.root_cluster = root_cluster;
    volume.sectors_per_cluster = sectors_per_cluster;
    volume.reserved_sectors = reserved_sectors;
    volume.fat_count = fat_count;
    volume.fsinfo_sector = fsinfo_sector;
    volume.backup_sector = backup_sector;
    volume.free_cluster_hint = FAT32_UNKNOWN;
    volume.next_cluster_hint = 2u;

    if (volume.cluster_count < 65525u ||
        volume.cluster_count > 0x0fffffeeu ||
        !is_valid_cluster(root_cluster)) {
        return 0;
    }
    uint32_t fat_entries = volume.cluster_count + 2u;
    uint32_t required_fat_sectors =
        fat_entries / (SECTOR_SIZE / sizeof(uint32_t)) +
        (fat_entries % (SECTOR_SIZE / sizeof(uint32_t)) != 0u);
    if (required_fat_sectors > fat_sectors) {
        return 0;
    }
    if (fsinfo_sector != 0u && fsinfo_sector < reserved_sectors &&
        ata_read_sector(partition_start + fsinfo_sector, sector_buffer) &&
        read_u32(&sector_buffer[0]) == 0x41615252u &&
        read_u32(&sector_buffer[484]) == 0x61417272u) {
        volume.fsinfo_valid = 1u;
        uint32_t free_count = read_u32(&sector_buffer[488]);
        uint32_t next_cluster = read_u32(&sector_buffer[492]);
        if (free_count <= volume.cluster_count) {
            volume.free_cluster_hint = free_count;
        }
        if (is_valid_cluster(next_cluster)) {
            volume.next_cluster_hint = next_cluster;
        }
        if (backup_sector != 0u && backup_sector != 0xffffu &&
            backup_sector + fsinfo_sector < reserved_sectors &&
            ata_read_sector(partition_start + backup_sector + fsinfo_sector,
                            sector_buffer) &&
            read_u32(&sector_buffer[0]) == 0x41615252u &&
            read_u32(&sector_buffer[484]) == 0x61417272u) {
            volume.backup_fsinfo_valid = 1u;
        }
    }

    mounted = 1;
    return 1;
}

uint32_t fat32_root_cluster(void)
{
    return mounted ? volume.root_cluster : 0u;
}

int fat32_find(uint32_t directory, const char *name,
               struct fat32_entry *entry)
{
    if (!mounted || entry == 0) {
        return 0;
    }
    return find_entry(directory, name, entry, 0) == 1;
}

int fat32_list(uint32_t directory, struct fat32_entry *entries,
               uint32_t capacity, uint32_t *count)
{
    if (!mounted || !is_valid_cluster(directory) ||
        (capacity != 0u && entries == 0) || count == 0) {
        return 0;
    }

    *count = 0;
    struct lfn_state lfn = {0};
    uint32_t cluster = directory;
    for (uint32_t chain = 0; chain < volume.cluster_count; ++chain) {
        if (!is_valid_cluster(cluster)) {
            return 0;
        }
        uint32_t first_sector = cluster_sector(cluster);
        for (uint32_t sector_index = 0;
             sector_index < volume.sectors_per_cluster; ++sector_index) {
            if (!ata_read_sector(first_sector + sector_index, sector_buffer)) {
                return 0;
            }
            for (uint32_t index = 0; index < ENTRIES_PER_SECTOR; ++index) {
                uint32_t offset = index * DIRECTORY_ENTRY_SIZE;
                uint8_t first = sector_buffer[offset];
                if (first == 0u) {
                    return 1;
                }
                if (first == 0xe5u) {
                    reset_lfn_state(&lfn);
                    continue;
                }
                if (sector_buffer[offset + 11u] == 0x0fu) {
                    decode_lfn_slot(
                        &lfn, &sector_buffer[offset],
                        first_sector + sector_index,
                        (uint16_t)offset);
                    continue;
                }
                if ((sector_buffer[offset + 11u] & 0x08u) != 0u) {
                    reset_lfn_state(&lfn);
                    continue;
                }
                if (*count == capacity) {
                    return 0;
                }
                decode_entry(&sector_buffer[offset], &lfn, &entries[*count]);
                ++*count;
                reset_lfn_state(&lfn);
            }
        }

        uint32_t next;
        int at_end;
        if (!next_cluster(cluster, &next, &at_end)) {
            return 0;
        }
        if (at_end) {
            return 1;
        }
        cluster = next;
    }
    return 0;
}

int fat32_read_file(uint32_t directory, const char *name, void *buffer,
                    uint32_t capacity, uint32_t *size)
{
    struct fat32_entry entry;
    if (size == 0 || !fat32_find(directory, name, &entry) ||
        (entry.attributes & FAT32_ATTRIBUTE_DIRECTORY) != 0u ||
        entry.size > capacity || (entry.size != 0u && buffer == 0)) {
        return 0;
    }
    *size = entry.size;
    if (entry.size == 0u) {
        return 1;
    }

    uint8_t *destination = (uint8_t *)buffer;
    uint32_t remaining = entry.size;
    uint32_t cluster = entry.first_cluster;
    uint32_t cluster_bytes = volume.sectors_per_cluster * SECTOR_SIZE;
    uint32_t cluster_count = (entry.size - 1u) / cluster_bytes + 1u;
    for (uint32_t current = 0; current < cluster_count; ++current) {
        if (!is_valid_cluster(cluster)) {
            return 0;
        }
        uint32_t first_sector = cluster_sector(cluster);
        for (uint32_t sector_index = 0;
             sector_index < volume.sectors_per_cluster; ++sector_index) {
            if (!ata_read_sector(first_sector + sector_index, sector_buffer)) {
                return 0;
            }
            uint32_t bytes = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
            copy_bytes(&destination[entry.size - remaining], sector_buffer, bytes);
            remaining -= bytes;
            if (remaining == 0u) {
                break;
            }
        }
        if (current + 1u < cluster_count) {
            uint32_t next;
            int at_end;
            if (!next_cluster(cluster, &next, &at_end) || at_end) {
                return 0;
            }
            cluster = next;
        }
    }
    return remaining == 0u;
}

int fat32_write_file(uint32_t directory, const char *name, const void *buffer,
                     uint32_t size)
{
    uint8_t short_name[11];
    uint32_t name_length;
    int needs_lfn;
    struct fat32_entry existing;
    if (!mounted || !is_valid_cluster(directory) ||
        !prepare_entry_name(directory, name, short_name, &name_length,
                            &needs_lfn) ||
        (size != 0u && buffer == 0) ||
        find_entry(directory, name, &existing, 0) != 0) {
        return 0;
    }

    uint32_t first_cluster = 0;
    uint32_t cluster_bytes = volume.sectors_per_cluster * SECTOR_SIZE;
    uint32_t clusters_needed =
        size == 0u ? 0u : (size - 1u) / cluster_bytes + 1u;
    uint32_t previous = 0;
    for (uint32_t index = 0; index < clusters_needed; ++index) {
        uint32_t cluster;
        if (!allocate_cluster(&cluster)) {
            if (first_cluster != 0u) {
                release_chain(first_cluster);
            }
            return 0;
        }
        if (first_cluster == 0u) {
            first_cluster = cluster;
        } else if (!fat_write(previous, cluster)) {
            int rollback_succeeded = fat_write(previous,
                                               FAT32_END_MARKER);
            if (rollback_succeeded) {
                rollback_succeeded = release_chain(first_cluster) &&
                                     release_chain(cluster);
            }
            if (!rollback_succeeded) {
                mounted = 0;
            }
            return 0;
        }
        previous = cluster;
    }

    if (size != 0u &&
        !write_file_data(first_cluster, (const uint8_t *)buffer, size)) {
        if (first_cluster != 0u && !release_chain(first_cluster)) {
            mounted = 0;
        }
        return 0;
    }

    uint8_t raw[DIRECTORY_ENTRY_SIZE];
    encode_entry(raw, short_name, 0x20u, first_cluster, size);
    if (!write_named_directory_entry(directory, name, name_length, needs_lfn,
                                     short_name, raw)) {
        if (first_cluster != 0u && !release_chain(first_cluster)) {
            mounted = 0;
        }
        return 0;
    }
    return 1;
}

int fat32_create_directory(uint32_t parent, const char *name,
                           uint32_t *directory_cluster)
{
    uint8_t short_name[11];
    uint32_t name_length;
    int needs_lfn;
    struct fat32_entry existing;
    if (!mounted || !is_valid_cluster(parent) || directory_cluster == 0 ||
        !prepare_entry_name(parent, name, short_name, &name_length,
                            &needs_lfn) ||
        find_entry(parent, name, &existing, 0) != 0) {
        return 0;
    }

    uint32_t cluster;
    if (!allocate_cluster(&cluster)) {
        return 0;
    }
    uint8_t empty_sector[SECTOR_SIZE];
    fill_bytes(empty_sector, 0u, SECTOR_SIZE);
    for (uint32_t index = 0; index < volume.sectors_per_cluster; ++index) {
        if (!ata_write_sector(cluster_sector(cluster) + index, empty_sector)) {
            if (!release_chain(cluster)) {
                mounted = 0;
            }
            return 0;
        }
    }

    uint8_t dot[DIRECTORY_ENTRY_SIZE];
    uint8_t dot_dot[DIRECTORY_ENTRY_SIZE];
    uint8_t dot_name[11];
    uint8_t dot_dot_name[11];
    fill_bytes(dot_name, ' ', sizeof(dot_name));
    fill_bytes(dot_dot_name, ' ', sizeof(dot_dot_name));
    dot_name[0] = '.';
    dot_dot_name[0] = '.';
    dot_dot_name[1] = '.';
    encode_entry(dot, dot_name, FAT32_ATTRIBUTE_DIRECTORY, cluster, 0u);
    encode_entry(dot_dot, dot_dot_name, FAT32_ATTRIBUTE_DIRECTORY, parent, 0u);
    copy_bytes(empty_sector, dot, sizeof(dot));
    copy_bytes(&empty_sector[DIRECTORY_ENTRY_SIZE], dot_dot,
               sizeof(dot_dot));
    if (!ata_write_sector(cluster_sector(cluster), empty_sector)) {
        if (!release_chain(cluster)) {
            mounted = 0;
        }
        return 0;
    }

    uint8_t raw[DIRECTORY_ENTRY_SIZE];
    encode_entry(raw, short_name, FAT32_ATTRIBUTE_DIRECTORY, cluster, 0u);
    if (!write_named_directory_entry(parent, name, name_length, needs_lfn,
                                     short_name, raw)) {
        if (!release_chain(cluster)) {
            mounted = 0;
        }
        return 0;
    }
    *directory_cluster = cluster;
    return 1;
}

int fat32_delete(uint32_t directory, const char *name)
{
    struct fat32_entry entry;
    struct directory_location location;
    if (!mounted || !is_valid_cluster(directory) ||
        name == 0 || (name[0] == '.' &&
         (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) ||
        find_entry(directory, name, &entry, &location) != 1 ||
        (entry.attributes & 0x08u) != 0u) {
        return 0;
    }

    if ((entry.attributes & FAT32_ATTRIBUTE_DIRECTORY) != 0u) {
        if (entry.first_cluster == volume.root_cluster ||
            !is_valid_cluster(entry.first_cluster)) {
            return 0;
        }
        struct fat32_entry children[2];
        uint32_t child_count;
        if (!fat32_list(entry.first_cluster, children, 2u, &child_count)) {
            return 0;
        }
        for (uint32_t index = 0; index < child_count; ++index) {
            if (children[index].name[0] != '.' ||
                (children[index].name[1] != '\0' &&
                 !(children[index].name[1] == '.' &&
                   children[index].name[2] == '\0'))) {
                return 0;
            }
        }
    }

    for (uint32_t index = 0u; index < location.lfn_count; ++index) {
        if (!ata_read_sector(location.lfn_slots[index].sector, sector_buffer)) {
            return 0;
        }
        sector_buffer[location.lfn_slots[index].offset] = 0xe5u;
        if (!ata_write_sector(location.lfn_slots[index].sector, sector_buffer)) {
            return 0;
        }
    }
    if (!ata_read_sector(location.sector, sector_buffer)) {
        return 0;
    }
    sector_buffer[location.offset] = 0xe5u;
    if (!ata_write_sector(location.sector, sector_buffer)) {
        return 0;
    }
    if (entry.first_cluster >= 2u && !release_chain(entry.first_cluster)) {
        mounted = 0;
        return 0;
    }
    return 1;
}
