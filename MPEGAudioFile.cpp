// Includes
#include "MPEGAudioFile.h"
//#include <mad.h>	// Necessary soon

struct MP3FrameHeader
{
	uint16_t frame_sync;    // 11 Bits
	uint8_t mpeg_version;   // 2 Bits
	uint8_t layer;          // 2 Bits
	uint8_t protection;     // 1 Bits
	uint8_t bit_rate;       // 4 Bits
	uint8_t sample_rate;    // 2 Bits
	uint8_t padding;        // 1 Bits
	uint8_t pri;            // 1 Bits
	uint8_t channel_mode;   // 2 Bits
	uint8_t mode_extension; // 2 Bits
	uint8_t copyright;      // 1 Bits
	uint8_t original;       // 1 Bits
	uint8_t emphasis;       // 2 Bits
};

#pragma pack(push,1)
struct ID3Header
{
	char id3tag[3];
	uint16_t version;
	uint8_t unsync:1;
	uint8_t extended:1;
	uint8_t experimental:1;
	uint8_t footer:1;
	uint8_t zeros:4;
	uint8_t size1;
	uint8_t size2;
	uint8_t size3;
	uint8_t size4;
};
#pragma pack(pop)

// Bitrates - use [version_idx][layer_idx][bitrate_idx]
static const uint16_t mpeg_bitrates[4][4][16] = {
  { // Version 2.5
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Reserved
    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 3
    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 2
    { 0,  32,  48,  56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, 0 }  // Layer 1
  },
  { // Reserved
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Invalid
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Invalid
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Invalid
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }  // Invalid
  },
  { // Version 2
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Reserved
    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 3
    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, 0 }, // Layer 2
    { 0,  32,  48,  56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, 0 }  // Layer 1
  },
  { // Version 1
    { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0, 0 }, // Reserved
    { 0,  32,  40,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 0 }, // Layer 3
    { 0,  32,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, 0 }, // Layer 2
    { 0,  32,  64,  96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0 }, // Layer 1
  }
};

// Sample rates - [version_idx][sample_rat_idx]
static const uint16_t mpeg_srates[4][4] = {
    { 11025, 12000,  8000, 0 }, // MPEG 2.5
    {     0,     0,     0, 0 }, // Reserved
    { 22050, 24000, 16000, 0 }, // MPEG 2
    { 44100, 48000, 32000, 0 }  // MPEG 1
};

// Samples per frame - [version_idx][layer_idx]
static const uint16_t mpeg_frame_samples[4][4] = {
//    Rsvd     3     2     1  < Layer  v Version
    {    0,  576, 1152,  384 }, //       2.5
    {    0,    0,    0,    0 }, //       Reserved
    {    0,  576, 1152,  384 }, //       2
    {    0, 1152, 1152,  384 }  //       1
};

// Slot size (MPEG unit of measurement) - [layer_idx]
static const uint8_t mpeg_slot_size[4] = { 0, 1, 1, 4 }; // Rsvd, 3, 2, 1

// Number of output channels - [channel_idx]
static const uint8_t mpeg_channels[4] = { 2, 2, 2, 1 };

/// Skips past an ID3v2 header if it finds one
size_t mpeg_audio_file_skip_id3v2(GInputStream *stream)
{
	ID3Header id3_header;

	// Attempt to read an ID3 header
	if (g_input_stream_read(stream, &id3_header, sizeof(ID3Header), NULL, NULL) != sizeof(ID3Header))
	{
		fprintf(stderr, "mpeg_audio_file_skip_id3v2(): Failed to read whilst checking for ID3 header\n");
		return (size_t)-1;
	}

	// If we have an ID3 header
	if (id3_header.id3tag[0] == 'I' && id3_header.id3tag[1] == 'D' && id3_header.id3tag[2] == '3' && (id3_header.version & 0xFF00 >> 8) < 0xFF && (id3_header.version & 0x00FF) < 0xFF && id3_header.size1 < 0x80 && id3_header.size2 < 0x80 && id3_header.size3 < 0x80 && id3_header.size4 < 0x80)
	{
		// Calculate the size of the ID3 body. This is a 'synchsafe' integer
		// (see http://id3.org/id3v2.4.0-structure). Essentially it's four
		// bytes where the MSB of each byte is always zero and needs to be
		// removed. This line extracts the 28 relevant bits
		uint32_t size = (uint32_t)id3_header.size4 | ((uint32_t)id3_header.size3 << 7) | ((uint32_t)id3_header.size2 << 14) | ((uint32_t)id3_header.size1 << 21);

		fprintf(stderr, "mpeg_audio_file_skip_id3v2(): ID3v2 tag found. Skipping %u bytes\n", size);

		// Seek past the ID3 body
		if (!g_seekable_seek(G_SEEKABLE(stream), size, G_SEEK_CUR, NULL, NULL))
		{
			fprintf(stderr, "mpeg_audio_file_skip_id3v2(): Seek failed when skipping ID3 body\n");
			return (size_t)-1;
		}

		// Return the number of bytes we read and skipped
		return size + sizeof(ID3Header);
	}
	else
	{
		// We didn't have an ID3 header. Seek back the bytes we just read
		if (!g_seekable_seek(G_SEEKABLE(stream), -sizeof(ID3Header), G_SEEK_CUR, NULL, NULL))
		{
			fprintf(stderr, "mpeg_audio_file_skip_id3v2(): Seek failed. MP3 Header will likely fail\n");
			return (size_t)-1;
		}
		else
		{
			return 0;
		}
	}
}

bool mpeg_audio_file_find_frames(GInputStream *stream, uint16_t *channels, uint32_t *sample_rate, uint32_t *frames, uint64_t *samples, std::vector<MP3FrameInfo> *frame_info)
{
	size_t total_read = 0, frame_idx = 0, total_samples = 0;

	// Get the size of the ID3 header and skip past it in the stream
	size_t id3_size = mpeg_audio_file_skip_id3v2(stream);

	// If there was an error skipping past the ID3 header, fail
	if (id3_size == (size_t)-1)
	{
		return false;
	}

	// Keep track of our location in the file
	total_read += id3_size;

	// Scan through the file
	bool scanning = true;
	bool lost_sync = false;
	while (scanning)
	{
		// Keep track of the offset in the file where this frame started
		size_t frame_start = total_read;

		// Read four bytes that should be a MP3 Frame Header
		unsigned char frame_header_buffer[4];
		gssize read = g_input_stream_read(stream, frame_header_buffer, 4, NULL, NULL);
		if (read < 4)
		{
			// Stop scanning if we fail to read a whole 4 bytes
			scanning = false;
			break;
		}

		// Build a MP3 Header structure from the buffer
		MP3FrameHeader frame_header;
		frame_header.frame_sync = ((uint16_t)frame_header_buffer[0] << 3) | (((uint8_t)frame_header_buffer[1] & 0xE0) >> 5);
		frame_header.mpeg_version = ((uint8_t)frame_header_buffer[1] & 0x18) >> 3;
		frame_header.layer = ((uint8_t)frame_header_buffer[1] & 0x06) >> 1;
		frame_header.protection = (uint8_t)frame_header_buffer[1] & 0x01;
		frame_header.bit_rate = ((uint8_t)frame_header_buffer[2] & 0xF0) >> 4;
		frame_header.sample_rate = ((uint8_t)frame_header_buffer[2] & 0x0C) >> 2;
		frame_header.padding = ((uint8_t)frame_header_buffer[2] & 0x02) >> 1;
		frame_header.pri = (uint8_t)frame_header_buffer[2] & 0x01;
		frame_header.channel_mode = ((uint8_t)frame_header_buffer[3] & 0xC0) >> 6;
		frame_header.mode_extension = ((uint8_t)frame_header_buffer[3] & 0x20) >> 4;
		frame_header.copyright = ((uint8_t)frame_header_buffer[3] & 0x08) >> 3;
		frame_header.original = ((uint8_t)frame_header_buffer[3] & 0x04) >> 2;
		frame_header.emphasis = (uint8_t)frame_header_buffer[3] & 0x02;

		// Check for a frame sync
		if (frame_header.frame_sync != 0x7FF)
		{
			// Check if we've hit a TAG header (ID3v1 TAG) which would
			// normally be at the end of the file. Only check for this
			// if we're not out-of-sync however as this potentially is
			// valid audio data
			if (!lost_sync && frame_header_buffer[0] == 'T' && frame_header_buffer[1] == 'A' && frame_header_buffer[2] == 'G')
			{
				fprintf(stderr, "mpeg_audio_file_find_frames(): ID3v1 tag found, skipping 128 bytes\n");

				// Seek past the TAG block. It should be 128 bytes,
				// so we seek 124 (as we've already read the first 4)
				if (!g_seekable_seek(G_SEEKABLE(stream), 124, G_SEEK_CUR, NULL, NULL))
				{
					fprintf(stderr, "mpeg_audio_file_find_frames(): Seek failed\n");
					return false;
				}

				total_read += 128;
				continue;
			}

			// Don't spam this message, but log when we lose sync
			if (lost_sync != true)
			{
				lost_sync = true;
				fprintf(stderr, "mpeg_audio_file_find_frames(): Lost MP3 frame sync at offset %lu\n", frame_start);
			}

			// If we don't find frame sync, seek back three bytes
			// (i.e. one less than the size of the frame header) and
			// try again
			if (!g_seekable_seek(G_SEEKABLE(stream), -3, G_SEEK_CUR, NULL, NULL))
			{
				fprintf(stderr, "mpeg_audio_file_find_frames(): Seek failed\n");
				return false;
			}

			// We've only moved forward one byte
			total_read += 1;

			continue;
		}
		else
		{
			// Log when we regain sync
			if (lost_sync)
			{
				fprintf(stderr, "mpeg_audio_file_find_frames(): Re-sync'd MP3 frame at offset %lu\n", frame_start);
				lost_sync = true;
			}
		}

		// Figure out the frame size
		uint16_t samples_per_frame = mpeg_frame_samples[frame_header.mpeg_version][frame_header.layer];
		uint16_t frame_bit_rate = mpeg_bitrates[frame_header.mpeg_version][frame_header.layer][frame_header.bit_rate];
		uint16_t frame_sample_rate = mpeg_srates[frame_header.mpeg_version][frame_header.sample_rate];
		uint8_t frame_slot_size = mpeg_slot_size[frame_header.layer];
		uint16_t frame_bits_per_sample = samples_per_frame / 8;
		uint16_t frame_size = (uint16_t)((float)frame_bits_per_sample * (float)(frame_bit_rate * 1000) / (float)frame_sample_rate) + (frame_header.padding ? frame_slot_size : 0);

		// Store the sample rate
		*sample_rate = frame_sample_rate;
		*channels = mpeg_channels[frame_header.channel_mode];

		// Store the frame details
		frame_info->push_back(MP3FrameInfo{ total_read, frame_size, total_samples, samples_per_frame });

		// Move on to the next frame
		total_samples += samples_per_frame;
		total_read += frame_size;
		frame_idx++;

		// Seek to the next frame (note the -4 to account for the frame
		// header that we've already read)
		if (!g_seekable_seek(G_SEEKABLE(stream), frame_size - 4, G_SEEK_CUR, NULL, NULL))
		{
			fprintf(stderr, "mpeg_audio_file_find_frames(): Seek failed\n");
			return false;
		}
	}

	if (lost_sync)
	{
		fprintf(stderr, "mpeg_audio_file_find_frames(): Unsync'd at EOF - truncated file?\n");
	}

	// Return the data
	*frames = frame_idx;
	*samples = total_samples;

	return true;
}
