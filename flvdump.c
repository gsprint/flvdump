// Mandatory includes
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>

// Defines and typedefs
#define  TRAP(c, s, m)  if (c) {fprintf(stderr, "%s\n", (m)); return (s);}
#define  FLVSIZE(d)     ntohl(*(u_int32_t *)(d))
#define  TAGTYPE(t)     ((t) == 8 ? "audio" : ((t) == 9 ? "video" : ((t) == 18 ? "data " : "?????")))
#define  TAGSIZE(d)     ((*(d) << 16) + (*((d) + 1) << 8) + *((d) + 2))
#define  TAGTIME(d)     ((*(d) << 16) + (*((d) + 1) << 8) + *((d) + 2) + (*((d) + 3) << 24))
#ifndef  min
#define  min(a, b)      ((a) < (b) ? (a) : (b))
#endif

// Human-readable timestamp
char *ms2tc(u_int32_t ms, u_char mode)
{
    u_int32_t   hours, minutes, seconds;
    static char human[32];

    hours    = ms / 3600000;
    ms      -= hours * 3600000;
    minutes  = ms / 60000;
    ms      -= minutes * 60000;
    seconds  = ms / 1000;
    ms      -= seconds * 1000;
    if (mode)
    {
        if (hours)
        {
            sprintf(human, "%dh ", hours);
        }
        if (hours || minutes)
        {
            sprintf(human + strlen(human), "%dmn ", minutes);
        }
        sprintf(human + strlen(human), "%ds", seconds);
    }
    else
    {
        sprintf(human, "%d:%02d:%02d.%03d", hours, minutes, seconds, ms);
    }
    return human;
}

// Human-readable memory dump
void hexdump(u_char *input, u_int32_t size, u_int32_t indent)
{
    u_int32_t index1, index2 = 0;
    char      ascii[64], spaces[64];

    memset(spaces, ' ', sizeof(spaces));
    spaces[min(indent, sizeof(spaces) - 1)] = 0;
    sprintf(ascii, "%%%ldd 00000000  ", min(indent, sizeof(spaces) - 1) - 1);
    printf(ascii, size);
    memset(ascii, 0, sizeof(ascii));
    for (index1 = 0; index1 < size; index1 ++)
    {
        if (index1 && ! (index1 % 16))
        {
            printf(" ");
            if (! (index1 % 32))
            {
                printf("%s\n%s%08x  ", ascii, spaces, index1);
                memset(ascii, 0, sizeof(ascii));
                index2 = 0;
            }
        }
        printf("%02x ", input[index1]);
        ascii[index2 ++] = isprint(input[index1]) ? input[index1] : '.';
    }
    index1 = strlen(ascii);
    if (index1)
    {
        for (index2 = 0; index2 < 32 - index1; index2 ++)
        {
            printf("   ");
        }
        printf("%s %s\n", index1 >= 16 ? "" : " ", ascii);
    }
}

// Main program entry
int main(int argc, char **argv)
{
    struct stat info;
    int         source;
    u_int32_t   last = 0, time, size, dsize;
    u_char      *data, *current, type, bsize = 0, bdump = 0;

    TRAP(argc < 2, 1, "Usage: fldump [-s] [-d] <file>");
    argv ++;
    while (*argv)
    {
        if (! strcmp(*argv, "-s"))
        {
            bsize = 1;
        }
        else if (! strcmp(*argv, "-d"))
        {
            bdump = 1;
        }
        else
        {
            break;
        }
        argv ++;
    }
    TRAP(!*argv || stat(*argv, &info) < 0 || !S_ISREG(info.st_mode) || (source = open(*argv, O_RDONLY)) < 0 || !(data = mmap(NULL, info.st_size, PROT_READ, MAP_PRIVATE, source, 0)), 2, "Cannot open FLV file - aborting");
    TRAP(info.st_size < 9 + 4 || memcmp(data, "FLV", 3) || FLVSIZE(data + 5) != 9 || FLVSIZE(data + 9) != 0, 3, "Invalid FLV file - aborting");
    TRAP(info.st_size - (last = FLVSIZE(data + info.st_size - 4)) < 0, 4, "Malformed FLV file - aborting");
    if (info.st_size > 9 + 4)
    {
        last = TAGTIME(data + info.st_size - last) - TAGTIME(data + 9 + 4 + 4);
    }
    printf("Filename: %s\n", argv[1]);
    printf("Filesize: %lu bytes\n", info.st_size);
    printf("Version:  %d\n", data[3]);
    printf("Duration: %s\n", ms2tc(last, 1));
    printf("Tracks:   %s%s\n\n", (data[4] & 0x04) ? "audio " : "", (data[4] & 0x01) ? "video" : "");
    current = data + 9 + 4;
    while (1)
    {
        if (current + 4 >= data + info.st_size)
        {
            break;
        }
        type     = *current;
        size     = TAGSIZE(current + 1);
        time     = TAGTIME(current + 4);
        current += 11;
        printf("%s ", ms2tc(time, 0));
        if (bsize)
        {
            printf("%6d ", size);
        }
        printf("%s ", TAGTYPE(type));
        if (type == 8)
        {
            switch (*current >> 4)
            {
                case  0:  printf("pcm "); break;
                case  1:  printf("adpcm "); break;
                case  2:  printf("mp3 "); break;
                case  3:  printf("pcm_le "); break;
                case  4:  printf("nellymoser_16kHz "); break;
                case  5:  printf("nellymoser_8kHz "); break;
                case  6:  printf("nellymoser "); break;
                case  7:  printf("g711_alaw "); break;
                case  8:  printf("g711_ulaw "); break;
                case 10:  printf("aac "); break;
                case 11:  printf("speex "); break;
                case 14:  printf("mp3_8kHz "); break;
                case 15:  printf("device "); break;
                default: printf("??? "); break;
            }
            dsize = size - ((*current >> 4) == 10 ? 2 : 1);
            if ((*current >> 4) == 10 && *(current + 1) == 0)
            {
                printf("sh\n");
            }
            else
            {
                switch ((*current >> 2) & 0x03)
                {
                    case  0:  printf("5.5kHz "); break;
                    case  1:  printf("11kHz "); break;
                    case  2:  printf("22kHz "); break;
                    case  3:  printf("44kHz "); break;
                }
                printf("%s ", (*current & 0x02) ? "16bit" : "8bit");
                printf("%s ", (*current & 0x01) ? "stereo" : "mono");
                printf("\n");
            }
        }
        else if (type == 9)
        {
            switch (*current & 0x0f)
            {
                case 1:  printf("jpeg "); break;
                case 2:  printf("h263 "); break;
                case 3:  printf("screen "); break;
                case 4:  printf("vp6 "); break;
                case 5:  printf("vp6a "); break;
                case 6:  printf("screen2 "); break;
                case 7:  printf("avc "); break;
                default: printf("??? "); break;
            }
            if ((*current & 0x0f) == 7 && *(current + 1) == 0)
            {
                printf("sh\n");
                dsize = size - 5;
            }
            else if ((*current & 0x0f) == 7 && *(current + 1) == 2)
            {
                printf("eos\n");
                dsize = 0;
            }
            else
            {
                switch (*current >> 4)
                {
                    case 1:  printf("kf "); break;
                    case 2:  printf("if "); break;
                    case 3:  printf("dif "); break;
                    case 4:  printf("gkf "); break;
                    case 5:  printf("vicfcf "); break;
                    default: printf("??? "); break;
                }
                switch (*current & 0x0f)
                {
                    case 2:
                         switch (((*(current + 4) & 0x03) << 1) + (*(current + 5) >> 7))
                         {
                             case 0: printf("%dx%d ",
                                            ((*(current + 5) << 1) & 0xfe) + ((*(current + 6) >> 7) & 0x01),
                                            ((*(current + 6) << 1) & 0xfe) + ((*(current + 7) >> 7) & 0x01)
                                           );
                                     break;
                             case 1: printf("%dx%d ",
                                            ((((*(current + 5) << 1) & 0xfe) + ((*(current + 6) >> 7) & 0x01)) << 8) + ((*(current + 6) << 1) & 0xfe) + ((*(current + 7) >> 7) & 0x01),
                                            ((((*(current + 7) << 1) & 0xfe) + ((*(current + 8) >> 7) & 0x01)) << 8) + ((*(current + 8) << 1) & 0xfe) + ((*(current + 9) >> 7) & 0x01)
                                           );
                                     break;
                             case 2: printf("352x288 "); break;
                             case 3: printf("176x144 "); break;
                             case 4: printf("128x96 ");  break;
                             case 5: printf("320x240 "); break;
                             case 6: printf("160x120 "); break;
                         }
                         break;

                    case 3:
                    case 6:
                         printf("%dx%d ", ((*(current + 1) & 0x0f) << 8) + *(current + 2), ((*(current + 3) & 0x0f) << 8) + *(current + 4));
                         break;

                    case 4:
                         printf("%dx%d ", (*(current + 8) * 16) - (*(current + 1) >> 4), (*(current + 7) * 16) - (*(current + 1) & 0x0f));
                         break;

                    case 5:
                         break;

                    case 7:
                        printf("+%dms", (*(current + 2) << 24) + (*(current + 3) << 8) + *(current + 4));
                        break;
                }
                printf("\n");
                dsize = size - ((*current & 0x0f) == 7 ? 5 : 1);
            }
        }
        else
        {
            printf("\n");
            dsize = size;
        }
        if (bdump && dsize)
        {
            hexdump(current + size - dsize, dsize, 12 + (bsize * 7));
        }
        current += size + 4;
    }
    return 0;
}
