/*
 * \brief  Terminal graphics backend for textual screen
 * \author Norman Feske
 * \date   2018-02-06
 */

/*
 * Copyright (C) 2011-2018 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _TEXT_SCREEN_SURFACE_H_
#define _TEXT_SCREEN_SURFACE_H_

/* Genode includes */
#include <os/pixel_rgb565.h>

/* terminal includes */
#include <terminal/char_cell_array_character_screen.h>

/* nitpicker graphic back end */
#include <nitpicker_gfx/text_painter.h>
#include <nitpicker_gfx/box_painter.h>

/* local includes */
#include "color_palette.h"
#include "framebuffer.h"

namespace Terminal { template <typename> class Text_screen_surface; }


template <typename PT>
class Terminal::Text_screen_surface
{
	public:

		class Invalid_framebuffer_size : Genode::Exception { };

	private:

		typedef Text_painter::Font             Font;
		typedef Glyph_painter::Fixpoint_number Fixpoint_number;

		Font          const &_font;
		Color_palette const &_palette;
		Framebuffer         &_framebuffer;

		/* take size of space character as character cell size */
		Fixpoint_number const _char_width = _font.string_width(Utf8_ptr("M"));

		unsigned const _char_height { _font.height() };

		/* number of characters fitting on the framebuffer */
		unsigned const _columns { (_framebuffer.w() << 8)/_char_width.value };
		unsigned const _lines   { _framebuffer.h()/_char_height };

		void _check_size() const {
			if (_columns*_lines == 0)
				throw Invalid_framebuffer_size(); }

		bool const _checked_size = (_check_size(), true);

		Cell_array<Char_cell>            _cell_array;
		Char_cell_array_character_screen _character_screen { _cell_array };

		Decoder _decoder { _character_screen };

	public:

		/**
		 * Constructor
		 *
		 * \throw Invalid_framebuffer_size
		 */
		Text_screen_surface(Allocator &alloc, Font const &font,
		                    Color_palette &palette, Framebuffer &framebuffer)
		:
			_font(font),
			_palette(palette),
			_framebuffer(framebuffer),
			_cell_array(_columns, _lines, alloc)
		{ }

		void redraw()
		{
			Area const fb_size(_framebuffer.w(), _framebuffer.h());

			PT *fb_base = _framebuffer.pixel<PT>();

			Surface<PT> surface(fb_base, fb_size);

			unsigned const fg_alpha = 255;

			/* area covered by the character grid */
			Area const used((_columns*_char_width.value)>>8, _lines*_char_height);

			/* excess area */
			Area const unused(fb_size.w() - used.w(), fb_size.h() - used.h());

			/*
			 * Start position of character grid
			 *
			 * Leave PAD_LEFT/BOTTOM pixel border at the left/bottom, align to
			 * left/bottom.
			 */
			enum { PAD_LEFT = 1, PAD_BOTTOM = 1 };
			Point const start(unused.w() >= PAD_LEFT ? PAD_LEFT : 0,
			                  unused.h() >= PAD_BOTTOM ? unused.h() - PAD_BOTTOM : unused.h());

			/* clear border */
			{
				Rect r[4] { };
				Rect(Point(0, 0), fb_size).cut(Rect(start, used), &r[0], &r[1], &r[2], &r[3]);
				for (unsigned i = 0; i < 4; i++)
					Box_painter::paint(surface, r[i], Color(0, 0, 0));
			}

			int const clip_top  = 0, clip_bottom = start.y() + fb_size.h(),
			          clip_left = 0, clip_right  = start.x() + fb_size.w();

			unsigned y = start.y();
			for (unsigned line = 0; line < _cell_array.num_lines(); line++) {

				if (_cell_array.line_dirty(line)) {

					Fixpoint_number x { (int)start.x() };
					for (unsigned column = 0; column < _cell_array.num_cols(); column++) {

						Char_cell     cell  = _cell_array.get_cell(column, line);
						unsigned char ascii = cell.ascii;

						if (ascii == 0)
							ascii = ' ';

						Text_painter::Codepoint const c { ascii };

						_font.apply_glyph(c, [&] (Glyph_painter::Glyph const &glyph) {

							Color_palette::Highlighted const highlighted { cell.highlight() };
							Color_palette::Inverse     const inverse     { cell.inverse() };

							Color fg_color =
								_palette.foreground(Color_palette::Index{cell.colidx_fg()},
								                    highlighted, inverse);

							Color bg_color =
								_palette.background(Color_palette::Index{cell.colidx_bg()},
								                    highlighted, inverse);

							if (cell.has_cursor()) {
								fg_color = Color( 63,  63,  63);
								bg_color = Color(255, 255, 255);
							}

							PT const pixel(fg_color.r, fg_color.g, fg_color.b);

							Fixpoint_number next_x = x;
							next_x.value += _char_width.value;

							Box_painter::paint(surface,
							                   Rect(Point(x.decimal(), y),
							                        Point(next_x.decimal() - 1,
							                              y + _char_height - 1)), bg_color);

							/* horizontally align glyph within cell */
							x.value += (_char_width.value - (int)((glyph.width - 1)<<8)) >> 1;

							Glyph_painter::paint(Glyph_painter::Position(x, (int)y + 1),
							                     glyph, fb_base, fb_size.w(),
							                     clip_top, clip_bottom, clip_left, clip_right,
							                     pixel, fg_alpha);
							x = next_x;
						});
					}
				}
				y += _char_height;
			}

			int first_dirty_line =  10000,
			    last_dirty_line  = -10000;

			for (int line = 0; line < (int)_cell_array.num_lines(); line++) {
				if (!_cell_array.line_dirty(line)) continue;

				first_dirty_line = min(line, first_dirty_line);
				last_dirty_line  = max(line, last_dirty_line);

				_cell_array.mark_line_as_clean(line);
			}

			int const num_dirty_lines = last_dirty_line - first_dirty_line + 1;
			if (num_dirty_lines > 0)
				_framebuffer.refresh(Rect(Point(0, first_dirty_line*_char_height),
				                          Area(fb_size.w(),
				                               num_dirty_lines*_char_height + unused.h())));
		}

		void apply_character(Character c)
		{
			/* submit character to sequence decoder */
			_decoder.insert(c.c);
		}

		Session::Size size() const { return Session::Size(_columns, _lines); }
};

#endif /* _TEXT_SCREEN_SURFACE_H_ */
