/*
 * RAWGL32X - the 32X renderer: Another World's four 320x200 pages kept at 4 bits per pixel,
 * like the original game (4 x 32,000 bytes = 125 KB of SDRAM instead of 250 KB).
 *
 * It reproduces rawgl's GraphicsSoft in the "original" (1991) mode exactly: same polygon
 * rasteriser (copied), same three span modes (colour, copy from page 0, "transparent" =
 * set bit 3), same font, sprite masks and page copies.  The PC test build (host32x) draws
 * every frame with this renderer and compares it with GraphicsSoft: "0 different".
 *
 * Packing: pixel (x, y) is in byte y * 160 + x / 2; even x = high nibble.
 * Bitmaps: decoded straight from the planar data (FMT_PLANAR_AMIGA / _ATARI), no temp buffer.
 * Display: on the 32X, plat_present_4bpp() expands the page into the frame buffer;
 * on the PC, drawBuffer() converts to RGB555 for the harness, as GraphicsSoft does.
 */
#include "../rawgl-master/graphics.h"
#include "../rawgl-master/systemstub.h"
#include "../rawgl-master/util.h"
#ifdef RAWGL32X_MARS
#include "plat.h"
#endif

enum { PAGE_PITCH = GFX_W / 2, PAGE_SIZE = PAGE_PITCH * GFX_H };

struct Graphics4bpp : Graphics {
	enum { MODE_COLOR, MODE_PAGE, MODE_ALPHA };

	uint8_t *_pages[4];
	uint8_t *_draw;          // page being drawn (_drawPagePtr in GraphicsSoft)
	Color _pal[16];

	Graphics4bpp() {
		_fixUpPalette = FIXUP_PALETTE_NONE;
		for (int i = 0; i < 4; ++i) _pages[i] = 0;
		_draw = 0;
		memset(_pal, 0, sizeof(_pal));
	}
	virtual ~Graphics4bpp() {
		for (int i = 0; i < 4; ++i) free(_pages[i]);
	}

	uint8_t *page(int n) {
		assert(n >= 0 && n < 4);
		return _pages[n];
	}

	static void put(uint8_t *row, int x, uint8_t c) {
		uint8_t *p = row + (x >> 1);
		*p = (x & 1) ? (uint8_t)((*p & 0xF0) | c) : (uint8_t)((*p & 0x0F) | (c << 4));
	}
	static uint8_t get(const uint8_t *row, int x) {
		const uint8_t b = row[x >> 1];
		return (x & 1) ? (b & 15) : (b >> 4);
	}
	void pixel(int x, int y, int mode, uint8_t c) {
		uint8_t *row = _draw + y * PAGE_PITCH;
		switch (mode) {
		case MODE_COLOR: put(row, x, c); break;
		case MODE_ALPHA: put(row, x, get(row, x) | 8); break;
		case MODE_PAGE:  put(row, x, get(_pages[0] + y * PAGE_PITCH, x)); break;
		}
	}

	// one horizontal span [x1, x2] (either order), as drawLineN / drawLineP / drawLineT
	void span(int16_t xa, int16_t xb, int16_t y, int mode, uint8_t c) {
		if (mode == MODE_PAGE && _draw == _pages[0]) {
			return;  // GraphicsSoft::drawLineP: nothing to copy onto page 0 itself
		}
		int x1 = MIN(xa, xb), x2 = MAX(xa, xb);
		if (x1 & 1) {
			pixel(x1, y, mode, c);
			++x1;
		}
		if (x1 > x2) return;
		if (!(x2 & 1)) {
			pixel(x2, y, mode, c);
			--x2;
		}
		if (x1 > x2) return;
		uint8_t *d = _draw + y * PAGE_PITCH + (x1 >> 1);
		const int n = ((x2 - x1) >> 1) + 1;
		switch (mode) {
		case MODE_COLOR: {
				const uint8_t b = (uint8_t)(c * 0x11);
				for (int i = 0; i < n; ++i) d[i] = b;
			}
			break;
		case MODE_ALPHA:
			for (int i = 0; i < n; ++i) d[i] |= 0x88;
			break;
		case MODE_PAGE: {
				const uint8_t *s = _pages[0] + y * PAGE_PITCH + (x1 >> 1);
				for (int i = 0; i < n; ++i) d[i] = s[i];
			}
			break;
		}
	}

	static uint32_t calcStep(const Point &p1, const Point &p2, uint16_t &dy) {
		dy = p2.y - p1.y;
		uint16_t delta = (dy <= 1) ? 1 : dy;
		return ((p2.x - p1.x) * (0x4000 / delta)) << 2;
	}

	// GraphicsSoft::drawPolygon, unchanged apart from the span call (320x200, no scaling)
	void drawPolygon(uint8_t color, const QuadStrip &qs) {
		int i = 0;
		int j = qs.numVertices - 1;
		int16_t x2 = qs.vertices[i].x;
		int16_t x1 = qs.vertices[j].x;
		int16_t hliney = MIN(qs.vertices[i].y, qs.vertices[j].y);
		++i;
		--j;
		int mode = MODE_COLOR;
		if (color == COL_PAGE) mode = MODE_PAGE;
		else if (color == COL_ALPHA) mode = MODE_ALPHA;
		const uint8_t c = color & 15;
		uint32_t cpt1 = x1 << 16;
		uint32_t cpt2 = x2 << 16;
		int numVertices = qs.numVertices;
		while (1) {
			numVertices -= 2;
			if (numVertices == 0) {
				return;
			}
			uint16_t h;
			uint32_t step1 = calcStep(qs.vertices[j + 1], qs.vertices[j], h);
			uint32_t step2 = calcStep(qs.vertices[i - 1], qs.vertices[i], h);
			++i;
			--j;
			cpt1 = (cpt1 & 0xFFFF0000) | 0x7FFF;
			cpt2 = (cpt2 & 0xFFFF0000) | 0x8000;
			if (h == 0) {
				cpt1 += step1;
				cpt2 += step2;
			} else {
				while (h--) {
					if (hliney >= 0) {
						x1 = cpt1 >> 16;
						x2 = cpt2 >> 16;
						if (x1 < GFX_W && x2 >= 0) {
							if (x1 < 0) x1 = 0;
							if (x2 >= GFX_W) x2 = GFX_W - 1;
							span(x1, x2, hliney, mode, c);
						}
					}
					cpt1 += step1;
					cpt2 += step2;
					++hliney;
					if (hliney >= GFX_H) return;
				}
			}
		}
	}

	virtual void init(int targetW, int targetH) {
		Graphics::init(targetW, targetH);
		if (targetW != GFX_W || targetH != GFX_H) {
			error("Graphics4bpp: only 320x200 (asked %dx%d)", targetW, targetH);
		}
		for (int i = 0; i < 4; ++i) {
			if (!_pages[i]) {
				_pages[i] = (uint8_t *)malloc(PAGE_SIZE);
				if (!_pages[i]) {
					error("Not enough memory for the 4 screen pages");
				}
			}
			memset(_pages[i], 0, PAGE_SIZE);
		}
		_draw = _pages[2];  // GraphicsSoft::setSize -> setWorkPagePtr(2)
	}
	virtual void setFont(const uint8_t *src, int w, int h) {}
	virtual void setPalette(const Color *colors, int count) {
		memcpy(_pal, colors, sizeof(Color) * MIN(count, 16));
	}
	virtual void setSpriteAtlas(const uint8_t *src, int w, int h, int xSize, int ySize) {}

	virtual void drawSprite(int buffer, int num, const Point *pt, uint8_t color) {
		if (_is1991 && num < _shapesMaskCount) {
			_draw = page(buffer);
			const uint8_t *data = _shapesMaskData + _shapesMaskOffset[num];
			const int w = *data++;
			const int x = pt->x - w / 2;
			const int h = *data++;
			const int y = pt->y - h / 2;
			for (int j = 0; j < h; ++j) {
				const int yy = y + j;
				for (int i = 0; i <= w / 16; ++i) {
					const uint16_t mask = READ_BE_UINT16(data); data += 2;
					if (yy < 0 || yy >= GFX_H) {
						continue;
					}
					const int xx = x + i * 16;
					for (int b = 0; b < 16; ++b) {
						if (xx + b < 0 || xx + b >= GFX_W) {
							continue;
						}
						if (mask & (1 << (15 - b))) {
							put(_draw + yy * PAGE_PITCH, xx + b, color & 15);
						}
					}
				}
			}
		}
	}

	virtual void drawBitmap(int buffer, const uint8_t *data, int w, int h, int fmt) {
		if (w != GFX_W || h != GFX_H) {
			warning("Graphics4bpp::drawBitmap() unhandled size %dx%d", w, h);
			return;
		}
		uint8_t *dst = page(buffer);
		switch (fmt) {
		case FMT_CLUT:
			for (int i = 0; i < PAGE_SIZE; ++i) {
				dst[i] = (uint8_t)(((data[2 * i] & 15) << 4) | (data[2 * i + 1] & 15));
			}
			return;
#ifdef RAWGL32X
		case FMT_PLANAR_AMIGA: {
				// video.cpp decode_amiga: 4 planes of 8000 bytes, bit 7 = leftmost pixel
				static const int planeSize = GFX_W * GFX_H / 8;
				for (int k = 0; k < planeSize; ++k) {
					const uint8_t p0 = data[k], p1 = data[k + planeSize];
					const uint8_t p2 = data[k + 2 * planeSize], p3 = data[k + 3 * planeSize];
					for (int b = 0; b < 8; b += 2) {
						const int s0 = 7 - b, s1 = 6 - b;
						const uint8_t c0 = ((p0 >> s0) & 1) | (((p1 >> s0) & 1) << 1) | (((p2 >> s0) & 1) << 2) | (((p3 >> s0) & 1) << 3);
						const uint8_t c1 = ((p0 >> s1) & 1) | (((p1 >> s1) & 1) << 1) | (((p2 >> s1) & 1) << 2) | (((p3 >> s1) & 1) << 3);
						*dst++ = (uint8_t)((c0 << 4) | c1);
					}
				}
			}
			return;
		case FMT_PLANAR_ATARI: {
				// video.cpp decode_atari: per 16 pixels, 4 big-endian plane words
				for (int k = 0; k < GFX_W * GFX_H / 16; ++k) {
					const uint16_t w0 = READ_BE_UINT16(data), w1 = READ_BE_UINT16(data + 2);
					const uint16_t w2 = READ_BE_UINT16(data + 4), w3 = READ_BE_UINT16(data + 6);
					data += 8;
					for (int b = 0; b < 16; b += 2) {
						const int s0 = 15 - b, s1 = 14 - b;
						const uint8_t c0 = ((w0 >> s0) & 1) | (((w1 >> s0) & 1) << 1) | (((w2 >> s0) & 1) << 2) | (((w3 >> s0) & 1) << 3);
						const uint8_t c1 = ((w0 >> s1) & 1) | (((w1 >> s1) & 1) << 1) | (((w2 >> s1) & 1) << 2) | (((w3 >> s1) & 1) << 3);
						*dst++ = (uint8_t)((c0 << 4) | c1);
					}
				}
			}
			return;
#endif
		}
		warning("Graphics4bpp::drawBitmap() unhandled fmt %d", fmt);
	}

	virtual void drawPoint(int buffer, uint8_t color, const Point *pt) {
		_draw = page(buffer);
		if (pt->x < 0 || pt->x >= GFX_W || pt->y < 0 || pt->y >= GFX_H) {
			return;  // GraphicsSoft writes outside the page here; nothing sensible to copy
		}
		int mode = MODE_COLOR;
		if (color == COL_PAGE) mode = MODE_PAGE;
		else if (color == COL_ALPHA) mode = MODE_ALPHA;
		pixel(pt->x, pt->y, mode, color & 15);
	}
	virtual void drawQuadStrip(int buffer, uint8_t color, const QuadStrip *qs) {
		_draw = page(buffer);
		drawPolygon(color, *qs);
	}
	virtual void drawStringChar(int buffer, uint8_t color, char c, const Point *pt) {
		_draw = page(buffer);
		const int x = pt->x, y = pt->y;
		if ((uint16_t)x <= GFX_W - 8 && (uint16_t)y <= GFX_H - 8) {
			const uint8_t *ft = _font + ((uint8_t)c - 0x20) * 8;
			for (int j = 0; j < 8; ++j) {
				const uint8_t ch = ft[j];
				for (int i = 0; i < 8; ++i) {
					if (ch & (1 << (7 - i))) {
						put(_draw + (y + j) * PAGE_PITCH, x + i, color & 15);
					}
				}
			}
		}
	}
	virtual void clearBuffer(int num, uint8_t color) {
		memset(page(num), (color & 15) * 0x11, PAGE_SIZE);
	}
	virtual void copyBuffer(int dst, int src, int vscroll) {
		if (vscroll == 0) {
			memcpy(page(dst), page(src), PAGE_SIZE);
		} else if (vscroll >= -199 && vscroll <= 199) {
			const int dy = vscroll;
			if (dy < 0) {
				memcpy(page(dst), page(src) - dy * PAGE_PITCH, (GFX_H + dy) * PAGE_PITCH);
			} else {
				memcpy(page(dst) + dy * PAGE_PITCH, page(src), (GFX_H - dy) * PAGE_PITCH);
			}
		}
	}
	virtual void drawBuffer(int num, SystemStub *stub) {
#ifdef RAWGL32X_MARS
		static uint16_t cram[256];
		for (int i = 0; i < 16; ++i) {  // 32X CRAM: 0BBBBBGGGGGRRRRR
			cram[i] = (uint16_t)(((_pal[i].b >> 3) << 10) | ((_pal[i].g >> 3) << 5) | (_pal[i].r >> 3));
		}
		cram[16] = 0x7FFF;   // R4: border status text (plat_set_status), white
		plat_present_4bpp(page(num), cram);
#else
		static uint16_t rgb[GFX_W * GFX_H];
		uint16_t pal[16];
		for (int i = 0; i < 16; ++i) pal[i] = _pal[i].rgb555();
		const uint8_t *src = page(num);
		for (int i = 0; i < PAGE_SIZE; ++i) {
			rgb[2 * i] = pal[src[i] >> 4];
			rgb[2 * i + 1] = pal[src[i] & 15];
		}
		int w, h;
		float ar[4];
		stub->prepareScreen(w, h, ar);
		stub->setScreenPixels555(rgb, GFX_W, GFX_H);
		stub->updateScreen();
#endif
	}
	virtual void drawRect(int num, uint8_t color, const Point *pt, int w, int h) {
		_draw = page(num);  // only used by the 3DO version; outline, clipped
		const int x1 = pt->x, y1 = pt->y, x2 = pt->x + w - 1, y2 = pt->y + h - 1;
		for (int x = x1; x <= x2; ++x) {
			if (x < 0 || x >= GFX_W) continue;
			if (y1 >= 0 && y1 < GFX_H) put(_draw + y1 * PAGE_PITCH, x, color & 15);
			if (y2 >= 0 && y2 < GFX_H) put(_draw + y2 * PAGE_PITCH, x, color & 15);
		}
		for (int y = y1; y <= y2; ++y) {
			if (y < 0 || y >= GFX_H) continue;
			if (x1 >= 0 && x1 < GFX_W) put(_draw + y * PAGE_PITCH, x1, color & 15);
			if (x2 >= 0 && x2 < GFX_W) put(_draw + y * PAGE_PITCH, x2, color & 15);
		}
	}
	virtual void drawBitmapOverlay(const uint8_t *data, int w, int h, int fmt, SystemStub *stub) {}
};

Graphics *Graphics4bpp_create() {
	return new Graphics4bpp();
}
