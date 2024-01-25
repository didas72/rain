#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>

#include "portaudio/portaudio.h"


#define SAMPLE_RATE 22050
//#define SAMPLE_RATE 44100

#define USE_PRECALC 1

const float MAXDIST = 200.0;
const float MINDIST = 40.0;

const double CS = 343.0;
const double LPOS = -0.075;
const double RPOS = +0.075;
const double DECAY = 1.1;
const double EXP_DROP = 0.015;
const double EXP_SCALE = 45.0;
const double S_BASE = 45.0;
const double PITCH_SCALE = 18.0;

const double VOLUME = 0.55;
const double DROPCOUNT = 50;



typedef struct
{
	//position of drop, negative is left
	double posr, posf;
	//pitch of sound
	double size;
	//frequency of drop
	double period;

	double precalc_dr, precalc_dl;
	double precalc_dpcsr, precalc_dpcsl;
} drop_t;

typedef struct 
{
	double time;
	double volume;
	drop_t *dropv;
	size_t dropc;
} state_data;



void error_crash(PaError err);
int stream_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *data);



float randomf(float min, float max)
{
	return ((float)rand() / (float)(RAND_MAX)) * (max - min) + min;
}

state_data random_drops(double volume, size_t dropc)
{
	drop_t *drops = malloc(dropc * sizeof(drop_t));

	for (size_t i = 0; i < dropc; i++)
	{
		drops[i].posr = randomf(-MAXDIST, MAXDIST);
		drops[i].posf = randomf(MINDIST, MAXDIST);
		drops[i].size = randomf(0.8, 1.2);
		drops[i].period = randomf(1.5, 5.5);
		double offr = RPOS - drops[i].posr;
		double offl = LPOS - drops[i].posr;
		double posf2 = drops[i].posf * drops[i].posf;
		drops[i].precalc_dr = sqrt(offr * offr + posf2);
		drops[i].precalc_dl = sqrt(offl * offl + posf2);
		drops[i].precalc_dpcsr = drops[i].precalc_dr / CS;
		drops[i].precalc_dpcsl = drops[i].precalc_dl / CS;
	}

	state_data ret = { .time = 2.0, .volume = volume, .dropv = drops, .dropc = dropc };
	return ret;
}

double drop_sound_precalc(drop_t drop, double time, bool right)
{
	double rel_time = fmod(time, drop.period);
	double t = rel_time - (right ? drop.precalc_dpcsr : drop.precalc_dpcsl);

	if (t < 0.0)
		return 0.0;

	double s = pow(S_BASE, 1.0 + t);
	double source_sample = exp((-t - EXP_DROP) * EXP_SCALE) * sin(s * PITCH_SCALE * drop.size);
	double final_sample = source_sample / (1 + (right ? drop.precalc_dr : drop.precalc_dl) * DECAY);
	return final_sample;
}

double drop_sound(drop_t drop, double time, double sample_posr)
{
	double rel_time = fmod(time, drop.period);
	double offr = sample_posr - drop.posr;
	double d = sqrt(offr * offr + drop.posf * drop.posf);
	double t = rel_time - d / CS;

	if (t < 0.0)
		return 0.0;

	double s = pow(S_BASE, 1.0 + t);
	double source_sample = exp((-t - EXP_DROP) * EXP_SCALE) * sin(s * PITCH_SCALE * drop.size);
	double final_sample = source_sample / (1 + d * DECAY);
	return final_sample;
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	PaError err;

	err = Pa_Initialize();

	if (err != paNoError) error_crash(err);

	PaStream *stream;
	state_data data = random_drops(VOLUME, DROPCOUNT);

	err = Pa_OpenDefaultStream(
		&stream,						//stream pointer
		0,								//no input channels
		2,								//stereo output
		paFloat32,						//sample format
		SAMPLE_RATE,					//sample rate
		paFramesPerBufferUnspecified,	//frames per buffer (optimised)
		stream_callback,				//callback
		&data							//auxiliar data if needed
	);
	if (err != paNoError) error_crash(err);

	err = Pa_StartStream(stream);
	if (err != paNoError) error_crash(err);

	Pa_Sleep(50000);

	err = Pa_StopStream(stream);
	if (err != paNoError) error_crash(err);

	err = Pa_CloseStream(stream);
	if (err != paNoError) error_crash(err);

	err = Pa_Terminate();
	if (err != paNoError) error_crash(err);
	return 0;
}



void error_crash(PaError err)
{
	printf("PortAudio error: '%s'.\n", Pa_GetErrorText(err));
	exit(1);
}

int stream_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *data)
{
	(void)inputBuffer;
	float *out_buff = (float*)outputBuffer;
	state_data *state = (state_data*)data;
	(void)timeInfo;
	(void)statusFlags;
	unsigned long i;

	for (i = 0; i < framesPerBuffer; ++i)
	{
		double lsample = 0.0, rsample = 0.0;

		for (size_t i = 0; i < state->dropc; i++)
		{
			#if USE_PRECALC == 1
			lsample += drop_sound_precalc(state->dropv[i], state->time, false);
			rsample += drop_sound_precalc(state->dropv[i], state->time, true);
			#else
			lsample += drop_sound(state->dropv[i], state->time, LPOS);
			rsample += drop_sound(state->dropv[i], state->time, RPOS);
			#endif
		}

		*(out_buff++) = (float)(lsample * state->volume);
		*(out_buff++) = (float)(rsample * state->volume);

		state->time += 1.0 / (double)SAMPLE_RATE;
	}

	return 0;
}
