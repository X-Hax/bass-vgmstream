#include "bass_vgmstream.h"

#include <vgmstream.h>
#include <stdlib.h>

// Stuff removed from vgmstream
void put_8bit(uint8_t* buf, int8_t i) {
	buf[0] = i;
}

void put_16bitLE(uint8_t* buf, int16_t i) {
	buf[0] = (i & 0xFF);
	buf[1] = i >> 8;
}

void put_32bitLE(uint8_t* buf, int32_t i) {
	buf[0] = (uint8_t)(i & 0xFF);
	buf[1] = (uint8_t)((i >> 8) & 0xFF);
	buf[2] = (uint8_t)((i >> 16) & 0xFF);
	buf[3] = (uint8_t)((i >> 24) & 0xFF);
}

void put_16bitBE(uint8_t* buf, int16_t i) {
	buf[0] = i >> 8;
	buf[1] = (i & 0xFF);
}

void put_32bitBE(uint8_t* buf, int32_t i) {
	buf[0] = (uint8_t)((i >> 24) & 0xFF);
	buf[1] = (uint8_t)((i >> 16) & 0xFF);
	buf[2] = (uint8_t)((i >> 8) & 0xFF);
	buf[3] = (uint8_t)(i & 0xFF);
}

void swap_samples_le(sample_t* buf, int count) {
	/* Windows can't be BE... I think */
#if !defined(_WIN32)
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
	int i;
	for (i = 0; i < count; i++) {
		/* 16b sample in memory: aabb where aa=MSB, bb=LSB */
		uint8_t b0 = buf[i] & 0xff;
		uint8_t b1 = buf[i] >> 8;
		uint8_t* p = (uint8_t*)&(buf[i]);
		/* 16b sample in buffer: bbaa where bb=LSB, aa=MSB */
		p[0] = b0;
		p[1] = b1;
		/* when endianness is LE, buffer has bbaa already so this function can be skipped */
	}
#endif
#endif
}

// WAV header function from vgmstream r1040 modified to store loop size
void make_wav_header(uint8_t* buf, int32_t sample_count, int32_t sample_rate, int channels, int loop) 
{
	size_t bytecount;

	bytecount = sample_count * channels * sizeof(sample_t);

	/* RIFF header */
	memcpy(buf + 0, "RIFF", 4);

	/* size of RIFF */
	put_32bitLE(buf + 4, (int32_t)(bytecount + 0x2c - 8 + loop * 0x44));

	/* WAVE header */
	memcpy(buf + 8, "WAVE", 4);

	/* WAVE fmt chunk */
	memcpy(buf + 0xc, "fmt ", 4);

	/* size of WAVE fmt chunk */
	put_32bitLE(buf + 0x10, 0x10);

	/* compression code 1=PCM */
	put_16bitLE(buf + 0x14, 1);

	/* channel count */
	put_16bitLE(buf + 0x16, channels);

	/* sample rate */
	put_32bitLE(buf + 0x18, sample_rate);

	/* bytes per second */
	put_32bitLE(buf + 0x1c, sample_rate * channels * sizeof(sample_t));

	/* block align */
	put_16bitLE(buf + 0x20, (int16_t)(channels * sizeof(sample_t)));

	/* significant bits per sample */
	put_16bitLE(buf + 0x22, sizeof(sample_t) * 8);

	/* PCM has no extra format bytes, so we don't even need to specify a count */

	/* WAVE data chunk */
	memcpy(buf + 0x24, "data", 4);

	/* size of WAVE data chunk */
	put_32bitLE(buf + 0x28, (int32_t)bytecount);
}

// Read from memory: https://github.com/vgmstream/vgmstream/issues/662
STREAMFILE* open_memory_streamfile(uint8_t* buf, size_t bufsize, const char* name);

typedef struct {
	STREAMFILE sf; /* pre-alloc'd part */

	uint8_t* buf;
	size_t bufsize;
	const char* name;
	offv_t offset;
} MEMORY_STREAMFILE;

static size_t memory_read(MEMORY_STREAMFILE* sf, uint8_t* dst, offv_t offset, size_t length) {
	if (!dst || length <= 0 || offset < 0 || offset >= sf->bufsize)
		return 0;
	if (offset + length > sf->bufsize)
		length = sf->bufsize - offset; /* clamp */

	memcpy(dst, sf->buf + offset, length);
	sf->offset = offset;
	return length;
}

static size_t memory_get_size(MEMORY_STREAMFILE* sf) {
	return sf->bufsize;
}

static size_t memory_get_offset(MEMORY_STREAMFILE* sf) {
	return sf->offset;
}

static void memory_get_name(MEMORY_STREAMFILE* sf, char* buffer, size_t length) {
	strncpy(buffer, sf->name, length);
	buffer[length - 1] = '\0';
}

static STREAMFILE* memory_open(MEMORY_STREAMFILE* sf, const char* const filename, size_t buffersize) {
	/* Some formats need to open companion files though, maybe pass N bufs+name per file */
	/* also must detect "reopens", as internal processes need to clone this streamfile at times */
	if (strcmp(filename, sf->name) == 0)
		return open_memory_streamfile(sf->buf, sf->bufsize, sf->name); /* reopen */
	return NULL;
}

static void memory_close(MEMORY_STREAMFILE* sf) {
	free(sf);
}

STREAMFILE* open_memory_streamfile(uint8_t* buf, size_t bufsize, const char* name) {
	MEMORY_STREAMFILE* this_sf = NULL;

	this_sf = calloc(1, sizeof(MEMORY_STREAMFILE));
	if (!this_sf) goto fail;

	this_sf->sf.read = (void*)memory_read;
	this_sf->sf.get_size = (void*)memory_get_size;
	this_sf->sf.get_offset = (void*)memory_get_offset;
	this_sf->sf.get_name = (void*)memory_get_name;
	this_sf->sf.open = (void*)memory_open;
	this_sf->sf.close = (void*)memory_close;

	/* assumes bufs live externally during decode, otherwise malloc/memcpy and free on close */
	this_sf->buf = buf;
	this_sf->bufsize = bufsize;
	this_sf->name = name;

	return &this_sf->sf;
fail:
	free(this_sf);
	return NULL;
}

// Conversion

/* make a loop chunk for PCM .wav */
/* buffer must be 0x44 bytes */
void make_smpl_header(uint8_t* buf, int32_t loop_start, int32_t loop_end)
{
	// SMLP header
	memcpy(buf + 0, "smpl", 4);
	// Size of SMPL chunk (always 0x3C for one loop)
	put_32bitLE(buf + 4, 0x3C);
	// Manufacturer
	put_32bitLE(buf + 8, 0);
	// Product
	put_32bitLE(buf + 12, 0);
	// Sample Period
	put_32bitLE(buf + 16, 45351); // SADX value
	// MIDI Unity Note
	put_32bitLE(buf + 20, 60); // SADX value
	// MIDI pitch fraction
	put_32bitLE(buf + 24, 0);
	// SMPTE format
	put_32bitLE(buf + 28, 30); // SADX value
	// SMPTE offset
	put_32bitLE(buf + 32, 0);
	// Number of sample loops (always 1 in SADX)
	put_32bitLE(buf + 36, 1);
	// Sampler specific data (unused in SADX)
	put_32bitLE(buf + 40, 0);
	// Sample loop ID (always 0 in SADX)
	put_32bitLE(buf + 44, 0);
	// Sample loop type (always 0 in SADX)
	put_32bitLE(buf + 48, 0);
	// Loop start
	put_32bitLE(buf + 52, loop_start);
	// Loop end
	put_32bitLE(buf + 56, loop_end);
	// Sample loop fraction (always 0 in SADX)
	put_32bitLE(buf + 60, 0);
	// Sample loop repeat count (always infinite?)
	put_32bitLE(buf + 64, 0);
}

int Convert(VGMSTREAM* vgmstream, unsigned char* outputdata)
{
	int result = 1; // 0 if there is no error
	sample_t* buf = NULL;
	int32_t len;
	const int BUFSIZE = 4000; // PCM sample size
	int position = 0x2C; // Buffer seeking

	// Init
	if (!vgmstream)
	{
		return 1;
	}

	// Allocate buffer
	buf = malloc(BUFSIZE * sizeof(sample_t) * vgmstream->channels);
	if (buf == NULL)
		return 1;

	len = get_vgmstream_play_samples(0, 0, 0, vgmstream);
	if (vgmstream->loop_end_sample > len)
		len += (vgmstream->loop_end_sample - len);

	// Add WAV header
	make_wav_header(outputdata, len, vgmstream->sample_rate, vgmstream->channels, vgmstream->loop_flag);
	
	// Decode data
	for (int i = 0; i < len; i += BUFSIZE)
	{
		int toget = BUFSIZE;
		if (i + BUFSIZE > len)
			toget = len - i;

		render_vgmstream(buf, toget, vgmstream);
		swap_samples_le(buf, vgmstream->channels * toget);
		memcpy(&outputdata[position], buf, sizeof(sample_t) * vgmstream->channels * toget);
		position += sizeof(sample_t) * vgmstream->channels * toget;
	}

	// Add loop header
	if (vgmstream->loop_flag == 1)
	{
		make_smpl_header(&outputdata[position], vgmstream->loop_start_sample, vgmstream->loop_end_sample);
	}

	// Cleanup
	free(buf);
	buf = NULL;
	return 0;
}

// API functions
BASS_VGMSTREAM_API void* BASS_VGMSTREAM_InitVGMStreamFromMemory(void* data, int size, const char* name)
{
	STREAMFILE* sf = open_memory_streamfile(data, size, name);
	return (void*)init_vgmstream_from_STREAMFILE(sf);
}

BASS_VGMSTREAM_API void BASS_VGMSTREAM_CloseVGMStream(void* vgmstream)
{
	close_vgmstream((VGMSTREAM*)vgmstream);
}

BASS_VGMSTREAM_API int BASS_VGMSTREAM_GetVGMStreamOutputSize(void* vgmstream)
{
	VGMSTREAM* stream = (VGMSTREAM*)vgmstream;
	int numsample = get_vgmstream_play_samples(0, 0, 0, stream);
	if (stream->loop_end_sample > numsample)
		numsample += (stream->loop_end_sample - numsample);
	return numsample * sizeof(sample_t) * stream->channels + 0x2C + (stream->loop_flag ? 0x44 : 0); // + WAV header + possibly smpl header for 1 loop
}

BASS_VGMSTREAM_API int BASS_VGMSTREAM_ConvertVGMStreamToWav(void* vgmstream, unsigned char* outputdata)
{
	return Convert((VGMSTREAM*)vgmstream, outputdata);
}

int main(int argc, char* argv[])
{
	const char* srcFile = "test.adx";
	const char* dstFile = "test.wav";
	int inputSize;
	int outputSize = 0;
	void* InputBuffer;
	unsigned char* OutputBuffer = NULL;
	double loop_count = 10.0;
	double fade_seconds = 0.0;
	double fade_delay_seconds = 0.0;
	int fade_samples = 0;
	FILE* f = NULL;
	errno_t err;

	// Open input file
	err = fopen_s(&f, srcFile, "rb");
	if (err || f == NULL)
	{
		printf("Input file error\n");
		return;
	}
	fseek(f, 0L, SEEK_END);
	
	// Get input size and allocate memory
	inputSize = ftell(f);
	fseek(f, 0L, SEEK_SET);
	
	// Read source file into memory
	InputBuffer = malloc(inputSize);
	if (InputBuffer == NULL)
	{
		printf("Could not allocate input buffer\n");
		return;
	}
	fread(InputBuffer, 1, inputSize, f);
	fclose(f);

	// Create vgmstream from memory
	VGMSTREAM* vgmstream = BASS_VGMSTREAM_InitVGMStreamFromMemory(InputBuffer, inputSize, srcFile);

	// Print loop info
	if (vgmstream->loop_flag)
	{
		printf("Loop start: %u\n", vgmstream->loop_start_sample);
		printf("Loop end: %u\n", vgmstream->loop_end_sample);
	}

	// Get output size and allocate memory
	outputSize = BASS_VGMSTREAM_GetVGMStreamOutputSize(vgmstream);
	OutputBuffer = malloc(outputSize);

	// Convert data
	Convert(vgmstream, OutputBuffer);		

	// Write output file
	err = fopen_s(&f, dstFile, "wb");
	if (err || f == NULL)
	{
		printf("Output file error\n");
		return;
	}
	if (OutputBuffer == NULL)
	{
		printf("Output buffer empty\n");
		return;
	}
	fwrite(OutputBuffer, outputSize, 1, f);
	fclose(f);
	return 0;
}