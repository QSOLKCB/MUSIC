#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
    MusicProject p; char error[256]; FILE *file; long length; unsigned char *bytes;
    music_init(&p);
    if(argc==2 && !strcmp(argv[1],"--version")) { puts(MUSIC_VERSION); return 0; }
    if(argc!=3 || strcmp(argv[1],"--inspect")) { puts("Usage: music-cli --inspect project.music\n       music-cli --version"); return argc==1?0:2; }
    file=fopen(argv[2],"rb");
    if(!file) { perror(argv[2]); return 1; }
    if(fseek(file,0,SEEK_END) || (length=ftell(file))<0 || (unsigned long)length>MUSIC_MEMORY_LIMIT+16384u || fseek(file,0,SEEK_SET)) {
        fclose(file); fputs("Invalid project size.\n",stderr); return 1;
    }
    bytes=(unsigned char*)malloc((size_t)length+1);
    if(!bytes || fread(bytes,1,(size_t)length,file)!=(size_t)length) { free(bytes); fclose(file); return 1; }
    fclose(file);
    if(!music_load(&p,bytes,(size_t)length,error)) { free(bytes); fprintf(stderr,"%s\n",error); return 1; }
    free(bytes);
    printf("MUSIC %s\n%u tracks | %u BPM | %.3f seconds | 48000 Hz stereo\n",MUSIC_VERSION,p.count,p.bpm,(double)music_duration(&p)/MUSIC_RATE);
    music_free(&p); return 0;
}
