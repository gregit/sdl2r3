/*
 * "REBOL 3 Extension to bind Simple DirectMedia Layer version 2 (SDL2R3)" by "Grégory Pecheret and François Jouen" is licensed under CC0
 */

#include "reb-host.h"
#include <SDL.h>


// Andreas Bolka's helpers ---

/** Copy from a C char* into a REBOL string!/binary!'s data. */
static REBSER *rlu_strncpy(REBSER *dest, const char *source, size_t n) {
    int i;
    for (i = 0; i < n; ++i) {
        RL_SET_CHAR(dest, i, source[i]);
    }
    return dest;
}

/** Create a REBOL (Latin1-)string!'s data from a C char*. */
static REBSER *rlu_make_string(const char *source) {
    size_t size = strlen(source);
    REBSER *result = RL_MAKE_STRING(size, FALSE); // FALSE: Latin1, no Unicode
    return rlu_strncpy(result, source, size);
}

/** Create a REBOL binary!'s data from a C char* & size_t. */
static REBSER *rlu_make_binary(const char *source, size_t size) {
    REBSER *result = RL_MAKE_STRING(size, FALSE); // @@ A111+: RL_MAKE_BINARY
    return rlu_strncpy(result, source, size);
}

/** Copy a REBOL binary!'s data & size into a C char* & size_t, respectively. */
static char *rlu_copy_binary(const RXIARG binary, size_t *size) {
    REBSER *binary_series = binary.series;
    size_t binary_index = binary.index;
    size_t binary_tail = RL_SERIES(binary_series, RXI_SER_TAIL);
    char *binary_data = (char*)RL_SERIES(binary_series, RXI_SER_DATA);
    char *result;

    *size = binary_tail - binary_index;
    result = (char*)malloc(*size); // @@ check
    memcpy(result, binary_data + binary_index, *size);

    return result;
}

/** Copy a REBOL string!'s data into a C char* (null-terminated). */
static char *rlu_copy_string(const RXIARG string) {
    // @@ should use RL_GET_STRING or something. won't work for unicode strings
    REBSER *string_series = string.series;
    size_t string_head = string.index;
    size_t string_tail = RL_SERIES(string_series, RXI_SER_TAIL);
    size_t string_size = string_tail - string_head;
    char *string_data = (char*)RL_SERIES(string_series, RXI_SER_DATA);
    char *result;

    result = (char*)malloc(string_size + 1); // @@ check
    result[string_size] = 0;
    memcpy(result, string_data + string_head, string_size);

    return result;
}

static void Add_Event_Drop(REBSER *filename, REBINT flags)
{
	REBEVT evt;
	evt.type  = EVT_DROP_FILE;
	evt.flags = (u8) (flags | (1<<EVF_COPIED));
	evt.model = EVM_GUI;
	evt.ser = (void*)filename;
	RL->event(&evt);
}

static void Add_Event_XY(REBGOB *gob, REBINT id, REBINT xy, REBINT flags)
{
	REBEVT evt;

	evt.type  = id;
	//evt.flags = (u8) (flags | (1<<EVF_HAS_XY));
	evt.flags = flags;
	evt.model = EVM_GUI;
	evt.data  = xy;
	evt.ser = (void*)gob;
	//RL_Event(&evt);	// returns 0 if queue is full
	RL->event(&evt);
}



RL_LIB *RL;
REBGOB *RootGob;
SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;

RXICBI* CBI_SDL_TIMER;

const char *init_block =
	"REBOL [\n"
		"Title: {Simple DirectMedia Layer REBOL Extension}\n"
		"Name: SDL2\n"
		"Type: extension\n"
	"]\n"
	"export sdl-version: command [{<Returns SDL version.}]\n"
	"export poll-event: command [{<Polls for pending events. Sets green flag to allow 10ms between events polling.} green? [logic!]]\n"
	"export create-window: command [{Creates a Window.} title [string!] dimension [pair!]]\n"
	"export destroy-window: command [{Destroys a Window.}]\n"
	"export refresh-screen: command [{Refreshes the screen by updating with any rendering performed since the previous call.}]\n"
	"export clear-screen: command [{Clears the window screen and paints it with a color. It doesn't call RefreshScreen.} color [tuple!]]\n"
	"export set-draw-color: command [{Set color to be used for drawing.} color [tuple!]]\n"
	"export draw-point: command [{Draws a point.} position [pair!]]\n"
	"export draw-line: command [{Draws a line.} start [pair!] end [pair!]]\n"
	"export draw-rect: command [{Draws a rectangle outline.} position [pair!] dimension [pair!]]\n"
	"export draw-fill-rect: command [{Draws a color filled rectangle. Color is set using set-draw-color command.} position [pair!] dimension [pair!]]\n"
	"export make-texture: command [{Make a texture.} path [string!]]\n"
	"export set-texture-blending: command [{Sets the blend mode for a texture.} texture [handle!] mode [integer!]]\n"
	"export draw-texture: command [{Draws a texture at specified location and stretchable dimension.} texture [handle!] position [pair!] dimension [pair!]]\n"
	"export draw-clip-texture: command [{Draws a clipped texture at specified location and stretchable dimension.} texture [handle!] position [pair!] dimension [pair!] clipXY [pair!] clipWH [pair!]]\n"
	"export video-drivers?: command [{<Returns the number of video drivers compiled into SDL.}]\n"
	"export to-video-driver-name: command [{Returns the name of a built in video driver.} index [integer!]]\n"
	"export platform?: command [{<Returns the name of the platform.}]\n"
	"export add-timer: command [{Adds a timer.} ms [integer!]]\n"
	"export init-gob: command [{Sets GOB root.} rg [gob!]]\n"
;

RXIEXT int R3SDL_getVersion (RXIFRM *frm);
RXIEXT int R3SDL_pollEvent (RXIFRM *frm);
RXIEXT int R3SDL_createWindow (RXIFRM *frm);
RXIEXT int R3SDL_destroyWindow (RXIFRM *frm);
RXIEXT int R3SDL_refreshScreen (RXIFRM *frm);
RXIEXT int R3SDL_clearScreen (RXIFRM *frm);
RXIEXT int R3SDL_setDrawColor (RXIFRM *frm);
RXIEXT int R3SDL_drawPoint (RXIFRM *frm);
RXIEXT int R3SDL_drawLine (RXIFRM *frm);
RXIEXT int R3SDL_drawRect (RXIFRM *frm);
RXIEXT int R3SDL_drawFillRect (RXIFRM *frm);
RXIEXT int R3SDL_makeTexture (RXIFRM *frm);
RXIEXT int R3SDL_setTextureBlendMode (RXIFRM *frm);
RXIEXT int R3SDL_drawTexture (RXIFRM *frm);
RXIEXT int R3SDL_drawClipTexture (RXIFRM *frm);
RXIEXT int R3SDL_getNumVideoDrivers (RXIFRM *frm);
RXIEXT int R3SDL_getVideoDriver (RXIFRM *frm);
RXIEXT int R3SDL_getPlatform (RXIFRM *frm);
RXIEXT int R3SDL_add_timer (RXIFRM *frm);
RXIEXT int R3SDL_init_gob (RXIFRM *frm);
Uint32 callback_timer( Uint32 interval, void* param );



RXIEXT const char *RX_Init(int options, RL_LIB *library) {
	RL = library;
	if (!CHECK_STRUCT_ALIGN) return 0;
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
		SDL_Quit();
		RL_PRINT("%s\n", "Failed to initialize SDL");
		return 0;
	};
	return init_block;
}

RXIEXT int RX_Quit(int options) {
	SDL_Quit();
	return 0;
}

RXIEXT int RX_Call(int command, RXIFRM *frm, void *data) {
	switch (command) {
	case  0: return R3SDL_getVersion (frm);
	case  1: return R3SDL_pollEvent (frm);
	case  2: return R3SDL_createWindow (frm);
	case  3: return R3SDL_destroyWindow (frm);
	case  4: return R3SDL_refreshScreen (frm);
	case  5: return R3SDL_clearScreen (frm);
	case  6: return R3SDL_setDrawColor (frm);
	case  7: return R3SDL_drawPoint (frm);
	case  8: return R3SDL_drawLine (frm);
	case  9: return R3SDL_drawRect (frm);
	case 10: return R3SDL_drawFillRect (frm);
	case 11: return R3SDL_makeTexture (frm);
	case 12: return R3SDL_setTextureBlendMode (frm);
	case 13: return R3SDL_drawTexture (frm);
	case 14: return R3SDL_drawClipTexture (frm);
	case 15: return R3SDL_getNumVideoDrivers (frm);
	case 16: return R3SDL_getVideoDriver (frm);
	case 17: return R3SDL_getPlatform (frm);
	case 18: return R3SDL_add_timer(frm);
	case 19: return R3SDL_init_gob(frm);
	}
	return RXR_NO_COMMAND;
}


RXIEXT int R3SDL_getVersion (RXIFRM *frm) {
	SDL_version linked;
	SDL_GetVersion(&linked);
	RXA_TUPLE(frm, 1)[0] = 3;
	RXA_TUPLE(frm, 1)[1] = linked.major;
	RXA_TUPLE(frm, 1)[2] = linked.minor;
	RXA_TUPLE(frm, 1)[3] = linked.patch;
	RXA_TYPE(frm, 1) = RXT_TUPLE;
	return RXR_VALUE;		
}


RXIEXT int R3SDL_pollEvent (RXIFRM *frm) {
	int xyd;
	char* filedir;
	REBSER* fileser;
	REBINT flags = 0;
	SDL_Event event;
	if(RXA_LOGIC(frm, 1)) {SDL_WaitEvent(&event);} else {SDL_PollEvent(&event);}
	RXICBI cbi;
	RXIARG args[4];
	REBCNT n;
	CLEAR(&cbi, sizeof(cbi));
	CLEAR(&args[0], sizeof(args));
	cbi.args = args;
	switch(event.type) {
	case SDL_QUIT:
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		Add_Event_XY(RootGob, EVT_CLOSE, 0, 0);
		break;
	case SDL_MOUSEMOTION:
		Add_Event_XY(RootGob, EVT_MOVE, (event.motion.x + (event.motion.y << 16)), 0);
		break;
	case SDL_MOUSEBUTTONDOWN:
		Add_Event_XY(RootGob, EVT_DOWN, (event.button.x + (event.button.y << 16)), 0);
		break;
	case SDL_MOUSEBUTTONUP:
		Add_Event_XY(RootGob, EVT_UP, (event.button.x + (event.button.y << 16)), 0);
		break;
	case SDL_DROPFILE:
		filedir = event.drop.file;
		fileser = rlu_make_string(filedir);
		Add_Event_Drop(fileser, 0);
		break;
	case SDL_KEYDOWN:
		if(SDL_GetModState() & KMOD_SHIFT) {flags |= (1<<EVF_SHIFT);}
		Add_Event_XY(RootGob, EVT_KEY, event.key.keysym.sym, 0);
		break;
	}
	return RXR_UNSET;
}



RXIEXT int R3SDL_createWindow (RXIFRM *frm) {
	RXIARG arg = RXA_ARG(frm, 1);
	char *title = rlu_copy_string(arg);
	window = SDL_CreateWindow( title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, RXA_PAIR(frm, 2).x, RXA_PAIR(frm, 2).y, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_RENDERER_PRESENTVSYNC);
	free(title);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	return RXR_UNSET;
}

RXIEXT int R3SDL_destroyWindow (RXIFRM *frm) {
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	return RXR_UNSET;
}

RXIEXT int R3SDL_refreshScreen (RXIFRM *frm) {
	SDL_RenderPresent(renderer);
	return RXR_UNSET;
}

RXIEXT int R3SDL_clearScreen (RXIFRM *frm) {
	SDL_SetRenderDrawColor(renderer, RXA_TUPLE(frm, 1)[1], RXA_TUPLE(frm, 1)[2], RXA_TUPLE(frm, 1)[3], RXA_TUPLE(frm, 1)[4]);
	SDL_RenderClear(renderer);
	return RXR_UNSET;
}

RXIEXT int R3SDL_setDrawColor (RXIFRM *frm) {
	SDL_SetRenderDrawColor(renderer, RXA_TUPLE(frm, 1)[1], RXA_TUPLE(frm, 1)[2], RXA_TUPLE(frm, 1)[3], RXA_TUPLE(frm, 1)[4]);
	return RXR_UNSET;
}

RXIEXT int R3SDL_drawPoint (RXIFRM *frm) {
	SDL_RenderDrawPoint(renderer, RXA_PAIR(frm, 1).x, RXA_PAIR(frm, 1).y);
	return RXR_UNSET;
}

RXIEXT int R3SDL_drawLine (RXIFRM *frm) {
	SDL_RenderDrawLine(renderer, RXA_PAIR(frm, 1).x, RXA_PAIR(frm, 1).y, RXA_PAIR(frm, 2).x, RXA_PAIR(frm, 2).y);
	return RXR_UNSET;
}

RXIEXT int R3SDL_drawRect (RXIFRM *frm) {
	SDL_Rect rect = {RXA_PAIR(frm, 1).x, RXA_PAIR(frm, 1).y, RXA_PAIR(frm, 2).x, RXA_PAIR(frm, 2).y};
	SDL_RenderDrawRect(renderer, &rect );
	return RXR_UNSET;
}

RXIEXT int R3SDL_drawFillRect (RXIFRM *frm) {
	SDL_Rect rect = {RXA_PAIR(frm, 1).x, RXA_PAIR(frm, 1).y, RXA_PAIR(frm, 2).x, RXA_PAIR(frm, 2).y};
	SDL_RenderFillRect(renderer, &rect );
	return RXR_UNSET;
}

RXIEXT int R3SDL_makeTexture (RXIFRM *frm) {
	RXIARG arg = RXA_ARG(frm, 1);
	char *path = rlu_copy_string(arg);
	SDL_Surface* loadingSurface = SDL_LoadBMP(path);
	free(path);
	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, loadingSurface);
	SDL_FreeSurface(loadingSurface);
	RXA_HANDLE(frm, 1) = texture;
	RXA_TYPE(frm, 1) = RXT_HANDLE;
	return RXR_VALUE;
}

RXIEXT int R3SDL_setTextureBlendMode (RXIFRM *frm) {
	SDL_Texture* texture =  RXA_HANDLE(frm, 1);
	SDL_BlendMode blending = SDL_BLENDMODE_NONE;
	switch(RXA_INT64(frm,2)) {
		case 1: blending = SDL_BLENDMODE_BLEND; break;
		case 2: blending = SDL_BLENDMODE_ADD; break;
		case 3: blending = SDL_BLENDMODE_MOD; break;
		default: blending = SDL_BLENDMODE_NONE;
	}
	SDL_SetTextureBlendMode(texture, blending);
	return RXR_UNSET;
}

RXIEXT int R3SDL_drawTexture (RXIFRM *frm) {
	SDL_Texture* texture =  RXA_HANDLE(frm, 1);
	SDL_Rect destRect;
	destRect.x = RXA_PAIR(frm, 2).x;
	destRect.y = RXA_PAIR(frm, 2).y;
	destRect.w = RXA_PAIR(frm, 3).x;
	destRect.h = RXA_PAIR(frm, 3).y;
	SDL_RenderCopy(renderer, texture, NULL, &destRect);
	return RXR_UNSET;
}

RXIEXT int R3SDL_drawClipTexture (RXIFRM *frm) {
	SDL_Texture* texture =  RXA_HANDLE(frm, 1);
	SDL_Rect destRect;
	SDL_Rect clipRect;
	destRect.x = RXA_PAIR(frm, 2).x;
	destRect.y = RXA_PAIR(frm, 2).y;
	destRect.w = RXA_PAIR(frm, 3).x;
	destRect.h = RXA_PAIR(frm, 3).y;
	clipRect.x = RXA_PAIR(frm, 4).x;
	clipRect.y = RXA_PAIR(frm, 4).y;
	clipRect.w = RXA_PAIR(frm, 5).x;
	clipRect.h = RXA_PAIR(frm, 5).y;
	SDL_RenderCopy(renderer, texture, &clipRect, &destRect);
	return RXR_UNSET;
}

RXIEXT int R3SDL_getNumVideoDrivers (RXIFRM *frm){
	RXA_INT64(frm,1) = SDL_GetNumVideoDrivers();
	RXA_TYPE(frm, 1)= RXT_INTEGER;
	return RXR_VALUE;
}

RXIEXT int R3SDL_getVideoDriver (RXIFRM *frm){
	const char *driverName = SDL_GetVideoDriver (RXA_INT64(frm,1));
	RXA_SERIES(frm, 1) = rlu_make_string(driverName);
	RXA_INDEX(frm, 1) = 0;
	RXA_TYPE(frm, 1) = RXT_STRING;
	return RXR_VALUE;
}

RXIEXT int R3SDL_getPlatform (RXIFRM *frm){
	RXA_SERIES(frm, 1) = rlu_make_string(SDL_GetPlatform ());
	RXA_INDEX(frm, 1) = 0;
	RXA_TYPE(frm, 1) = RXT_STRING;
	return RXR_VALUE;
}


RXIEXT int R3SDL_add_timer (RXIFRM *frm){
	SDL_TimerID timerID = SDL_AddTimer(RXA_INT64(frm,1), callback_timer, "waited!");
	return RXR_UNSET;
}

RXIEXT int R3SDL_init_gob (RXIFRM *frm){
	RootGob = (REBGOB*)RXA_SERIES(frm, 1); // system/view/screen-gob
	REBGOB **gp;
	i32 n;
	REBYTE* color;
	SDL_Rect rect;
	//REBVAL *val;

	if (GOB_PANE(RootGob)) {
		gp = (REBGOB **) RL_SERIES(GOB_PANE(RootGob), RXI_SER_DATA);
		//gp = GOB_HEAD(RootGob);
		for (n = (REBCNT)RL_SERIES(GOB_PANE(RootGob), RXI_SER_TAIL) - 1; n >= 0; n--,gp++) {
		//for (n = GOB_TAIL(RootGob)-1; n >= 0; n--, gp++) {
			color = (REBYTE*)&GOB_CONTENT(*gp);
			SDL_SetRenderDrawColor(renderer, color[2], color[1], color[0], 255);
			rect.x = GOB_X_INT(*gp);
			rect.y = GOB_Y_INT(*gp);
			rect.w = GOB_W_INT(*gp);
			rect.h = GOB_H_INT(*gp);
			SDL_RenderFillRect(renderer, &rect );
			//RL_PRINT("%d\n", (*gp)->alpha);
		}
		SDL_RenderPresent(renderer);
	}
	return RXR_UNSET;
}

Uint32 callback_timer( Uint32 interval, void* param ) {
	Add_Event_XY(RootGob, EVT_TIME, 0, 0);
	return 0;
}

