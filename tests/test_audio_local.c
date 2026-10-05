#include <3ds.h>
#include <assert.h>
#include <stdio.h>
#include <mpg123.h>
#include "audio/player.h"
static s16 first[64];static bool captured;
void host_audio_buffer(const s16 *data,unsigned frames){if(!captured&&frames>=32){memcpy(first,data,sizeof(first));captured=true;}}
int main(int argc,char **argv){
 assert(argc==2);assert(audio_player_init());int64_t target=11230000;
 assert(audio_player_play(argv[1],30000000,target));
 for(int i=0;i<300&&audio_player_get_status().state!=PLAYER_STOPPED;i++)svcSleepThread(10000000);
 assert(audio_player_get_status().state==PLAYER_STOPPED && captured);
 mpg123_handle *reference=mpg123_new(NULL,NULL);assert(reference);mpg123_param(reference,MPG123_FLAGS,MPG123_FORCE_STEREO,0);
 assert(mpg123_open(reference,argv[1])==MPG123_OK);long rate;int channels,encoding;
 assert(mpg123_getformat(reference,&rate,&channels,&encoding)==MPG123_OK);
 off_t sample=(off_t)(target/10000000*rate+target%10000000*rate/10000000);
 assert(mpg123_seek(reference,sample,SEEK_SET)>=0);s16 expected[64];size_t got=0;
 int result=mpg123_read(reference,(unsigned char *)expected,sizeof(expected),&got);
 if(result==MPG123_NEW_FORMAT)result=mpg123_read(reference,(unsigned char *)expected,sizeof(expected),&got);
 assert(got==sizeof(expected) && !memcmp(first,expected,sizeof(first)));
 mpg123_close(reference);mpg123_delete(reference);audio_player_cleanup();
 puts("PASS: production saved-MP3 resume decodes the exact samples at the requested local seek position (mock DSP)");
}
