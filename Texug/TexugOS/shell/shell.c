#include <stdint.h>

#include "console.h"
#include "desktop.h"
#include "ata.h"
#include "fat32.h"
#include "frame_allocator.h"
#include "keyboard.h"
#include "mouse.h"
#include "network.h"
#include "pci.h"
#include "rtl8192se.h"
#include "rtl8192se_hw.h"
#include "shell.h"
#include "tef.h"
#include "user_mode.h"

#define COMMAND_BUFFER_SIZE 128u
#define COMMAND_NAME_SIZE 16u
#define SHELL_PATH_SIZE 96u
#define LIST_CAPACITY 64u
#define FILE_BUFFER_SIZE 2048u

static char command_buffer[COMMAND_BUFFER_SIZE];
static char current_path[SHELL_PATH_SIZE] = "/";
static uint32_t current_directory;
static uint8_t tef_file_buffer[TEF_FILE_MAX_SIZE];
static void (*idle_hook)(void);

void shell_set_idle_hook(void (*hook)(void))
{
    idle_hook = hook;
}

/* Espera a proxima interrupcao. Quando o shell roda dentro do app Terminal
 * do desktop, o gancho redesenha a tela para a interface nao congelar
 * durante esperas longas (scan Wi-Fi, digitar senha, etc.). */
static void shell_idle(void)
{
    if (idle_hook != 0) {
        idle_hook();
    }
    __asm__ volatile ("sti; hlt");
}

static int string_equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static int string_starts_with(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static uint32_t string_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void copy_string(char *destination, const char *source)
{
    while (*source != '\0') {
        *destination++ = *source++;
    }
    *destination = '\0';
}

void shell_print_prompt(void)
{
    console_write("texugos:");
    console_write(current_path);
    console_write(" > ");
}

static void read_command_line(char *buffer, uint32_t capacity)
{
    uint32_t length = 0;
    for (;;) {
        char character;
        if (!keyboard_read_character(&character)) {
            shell_idle();
            continue;
        }
        if (character == '\n') {
            console_write("\n");
            buffer[length] = '\0';
            return;
        }
        if (character == '\b') {
            if (length != 0u) {
                --length;
                console_write_character('\b');
            }
            continue;
        }
        if (character >= ' ' && character <= '~' &&
            length + 1u < capacity) {
            buffer[length++] = character;
            console_write_character(character);
        }
    }
}

static uint32_t read_secret_line(uint8_t *buffer, uint32_t capacity)
{
    uint32_t length = 0u;
    for (;;) {
        char character;
        if (!keyboard_read_character(&character)) {
            shell_idle();
            continue;
        }
        if (character == '\n') {
            console_write("\n");
            if (capacity != 0u) {
                buffer[length] = 0u;
            }
            return length;
        }
        if (character == '\b') {
            if (length != 0u) {
                buffer[--length] = 0u;
                console_write_character('\b');
            }
            continue;
        }
        if (character >= ' ' && character <= '~' &&
            length + 1u < capacity) {
            buffer[length++] = (uint8_t)character;
            console_write_character(character);
        }
    }
}

static void clear_secret(uint8_t *secret, uint32_t capacity)
{
    volatile uint8_t *bytes = secret;
    for (uint32_t index = 0u; index < capacity; ++index) {
        bytes[index] = 0u;
    }
}

static int parse_command(char *line, char command[COMMAND_NAME_SIZE],
                         char **arguments)
{
    while (*line == ' ') {
        ++line;
    }
    if (*line == '\0') {
        command[0] = '\0';
        *arguments = line;
        return 1;
    }

    uint32_t command_length = 0;
    while (*line != '\0' && *line != ' ') {
        if (command_length + 1u >= COMMAND_NAME_SIZE) {
            return 0;
        }
        command[command_length++] = *line++;
    }
    command[command_length] = '\0';
    while (*line == ' ') {
        ++line;
    }
    *arguments = line;

    uint32_t length = string_length(line);
    while (length != 0u && line[length - 1u] == ' ') {
        line[--length] = '\0';
    }
    return 1;
}

static void print_help(void)
{
    console_write("Commands:\n");
    console_write("  help clear echo ls cd mkdir touch\n");
    console_write("  cat rm reboot shutdown memory\n");
    console_write("  version processes mouse devices\n");
    console_write("  desktop net dhcp dns ping texugo\n");
    console_write("  run TEF_FILE\n");
    console_write("  wifi scan | status | disconnect\n");
    console_write("  wifi connect INDEX|SSID\n");
}

/* ASCII do texugo (comando `texugo`). Cada linha tem no maximo 38 colunas
 * para caber na janela do Terminal do desktop. */
static const char *const badger_art[] = {
    "   .-+#+-.                  .-+#+-.",
    "  +#'   '#+ .++########++. +#'   '#+",
    "  ##     ##.+############+.##     ##",
    "  '#+   +#'  ############  '#+   +#'",
    "   '#####'.   ##########   .'#####'",
    "    +####+.  o ######## o  .+####+",
    "    +#####+.    ######    .+#####+",
    "     +######.    ####    .######+",
    "       .+#####.  ++++  .#####+.",
    "          .+###+.    .+###+.",
};

static void print_badger(void)
{
    for (uint32_t row = 0u;
         row < sizeof(badger_art) / sizeof(badger_art[0]); ++row) {
        console_write(badger_art[row]);
        console_write("\n");
    }
    console_write("      TexugOS 0.1.0 - badger approved.\n");
}

static void list_directory(void)
{
    struct fat32_entry entries[LIST_CAPACITY];
    uint32_t count;
    if (!fat32_list(current_directory, entries, LIST_CAPACITY, &count)) {
        console_write("Unable to list directory (capacity: ");
        console_write_u32(LIST_CAPACITY);
        console_write(" entries).\n");
        return;
    }

    for (uint32_t index = 0; index < count; ++index) {
        console_write(entries[index].name);
        if ((entries[index].attributes & FAT32_ATTRIBUTE_DIRECTORY) != 0u) {
            console_write("/");
        } else {
            console_write("  ");
            console_write_u32(entries[index].size);
            console_write(" bytes");
        }
        console_write("\n");
    }
}

static void change_directory(const char *name)
{
    if (*name == '\0' || string_equal(name, "/")) {
        current_directory = fat32_root_cluster();
        current_path[0] = '/';
        current_path[1] = '\0';
        return;
    }
    if (string_equal(name, ".")) {
        return;
    }
    if (string_equal(name, "..") &&
        current_directory == fat32_root_cluster()) {
        return;
    }

    struct fat32_entry entry;
    if (!fat32_find(current_directory, name, &entry) ||
        (entry.attributes & FAT32_ATTRIBUTE_DIRECTORY) == 0u) {
        console_write("Directory not found.\n");
        return;
    }

    if (string_equal(name, "..")) {
        current_directory = entry.first_cluster;
        uint32_t length = string_length(current_path);
        while (length > 1u && current_path[length - 1u] != '/') {
            current_path[--length] = '\0';
        }
        if (length > 1u) {
            current_path[length - 1u] = '\0';
        }
        return;
    }

    uint32_t path_length = string_length(current_path);
    uint32_t name_length = string_length(entry.name);
    uint32_t separator = path_length == 1u ? 0u : 1u;
    if (path_length + separator + name_length >= SHELL_PATH_SIZE) {
        console_write("Path is too long.\n");
        return;
    }
    current_directory = entry.first_cluster;
    if (separator != 0u) {
        current_path[path_length++] = '/';
    }
    copy_string(&current_path[path_length], entry.name);
}

static void make_directory(const char *name)
{
    if (ata_storage_read_only()) {
        console_write("Storage is read-only; cannot create directory.\n");
        return;
    }
    uint32_t cluster;
    if (*name != '\0' &&
        fat32_create_directory(current_directory, name, &cluster)) {
        console_write("Directory created.\n");
    } else {
        console_write("Unable to create directory.\n");
    }
}

static void create_file(const char *name)
{
    if (ata_storage_read_only()) {
        console_write("Storage is read-only; cannot create file.\n");
        return;
    }
    static const char empty_file[] = "";
    if (*name != '\0' &&
        fat32_write_file(current_directory, name, empty_file, 0u)) {
        console_write("File created.\n");
    } else {
        console_write("Unable to create file.\n");
    }
}

static void print_file(const char *name)
{
    char contents[FILE_BUFFER_SIZE];
    uint32_t size;
    if (*name == '\0' ||
        !fat32_read_file(current_directory, name, contents,
                         sizeof(contents), &size)) {
        console_write("Unable to read file (maximum ");
        console_write_u32(FILE_BUFFER_SIZE);
        console_write(" bytes).\n");
        return;
    }

    for (uint32_t index = 0; index < size; ++index) {
        console_write_character(contents[index]);
    }
    console_write("\n");
}

static void run_tef_file(const char *filename)
{
    uint32_t file_size = 0u;
    struct tef_executable executable;
    if (*filename == '\0' ||
        !fat32_read_file(current_directory, filename, tef_file_buffer,
                         sizeof(tef_file_buffer), &file_size)) {
        console_write("Unable to read TEF file (maximum ");
        console_write_u32(sizeof(tef_file_buffer));
        console_write(" bytes).\n");
        return;
    }
    if (!tef_parse(tef_file_buffer, file_size, &executable)) {
        console_write("Invalid or unsupported TEF executable.\n");
        return;
    }

    console_write("Application: ");
    for (uint32_t index = 0u; index < executable.name_length; ++index) {
        console_write_character((char)executable.name[index]);
    }
    console_write("\nLogo:\n");
    for (uint32_t row = 0u; row < TEF_ICON_HEIGHT; ++row) {
        for (uint32_t column = 0u; column < TEF_ICON_WIDTH; ++column) {
            uint8_t pixel =
                executable.icon_rows[row] & (0x80u >> column);
            console_write_character(pixel != 0u ? '#' : '.');
        }
        console_write("\n");
    }

    uint32_t exit_code = 0u;
    console_write("Program output: ");
    if (!user_mode_execute(executable.binary, executable.binary_size,
                           executable.entry_offset, &exit_code)) {
        console_write("\nTEF program stopped after a CPU exception or "
                      "could not be started.\n");
        return;
    }
    console_write("\nTEF program exited with code ");
    console_write_u32(exit_code);
    console_write(".\n");
}

static void remove_entry(const char *name)
{
    if (ata_storage_read_only()) {
        console_write("Storage is read-only; cannot remove entry.\n");
        return;
    }
    if (*name != '\0' && fat32_delete(current_directory, name)) {
        console_write("Removed.\n");
    } else {
        console_write("Unable to remove entry.\n");
    }
}

static void list_pci_devices(void)
{
    uint32_t count = pci_device_count();
    console_write("PCI devices: ");
    console_write_u32(count);
    console_write("\n");
    if (pci_inventory_truncated()) {
        console_write("PCI inventory truncated at 64 devices.\n");
    }
    for (uint32_t index = 0; index < count; ++index) {
        struct pci_device device;
        if (!pci_get_device(index, &device)) {
            console_write("PCI inventory changed while reading.\n");
            return;
        }
        console_write("PCI ");
        console_write_u32(device.bus);
        console_write(":");
        console_write_u32(device.slot);
        console_write(".");
        console_write_u32(device.function);
        console_write(" ID ");
        console_write_hex32(((uint32_t)device.vendor_id << 16) |
                            device.device_id);
        console_write(" class ");
        console_write_hex32(((uint32_t)device.class_code << 16) |
                            ((uint32_t)device.subclass << 8) |
                            device.programming_interface);
        console_write("\n");
    }
}

static int parse_ipv4_address(const char *text, uint8_t address[4])
{
    for (uint32_t part = 0u; part < 4u; ++part) {
        uint32_t value = 0u;
        uint32_t digits = 0u;
        while (*text >= '0' && *text <= '9') {
            value = value * 10u + (uint32_t)(*text++ - '0');
            if (value > 255u || ++digits > 3u) {
                return 0;
            }
        }
        if (digits == 0u) {
            return 0;
        }
        address[part] = (uint8_t)value;
        if (part < 3u) {
            if (*text != '.') {
                return 0;
            }
            ++text;
        } else if (*text != '\0') {
            return 0;
        }
    }
    return 1;
}

static void print_ipv4_address(const uint8_t address[4])
{
    for (uint32_t index = 0u; index < 4u; ++index) {
        if (index != 0u) {
            console_write(".");
        }
        console_write_u32(address[index]);
    }
}

static void print_mac_address(const uint8_t address[6])
{
    static const char digits[] = "0123456789abcdef";
    for (uint32_t index = 0u; index < 6u; ++index) {
        if (index != 0u) {
            console_write(":");
        }
        char octet[3] = {
            digits[address[index] >> 4],
            digits[address[index] & 0x0fu],
            '\0',
        };
        console_write(octet);
    }
}

static void print_network_status(void)
{
    if (!network_is_available()) {
        console_write("Network interface unavailable.\n");
        return;
    }
    uint8_t mac[6];
    struct network_config config;
    network_get_mac(mac);
    network_get_config(&config);
    console_write(network_interface_name());
    console_write(" MAC: ");
    print_mac_address(mac);
    console_write("\n");
    if (config.configured == 0u) {
        console_write("IPv4: not configured.\n");
        return;
    }
    console_write("IPv4: ");
    print_ipv4_address(config.address);
    console_write("\nSubnet: ");
    print_ipv4_address(config.subnet_mask);
    console_write("\nGateway: ");
    print_ipv4_address(config.gateway);
    console_write("\nDNS: ");
    print_ipv4_address(config.dns_server);
    console_write("\n");
}

static int parse_network_index(const char *text, uint32_t *index)
{
    if (text == 0 || index == 0 || *text == '\0') {
        return 0;
    }
    uint32_t value = 0u;
    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return 0;
        }
        uint32_t digit = (uint32_t)(*text++ - '0');
        if (value > (UINT32_MAX - digit) / 10u) {
            return 0;
        }
        value = value * 10u + digit;
    }
    *index = value;
    return 1;
}

static int wifi_scan_result_matches_ssid(
    const struct rtl8192se_scan_result *result, const char *ssid,
    uint32_t ssid_length)
{
    if (result->ssid_length != ssid_length) {
        return 0;
    }
    for (uint32_t index = 0u; index < ssid_length; ++index) {
        if (result->ssid[index] != (uint8_t)ssid[index]) {
            return 0;
        }
    }
    return 1;
}

static int find_wifi_scan_result(const char *selector,
                                 struct rtl8192se_scan_result *result,
                                 int *ambiguous)
{
    uint32_t index;
    if (parse_network_index(selector, &index) &&
        rtl8192se_scan_get_result(index, result)) {
        *ambiguous = 0;
        return 1;
    }

    uint32_t selector_length = string_length(selector);
    uint32_t matches = 0u;
    for (uint32_t scan_index = 0u;
         scan_index < rtl8192se_scan_result_count(); ++scan_index) {
        struct rtl8192se_scan_result candidate;
        if (rtl8192se_scan_get_result(scan_index, &candidate) &&
            wifi_scan_result_matches_ssid(&candidate, selector,
                                          selector_length)) {
            *result = candidate;
            ++matches;
        }
    }
    *ambiguous = matches > 1u;
    return matches == 1u;
}

static void print_wifi_scan_results(void)
{
    uint32_t count = rtl8192se_scan_result_count();
    console_write("RTL8191SE channel scan found ");
    console_write_u32(count);
    console_write(" network(s):\n");
    if (count == 0u) {
        console_write("  scan diagnostics: ");
        console_write_u32(rtl8192se_scan_received_frame_count());
        console_write(" RX frames, ");
        console_write_u32(rtl8192se_scan_parsed_frame_count());
        console_write(" valid beacon/probe-response frames\n");
        console_write("  RX frame types: ");
        console_write_u32(rtl8192se_scan_management_frame_count());
        console_write(" management (");
        console_write_u32(rtl8192se_scan_beacon_frame_count());
        console_write(" beacon, ");
        console_write_u32(rtl8192se_scan_probe_response_frame_count());
        console_write(" probe response), ");
        console_write_u32(rtl8192se_scan_other_frame_count());
        console_write(" other, ");
        console_write_u32(rtl8192se_scan_short_frame_count());
        console_write(" too short\n");
    }
    for (uint32_t index = 0u; index < count; ++index) {
        struct rtl8192se_scan_result result;
        if (!rtl8192se_scan_get_result(index, &result)) {
            console_write("Wi-Fi scan result changed unexpectedly.\n");
            return;
        }
        console_write("  ");
        console_write_u32(index);
        console_write(": ");
        if (result.ssid_length == 0u) {
            console_write("<hidden>");
        } else {
            for (uint32_t byte = 0u; byte < result.ssid_length; ++byte) {
                uint8_t value = result.ssid[byte];
                char character[2] = {
                    value >= ' ' && value <= '~' ? (char)value : '?',
                    '\0',
                };
                console_write(character);
            }
        }
        console_write(" channel ");
        console_write_u32(result.channel);
        console_write(" ");
        console_write(result.security == RTL8192SE_SECURITY_OPEN
                          ? "open"
                          : result.security ==
                                    RTL8192SE_SECURITY_WPA2_PSK_CCMP
                                  ? "WPA2-PSK/CCMP"
                                : "security unsupported");
        console_write("\n");
    }
}

static enum rtl8192se_scan_state wait_for_wifi_scan(void)
{
    enum rtl8192se_scan_state state;
    do {
        state = rtl8192se_scan_poll();
        if (state == RTL8192SE_SCAN_ACTIVE) {
            shell_idle();
        }
    } while (state == RTL8192SE_SCAN_ACTIVE);
    return state;
}

static void wifi_command(const char *arguments)
{
    if (string_equal(arguments, "scan")) {
        console_write("Scanning Wi-Fi channels (about 7 seconds)...\n");
        if (!rtl8192se_scan_start()) {
            console_write("Wi-Fi scan unavailable; check RTL8191SE "
                          "initialization details reported at boot.\n");
            return;
        }
        if (wait_for_wifi_scan() == RTL8192SE_SCAN_FAILED) {
            console_write("Wi-Fi scan failed.\n");
            return;
        }
        print_wifi_scan_results();
    } else if (string_equal(arguments, "disconnect")) {
        rtl8192se_mark_disconnected();
        network_initialize();
        console_write("Wi-Fi disconnected.\n");
    } else if (string_starts_with(arguments, "connect ")) {
        const char *selector = arguments + 8u;
        if (*selector == '\0') {
            console_write("Usage: wifi connect INDEX|SSID\n");
            return;
        }
        struct rtl8192se_scan_result result;
        int ambiguous = 0;
        int found = find_wifi_scan_result(selector, &result, &ambiguous);
        if (!found && !ambiguous) {
            uint32_t selector_length = string_length(selector);
            if (selector_length > 32u) {
                console_write("Wi-Fi SSID must contain at most 32 bytes.\n");
                return;
            }
            console_write("SSID not in the previous scan; sending directed "
                          "probes across channels 1-13 (about 7 seconds)...\n");
            if (!rtl8192se_scan_for_ssid_start(
                    (const uint8_t *)selector,
                    (uint8_t)selector_length)) {
                console_write("Wi-Fi scan unavailable; disconnect first "
                              "if already associated.\n");
                return;
            }
            if (wait_for_wifi_scan() == RTL8192SE_SCAN_FAILED) {
                console_write("Wi-Fi scan failed; connection was not "
                              "started.\n");
                return;
            }
            found = find_wifi_scan_result(selector, &result, &ambiguous);
        }
        if (!found) {
            if (ambiguous) {
                console_write("More than one network has that SSID; "
                              "connect using its scan index.\n");
            } else {
                console_write("SSID was not found by the directed scan; "
                              "connection was not started.\n");
                console_write("  RX frames: ");
                console_write_u32(rtl8192se_scan_received_frame_count());
                console_write(", probe responses: ");
                console_write_u32(
                    rtl8192se_scan_probe_response_frame_count());
                console_write(".\n");
            }
            return;
        }
        if (result.security != RTL8192SE_SECURITY_OPEN &&
            result.security != RTL8192SE_SECURITY_WPA2_PSK_CCMP) {
            console_write("The selected Wi-Fi security mode is unsupported.\n");
            return;
        }
        if (result.security == RTL8192SE_SECURITY_WPA2_PSK_CCMP) {
            uint8_t passphrase[64] = {0};
            console_write("WPA2 passphrase (visible input): ");
            uint32_t passphrase_length =
                read_secret_line(passphrase, sizeof(passphrase));
            if (passphrase_length < 8u || passphrase_length > 63u) {
                clear_secret(passphrase, sizeof(passphrase));
                console_write("WPA2 passphrases must contain 8-63 characters.\n");
                return;
            }
            int started = rtl8192se_wpa2_association_start(
                &result, passphrase, passphrase_length);
            clear_secret(passphrase, sizeof(passphrase));
            if (!started) {
                console_write("Could not start WPA2 association; check the "
                              "Wi-Fi adapter and selected network.\n");
                return;
            }
        } else if (!rtl8192se_open_association_start(&result)) {
            console_write("Could not start open-network association.\n");
            return;
        }
        enum rtl8192se_association_state state;
        do {
            state = rtl8192se_open_association_poll();
            if (state == RTL8192SE_ASSOCIATION_AUTHENTICATING ||
                state == RTL8192SE_ASSOCIATION_ASSOCIATING ||
                state == RTL8192SE_ASSOCIATION_HANDSHAKING) {
                shell_idle();
            }
        } while (state == RTL8192SE_ASSOCIATION_AUTHENTICATING ||
                 state == RTL8192SE_ASSOCIATION_ASSOCIATING ||
                 state == RTL8192SE_ASSOCIATION_HANDSHAKING);
        if (state != RTL8192SE_ASSOCIATION_CONNECTED_OPEN &&
            state != RTL8192SE_ASSOCIATION_CONNECTED_WPA2) {
            console_write("Wi-Fi association or WPA2 handshake failed.\n");
            enum rtl8192se_association_failure failure =
                rtl8192se_association_last_failure();
            switch (failure) {
            case RTL8192SE_ASSOCIATION_FAILURE_AUTH_TIMEOUT:
            case RTL8192SE_ASSOCIATION_FAILURE_TX_STUCK:
                console_write("  Authentication TX: ");
                console_write_u32(
                    rtl8192se_association_auth_tx_submission_count());
                console_write(" queued, ");
                console_write_u32(
                    rtl8192se_association_auth_tx_dma_completion_count());
                console_write(" descriptors returned by DMA.\n");
                console_write("  RX during authentication: ");
                console_write_u32(rtl8192se_association_rx_frame_count());
                console_write(" frame(s), including ");
                console_write_u32(
                    rtl8192se_association_auth_response_count());
                console_write(" authentication-subtype frame(s).\n");
                console_write("  RX descriptors discarded as invalid: ");
                console_write_u32(
                    rtl8192se_association_rx_invalid_descriptor_count());
                console_write(".\n");
                if (failure == RTL8192SE_ASSOCIATION_FAILURE_AUTH_TIMEOUT) {
                    console_write("Failure detail: timed out waiting for "
                                  "802.11 authentication response.\n");
                } else {
                    console_write("Failure detail: an authentication TX "
                                  "descriptor remained device-owned.\n");
                }
                break;
            case RTL8192SE_ASSOCIATION_FAILURE_AUTH_REJECTED:
                console_write("Failure detail: the access point rejected "
                              "802.11 authentication.\n");
                break;
            case RTL8192SE_ASSOCIATION_FAILURE_ASSOC_TIMEOUT:
                console_write("Failure detail: timed out waiting for "
                              "association response.\n");
                break;
            case RTL8192SE_ASSOCIATION_FAILURE_ASSOC_REJECTED:
                console_write("Failure detail: the access point rejected "
                              "association or its RSN parameters.\n");
                break;
            case RTL8192SE_ASSOCIATION_FAILURE_WPA2_TIMEOUT:
                console_write("Failure detail: WPA2 handshake timed out; "
                              "no acceptable EAPOL key response arrived.\n");
                break;
            case RTL8192SE_ASSOCIATION_FAILURE_WPA2_HANDSHAKE:
                console_write("Failure detail: an EAPOL key message failed "
                              "WPA2 validation.\n");
                break;
            case RTL8192SE_ASSOCIATION_FAILURE_TX:
                console_write("Failure detail: could not transmit an "
                              "authentication or association frame.\n");
                break;
            case RTL8192SE_ASSOCIATION_FAILURE_NONE:
            default:
                console_write("Failure detail unavailable.\n");
                break;
            }
            return;
        }
        network_initialize();
        console_write(state == RTL8192SE_ASSOCIATION_CONNECTED_WPA2
                          ? "WPA2 connected. Requesting DHCP...\n"
                          : "Associated with open Wi-Fi network. "
                            "Requesting DHCP...\n");
        if (network_dhcp()) {
            console_write("DHCP lease acquired.\n");
        } else {
            console_write("DHCP failed: ");
            console_write(network_dhcp_error());
            console_write(".\n");
        }
    } else if (string_equal(arguments, "status")) {
        console_write(!rtl8192se_network_is_associated()
                          ? "RTL8191SE is not associated.\n"
                          : rtl8192se_network_is_wpa2()
                                ? "RTL8191SE connected with WPA2-PSK/CCMP.\n"
                                : "RTL8191SE associated with an open network.\n");
    } else {
        console_write("Usage: wifi scan | wifi connect INDEX|SSID | "
                      "wifi disconnect | wifi status\n");
    }
}

static void reboot_system(void)
{
    uint8_t status;
    uint32_t attempts = 100000u;
    int reset_requested = 0;
    do {
        __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"(0x64u));
        if ((status & 0x02u) == 0u) {
            uint8_t reset = 0xfeu;
            __asm__ volatile ("outb %0, %1" : : "a"(reset), "Nd"(0x64u));
            reset_requested = 1;
            break;
        }
    } while (--attempts != 0u);

    if (reset_requested == 0) {
        console_write("Keyboard controller reset is unavailable; system halted.\n");
        for (uint32_t frame = 0u; frame < 4u; ++frame) {
            shell_idle();
        }
    }
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void shutdown_system(void)
{
    console_write("System halted; ACPI power-off is unavailable.\n");
    for (uint32_t frame = 0u; frame < 4u; ++frame) {
        shell_idle();
    }
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void execute_command(char *line)
{
    char command[COMMAND_NAME_SIZE];
    char *arguments;
    if (!parse_command(line, command, &arguments)) {
        console_write("Command name is too long.\n");
        return;
    }

    if (string_equal(command, "")) {
        return;
    } else if (string_equal(command, "help")) {
        print_help();
    } else if (string_equal(command, "clear")) {
        console_clear();
    } else if (string_equal(command, "echo")) {
        console_write(arguments);
        console_write("\n");
    } else if (string_equal(command, "ls")) {
        list_directory();
    } else if (string_equal(command, "cd")) {
        change_directory(arguments);
    } else if (string_equal(command, "mkdir")) {
        make_directory(arguments);
    } else if (string_equal(command, "touch")) {
        create_file(arguments);
    } else if (string_equal(command, "cat")) {
        print_file(arguments);
    } else if (string_equal(command, "run")) {
        run_tef_file(arguments);
    } else if (string_equal(command, "rm")) {
        remove_entry(arguments);
    } else if (string_equal(command, "reboot")) {
        reboot_system();
    } else if (string_equal(command, "shutdown")) {
        shutdown_system();
    } else if (string_equal(command, "memory")) {
        uint32_t free_frames = frame_free_count();
        console_write("Free physical frames: ");
        console_write_u32(free_frames);
        console_write(" (");
        console_write_u32(free_frames / 256u);
        console_write(" MiB).\n");
    } else if (string_equal(command, "version")) {
        console_write("TexugOS version 0.1.0 (32-bit BIOS).\n");
    } else if (string_equal(command, "processes")) {
        console_write("User processes: 0 (process isolation not implemented).\n");
        console_write("Kernel threads use a cooperative scheduler.\n");
    } else if (string_equal(command, "mouse")) {
        struct mouse_state state;
        mouse_get_state(&state);
        console_write("Mouse packets: ");
        console_write_u32(state.packets);
        console_write(" position ");
        console_write_u32(state.x);
        console_write(",");
        console_write_u32(state.y);
        console_write(" buttons ");
        console_write_u32(state.buttons);
        console_write("\n");
    } else if (string_equal(command, "devices")) {
        list_pci_devices();
    } else if (string_equal(command, "desktop")) {
        if (desktop_is_running()) {
            console_write("The desktop is already running.\n");
        } else {
            desktop_run();
        }
    } else if (string_equal(command, "texugo") ||
               string_equal(command, "badger")) {
        print_badger();
    } else if (string_equal(command, "net")) {
        print_network_status();
    } else if (string_equal(command, "wifi")) {
        wifi_command(arguments);
    } else if (string_equal(command, "dhcp")) {
        if (network_dhcp()) {
            console_write("DHCP lease acquired.\n");
        } else {
            console_write("DHCP failed: ");
            console_write(network_dhcp_error());
            console_write(".\n");
        }
    } else if (string_equal(command, "ping")) {
        uint8_t address[4];
        uint32_t round_trip_ms;
        if (!parse_ipv4_address(arguments, address)) {
            console_write("Usage: ping A.B.C.D\n");
        } else if (network_ping(address, &round_trip_ms)) {
            console_write("Reply from ");
            print_ipv4_address(address);
            console_write(": time ");
            console_write_u32(round_trip_ms);
            console_write(" ms\n");
        } else {
            console_write("Request timed out from ");
            print_ipv4_address(address);
            console_write(".\n");
        }
    } else if (string_equal(command, "dns")) {
        uint8_t address[4];
        if (*arguments == '\0') {
            console_write("Usage: dns HOSTNAME\n");
        } else if (network_resolve_ipv4(arguments, address)) {
            console_write("DNS reply: ");
            print_ipv4_address(address);
            console_write("\n");
        } else {
            console_write("DNS lookup failed for ");
            console_write(arguments);
            console_write(".\n");
        }
    } else {
        console_write("Unknown command. Type 'help'.\n");
    }
}

void shell_execute_line(char *line)
{
    execute_command(line);
}

void shell_run(void)
{
    current_directory = fat32_root_cluster();
    console_write("TexugOS shell ready. Type 'help'.\n");
    for (;;) {
        shell_print_prompt();
        read_command_line(command_buffer, sizeof(command_buffer));
        execute_command(command_buffer);
    }
}
