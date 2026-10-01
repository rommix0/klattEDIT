/* synthrun.h - synthesizes a whole document through the Klatt DLL. */
#ifndef KPE_SYNTHRUN_H
#define KPE_SYNTHRUN_H

#include "model.h"

typedef struct {
	short *pcm;    /* n samples at sr Hz */
	int n, sr;
	int overload;  /* samples clipped to 16 bits */
} synth_result;

/* Always uses noise seed 1, so the same document gives the same samples. synth_doc_speed synthesizes the
 * document with its SPD replaced by spd: Play and Export WAV pass the document's own SPD. synth_doc is the
 * display's synthesis (the strips and the spectrogram show the document as written): it passes 1. Return 0, or
 * a negative KLATT_ERR_* code (out->pcm is then NULL). */
int synth_doc_speed(const kpe_doc *d, double spd, synth_result *out);
int synth_doc(const kpe_doc *d, synth_result *out);
void synth_result_free(synth_result *r);

/* The sample index at which frame k starts at speed spd (within SPD's range), using the exact formula the DLL
 * uses internally (floor(k * spf + 1e-9), spf = SR * FRAME_MS / 1000 / SPD), so callers agree with
 * synth_doc_speed's output to the sample. Valid for 0 <= k <= d->nframes; k == d->nframes gives the total sample
 * count. */
long synth_frame_start(const kpe_doc *d, int k, double spd);
/* The document time (ms) of sample n of the sound synthesized at speed spd: the frame playing it plus how far
 * through that frame it is. The inverse of synth_frame_start; the playhead uses it. */
double synth_sample_ms(const kpe_doc *d, long n, double spd);

#endif
