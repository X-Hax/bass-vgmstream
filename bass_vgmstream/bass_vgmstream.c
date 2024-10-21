/**
 * Copyright (c) 2008, Ruben "angryzor" Tytgat
 * 
 * Permission to use, copy, modify, and/or distribute this
 * software for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice and this permission
 * notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL,
 * DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH
 * THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bass_vgmstream.h"

#include <vgmstream.h>
#include <stdlib.h>

/**
 * Callback for BASS. Called when it needs more data.
 */
DWORD CALLBACK vgmStreamProc(
    HSTREAM handle,
    void *buffer,
    DWORD length,
    void *user
)
{
	VGMSTREAM* stream = (VGMSTREAM*)user;                         // We passed the VGMSTREAM as user data.
	BOOL ended = FALSE;                                           // Used to signal end of stream.
	int max_samples = length / sizeof(sample_t) / stream->channels; // Calculate the maximum amount of samples from max buffer size.
	int samples_to_do;                                            // Will hold the amount of samples to be copied.
	
	// If this is a looping VGM stream, we handle it as an infinite stream and read out the
	// next `max_samples` of data.
	//
	// Otherwise, we read out at most `max_samples`. If there is less data available,
	// we consider the stream ended and signal this to BASS
	if (!stream->loop_flag && stream->current_sample + max_samples > stream->num_samples) {
		samples_to_do = stream->num_samples - stream->current_sample;
		ended = TRUE;
	}
	else
		samples_to_do = max_samples;

	// Render the stream.
	render_vgmstream((sample_t*)buffer, samples_to_do, stream);
	// BASS expects you to return the amount of data read in bytes, so multiply by the sample size
	samples_to_do *= sizeof(sample_t) * stream->channels;

	// If we reached the end of a non-looping VGM stream, we'll check BASS' loop flag.
	// If it is set, we restart from the beginning. Otherwise, we signal the end of stream
	// to BASS.
	if(ended) {
		if(BASS_ChannelFlags(handle,0,0) & BASS_SAMPLE_LOOP)
			reset_vgmstream(stream);
		else
			samples_to_do |= BASS_STREAMPROC_END;
	}

	return samples_to_do;
}

/**
 * Called when the BASS handle is closed
 */
void CALLBACK vgmStreamOnFree(
    HSYNC handle,
    DWORD channel,
    DWORD data,
    void *user
)
{
	VGMSTREAM* stream = (VGMSTREAM*)user;
	close_vgmstream(stream);
}

BASS_VGMSTREAM_API HSTREAM BASS_VGMSTREAM_StreamCreate(const char* file, DWORD flags)
{
	HSTREAM h;
	VGMSTREAM* stream = init_vgmstream(file);
	if(!stream)
		return 0;

	h = BASS_StreamCreate(stream->sample_rate, stream->channels, flags, &vgmStreamProc, stream);
	if(!h)
		return 0;

	BASS_ChannelSetSync(h, BASS_SYNC_FREE|BASS_SYNC_MIXTIME, 0, &vgmStreamOnFree, stream);
	return h;
}

STREAMFILE* open_memory_streamfile(uint8_t* buf, size_t bufsize, const char* name);

BASS_VGMSTREAM_API HSTREAM BASS_VGMSTREAM_StreamCreateFromMemory(unsigned char* buf, int bufsize, const char* name, DWORD flags)
{
	HSTREAM h;
	if (!buf)
		return 0;

	STREAMFILE* sf = open_memory_streamfile(buf, bufsize, name);
	VGMSTREAM* vgmstream = init_vgmstream_from_STREAMFILE(sf);

	if (!vgmstream)
		return 0;

	if (vgmstream->channels > 1)
		flags &= ~BASS_SAMPLE_3D; // Cannot create 3D samples from stereo

	if (vgmstream->loop_flag)
	{
		if (vgmstream->loop_start_sample <= 1 && vgmstream->loop_end_sample == 1)
			vgmstream->loop_flag = 0; // Disable invalid loops (B01_00_02 in HIGHWAY_BANK01)
	}

	h = BASS_StreamCreate(vgmstream->sample_rate, vgmstream->channels, flags, &vgmStreamProc, vgmstream);
	if (!h)
		return 0;

	BASS_ChannelSetSync(h, BASS_SYNC_FREE | BASS_SYNC_MIXTIME, 0, &vgmStreamOnFree, vgmstream);
	return h;
}