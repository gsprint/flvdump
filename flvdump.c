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

u_char isAnnexB4Bytes(u_char *input, u_int32_t pos, u_int32_t size) {
    u_char ret = 0;

    if ((pos + 4 < size) && (*(input + pos ) == 0x0) && (*(input + pos + 1) == 0x00) && (*(input + pos + 2) == 0x00) && (*(input + pos + 3) == 0x01))
        ret = 1;

    return ret;
}

u_char isAnnexB3Bytes(u_char *input, u_int32_t pos, u_int32_t size) {
    u_char ret = 0;

    if ((pos + 3 < size) && (*(input + pos ) == 0x0) && (*(input + pos + 1) == 0x00) && (*(input + pos + 2) == 0x01))
        ret = 1;

    return ret;
}

u_int32_t findAnnexBStartCode(u_char *input, u_int32_t pos, u_int32_t size, u_char annexStartCodeBytes) {
    while ((pos + annexStartCodeBytes) < size) {
        if (annexStartCodeBytes == 3) {
            if (!isAnnexB3Bytes(input, pos, size))
                pos++;
            else
                break;
        }
        else {
            if (!isAnnexB4Bytes(input, pos, size))
                pos++;
            else
                break;
        }
    }
    return pos;
}

// Main program entry
int main(int argc, char **argv)
{
    struct stat info;
    int         source;
    u_int32_t   last = 0, time, size, dsize;
    u_char      *data, *current, *lastCurrentTag, type, bsize = 0, bdump = 0, blazy = 0, bshowavcnalu = 0, bshowoffsets =0;
    u_char      bisannexB = 0, naluSizeLength = 4;
    uint32_t    videoframesfromlastkf = 0;
    char        err[1024];

    TRAP(argc < 2, 1, "Usage: fldump [-s] [-o] [-d] [-l] [-n] <file>\n s: Show sizes, s: Show offsets, d: show data dump, l: lazy mode, do not break on some errors, -n Decode AVCC/AnnexB NALU type");
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
        else if (! strcmp(*argv, "-l"))
        {
            blazy = 1;
        }
        else if (! strcmp(*argv, "-n"))
        {
            bshowavcnalu = 1;
        }
        else if (! strcmp(*argv, "-o"))
        {
            bshowoffsets = 1;
        }
        else
        {
            break;
        }
        argv ++;
    }
    TRAP(!*argv || stat(*argv, &info) < 0 || !S_ISREG(info.st_mode) || (source = open(*argv, O_RDONLY)) < 0 || !(data = mmap(NULL, info.st_size, PROT_READ, MAP_PRIVATE, source, 0)), 2, "Cannot open FLV file - aborting");
    TRAP(info.st_size < 9 + 4 || memcmp(data, "FLV", 3) || FLVSIZE(data + 5) != 9 || FLVSIZE(data + 9) != 0, 3, "Invalid FLV file - aborting");
    TRAP(!blazy && info.st_size - (last = FLVSIZE(data + info.st_size - 4)) < 0, 4, "Malformed FLV file - aborting");
    if (!blazy && info.st_size > 9 + 4)
    {
        last = TAGTIME(data + info.st_size - last) - TAGTIME(data + 9 + 4 + 4);
    }
    printf("Filename: %s\n", *argv);
    printf("Filesize: %lld bytes\n", info.st_size);
    printf("Version:  %d\n", data[3]);
    printf("Duration: %s\n", ms2tc(last, 1));
    printf("Tracks:   %s%s\n\n", (data[4] & 0x04) ? "audio " : "", (data[4] & 0x01) ? "video" : "");
    current = data + 9 + 4;
    while (1)
    {
        lastCurrentTag = data;

        if (current + 4 >= data + info.st_size)
        {
            break;
        }
        type     = *current;
        size     = TAGSIZE(current + 1);
        time     = TAGTIME(current + 4);
        printf("%s ", ms2tc(time, 0));
        if (bshowoffsets) {
            printf("(%6ld) ", current - lastCurrentTag);
        }
        if (bsize)
        {
            printf("%6d ", size);
        }
        printf("%s ", TAGTYPE(type));
        current += 11;
        if (type == 8) // Audio
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
        else if (type == 9) // Video
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
                printf("sh");
                if (bshowavcnalu) {
                    u_char pos = 2;
                    if ((pos + 3) >= size) {
                        TRAP(!blazy, 6, "Can not read Composition time, this looks like annexB, try -l");
                        bisannexB = 1;
                    }
                    else
                    {
                        int compositionTime = *(current + pos) << 16 | *(current + pos + 1) << 8 | *(current + pos + 2);
                        // Should be 0 in AVCC Header
                        printf(" +%dms", compositionTime);

                        pos += 3;
                        if ((pos + 6) >= size) {
                            TRAP(!blazy, 7, "Can not read AVCDecoderConfigurationRecord, this looks like annexB, try -l");
                            bisannexB = 1;
                        }
                        else
                        {
                            printf(", AVCDecoderConfigurationRecord:");

                            // AVCDecoderConfigurationRecord
                            u_char configurationVersion = *(current + pos);
                            u_char avcProfileIndication = *(current + pos + 1);
                            u_char profile_compatibility = *(current + pos + 2);
                            u_char AVCLevelIndication = *(current + pos + 3);
                            u_char reserved252 = *(current + pos + 4) & 0b11111100; // 252
                            u_char lengthSizeMinusOne = *(current + pos + 4) & 0b11;
                            naluSizeLength = lengthSizeMinusOne + 1;
                            u_char reserved224 = *(current + pos + 5) & 0b11100000; //224
                            u_char numOfSequenceParameterSets = *(current + pos + 5) & 0b00011111;
                            u_int32_t spsLengthBytes = 0, ppsLengthBytes = 0;
                            pos += 6;
                            u_int32_t i = 0;
                            while ((i < numOfSequenceParameterSets) && ((pos + 2) < size)) {
                                spsLengthBytes = *(current + pos) << 8 | *(current + pos + 1);
                                pos += 2;
                                pos += spsLengthBytes;
                                i++;
                                sprintf(err, "SPS size is probably wrong (numOfSequenceParameterSets: %d, spsLengthBytes: %d, pos: %d, size: %d), out of bounds reading SPS", numOfSequenceParameterSets, spsLengthBytes, pos, size);
                                TRAP(!blazy && (pos > size), 8, err);
                            }
                            u_char numOfPictureParameterSets = *(current + pos);
                            pos++;
                            i = 0;
                            while ((i < numOfPictureParameterSets) && ((pos + 2) < size)) {
                                ppsLengthBytes = *(current + pos) << 8 | *(current + pos + 1);
                                pos += 2;
                                pos += ppsLengthBytes;
                                i++;
                                sprintf(err, "PPS size is probably wrong (numOfPictureParameterSets: %d, ppsLengthBytes: %d, pos: %d, size: %d), out of bounds reading PPS", numOfPictureParameterSets, ppsLengthBytes, pos, size);
                                TRAP(!blazy && (pos > size), 9, err);
                            }
                            printf(" ConfigurationVersion: %d, avcProfileIndication: %d, profile_compatibility: %d, AVCLevelIndication: %d, reserved252: %d, lengthSize: %d, reserved224: %d, numOfSequenceParameterSets: %d, spsLengthBytes = %d, numOfPictureParameterSets: %d, ppsLengthBytes: %d", configurationVersion, avcProfileIndication, profile_compatibility, AVCLevelIndication, reserved252 , naluSizeLength, reserved224, numOfSequenceParameterSets, spsLengthBytes, numOfPictureParameterSets, ppsLengthBytes);
                            if ( avcProfileIndication != 66 && avcProfileIndication != 77 && avcProfileIndication != 88 ) {
                                sprintf(err, "AVCDecoderConfigurationRecord seems malformed, for avcProfileIndication = %d needs to contain extended params (pos: %d, size: %d)", avcProfileIndication, pos, size);
                                TRAP(!blazy && (pos + 4 > size), 10, err);
                                if (pos + 4 <= size) {
                                    u_char reserved252 = *(current + pos) & 0b11111100; // 252
                                    u_char chromaFormat = *(current + pos) & 0b11;
                                    u_char reserved248 = *(current + pos + 1) & 0b11111000; // 248
                                    u_char bitDepthLumaMinus8 = *(current + pos + 1) & 0b111;
                                    u_char bitDepthLuma = bitDepthLumaMinus8 + 8;
                                    u_char reserved248_2 = *(current + pos + 2) & 0b11111000; // 248
                                    u_char bitDepthChromaMinus8 = *(current + pos + 2) & 0b111;
                                    u_char bitDepthChroma = bitDepthChromaMinus8 + 8;
                                    u_char numOfSequenceParameterSetExt = *(current + pos + 3);
                                    printf(" reserved252: %d, chromaFormat: %d, reserved248: %d, bitDepthLuma: %d, reserved248_2: %d, bitDepthChroma: %d, numOfSequenceParameterSetExt: %d", reserved252, chromaFormat, reserved248, bitDepthLuma, reserved248_2 , bitDepthChroma, numOfSequenceParameterSetExt);
                                    pos += 4;
                                    i = 0;
                                    while ((i < numOfSequenceParameterSetExt) && ((pos + 2) < size)) {
                                        u_int32_t spsExtLengthBytes = *(current + pos) << 8 | *(current + pos + 1);
                                        pos += 2;
                                        pos += spsExtLengthBytes;
                                        i++;
                                        sprintf(err, "SPS Ext size is probably wrong (numOfSequenceParameterSetExt: %d, spsExtLengthBytes: %d, pos: %d, size: %d), out of bounds reading SPS", numOfSequenceParameterSetExt, spsExtLengthBytes, pos, size);
                                        TRAP(!blazy && (pos > size), 11, err);
                                    }
                                }                                
                                //TODO
                            }
                        }
                    }
                }
                printf("\n");
                dsize = size - 5;
            }
            else if ((*current & 0x0f) == 7 && *(current + 1) == 2)
            {
                if (bshowavcnalu)
                    printf("AVCC endOfSeq\n");

                dsize = 0;
            }
            else // NALU
            {
                switch (*current >> 4)
                {
                    case 1:
                        printf("kf (last kf: %d frames ago)", videoframesfromlastkf);
                        videoframesfromlastkf = 0;
                    break;
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
                        if (bshowavcnalu)
                        {
                            printf("+%dms", (*(current + 2) << 16) + (*(current + 3) << 8) + *(current + 4));
                            u_int64_t pos = 5;
                            if (bisannexB)
                            {
                                if ((pos + 3) < size)
                                {
                                    printf(", AnnexB NALUs: ");
                                    u_char annexBSeqBytes = 0;
                                    if (isAnnexB3Bytes(current, pos, size))
                                        annexBSeqBytes = 3;
                                    else if (isAnnexB4Bytes(current, pos, size))
                                        annexBSeqBytes = 4;

                                    TRAP(!blazy && annexBSeqBytes !=3 && annexBSeqBytes != 4, 6, "We could not find annexB starting code");
                                    if (annexBSeqBytes !=3 && annexBSeqBytes != 4)
                                        annexBSeqBytes = 4; // Default to 4 and hope for the best

                                    // Show NALU types
                                    while ((pos + annexBSeqBytes + 1) < size)
                                    {
                                        pos += annexBSeqBytes;
                                        u_char nal_unit_type = *(current + pos) & 0b00011111;
                                        u_int32_t next_pos = findAnnexBStartCode(current, pos, size, annexBSeqBytes);
                                        printf("NALU type: %d (size: %llu) - ", nal_unit_type, next_pos - pos);
                                        pos = next_pos;
                                    }
                                }
                            }
                            else
                            {
                                printf(", AVCC NALUs: ");
                                // Show NALU types
                                while ((pos + naluSizeLength) < size)
                                {
                                    u_int32_t naluSize = 0;
                                    u_char i = 0;
                                    while (i < naluSizeLength)
                                    {
                                        naluSize = naluSize | *(current + pos + i) << ((naluSizeLength - 1 - i)*8);
                                        i++;
                                    }
                                    pos += naluSizeLength;
                                    if (naluSize > 0)
                                    {
                                        u_char nal_unit_type = *(current + pos) & 0b00011111;
                                        printf("NALU type: %d (size: %d) - ", nal_unit_type, naluSize);
                                    }
                                    pos += naluSize;
                                }
                            }
                        }
                        break;
                }
                printf("\n");
                dsize = size - ((*current & 0x0f) == 7 ? 5 : 1);

                videoframesfromlastkf++;
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
