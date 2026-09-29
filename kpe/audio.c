/* audio.c - playback through waveOut. The samples are copied, so the document may change while playing. */
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include <mmsystem.h> /* after windows.h, which app.h includes */

static HWAVEOUT wo;
static WAVEHDR hdr;
static short *buf;
static int first_sample, rate;
static unsigned generation; /* tells a finished playback's notification from a stale one */

static void CALLBACK done(HWAVEOUT h, UINT msg, DWORD_PTR instance, DWORD_PTR p1, DWORD_PTR p2)
{
	(void)h;
	(void)p1;
	(void)p2;
	if (msg == WOM_DONE) /* only PostMessage is safe inside a waveOut callback */
		PostMessageW(app.main, WM_APP_PLAYDONE, (WPARAM)instance, 0);
}

int audio_play(const short *pcm, int n, int sr, int first, int count)
{
	audio_stop();
	if (first < 0)
		first = 0;
	if (first + count > n)
		count = n - first;
	if (count <= 0)
		return -1;
	buf = malloc(sizeof(short) * count);
	if (!buf)
		return -1;
	memcpy(buf, pcm + first, sizeof(short) * count);
	WAVEFORMATEX wf = { WAVE_FORMAT_PCM, 1, (DWORD)sr, (DWORD)sr * 2, 2, 16, 0 };
	generation++;
	if (waveOutOpen(&wo, WAVE_MAPPER, &wf, (DWORD_PTR)done, generation, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
		wo = NULL;
		free(buf);
		buf = NULL;
		return -1;
	}
	memset(&hdr, 0, sizeof hdr);
	hdr.lpData = (LPSTR)buf;
	hdr.dwBufferLength = sizeof(short) * count;
	waveOutPrepareHeader(wo, &hdr, sizeof hdr);
	if (waveOutWrite(wo, &hdr, sizeof hdr) != MMSYSERR_NOERROR) {
		audio_stop();
		return -1;
	}
	first_sample = first;
	rate = sr;
	return 0;
}

void audio_stop(void)
{
	if (!wo)
		return;
	HWAVEOUT h = wo;
	wo = NULL; /* audio_playing() is false from here on */
	waveOutReset(h);
	waveOutUnprepareHeader(h, &hdr, sizeof hdr);
	waveOutClose(h);
	free(buf);
	buf = NULL;
}

int audio_playing(void)
{
	return wo != NULL;
}

double audio_pos_ms(void)
{
	if (!wo)
		return -1;
	MMTIME t;
	t.wType = TIME_SAMPLES;
	if (waveOutGetPosition(wo, &t, sizeof t) != MMSYSERR_NOERROR || t.wType != TIME_SAMPLES)
		return -1;
	return (first_sample + (double)t.u.sample) * 1000.0 / rate;
}

void audio_finished(WPARAM gen)
{
	if (gen == generation)
		audio_stop();
}
