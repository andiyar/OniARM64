// ======================================================================
// BFW_SS2_Platform_OpenAL.c
// ======================================================================

// ======================================================================
// includes
// ======================================================================

#include "bfw_math.h"

#include "BFW.h"
#include "BFW_SoundSystem2.h"
#include "BFW_SS2_Private.h"
#include "BFW_SS2_Platform.h"
#include "BFW_SS2_IMA.h"
#include "BFW_WindowManager.h"

#include "BFW_Console.h"

#if defined(__APPLE__)
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include "AL/al.h"
#include "AL/alc.h"
#endif
#include <math.h>

#include <libavutil/frame.h>
#include <libavutil/mem.h>

#include <libavcodec/avcodec.h>

// ======================================================================
// defines
// ======================================================================
#define SScMaxSoundChannels				16

#define CHECK_AL_ERROR() SS2rPlatform_CheckALError(__func__, __LINE__)

// ======================================================================
// globals
// ======================================================================
static ALCdevice* SSgDevice = NULL;
static ALCcontext* SSgContext = NULL;

// ======================================================================
// functions
// ======================================================================
// ----------------------------------------------------------------------
static void
SS2rPlatform_CheckALError(const char *fn, int line)
{
	ALenum error = alcGetError(SSgDevice);
	switch (error)
	{
	case ALC_NO_ERROR:
	break;
	case ALC_INVALID_VALUE:
		UUrPrintWarning("%s (%d) -> ALC_INVALID_VALUE: an invalid value was passed to an OpenAL function\n", fn, line);
	break;
	case ALC_INVALID_DEVICE:
		UUrPrintWarning("%s (%d) -> ALC_INVALID_DEVICE: a bad device was passed to an OpenAL function\n", fn, line);
	break;
	case ALC_INVALID_CONTEXT:
		UUrPrintWarning("%s (%d) -> ALC_INVALID_CONTEXT: a bad context was passed to an OpenAL function\n", fn, line);
	break;
	case ALC_INVALID_ENUM:
		UUrPrintWarning("%s (%d) -> ALC_INVALID_ENUM: an unknown enum value was passed to an OpenAL function\n", fn, line);
	break;
	case ALC_OUT_OF_MEMORY:
		UUrPrintWarning("%s (%d) -> ALC_OUT_OF_MEMORY: an unknown enum value was passed to an OpenAL function\n", fn, line);
	break;
	default:
		UUrPrintWarning("%s (%d) -> UNKNOWN ALC ERROR: %d\n", fn, line, error);
	}
}

// ----------------------------------------------------------------------
void
SS2rPlatform_GetDebugNeeds(
	UUtUns32					*outNumLines)
{
	*outNumLines = 0;
	//TODO: stubbed
}

// ----------------------------------------------------------------------
void
SS2rPlatform_ShowDebugInfo_Overall(
	IMtPoint2D					*inDest)
{
	//TODO: stubbed
}

// ----------------------------------------------------------------------
void
SS2rPlatform_PerformanceStartFrame(
	void)
{
	//TODO: stubbed
}

// ----------------------------------------------------------------------
void
SS2rPlatform_PerformanceEndFrame(
	void)
{
	//TODO: stubbed
}

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
void
SS2rPlatform_SoundChannel_Initialize(
	SStSoundChannel				*inSoundChannel)
{
	inSoundChannel->pd.buffer = 0;
	alGenBuffers(1, &inSoundChannel->pd.buffer);
	CHECK_AL_ERROR();
	inSoundChannel->pd.source = 0;
	alGenSources(1, &inSoundChannel->pd.source);
	CHECK_AL_ERROR();
}

// ----------------------------------------------------------------------
void
SS2rPlatform_SoundChannel_Pause(
	SStSoundChannel				*inSoundChannel)
{
	alSourcePause(inSoundChannel->pd.source);
	CHECK_AL_ERROR();
	SSiSoundChannel_SetPaused(inSoundChannel, UUcTrue);
}

// ----------------------------------------------------------------------
void
SS2rPlatform_SoundChannel_Play(
	SStSoundChannel				*inSoundChannel)
{
	ALint pre_state = -1, attached_buffer = -1, buffer_size = -1, buffer_freq = -1, buffer_bits = -1, buffer_chans = -1;
	ALfloat src_gain = -1, src_pitch = -1;
	alGetSourcei(inSoundChannel->pd.source, AL_SOURCE_STATE, &pre_state);
	alGetSourcei(inSoundChannel->pd.source, AL_BUFFER, &attached_buffer);
	alGetSourcef(inSoundChannel->pd.source, AL_GAIN, &src_gain);
	alGetSourcef(inSoundChannel->pd.source, AL_PITCH, &src_pitch);
	if (attached_buffer > 0) {
		alGetBufferi(attached_buffer, AL_SIZE, &buffer_size);
		alGetBufferi(attached_buffer, AL_FREQUENCY, &buffer_freq);
		alGetBufferi(attached_buffer, AL_BITS, &buffer_bits);
		alGetBufferi(attached_buffer, AL_CHANNELS, &buffer_chans);
	}
	UUrStartupMessage(
		"[SS2/OAL] Play src=%u buf=%d preState=%d looping=%d gain=%.2f pitch=%.2f bufSize=%d freq=%d bits=%d chans=%d",
		inSoundChannel->pd.source, attached_buffer, pre_state,
		SSiSoundChannel_IsLooping(inSoundChannel) == UUcTrue ? 1 : 0,
		src_gain, src_pitch,
		buffer_size, buffer_freq, buffer_bits, buffer_chans);

	alSourcei(inSoundChannel->pd.source, AL_LOOPING, SSiSoundChannel_IsLooping(inSoundChannel) == UUcTrue ? AL_TRUE : AL_FALSE);
	alSourcePlay(inSoundChannel->pd.source);
	CHECK_AL_ERROR();
	{
		ALenum al_err = alGetError();
		if (al_err != AL_NO_ERROR)
			UUrStartupMessage("[SS2/OAL] Play alGetError after alSourcePlay: 0x%x", al_err);
	}

	ALint post_state = -1;
	alGetSourcei(inSoundChannel->pd.source, AL_SOURCE_STATE, &post_state);
	UUrStartupMessage("[SS2/OAL] Play post src=%u state=%d (AL_PLAYING=%d)", inSoundChannel->pd.source, post_state, AL_PLAYING);

	SSiSoundChannel_SetPlaying(inSoundChannel, UUcTrue);
}

// ----------------------------------------------------------------------
void
SS2rPlatform_SoundChannel_Resume(
	SStSoundChannel				*inSoundChannel)
{
	UUrStartupMessage("[SS2/OAL] Resume src=%u", inSoundChannel->pd.source);
	alSourcePlay(inSoundChannel->pd.source);
	CHECK_AL_ERROR();
	{
		ALenum al_err = alGetError();
		if (al_err != AL_NO_ERROR)
			UUrStartupMessage("[SS2/OAL] Resume alGetError after alSourcePlay: 0x%x", al_err);
	}
	SSiSoundChannel_SetPaused(inSoundChannel, UUcFalse);
}

// ----------------------------------------------------------------------
UUtBool
SS2rPlatform_SoundChannel_IsPlaying(
	SStSoundChannel				*inSoundChannel)
{
	ALint state;
	alGetSourcei(inSoundChannel->pd.source, AL_SOURCE_STATE, &state);
	CHECK_AL_ERROR();
	return state == AL_PLAYING || state == AL_PAUSED;
}

// ----------------------------------------------------------------------
static UUtBool
SS2r_DecompressMSADPCM(
	SStSoundChannel	*inSoundChannel,
	SStSoundData	*inSoundData,
	UUtUns16		*decoded,
	size_t			*samples)
{
	UUtBool success = UUcFalse;
	unsigned channels = SSrSound_GetNumChannels(inSoundData);

	*samples = 0;

	if (!inSoundData->data || (uintptr_t)inSoundData->data < 0x10000) {
		return UUcFalse;
	}

	if (SScSamplesPerSecond != 8 && SScBitsPerSample != 16)
	{
		UUrPrintWarning("Unexpected SScBitsPerSample: %d", SScBitsPerSample);
		return UUcFalse;
	}

#ifdef DEBUGGING
	av_log_set_level(AV_LOG_DEBUG);
#endif

	const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_ADPCM_MS);
	if (!codec) {
		UUrPrintWarning("Failed to find ADPCM_MS codec");
		return UUcFalse;
	}
	AVCodecContext *c = avcodec_alloc_context3(codec);
	if (!c) {
		UUrPrintWarning("Failed to allocate ADPCM_MS codec context");
		return UUcFalse;
	}
	c->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;
	c->ch_layout.nb_channels = channels;
	c->sample_rate = SScSamplesPerSecond;
	c->sample_fmt = SScBitsPerSample == 8 ? AV_SAMPLE_FMT_U8 : AV_SAMPLE_FMT_S16;
	c->block_align = 512 * channels;
	c->bits_per_coded_sample = 4;
	int ret = avcodec_open2(c, codec, NULL);
	if (ret < 0) {
		UUrPrintWarning("Failed to open ADPCM_MS codec (error %d)", ret);
		goto ctx_error;
	}

	AVPacket *pkt = av_packet_alloc();
	if (!pkt) {
		UUrPrintWarning("Failed to allocate libav packet");
		goto error;
	}

	// we need to send data frame by frame
	// this pointer will iterate over the data by framesz
	char *frame = inSoundData->data;
	char *frames_end = frame + inSoundData->num_bytes;
	const size_t framesz = 512 * channels;

	AVFrame *decoded_frame = av_frame_alloc();
	if (!decoded_frame) {
		UUrPrintWarning("Failed to allocate libav frame");
		goto error;
	}

	while(1) {
		ret = avcodec_receive_frame(c, decoded_frame);
		if (ret == AVERROR(EAGAIN))
		{
			if (frame == frames_end) {
				ret = avcodec_send_packet(c, NULL);
				if (ret != 0) {
					UUrPrintWarning("Failed to send NULL libav packet (error %d)", ret);
					break;
				}
			} else {
				size_t left = frames_end - frame;
				size_t sz = (framesz <= left) ? framesz : left;
				//TODO: can we not allocate a new buffer for every frame??
				void *avbuf = av_memdup(frame, sz);
				if (!avbuf) {
					UUrPrintWarning("Failed to allocate libav buffer");
					break;
				}
				ret = av_packet_from_data(pkt, avbuf, sz);
				if (ret != 0) {
					UUrPrintWarning("Failed to initialize libav packet (error %d)", ret);
					av_free(avbuf);
					break;
				}
				ret = avcodec_send_packet(c, pkt);
				av_packet_unref(pkt);
				if (ret != 0) {
					UUrPrintWarning("Failed to send libav packet (error %d)", ret);
					break;
				}
				frame += sz;
			}
			continue;
		}
		if (ret == AVERROR_EOF) {
			success = UUcTrue;
			break;
		}
		if (ret < 0) {
			UUrPrintWarning("Failed to receive libav frame (error %d)", ret);
			break;
		}
		if (!decoded_frame->extended_data[0] || (uintptr_t)decoded_frame->extended_data[0] < 0x10000) {
			UUrPrintWarning("ADPCM frame has bad data ptr %p, aborting decode", decoded_frame->extended_data[0]);
			break;
		}
		int data_size = av_samples_get_buffer_size(
			NULL,
			decoded_frame->ch_layout.nb_channels,
			decoded_frame->nb_samples,
			decoded_frame->format,
			1
		);
		memcpy(decoded + *samples, decoded_frame->extended_data[0], data_size);
		*samples += data_size / sizeof(*decoded);
		// the next avcodec_receive_frame() automatically unrefs the current decoded_frame
	}
	av_frame_unref(decoded_frame);
	av_frame_free(&decoded_frame);

error:
	av_packet_free(&pkt);
ctx_error:
	avcodec_free_context(&c);

	return success;
}

// ----------------------------------------------------------------------
UUtBool
SS2rPlatform_SoundChannel_SetSoundData(
	SStSoundChannel				*inSoundChannel,
	SStSoundData				*inSoundData)
{
	{
		UUtUns8 *raw = (UUtUns8 *)inSoundData;
		UUrStartupMessage("[SS2/OAL] SoundData raw hex @ %p: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
			(void*)inSoundData,
			raw[0],raw[1],raw[2],raw[3], raw[4],raw[5],raw[6],raw[7],
			raw[8],raw[9],raw[10],raw[11], raw[12],raw[13],raw[14],raw[15],
			raw[16],raw[17],raw[18],raw[19], raw[20],raw[21],raw[22],raw[23]);
		UUrStartupMessage("[SS2/OAL] SoundData fields: flags=0x%x dur=%u num_bytes=%u data=%p",
			inSoundData->flags, inSoundData->duration_ticks,
			inSoundData->num_bytes, inSoundData->data);
		if (inSoundData->data && (uintptr_t)inSoundData->data > 0x10000) {
			UUtUns8 *d = (UUtUns8 *)inSoundData->data;
			UUrStartupMessage("[SS2/OAL] SoundData first 16 bytes: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
				d[0],d[1],d[2],d[3], d[4],d[5],d[6],d[7],
				d[8],d[9],d[10],d[11], d[12],d[13],d[14],d[15]);
		}
	}

	UUtUns32 num_channels = SSrSound_GetNumChannels(inSoundData);
	ALenum format;
	if (num_channels == 1 && SScBitsPerSample == 16)
		format = AL_FORMAT_MONO16;
	else if (num_channels == 2 && SScBitsPerSample == 16)
		format = AL_FORMAT_STEREO16;
	else if (num_channels == 1 && SScBitsPerSample == 8)
		format = AL_FORMAT_MONO8;
	else if (num_channels == 2 && SScBitsPerSample == 8)
		format = AL_FORMAT_STEREO8;
	else
	{
		UUrPrintWarning("Invalid format. bps=%d, channels=%u", SScBitsPerSample, num_channels);
		return UUcFalse;
	}

	if (inSoundData->flags & SScSoundDataFlag_Compressed)
	{
		UUtUns16 *decoded = UUrMemory_Block_New(inSoundData->num_bytes * 4);
		if (!decoded) {
			return UUcFalse;
		}
		size_t samples = 0;
		UUtBool success = SS2r_DecompressMSADPCM(inSoundChannel, inSoundData, decoded, &samples);
		UUrStartupMessage("[SS2/OAL] SetSoundData ADPCM src=%u samples=%zu %s",
			inSoundChannel->pd.source, samples, success ? "OK" : "FAIL");
		if (success)
		{
			alSourcei(inSoundChannel->pd.source, AL_BUFFER, 0);
			alBufferData(inSoundChannel->pd.buffer, format, decoded, samples * sizeof(*decoded), SScSamplesPerSecond);
			CHECK_AL_ERROR();
			alSourcei(inSoundChannel->pd.source, AL_BUFFER, inSoundChannel->pd.buffer);
			CHECK_AL_ERROR();
		}
		UUrMemory_Block_Delete(decoded);
		return success;
	}

	if (!inSoundData->data || (uintptr_t)inSoundData->data < 0x10000)
		return UUcFalse;

	{
		UUtUns32 num_packets = inSoundData->num_bytes / (num_channels * sizeof(SStIMA_SampleData));
		UUtUns32 num_samples = num_packets * SScIMA_SamplesPerPacket * num_channels;
		UUtUns32 pcm_bytes = num_samples * sizeof(UUtUns16);
		UUtUns8 *pcm = UUrMemory_Block_New(pcm_bytes);
		if (!pcm)
			return UUcFalse;

		SSrIMA_DecompressSoundData(inSoundData, pcm, num_packets, 0);

		{
			UUtInt16 *s = (UUtInt16*)pcm;
			UUrStartupMessage("[SS2/OAL] IMA first 8 samples: %d %d %d %d %d %d %d %d",
				s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
			UUtUns8 *raw = (UUtUns8*)inSoundData->data;
			UUrStartupMessage("[SS2/OAL] IMA src first 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
				raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7]);
		}

		UUrStartupMessage("[SS2/OAL] SetSoundData IMA src=%u fmt=0x%x packets=%u samples=%u pcmBytes=%u",
			inSoundChannel->pd.source, format, num_packets, num_samples, pcm_bytes);

		alSourcei(inSoundChannel->pd.source, AL_BUFFER, 0);
		alBufferData(inSoundChannel->pd.buffer, format, pcm, pcm_bytes, SScSamplesPerSecond);
		CHECK_AL_ERROR();
		alSourcei(inSoundChannel->pd.source, AL_BUFFER, inSoundChannel->pd.buffer);
		CHECK_AL_ERROR();

		UUrMemory_Block_Delete(pcm);
		return UUcTrue;
	}
}

// ----------------------------------------------------------------------
void
SS2rPlatform_SoundChannel_SetPan(
	SStSoundChannel				*inSoundChannel,
	UUtUns32					inPanFlags,
	float						inPan)
{
	// https://github.com/kcat/openal-soft/issues/194#issuecomment-392520073
	alSourcef(inSoundChannel->pd.source, AL_ROLLOFF_FACTOR, 0.0f);
	alSourcei(inSoundChannel->pd.source, AL_SOURCE_RELATIVE, AL_TRUE);
	switch (inPanFlags)
	{
		case SScPanFlag_Left:
			inPan = -inPan;
		break;

		case SScPanFlag_Right:
		break;

		case SScPanFlag_None:
		default:
			inPan = 0.0f;
		break;
	}
	alSource3f(inSoundChannel->pd.source, AL_POSITION, inPan, 0, -sqrtf(1.0f - inPan*inPan));
	CHECK_AL_ERROR();
}

// ----------------------------------------------------------------------
void
SS2rPlatform_SoundChannel_SetPitch(
	SStSoundChannel				*inSoundChannel,
	float						inPitch)
{
	alSourcef(inSoundChannel->pd.source, AL_PITCH, inPitch);
	CHECK_AL_ERROR();
}

// ----------------------------------------------------------------------
void
SS2rPlatform_SoundChannel_SetVolume(
	SStSoundChannel				*inSoundChannel,
	float						inVolume)
{
	alSourcef(inSoundChannel->pd.source, AL_GAIN, inVolume);
	CHECK_AL_ERROR();
}

// ----------------------------------------------------------------------
void
SS2rPlatform_SoundChannel_Silence(
	SStSoundChannel				*inSoundChannel)
{
	//Mac also does nothing
}

// ----------------------------------------------------------------------
void
SS2rPlatform_SoundChannel_Stop(
	SStSoundChannel				*inSoundChannel)
{
	alSourceStop(inSoundChannel->pd.source);
	CHECK_AL_ERROR();
	SSiSoundChannel_SetPlaying(inSoundChannel, UUcFalse);
}

// ----------------------------------------------------------------------
void
SS2rPlatform_SoundChannel_Terminate(
	SStSoundChannel				*inSoundChannel)
{
	alDeleteSources(1, &inSoundChannel->pd.source);
	inSoundChannel->pd.source = 0;
	CHECK_AL_ERROR();

	alDeleteBuffers(1, &inSoundChannel->pd.buffer);
	inSoundChannel->pd.buffer = 0;
	CHECK_AL_ERROR();
}

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
UUtError
SS2rPlatform_Initialize(
	UUtWindow					inWindow,			/* only used by Win32 */
	UUtUns32					*outNumChannels,
	UUtBool						inUseSound)
{
	*outNumChannels = 0;

	if (!inUseSound) {
		/* -nosound: skip OpenAL init entirely. SSgDevice / SSgContext stay
		   NULL; downstream SS code guards on SSgEnabled. */
		SSgDevice = NULL;
		SSgContext = NULL;
		return UUcError_None;
	}

	SSgDevice = alcOpenDevice(NULL);
	CHECK_AL_ERROR();
	UUmError_ReturnOnNull(SSgDevice);

	ALCint attrs[] = {
		ALC_FREQUENCY, SScSamplesPerSecond,
		ALC_MONO_SOURCES, SScMaxSoundChannels * 2,
		ALC_STEREO_SOURCES, SScMaxSoundChannels * 2,
		0
	};
	SSgContext = alcCreateContext(SSgDevice, &attrs);
	CHECK_AL_ERROR();
	UUmError_ReturnOnNull(SSgContext);

	// Apple's OpenAL does not honor ALC_MONO_SOURCES / ALC_STEREO_SOURCES in
	// alcGetIntegerv — output vars stay uninitialized. Initialize to 0 and
	// fall back to the count we requested via attrs if the query is a no-op.
	ALCint numMono = 0, numStereo = 0;
	alcGetIntegerv(SSgDevice, ALC_MONO_SOURCES, 1, &numMono);
	alcGetIntegerv(SSgDevice, ALC_STEREO_SOURCES, 1, &numStereo);
	CHECK_AL_ERROR();
	if (numMono <= 0 || numStereo <= 0) {
		numMono = SScMaxSoundChannels * 2;
		numStereo = SScMaxSoundChannels * 2;
	}
	*outNumChannels = (UUtUns32)UUmMin(numMono, numStereo);

	alcMakeContextCurrent(SSgContext);
	CHECK_AL_ERROR();

	UUrStartupMessage(
		"[SS2/OAL] Init OK: device=%p context=%p numChannels=%u (numMono=%d numStereo=%d)",
		(void*)SSgDevice, (void*)SSgContext,
		*outNumChannels, numMono, numStereo);

	return UUcError_None;
}

UUtError
SS2rPlatform_InitializeThread(
	void)
{
	UUrStartupMessage("[SS2/OAL] InitializeThread called (no-op returning UUcError_None)");
	return UUcError_None;
}

// ----------------------------------------------------------------------
void
SS2rPlatform_TerminateThread(
	void)
{
}

// ----------------------------------------------------------------------
void
SS2rPlatform_Terminate(
	void)
{
	alcMakeContextCurrent(NULL);
	CHECK_AL_ERROR();
	if (SSgContext)
	{
		alcDestroyContext(SSgContext);
		CHECK_AL_ERROR();
		SSgContext = NULL;
	}

	if (SSgDevice)
	{
		alcCloseDevice(SSgDevice);
		CHECK_AL_ERROR();
		SSgDevice = NULL;
	}
}

// ======================================================================
#if 0
#pragma mark -
#endif
// ======================================================================
// ----------------------------------------------------------------------
void
SSrDeleteGuard(
	SStGuard					*inGuard)
{
}

// ----------------------------------------------------------------------
void
SSrCreateGuard(
	SStGuard					**inGuard)
{
}

// ----------------------------------------------------------------------
void
SSrReleaseGuard(
	SStGuard					*inGuard)
{
}

// ----------------------------------------------------------------------
void
SSrWaitGuard(
	SStGuard					*inGuard)
{
}
