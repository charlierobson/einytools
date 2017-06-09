#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

typedef unsigned char BYTE;
typedef unsigned short WORD;

BYTE myData[512];

// maximum number of sectors is 10 per track, 80 tracks, single sided disk
// - until the ROM handles different disk types this is all we can handle
// ... but for conversipon processes this limitation isn't present, so double
//     the number of tracks stored so we can handle 2 sided images
int gSectorOffsets[10*80*2];

typedef struct
{
	char headerString[34];
	char creatorName[14];
	BYTE nTracks;
	BYTE nSides;
	BYTE unused[2];
	BYTE trackSizeTable[256-52];
}
DISK_INFORMATION_BLOCK;

typedef struct
{
	char headerString[13];
	BYTE unused[3];
	BYTE trackNumber;
	BYTE sideNumber;
	BYTE unused2[2];
	BYTE sectorSize;
	BYTE nSectors;
	BYTE GAPHASH3Length;
	BYTE filler;
}
TRACK_INFORMATION_BLOCK;

typedef struct
{
	BYTE track;
	BYTE side;
	BYTE sectorID;
	BYTE sectorSize;
	BYTE FDCStatusRegister1;
	BYTE FDCStatusRegister2;
	WORD dataLength;
}
SECTOR_INFORMATION_BLOCK;

enum
{
	DSK_OK,
	DSK_NOT_DSK_FILE,
	DSK_TOO_MANY_SIDES,
	DSK_TOO_MANY_TRACKS,
	DSK_TOO_MANY_SECTORS
};


char* cpUpTo(char* dest, const char* src, int max)
{
	int i;
	for(i = 0; i < max; ++i)
	{
		if (*src == ' ')
			break;
		if (*src == '/')
			*dest = '-';
		else
			*dest = *src & 127;
		++dest;
		++src;
	}

	return dest;
}


int parseDSK(FILE* infile, int* sectorOffsets, int* tracks, int* sectors)
{
	int i, j;

	DISK_INFORMATION_BLOCK* dib = (DISK_INFORMATION_BLOCK*)myData;
	TRACK_INFORMATION_BLOCK* tib = (TRACK_INFORMATION_BLOCK*)myData;
	SECTOR_INFORMATION_BLOCK* sib;

	*tracks = 0;
	*sectors = 0;

	fread(myData, 1, sizeof(DISK_INFORMATION_BLOCK), infile);

	if (!memcmp(myData, "EXTENDED", sizeof("EXTENDED")))
	{
		return DSK_NOT_DSK_FILE;
	}

	int sides = dib->nSides;
	printf("%d sides\n", sides);

	*tracks = dib->nTracks;
	if (*tracks > 80)
	{
		return DSK_TOO_MANY_TRACKS;
	}

	// only handling 10 sectors per track
	// 0x15(00)  =  21  =  256+20*256  =  sizeof(header)+10*512

	for (i = 0; i < *tracks * sides; ++i)
	{
		if (dib->trackSizeTable[i] != 0x15)
		{
			return DSK_TOO_MANY_SECTORS;
		}
	}

	// ok, work out the sector offsets in the file

	for (i = 0; i < *tracks * sides; ++i)
	{
		fread(myData, 1, 256, infile);
		int sectorBase = ftell(infile);

		sib = (SECTOR_INFORMATION_BLOCK*)(myData+sizeof(TRACK_INFORMATION_BLOCK));

		for (j = 0; j < tib->nSectors; ++j)
		{
			int x = tib->trackNumber * 10 + tib->sideNumber * 80 + sib->sectorID;
			//printf("trk %d sid %d sec %d : %d\n", tib->trackNumber, tib->sideNumber, sib->sectorID, x);

			sectorOffsets[x] = sectorBase;
			sectorBase += 512;
			++*sectors;
			++sib;
		}

		fseek(infile, 10*512, SEEK_CUR);
	}

	return DSK_OK;
}


void extractToRaw(char* inname, FILE* infile, int sectors)
{
	int i;
	FILE* outfile;

	char filename[128];

	strcpy(filename, inname);
	strcat(filename, ".raw");

	if ((outfile = fopen(filename, "wb")) != NULL)
	{
		for (i = 0; i < sectors; ++i)
		{
			fseek(infile, gSectorOffsets[i], SEEK_SET);
			fread(myData, 1, 512, infile);
			fwrite(myData, 1, 512, outfile);
		}

		fclose(outfile);
	}
}


void extractAll(char* inname, FILE* infile)
{
	FILE* outfile = NULL;

	char outname[256];
	char outname2[256];

	char* outp;

	int i, res;

	strcpy(outname, inname);
	outp = strchr(outname, '.');
	*outp = 0;

	//res = mkdir(outname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); // osx/linux
	res = _mkdir(outname); // windows
	printf("%s  res: %d", outname, res);

	strcat(outname, "/");

	for (i = 20; i < 24; ++i)
	{
		int j, k;
		char bfr [13]={0};
		typedef struct
		{
			BYTE user;
			char filename[8+3];
			BYTE extent;
			BYTE x, y;
			BYTE blockCount;
			short blockIDs[8];
		}
		dirent;
		dirent* p = (dirent*)myData;

		BYTE sectordata[512];

		fseek(infile, gSectorOffsets[i], SEEK_SET);
		fread(myData, 1, 512, infile);
		for (j = 0; j < 16; ++j)
		{
			if (p->user == 0 && p->blockCount != 0xe5)
			{
				char* pp = cpUpTo(bfr, &p->filename[0], 8);
				if (p->filename[8] != ' ')
				{
					*pp = '.';
					++pp;
				}
				pp = cpUpTo(pp, &p->filename[8], 3);
				*pp = 0;

				if (p->filename[8] & 128)
				{
					printf("*"); // read-only
				}
				else
				{
					printf(" ");
				}

				// when we encounter a new extent we'll close off the old file and start a new one
				int err = 0;
				if (p->extent == 0)
				{
					if (outfile != NULL)
					{
						fclose(outfile);
						outfile = NULL;
					}
					strcpy(outname2, outname);
					strcat(outname2, bfr);
					outfile = fopen(outname2, "wb");
				}
				if (outfile != NULL && p->blockCount)
				{
					printf("%s, [%d] $%02x\n", bfr, p->extent, p->blockCount);

					// write all of the blocks associated with this extent
					for (k = 0; k < p->blockCount;)
					{
						int blockbase = p->blockIDs[k / 16] - 1;
						int offset = k % 16;
						int sector = 24 + (blockbase * 4) + (offset / 4);
						int omod = offset % 4;
						int byteOffset = (offset % 4) * 128;

						//printf("%d, %d, %d, %d\n", k, blockbase, sector, byteOffset);

						if (omod == 0)
						{
							fseek(infile, gSectorOffsets[sector], SEEK_SET);
							fread(sectordata, 1, 512, infile);
						}
						fwrite(&sectordata[byteOffset], 1, 128, outfile);
						++k;
					}
				}
				++p;
			}
		}
	}

	// end of disk. close off any open file
	if (outfile != NULL)
	{
		fclose(outfile);
	}

	printf("\n");
}




int main(int argc, char** argv)
{
	FILE* infile;
	int tracks, sectors;

	assert(sizeof(DISK_INFORMATION_BLOCK) == 256);
	assert(sizeof(TRACK_INFORMATION_BLOCK) == 24);
	assert(sizeof(SECTOR_INFORMATION_BLOCK) == 8);
	assert(sizeof(int) == 4);

	infile = fopen(argv[1], "rb");
	if (infile)
	{
		int ret = parseDSK(infile, gSectorOffsets, &tracks, &sectors);
		if (!ret)
		{
			extractAll(argv[1], infile);
		}

		fclose(infile);
	}
	else
	{
		printf("Unable to open input file '%s'", argv[1]);
	}
}
