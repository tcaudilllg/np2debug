#include	"compiler.h"
#include	"resource.h"
#include	"np2.h"
#include	"viewer.h"
#include	"viewcmn.h"
#include	"viewmenu.h"
#include	"viewmem.h"
#include	"viewasm.h"
#include	"viewstat.h"
#include	"unasm.h"
#include	"cpucore.h"
#include	"break.h"

static const DWORD MAX_UNASM = 256;

static void set_viewseg(HWND hwnd, NP2VIEW_T *view, UINT16 seg) {

	if (view->seg != seg) {
		view->seg = seg;
		view->cursor = 0;
		view->cursorline = -1;
		InvalidateRect(hwnd, NULL, TRUE);
	}
}

static UINT viewasm_unasm_next(UNASM una, UINT8* hexbuf, NP2VIEW_T *view, UINT16 off)
{
	UINT8	*p;

	off &= CPU_ADRSMASK;
	if (view->lock) {
		p = (BYTE *)view->buf1.ptr;
		p += off;
		if (off > 0xfff0) {
			UINT32 pos = 0x10000 - off;
			CopyMemory(hexbuf, p, pos);
			CopyMemory(hexbuf + pos, view->buf1.ptr, 16 - pos);
			p = hexbuf;
		}
		else	{
			CopyMemory(hexbuf, p, 16);
		}
	}
	else {
		p = hexbuf;
		viewmem_read(&(view->dmem), (view->seg << 4) + off, hexbuf, 16);
	}
	return unasm(una, p, 16, FALSE, off);
}

static UINT16 viewasm_line_to_off(NP2VIEW_T *view, UINT16 line)
{
	return *(((UINT16 *)view->buf2.ptr) + line);
}

static void viewasm_toggle_breakpoint(NP2VIEW_T *view)
{
	if(view->cursorline < 0)	return;

	if(np2break_toggle(view->seg, view->cursor, NP2BP_EXECUTE))	{
		InvalidateRect(view->clientwnd, NULL, TRUE);
	}
}

static void viewasm_fill_line(DWORD top, DWORD height, COLORREF col, RECT *rc, HDC hdc)	{

	HBRUSH hbrush = CreateSolidBrush(col);
	RECT rect;
	rect.left = 0;
	rect.top = top;
	rect.right = rc->right;
	rect.bottom = top + height;
	FillRect(hdc, &rect, hbrush);
	DeleteObject(hbrush);
}

static void viewasm_paint(NP2VIEW_T *view, RECT *rc, HDC hdc) {

	LONG	x, y, i;
	UINT32	seg4;
	UINT16	off, prevoff;
	UINT32	pos;
	UINT8	*p;
	UINT8	buf[16];
	TCHAR	str[16];
	_UNASM	una;
	int		step;
#if defined(UNICODE)
	TCHAR	cnv[64];
#endif
	COLORREF	bkcol;

	if (view->lock) {
		if ((view->buf1.type != ALLOCTYPE_SEG) ||
			(view->buf1.arg != view->seg)) {
			if (viewcmn_alloc(&view->buf1, 0x10000)) {
				view->lock = FALSE;
				viewmenu_lock(view);
			}
			else {
				view->buf1.type = ALLOCTYPE_SEG;
				view->buf1.arg = view->seg;
				viewmem_read(&view->dmem, view->buf1.arg << 4,
											(BYTE *)view->buf1.ptr, 0x10000);
				view->buf2.type = ALLOCTYPE_NONE;
			}
			viewcmn_putcaption(view);
		}
	}

	seg4 = view->seg << 4;
	pos = view->pos;
	
	if ((view->buf2.type != ALLOCTYPE_ASM) ||
		(view->buf2.arg != (seg4 + view->off))) {
		if (viewcmn_alloc(&view->buf2, view->maxline*2)) {
			pos = 0;
		}
		else {
			DWORD i;
			UINT16 *r;
			r = (UINT16 *)view->buf2.ptr;
			view->buf2.type = ALLOCTYPE_ASM;
			view->buf2.arg = seg4 + view->off;
			off = view->off;
			for (i=0; i<(view->maxline - 1); i++) {
					
				*r++ = off;
				step = viewasm_unasm_next(NULL, buf, view, off);
				off += (UINT16)step;
			}
			*r = off;
		}
	}

	prevoff = off = viewasm_line_to_off(view, view->pos);

	for (y=0; y<rc->bottom; y+=viewcfg.font_height) {
		x = 0;
		// Emphasize wrapping
		if(prevoff > off)	{
			viewasm_fill_line(y, 2, viewcfg.color_hilite, rc, hdc);
		}
		
		bkcol = viewcfg.color_back;
		// IP?
		if(view->seg == CPU_CS && off == CPU_IP)	{
			viewasm_fill_line(y, viewcfg.font_height, viewcfg.color_text, rc, hdc);
			bkcol = viewcfg.color_text;
			SetTextColor(hdc, viewcfg.color_active);
		}
		else		{
			SetTextColor(hdc, viewcfg.color_text);
		}
		// Cursor?
		if(off == view->cursor)	{
			viewasm_fill_line(y, viewcfg.font_height, viewcfg.color_cursor, rc, hdc);
			bkcol = viewcfg.color_cursor;
		}

		// Breakpoint?
		if(np2break_is_set(view->seg, off))	{
			SetBkColor(hdc, viewcfg.color_hilite);
		}
		else {
			SetBkColor(hdc, bkcol);
		}

		wsprintf(str, _T("%04x:%04x"), view->seg, off);
		TextOut(hdc, x, y, str, 9);

		SetBkColor(hdc, bkcol);
		step = viewasm_unasm_next(&una, buf, view, off);
		p = buf;
		if (!step) {
			break;
		}
		x += 11 * 8;
		for(i = 0; i < step; i++)	{
			wsprintf(str + (i*2), _T("%02x"), *p);
			p++;
		}
		TextOut(hdc, x, y, str, i * 2);
		x += 13 * 8;
#if defined(UNICODE)
		TextOut(hdc, x, y, cnv, MultiByteToWideChar(CP_ACP, 
					MB_PRECOMPOSED, una.mnemonic, -1, cnv, NELEMENTS(cnv)));
#else
		TextOut(hdc,x, y, una.mnemonic, lstrlen(una.mnemonic));
#endif
		x += 7 * 8;
		if (una.operand[0]) {
#if defined(UNICODE)
			TextOut(hdc, x, y, cnv, MultiByteToWideChar(CP_ACP,
					MB_PRECOMPOSED, una.operand, -1, cnv, NELEMENTS(cnv)));
#else
			TextOut(hdc, x, y, una.operand, lstrlen(una.operand));
#endif
		}
		prevoff = off;
		off += (UINT16)step;
	}
}


LRESULT CALLBACK viewasm_proc(NP2VIEW_T *view, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {

	UINT step = 0;
	LONG newcursor;

	switch (msg) {
		case WM_SYSKEYDOWN:
		case WM_KEYDOWN:
			switch(wp)
			{
			case VK_UP:
				view->cursorline--;
				break;
			case VK_DOWN:
				view->cursorline++;
				break;
			case VK_PRIOR:
				view->cursorline -= view->step;
				break;
			case VK_NEXT:
				view->cursorline += view->step;
				break;
			}
			// Clamp
			if(view->cursorline < 0) {
				view->cursorline = 0;
			} else if((UINT32)view->cursorline > (view->maxline - 1)) {
				view->cursorline = view->maxline - 1;
			}

			viewer_scroll_fit_line(view, view->cursorline);
			newcursor = viewasm_line_to_off(view, view->cursorline);
			if(newcursor != view->cursor)	{
				view->cursor = newcursor;
				InvalidateRect(hwnd, NULL, TRUE);
			}
			break;

		case WM_COMMAND:
			switch(LOWORD(wp)) {
				case IDM_BREAK_TOGGLE:
					viewasm_toggle_breakpoint(view);
					break;

				case IDM_SEGCS:
					set_viewseg(hwnd, view, CPU_CS);
					break;

				case IDM_SEGDS:
					set_viewseg(hwnd, view, CPU_DS);
					break;

				case IDM_SEGES:
					set_viewseg(hwnd, view, CPU_ES);
					break;

				case IDM_SEGSS:
					set_viewseg(hwnd, view, CPU_SS);
					break;

				case IDM_SEGTEXT:
					set_viewseg(hwnd, view, 0xa000);
					break;

				case IDM_VIEWMODELOCK:
					view->lock ^= 1;
					viewmenu_lock(view);
					viewcmn_putcaption(view);
					InvalidateRect(hwnd, NULL, TRUE);
					break;

				case IDM_GOTO:
					view->seg = lp >> 16;
					view->off = lp & 0xffff;
					break;
			}
			break;

		case WM_LBUTTONDOWN:	{
				POINTS mpos = MAKEPOINTS(lp);
				view->cursorline = view->pos + (mpos.y / viewcfg.font_height);
				view->cursor = viewasm_line_to_off(view, view->cursorline);
				InvalidateRect(hwnd, NULL, TRUE);
			}
			break;

		case WM_PAINT:
			viewstat_update(view);
			viewcmn_paint(view, viewasm_paint);
			break;

		default:
			return(DefWindowProc(hwnd, msg, wp, lp));
	}
	return(0L);
}


// ---------------------------------------------------------------------------

void viewasm_init(NP2VIEW_T *dst, NP2VIEW_T *src) {

	if (src) {
		switch(src->type) {
			case VIEWMODE_SEG:
				dst->seg = dst->seg;
				dst->off = (UINT16)(dst->pos << 4);
				break;

			case VIEWMODE_1MB:
				if (dst->pos < 0x10000) {
					dst->seg = (UINT16)dst->pos;
					dst->off = 0;
				}
				else {
					dst->seg = 0xffff;
					dst->off = (UINT16)((dst->pos - 0xffff) << 4);
				}
				break;

			case VIEWMODE_ASM:
				dst->seg = src->seg;
				dst->off = src->off;
				break;

			default:
				src = NULL;
				break;
		}
	}
	if (!src) {
		dst->seg = CPU_CS;
		dst->off = CPU_IP;
	}
	dst->type = VIEWMODE_ASM;
	dst->maxline = MAX_UNASM;
	dst->mul = 1;
	dst->pos = 0;
	dst->cursor = 0;
	dst->cursorline = -1;
}

