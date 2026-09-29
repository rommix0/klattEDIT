/* synthrun.h - synthesizes a whole document through the Klatt DLL. */
#ifndef KPE_SYNTHRUN_H
#define KPE_SYNTHRUN_H

#include "model.h"

typedef struct {
	short *pcm;    /* n samples at sr Hz */
	int n, sr;
	int overload;  /* samples clipped to 16 bits */
} synth_result;

/* Always uses noise seed 1, so the same document gives the same samples. Returns 0, or a negative
 * KLATT_ERR_* code (out->pcm is then NULL). */
int synth_doc(const kpe_doc *d, synth_result *out);
void synth_result_free(synth_result *r);

#endif
