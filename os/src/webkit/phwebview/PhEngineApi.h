/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: BSD-2-Clause */

/*
 * PhEngineApi.h -- the C seam between the BrCtl facade (src/phebrctl, Symbian
 * C++ on the real e32 include line) and the WebKit engine (PhEngine.cpp, WebCore on the pure Open C line). Plain C, POD arguments,
 * UTF-16 strings (a Symbian TDesC16 goes straight through). This header must include neither e32 nor WebKit headers.
 *
 * Threads: the engine runs on ONE thread of its own (ph_eng_main -- the facade creates the RThread with a big heap and stack and
 * calls it; it returns after ph_eng_quit). Every other function is callable from any thread; commands are queued and answered
 * through ph_eng_poll / ph_eng_get_* / the pixel lock. `notify` is called ON THE ENGINE THREAD whenever a view has something new
 * (an event, a repaint): it must only complete a request on the UI thread, never touch WebKit or block.
 */
#ifndef PH_ENGINE_API_H
#define PH_ENGINE_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*PhEngNotify)(void* ctx, int view);

/* events (ph_eng_poll) */
enum {
    PH_EV_NONE = 0,
    PH_EV_START,        /* a main-frame navigation began (a = 1 when the page asked for it, 0 when the UI did) */
    PH_EV_COMMIT,       /* the new document was created (its URL is now ph_eng_get_str(PH_STR_URL)) */
    PH_EV_FIRST_LAYOUT, /* first layout of the new document */
    PH_EV_TITLE,        /* the title changed */
    PH_EV_PROGRESS,     /* a = progress 0..100 */
    PH_EV_FINISHED,     /* the load completed */
    PH_EV_FAILED,       /* a = error code (b = 1: HTTP/engine error, 0: other); text in PH_STR_ERROR */
    PH_EV_REPAINT,      /* new pixels available */
    PH_EV_HISTORY,      /* canBack / canForward changed */
    PH_EV_FOCUS,        /* a = the focused element kind (0 none, 1 anchor, 2 input) */
    PH_EV_NEW_WINDOW    /* the page asked for a new window (target=_blank): the URL is in PH_STR_NEWWIN */
};

typedef struct PhEvent {
    int kind;
    int a;
    int b;
} PhEvent;

/* ph_eng_get_str */
enum { PH_STR_URL = 0, PH_STR_TITLE = 1, PH_STR_ERROR = 2, PH_STR_FOCUS_URL = 3, PH_STR_NEWWIN = 4, PH_STR_TEXT = 5 };

/* ph_eng_cmd */
enum { PH_CMD_STOP = 0, PH_CMD_RELOAD = 1, PH_CMD_BACK = 2, PH_CMD_FORWARD = 3, PH_CMD_ACTIVATE = 4 };

/* ph_eng_pointer */
enum { PH_PTR_DOWN = 0, PH_PTR_MOVE = 1, PH_PTR_UP = 2 };

typedef struct PhViewState {
    int canBack;
    int canForward;
    int loading;
    int progress;
    int scrollX, scrollY;
    int contentW, contentH;
    int focusKind;
} PhViewState;

/* the engine thread: runs until ph_eng_quit. Returns 0 on a clean quit, else an error code. */
int ph_eng_main(PhEngNotify notify, void* ctx);
void ph_eng_quit(void);
int ph_eng_running(void);   /* the engine initialised and is serving commands */

/* views: ids are chosen by the caller (ph_eng_new_view_id), created asynchronously on the engine thread */
int ph_eng_new_view_id(void);
void ph_eng_view_create(int view, int width, int height);
void ph_eng_view_destroy(int view);   /* synchronous: no notify for the view after it returns */
void ph_eng_view_resize(int view, int width, int height);
void ph_eng_view_visible(int view, int visible);

void ph_eng_set_iap(const char* utf8Name);                  /* the Open C interface name curl uses (setdefaultif) */
void ph_eng_set_useragent(const unsigned short* s, int len);   /* the User-Agent (ASCII); len 0 = the engine's default */
void ph_eng_load(int view, const unsigned short* url, int len);
void ph_eng_cmd(int view, int cmd);
void ph_eng_pointer(int view, int type, int x, int y);
void ph_eng_scroll(int view, int dx, int dy);              /* relative, content pixels */
void ph_eng_key(int view, int winKey, int unicode, int down);
void ph_eng_setting(int view, int id, int value);          /* PH_SET_* */
enum { PH_SET_SCRIPT = 0, PH_SET_IMAGES = 1, PH_SET_FONTSCALE = 2, PH_SET_COOKIES = 3 };

int ph_eng_poll(int view, PhEvent* out);                   /* 1: one event copied, 0: none */
void ph_eng_get_state(int view, PhViewState* out);
int ph_eng_get_str(int view, int which, unsigned short* buf, int cap);   /* returns the full length (may exceed cap) */

/* pixels: BGRA 8888 (SkColorType N32 on little endian = Symbian EColor16MA/16MU), stride in bytes. Lock/unlock bracket the copy. */
int ph_eng_lock_pixels(int view, const unsigned int** px, int* width, int* height, int* strideBytes);
void ph_eng_unlock_pixels(int view);

#ifdef __cplusplus
}
#endif
#endif
