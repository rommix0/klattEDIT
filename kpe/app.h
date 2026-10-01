/* app.h - state and functions shared by the editor's Win32 files (main.c, strips.c, table.c, dialogs.c,
 * audio.c). One document per process. */
#ifndef KPE_APP_H
#define KPE_APP_H

#include <windows.h>
#include "model.h"
#include "spectro.h"
#include "synthrun.h"
#include "wavimport.h"

#define WM_APP_PLAYDONE (WM_APP + 1)

typedef struct {
	HINSTANCE inst;
	HWND main, panel, plot, status, table;
	HFONT font;
	int dpi;

	kpe_doc doc;
	wchar_t path[MAX_PATH];   /* L"" for a document never saved */
	wchar_t import_path[MAX_PATH]; /* the WAV an unsaved imported document came from, else L"" */

	synth_result snd;         /* the current synthesis of doc, at SPD 1 (the display's) */
	double play_spd;          /* the SPD of the sound playing, for the playhead */
	kpe_spectro spec;         /* its spectrogram */
	unsigned char *levels;    /* spec.ncols * spec.nbins darkness values */
	int narrowband;
	double spec_top_hz;       /* top of the formant strip */

	int visible[NP];          /* parameter strips; for F1-F4, tracks on the formant strip */
	double t0_ms, ms_per_px;  /* plot scroll position and zoom */
	int cursor;               /* frame, or -1 */
	int sel_a, sel_b;         /* selected frames sel_a..sel_b inclusive; sel_a < 0 when nothing is selected */
	double *clip;             /* frames from Cut/Copy */
	int nclip;
	double *ghost[NP];        /* Optimize preview: original tracks drawn faintly, or NULL */
	int ghost_first;          /* frame index of ghost[p][0] */
	int ghost_count;
	double play_ms;           /* playhead, or -1 when not playing */
} kpe_app;

extern kpe_app app;

/* Scales a 96-DPI pixel size to the screen. */
int S(int px);

/* main.c */
enum { CHG_VALUES = 1, CHG_FRAMES = 2, CHG_GLOBALS = 4, CHG_ALL = 7 };
void app_changed(int what);             /* after any document change: synthesize, refresh every view */
void app_set_cursor(int frame, int from_table);
void app_status(const wchar_t *text);   /* first status bar part */
const char *param_name(int kind, int p);
void app_error(const char *utf8);       /* message box with a UTF-8 message */

/* strips.c */
ATOM plot_register(HINSTANCE inst);
void plot_fit(void);
void plot_zoom(double factor);
void plot_sync_scroll(void);
void plot_new_spectrogram(void);        /* after app.levels changed */

/* table.c */
void table_show(void);
void table_refresh(void);               /* frame count or values changed */
void table_select(int frame);

/* dialogs.c */
int dlg_globals(HWND owner, double *globals, double *duration_ms); /* duration_ms NULL: Globals, else New */
void dlg_optimize(HWND owner);
void dlg_paramref(HWND owner);
int dlg_asknum(HWND owner, const wchar_t *title, const wchar_t *label, double *value, double lo, double hi);
int dlg_import(HWND owner, import_options *o); /* File > Import WAV's options; 1 on Import, 0 on Cancel */

/* audio.c */
int audio_play(const short *pcm, int n, int sr, int first, int count); /* 0 or -1 */
void audio_stop(void);
int audio_playing(void);
long audio_pos_sample(void);            /* the sample of pcm (audio_play's) playing now, or -1 */
void audio_finished(WPARAM generation);  /* on WM_APP_PLAYDONE; stale notifications are ignored */

#endif
