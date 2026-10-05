/**
 * config.c - Persistent configuration on SD card
 *
 * Simple INI-style format. No external parser dependency.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <3ds.h>

#include "util/config.h"

static void ensure_dirs(void)
{
    mkdir("sdmc:/3ds", 0755);
    mkdir("sdmc:/3ds/jellyfin-3ds", 0755);
    mkdir("sdmc:/3ds/jellyfin-3ds/cache", 0755);
}

static void trim_newline(char *s)
{
    int len = strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r'))
        s[--len] = '\0';
}

static void parse_line(const char *line, const char *key, char *out, int out_len)
{
    int klen = strlen(key);
    if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
        snprintf(out, out_len, "%s", line + klen + 1);
        trim_newline(out);
    }
}

bool config_load(jfin_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->audio_bitrate = 128;
    config->video_bitrate = 472;
    config->prefer_transcoding = true;
    config->auto_advance = true;

    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) f = fopen(CONFIG_PATH ".bak", "r");
    if (!f) return false;

    char line[1280];
    while (fgets(line, sizeof(line), f)) {
        parse_line(line, "server_url", config->server_url, sizeof(config->server_url));
        parse_line(line, "username", config->username, sizeof(config->username));
        parse_line(line, "access_token", config->access_token, sizeof(config->access_token));
        parse_line(line, "user_id", config->user_id, sizeof(config->user_id));
        parse_line(line, "device_id", config->device_id, sizeof(config->device_id));

        char buf[32];
        buf[0] = '\0';
        parse_line(line, "audio_bitrate", buf, sizeof(buf));
        if (buf[0] != '\0') config->audio_bitrate = atoi(buf);

        buf[0] = '\0';
        parse_line(line, "video_bitrate", buf, sizeof(buf));
        if (buf[0] != '\0') config->video_bitrate = atoi(buf);

        buf[0] = '\0';
        parse_line(line, "prefer_transcoding", buf, sizeof(buf));
        if (buf[0] != '\0') config->prefer_transcoding = (strcmp(buf, "1") == 0);

        buf[0] = '\0';
        parse_line(line, "auto_advance", buf, sizeof(buf));
        if (buf[0] != '\0') config->auto_advance = (strcmp(buf, "1") == 0);
    }

    fclose(f);
    return true;
}

bool config_save(const jfin_config_t *config)
{
    ensure_dirs();

    const char *values[] = {config->server_url, config->username,
        config->access_token, config->user_id, config->device_id};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        if (strpbrk(values[i], "\r\n")) return false;
    FILE *f = fopen(CONFIG_PATH ".part", "w");
    if (!f) return false;

    fprintf(f, "server_url=%s\n", config->server_url);
    fprintf(f, "username=%s\n", config->username);
    fprintf(f, "access_token=%s\n", config->access_token);
    fprintf(f, "user_id=%s\n", config->user_id);
    fprintf(f, "device_id=%s\n", config->device_id);
    fprintf(f, "audio_bitrate=%d\n", config->audio_bitrate);
    fprintf(f, "video_bitrate=%d\n", config->video_bitrate);
    fprintf(f, "prefer_transcoding=%d\n", config->prefer_transcoding ? 1 : 0);
    fprintf(f, "auto_advance=%d\n", config->auto_advance ? 1 : 0);

    bool ok = !ferror(f);
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(CONFIG_PATH ".part"); return false; }
    /* libctru overwrites rename destinations by deleting them first. Move the
     * previous settings aside so interrupted replacement can recover on boot. */
    bool backed_up = rename(CONFIG_PATH, CONFIG_PATH ".bak") == 0;
    if (!backed_up && errno != ENOENT) {
        remove(CONFIG_PATH ".part");
        return false;
    }
    if (rename(CONFIG_PATH ".part", CONFIG_PATH) == 0) return true;
    if (backed_up) rename(CONFIG_PATH ".bak", CONFIG_PATH);
    remove(CONFIG_PATH ".part");
    return false;
}

void config_ensure_device_id(jfin_config_t *config)
{
    if (config->device_id[0] != '\0')
        return;

    /* Generate a pseudo-random device ID.
     * On real hardware we could use the console serial, but for
     * compatibility we just use svcGetSystemTick(). */
    u64 tick = svcGetSystemTick();
    snprintf(config->device_id, sizeof(config->device_id),
             "3ds-%08lx%08lx", (unsigned long)(tick >> 32), (unsigned long)(tick & 0xFFFFFFFF));

    config_save(config);
}

bool config_save_session(jfin_config_t *config, const char *server_url,
                         const char *access_token, const char *user_id,
                         const char *username)
{
    snprintf(config->server_url, sizeof(config->server_url), "%s", server_url);
    snprintf(config->access_token, sizeof(config->access_token), "%s", access_token);
    snprintf(config->user_id, sizeof(config->user_id), "%s", user_id);
    snprintf(config->username, sizeof(config->username), "%s", username);
    return config_save(config);
}
