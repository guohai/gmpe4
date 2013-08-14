#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>

#include <time.h>
#include <string.h>

#define ALOGV \
			printf \

typedef int status_t;

typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;

static const char *VERSION = "0.9.0";

static const int OK = 0;
static const int ERR_IO = -201;
static const int ERR_MALFORMED = -202;

#define FOURCC(c1, c2, c3, c4) \
    (c1 << 24 | c2 << 16 | c3 << 8 | c4)

static void MakeFourCCString(uint32_t x, char *s) {
    s[0] = x >> 24;
    s[1] = (x >> 16) & 0xff;
    s[2] = (x >> 8) & 0xff;
    s[3] = x & 0xff;
    s[4] = '\0';
}

// XXX warning: these won't work on big-endian host.
uint64_t ntoh64(uint64_t x) {
    return ((uint64_t)ntohl(x & 0xffffffff) << 32) | ntohl(x >> 32);
}

uint64_t hton64(uint64_t x) {
    return ((uint64_t)htonl(x & 0xffffffff) << 32) | htonl(x >> 32);
}

uint16_t U16_AT(const uint8_t *ptr) {
    return ptr[0] << 8 | ptr[1];
}

uint32_t U32_AT(const uint8_t *ptr) {
    return ptr[0] << 24 | ptr[1] << 16 | ptr[2] << 8 | ptr[3];
}

uint64_t U64_AT(const uint8_t *ptr) {
    return ((uint64_t)U32_AT(ptr)) << 32 | U32_AT(ptr + 4);
}

status_t parseChunk(int fd, off64_t *offset, int depth);

// Given a time in seconds since Jan 1 1904, produce a human-readable string.
static void convertTimeToDate(int64_t time_1904, char *s) {
    time_t time_1970 = time_1904 - (((66 * 365 + 17) * 24) * 3600);

    char tmp[32];
    strftime(tmp, sizeof(tmp), "%Y-%m-%d T %H:%M:%S .000Z", gmtime(&time_1970));

    strncpy(s, tmp, sizeof(tmp));
}

ssize_t readAt(int fd, off64_t offset, void *buff, size_t size)
{
	lseek64(fd, offset, SEEK_SET);
	return read(fd, buff, size);
}

/**
	information for this presentation
*/

static int hasVideoTrack = 0;
static int hasAudioTrack = 0;

static int64_t creationTime;
static int64_t modificationTime;

typedef struct resolution {
	uint16_t width;
	uint16_t height;
} resolution_t;

resolution_t resolution;

static uint64_t duration;

static uint8_t trackCount;

static uint16_t channels;
static uint16_t sampleRate;

void summary()
{
	char cTime[32];
	char mTime[32];
	convertTimeToDate(creationTime, cTime);
	convertTimeToDate(modificationTime, mTime);

	ALOGV("\n\nsummary of this presentation\n");
	ALOGV("  duration: %lld s\n", duration);
	ALOGV("  created at: %s\n", cTime);
	ALOGV("  last modified at: %s\n", mTime);

	ALOGV("  has %d track(s)\n", trackCount);

	if (hasVideoTrack) {
		ALOGV("    video track info:\n");
		ALOGV("      width: %d\n", resolution.width);
		ALOGV("      height: %d\n", resolution.height);
	}

	if (hasAudioTrack) {
		ALOGV("    audio track info:\n");
		ALOGV("      channels: %d\n", channels);
		ALOGV("      sample rate: %d Hz\n", sampleRate);
	}
}

int main(int argc, char **argv)
{
	char *file = argv[1];

	printf("welcome using gmpe4, version: %s\n", VERSION);
	printf("mpeg4 file: %s\n\n", file);
	
	int fd = open(file, S_IRUSR); // O_RDONLY | O_LARGEFILE
	if (fd < 0) {
		printf("can not open file successfully, quit!\n");
		close(fd);
		return OK;
	}

    off64_t offset = 0;
    status_t err;
    while ((err = parseChunk(fd, &offset, 0)) == OK) {
    }

	close(fd);

	summary();

	return 0;
}

status_t parseChunk(int fd, off64_t *offset, int depth)
{
//	ALOGV("entering parseChunk %lld/%d\n", *offset, depth);

	uint32_t hdr[2];
	if (readAt(fd, *offset, hdr, 8) < 8) {
		return ERR_IO;
	}
	uint64_t chunk_size = ntohl(hdr[0]);
	uint32_t chunk_type = ntohl(hdr[1]);
	off64_t data_offset = *offset + 8; // seek after size & type，absolute file offset here

	if (chunk_size == 1) {
		if (readAt(fd, *offset + 8, &chunk_size, 8) < 8) {
			return ERR_IO;
		}
		chunk_size = ntoh64(chunk_size);
		data_offset += 8;

		if (chunk_size < 16) {
		    // The smallest valid chunk is 16 bytes long in this case.
		    return ERR_MALFORMED;
		}
	} else if (chunk_size < 8) {
		// The smallest valid chunk is 8 bytes long.
		return ERR_MALFORMED;
	}

#define DIVIDER_LENGTH 4
	char *divs = malloc(depth * sizeof(char) * DIVIDER_LENGTH + 1);

	size_t depth_idx;
	size_t divs_count;
	for (depth_idx = 0, divs_count = depth * sizeof(char) * DIVIDER_LENGTH;
			depth_idx < divs_count; depth_idx++) {
		divs[depth_idx] = ' ';	
	}
	divs[depth_idx] = '\0'; // end-of-string
#undef DIVIDER_LENGTH

	char chunk[5];
	MakeFourCCString(chunk_type, chunk);
	ALOGV("%sbox: %s @ %lld size %lld\n", divs, chunk, *offset, chunk_size);

	free(divs);

	// size of chunk subtract size of some other properties(size and type)
    off64_t chunk_data_size = *offset + chunk_size - data_offset;

	switch (chunk_type) {
	case FOURCC('m', 'o', 'o', 'v'):
	case FOURCC('t', 'r', 'a', 'k'):
	case FOURCC('m', 'd', 'i', 'a'):
	case FOURCC('m', 'i', 'n', 'f'):
	case FOURCC('d', 'i', 'n', 'f'):
	case FOURCC('s', 't', 'b', 'l'):
	case FOURCC('m', 'v', 'e', 'x'):
	case FOURCC('m', 'o', 'o', 'f'):
	case FOURCC('t', 'r', 'a', 'f'):
	case FOURCC('m', 'f', 'r', 'a'):
	case FOURCC('u', 'd', 't', 'a'):
	case FOURCC('i', 'l', 's', 't'):
	{
		// these boxes has some sub-box(es), so put it here

		if (chunk_type == FOURCC('s', 't', 'b', 'l')) {
//			ALOGV("sampleTable chunk is %d bytes long.\n", (size_t)chunk_size);
		}

		if (chunk_type == FOURCC('t', 'r', 'a', 'k')) {
			trackCount++;
		}

		off64_t stop_offset = *offset + chunk_size;
		*offset = data_offset;
		while (*offset < stop_offset) {
			status_t err = parseChunk(fd, offset, depth + 1);
			if (err != OK) {
				return err;
			}
		}

		if (*offset != stop_offset) {
			return ERR_MALFORMED;
		}

		break;
	}

	case FOURCC('t', 'k', 'h', 'd'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('m', 'd', 'h', 'd'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('s', 't', 's', 'd'): // Sample Description Box
	{
		uint8_t buffer[8];
		if (chunk_data_size < (off64_t)sizeof(buffer)) {
			return ERR_MALFORMED;
		}

		if (readAt(fd, data_offset, buffer, sizeof(buffer))
				< (ssize_t)sizeof(buffer)) {
			return ERR_IO;
		}

		if (U32_AT(buffer) != 0) {
			// Should be version 0, flags 0.
			return ERR_MALFORMED;
		}

		uint32_t entry_count = U32_AT(&buffer[4]);

		off64_t stop_offset = *offset + chunk_size;
		*offset = data_offset + 8;
		uint32_t i;
		for (i = 0; i < entry_count; ++i) {
			status_t err = parseChunk(fd, offset, depth + 1);
			if (err != OK) {
				return err;
			}
		}

		if (*offset != stop_offset) {
			return ERR_MALFORMED;
		}
		break;
	}

	case FOURCC('m', 'p', '4', 'a'): // Stream
	case FOURCC('s', 'a', 'm', 'r'):
	case FOURCC('s', 'a', 'w', 'b'):
	{
		hasAudioTrack = 1;

		uint8_t buffer[8 + 20];
		// rough check
		if (chunk_data_size < (ssize_t)sizeof(buffer)) {
			// Basic AudioSampleEntry size.
			return ERR_MALFORMED;
		}

		// do parsing
		if (readAt(fd, data_offset, buffer, sizeof(buffer))
				< (ssize_t)sizeof(buffer)) {
			return ERR_IO;
		}

		uint16_t data_ref_index = U16_AT(&buffer[6]);
		uint16_t num_channels = U16_AT(&buffer[16]);

		uint16_t sample_size = U16_AT(&buffer[18]);
		uint32_t sample_rate = U32_AT(&buffer[24]) >> 16;

#if 0
		if (mime type is Narrow Band) {
			// AMR NB audio is always mono, 8kHz
			num_channels = 1;
			sample_rate = 8000;
		} else if (mime type is Wide Band) {
			// AMR WB audio is always mono, 16kHz
			num_channels = 1;
			sample_rate = 16000;
		}
#endif

#if 0
		printf("*** coding='%s' %d channels, size %d, rate %d\n",
			   chunk, num_channels, sample_size, sample_rate);
#endif

		channels = num_channels;
		sampleRate = sample_rate;

		off64_t stop_offset = *offset + chunk_size;
		*offset = data_offset + sizeof(buffer);
		while (*offset < stop_offset) {
			status_t err = parseChunk(fd, offset, depth + 1);
			if (err != OK) {
				return err;
			}
		}

		if (*offset != stop_offset) {
			return ERR_MALFORMED;
		}
		break;
	}

	case FOURCC('m', 'p', '4', 'v'): // Brand
	case FOURCC('s', '2', '6', '3'):
	case FOURCC('H', '2', '6', '3'):
	case FOURCC('h', '2', '6', '3'):
	case FOURCC('a', 'v', 'c', '1'):
	{
		hasVideoTrack = 1;

        uint8_t buffer[78];
        if (chunk_data_size < (ssize_t)sizeof(buffer)) {
            // Basic VisualSampleEntry size.
            return ERR_MALFORMED;
        }

        if (readAt(fd, data_offset, buffer, sizeof(buffer))
        		< (ssize_t)sizeof(buffer)) {
            return ERR_IO;
        }

        uint16_t data_ref_index = U16_AT(&buffer[6]);
        uint16_t width = U16_AT(&buffer[6 + 18]);
        uint16_t height = U16_AT(&buffer[6 + 20]);

		resolution.width = width;
		resolution.height = height;

        off64_t stop_offset = *offset + chunk_size;
        *offset = data_offset + sizeof(buffer);
        while (*offset < stop_offset) {
            status_t err = parseChunk(fd, offset, depth + 1);
            if (err != OK) {
                return err;
            }
        }

        if (*offset != stop_offset) {
            return ERR_MALFORMED;
        }
		break;
	}

	case FOURCC('s', 't', 'c', 'o'):
	case FOURCC('c', 'o', '6', '4'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('s', 't', 's', 'c'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('s', 't', 's', 'z'):
	case FOURCC('s', 't', 'z', '2'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('s', 't', 't', 's'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('c', 't', 't', 's'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('s', 't', 's', 's'): // Sync Sample Box
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('\xA9', 'x', 'y', 'z'): // @xyz
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('e', 's', 'd', 's'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('a', 'v', 'c', 'C'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('d', '2', '6', '3'):
	{
		*offset += chunk_size;
		break;
	}
	
	case FOURCC('m', 'e', 't', 'a'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('m', 'e', 'a', 'n'):
	case FOURCC('n', 'a', 'm', 'e'):
	case FOURCC('d', 'a', 't', 'a'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('m', 'v', 'h', 'd'): // Movie Header Box
	{
		// XXX can we allocate buffer after the version detected?
		// thus may can save some memory, but more IO access
		uint8_t header[32]; // version flags creation-time
							// modification-time time-scale
							// duration based on time-scale
		// rough check
		if (chunk_data_size < sizeof(header)) {
			return ERR_MALFORMED;
		}

		// do parsing
		if (readAt(fd, data_offset, header, sizeof(header))
				< (ssize_t)sizeof(header)) {
			return ERR_IO;
		}

		int timeScale;
		int64_t durationTS;

		if (header[0] == 1) {
			creationTime = U64_AT(&header[4]);
			modificationTime = U64_AT(&header[4 + 8]);
			timeScale = U32_AT(&header[4 + 8 + 8]);
			durationTS = U64_AT(&header[4 + 8 + 8 + 4]);
		} else if (header[0] != 0) {
			return ERR_MALFORMED;
		} else {
			creationTime = U32_AT(&header[4]);
			modificationTime = U32_AT(&header[4 + 4]);
			timeScale = U32_AT(&header[4 + 4 + 4]);
			durationTS = U32_AT(&header[4 + 4 + 4 + 4]);
		}

		duration = durationTS / timeScale;

		*offset += chunk_size;
		break;
	}

	case FOURCC('m', 'd', 'a', 't'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('h', 'd', 'l', 'r'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('t', 'x', '3', 'g'):
	{
		*offset += chunk_size;
		break;
	}

	case FOURCC('-', '-', '-', '-'):
	{
		*offset += chunk_size;
		break;
	}

	default:
	{
		*offset += chunk_size;
		break;
	}

	} // end of switch

	return OK;
}

