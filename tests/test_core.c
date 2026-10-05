#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "ui/timeline.h"
#include "util/config.h"
int main(void) {
    int64_t duration=36000000000LL;
    assert(timeline_position(20,duration)==0);
    assert(timeline_position(160,duration)==duration/2);
    assert(timeline_position(300,duration)==duration-10000000LL);
    assert(timeline_position(-100,duration)==0);
    assert(timeline_position(999,duration)==duration-10000000LL);
    assert(timeline_position(300,5000000)==0);
    assert(timeline_position(100,0)==0);
    assert(timeline_position(300,INT64_MAX)==INT64_MAX-10000000LL);
    assert(timeline_hit(100,40,duration));
    assert(!timeline_hit(100,80,duration));
    assert(!timeline_hit(100,40,0));
    mkdir("sdmc:",0755);
    jfin_config_t config, loaded;
    assert(!config_load(&config));
    strcpy(config.server_url,"https://example.com/jellyfin");
    strcpy(config.username,"user");
    strcpy(config.access_token,"token");
    strcpy(config.user_id,"userid");
    config_ensure_device_id(&config);
    char id[64]; strcpy(id,config.device_id);
    assert(config_load(&loaded));
    assert(!strcmp(config.server_url,loaded.server_url));
    assert(!strcmp(loaded.access_token,"token"));
    config_ensure_device_id(&loaded);
    assert(!strcmp(loaded.device_id,id));
    strcpy(config.username,"bad\naccess_token=injected");
    assert(!config_save(&config));
    assert(config_load(&loaded)); assert(!strcmp(loaded.username,"user"));
    strcpy(config.username,"new user");
    mkdir(CONFIG_PATH ".part",0755);
    assert(!config_save(&config));
    assert(config_load(&loaded)); assert(!strcmp(loaded.username,"user"));
    assert(rename(CONFIG_PATH, CONFIG_PATH ".bak") == 0);
    assert(config_load(&loaded)); assert(!strcmp(loaded.username,"user"));
    puts("PASS: timeline bounds, short clips, overflow, saved token, device ID and failed-save preservation and backup recovery");
}
