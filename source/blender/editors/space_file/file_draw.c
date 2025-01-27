/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2008 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup spfile
 */

#include <math.h>
#include <string.h>
#include <errno.h>

#include "BLI_blenlib.h"
#include "BLI_fileops_types.h"
#include "BLI_utildefines.h"
#include "BLI_math.h"

#ifdef WIN32
#  include "BLI_winstuff.h"
#endif

#include "BIF_glutil.h"

#include "BKE_context.h"
#include "BKE_main.h"

#include "BLO_readfile.h"

#include "BLT_translation.h"

#include "BLF_api.h"

#include "IMB_imbuf_types.h"

#include "DNA_userdef_types.h"
#include "DNA_windowmanager_types.h"

#include "RNA_access.h"

#include "ED_fileselect.h"
#include "ED_screen.h"

#include "UI_interface.h"
#include "UI_interface_icons.h"
#include "UI_resources.h"
#include "UI_view2d.h"

#include "WM_api.h"
#include "WM_types.h"

#include "GPU_immediate.h"
#include "GPU_immediate_util.h"
#include "GPU_state.h"

#include "filelist.h"

#include "file_intern.h"  // own include

void ED_file_path_button(bScreen *screen,
                         const SpaceFile *sfile,
                         FileSelectParams *params,
                         uiBlock *block)
{
  PointerRNA params_rna_ptr;
  uiBut *but;

  RNA_pointer_create(&screen->id, &RNA_FileSelectParams, params, &params_rna_ptr);

  /* callbacks for operator check functions */
  UI_block_func_set(block, file_draw_check_cb, NULL, NULL);

  but = uiDefButR(block,
                  UI_BTYPE_TEXT,
                  -1,
                  "",
                  0,
                  0,
                  UI_UNIT_X * 10,
                  UI_UNIT_Y,
                  &params_rna_ptr,
                  "directory",
                  0,
                  0.0f,
                  (float)FILE_MAX,
                  0.0f,
                  0.0f,
                  TIP_("File path"));

  BLI_assert(!UI_but_flag_is_set(but, UI_BUT_UNDO));
  BLI_assert(!UI_but_is_utf8(but));

  UI_but_func_complete_set(but, autocomplete_directory, NULL);
  UI_but_funcN_set(but, file_directory_enter_handle, NULL, but);

  /* TODO, directory editing is non-functional while a library is loaded
   * until this is properly supported just disable it. */
  if (sfile && sfile->files && filelist_lib(sfile->files)) {
    UI_but_flag_enable(but, UI_BUT_DISABLED);
  }

  /* clear func */
  UI_block_func_set(block, NULL, NULL, NULL);
}

/* Dummy helper - we need dynamic tooltips here. */
static char *file_draw_tooltip_func(bContext *UNUSED(C), void *argN, const char *UNUSED(tip))
{
  char *dyn_tooltip = argN;
  return BLI_strdup(dyn_tooltip);
}

static void draw_tile(int sx, int sy, int width, int height, int colorid, int shade)
{
  float color[4];
  UI_GetThemeColorShade4fv(colorid, shade, color);
  UI_draw_roundbox_corner_set(UI_CNR_ALL);
  UI_draw_roundbox_aa(
      true, (float)sx, (float)(sy - height), (float)(sx + width), (float)sy, 5.0f, color);
}

static void file_draw_icon(
    uiBlock *block, const char *path, int sx, int sy, int icon, int width, int height, bool drag)
{
  uiBut *but;
  int x, y;
  // float alpha = 1.0f;

  x = sx;
  y = sy - height;

  /*if (icon == ICON_FILE_BLANK) alpha = 0.375f;*/

  but = uiDefIconBut(
      block, UI_BTYPE_LABEL, 0, icon, x, y, width, height, NULL, 0.0f, 0.0f, 0.0f, 0.0f, NULL);
  UI_but_func_tooltip_set(but, file_draw_tooltip_func, BLI_strdup(path));

  if (drag) {
    /* path is no more static, cannot give it directly to but... */
    UI_but_drag_set_path(but, BLI_strdup(path), true);
  }
}

static void file_draw_string(int sx,
                             int sy,
                             const char *string,
                             float width,
                             int height,
                             eFontStyle_Align align,
                             const uchar col[4])
{
  uiStyle *style;
  uiFontStyle fs;
  rcti rect;
  char fname[FILE_MAXFILE];

  if (string[0] == '\0' || width < 1) {
    return;
  }

  style = UI_style_get();
  fs = style->widgetlabel;

  BLI_strncpy(fname, string, FILE_MAXFILE);
  UI_text_clip_middle_ex(&fs, fname, width, UI_DPI_ICON_SIZE, sizeof(fname), '\0');

  /* no text clipping needed, UI_fontstyle_draw does it but is a bit too strict
   * (for buttons it works) */
  rect.xmin = sx;
  rect.xmax = sx + round_fl_to_int(width);
  rect.ymin = sy - height;
  rect.ymax = sy;

  UI_fontstyle_draw(&fs,
                    &rect,
                    fname,
                    col,
                    &(struct uiFontStyleDraw_Params){
                        .align = align,
                    });
}

void file_calc_previews(const bContext *C, ARegion *ar)
{
  SpaceFile *sfile = CTX_wm_space_file(C);
  View2D *v2d = &ar->v2d;

  ED_fileselect_init_layout(sfile, ar);
  UI_view2d_totRect_set(v2d, sfile->layout->width, sfile->layout->height);
}

static void file_draw_preview(uiBlock *block,
                              const char *path,
                              int sx,
                              int sy,
                              const float icon_aspect,
                              ImBuf *imb,
                              const int icon,
                              FileLayout *layout,
                              const bool is_icon,
                              const int typeflags,
                              const bool drag)
{
  uiBut *but;
  float fx, fy;
  float dx, dy;
  int xco, yco;
  float ui_imbx, ui_imby;
  float scaledx, scaledy;
  float scale;
  int ex, ey;
  bool show_outline = !is_icon &&
                      (typeflags & (FILE_TYPE_IMAGE | FILE_TYPE_MOVIE | FILE_TYPE_BLENDER));

  BLI_assert(imb != NULL);

  ui_imbx = imb->x * UI_DPI_FAC;
  ui_imby = imb->y * UI_DPI_FAC;
  /* Unlike thumbnails, icons are not scaled up. */
  if (((ui_imbx > layout->prv_w) || (ui_imby > layout->prv_h)) ||
      (!is_icon && ((ui_imbx < layout->prv_w) || (ui_imby < layout->prv_h)))) {
    if (imb->x > imb->y) {
      scaledx = (float)layout->prv_w;
      scaledy = ((float)imb->y / (float)imb->x) * layout->prv_w;
      scale = scaledx / imb->x;
    }
    else {
      scaledy = (float)layout->prv_h;
      scaledx = ((float)imb->x / (float)imb->y) * layout->prv_h;
      scale = scaledy / imb->y;
    }
  }
  else {
    scaledx = ui_imbx;
    scaledy = ui_imby;
    scale = UI_DPI_FAC;
  }

  ex = (int)scaledx;
  ey = (int)scaledy;
  fx = ((float)layout->prv_w - (float)ex) / 2.0f;
  fy = ((float)layout->prv_h - (float)ey) / 2.0f;
  dx = (fx + 0.5f + layout->prv_border_x);
  dy = (fy + 0.5f - layout->prv_border_y);
  xco = sx + (int)dx;
  yco = sy - layout->prv_h + (int)dy;

  GPU_blend(true);

  /* the large image */

  float col[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  if (is_icon) {
    /*  File and Folder icons draw with lowered opacity until we add themes */
    col[3] = 0.6f;
    /*  Use dark images if background is light */
    float bg[3];
    UI_GetThemeColor3fv(TH_BACK, bg);
    if (rgb_to_grayscale(bg) > 0.5f) {
      col[0] = 0;
      col[1] = 0;
      col[2] = 0;
    }
  }
  else if (typeflags & FILE_TYPE_FTFONT) {
    UI_GetThemeColor4fv(TH_TEXT, col);
  }

  if (!is_icon && typeflags & FILE_TYPE_BLENDERLIB) {
    /* Datablock preview images use premultiplied alpha. */
    GPU_blend_set_func_separate(
        GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
  }

  IMMDrawPixelsTexState state = immDrawPixelsTexSetup(GPU_SHADER_2D_IMAGE_COLOR);
  immDrawPixelsTexScaled(&state,
                         (float)xco,
                         (float)yco,
                         imb->x,
                         imb->y,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         GL_NEAREST,
                         imb->rect,
                         scale,
                         scale,
                         1.0f,
                         1.0f,
                         col);

  GPU_blend_set_func_separate(
      GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);

  if (icon && (icon != ICON_FILE_FONT)) {
    /* size of center icon is scaled to fit container and UI scale */
    float icon_x, icon_y;

    if (is_icon) {
      const float icon_size = 16.0f / icon_aspect * U.dpi_fac;
      float icon_opacity = 0.3f;
      uchar icon_color[4] = {0, 0, 0, 255};
      float bgcolor[4];
      UI_GetThemeColor4fv(TH_TEXT, bgcolor);
      if (rgb_to_grayscale(bgcolor) < 0.5f) {
        icon_color[0] = 255;
        icon_color[1] = 255;
        icon_color[2] = 255;
      }
      icon_x = xco + (ex / 2.0f) - (icon_size / 2.0f);
      icon_y = yco + (ey / 2.0f) - (icon_size * ((typeflags & FILE_TYPE_DIR) ? 0.78f : 0.75f));
      UI_icon_draw_ex(
          icon_x, icon_y, icon, icon_aspect / U.dpi_fac, icon_opacity, 0.0f, icon_color, false);
    }
    else {
      const uchar dark[4] = {0, 0, 0, 255};
      const uchar light[4] = {255, 255, 255, 255};

      /* Smaller, fainter icon for preview image thumbnail. */
      icon_x = xco + (2.0f * UI_DPI_FAC);
      icon_y = yco + (2.0f * UI_DPI_FAC);

      UI_icon_draw_ex(icon_x + 1, icon_y - 1, icon, 1.0f / U.dpi_fac, 0.2f, 0.0f, dark, false);
      UI_icon_draw_ex(icon_x, icon_y, icon, 1.0f / U.dpi_fac, 0.6f, 0.0f, light, false);
    }
  }

  /* Contrasting outline around some preview types. */
  if (show_outline) {
    GPUVertFormat *format = immVertexFormat();
    uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
    float border_color[4] = {1.0f, 1.0f, 1.0f, 0.4f};
    float bgcolor[4];
    UI_GetThemeColor4fv(TH_BACK, bgcolor);
    if (rgb_to_grayscale(bgcolor) > 0.5f) {
      border_color[0] = 0.0f;
      border_color[1] = 0.0f;
      border_color[2] = 0.0f;
    }
    immUniformColor4fv(border_color);
    imm_draw_box_wire_2d(pos, (float)xco, (float)yco, (float)(xco + ex), (float)(yco + ey));
    immUnbindProgram();
  }

  but = uiDefBut(block, UI_BTYPE_LABEL, 0, "", xco, yco, ex, ey, NULL, 0.0, 0.0, 0, 0, NULL);

  /* dragregion */
  if (drag) {
    /* path is no more static, cannot give it directly to but... */
    UI_but_drag_set_image(but, BLI_strdup(path), icon, imb, scale, true);
  }

  GPU_blend(false);
}

static void renamebutton_cb(bContext *C, void *UNUSED(arg1), char *oldname)
{
  Main *bmain = CTX_data_main(C);
  char newname[FILE_MAX + 12];
  char orgname[FILE_MAX + 12];
  char filename[FILE_MAX + 12];
  wmWindowManager *wm = CTX_wm_manager(C);
  SpaceFile *sfile = (SpaceFile *)CTX_wm_space_data(C);
  ScrArea *sa = CTX_wm_area(C);
  ARegion *ar = CTX_wm_region(C);

  const char *blendfile_path = BKE_main_blendfile_path(bmain);
  BLI_make_file_string(blendfile_path, orgname, sfile->params->dir, oldname);
  BLI_strncpy(filename, sfile->params->renamefile, sizeof(filename));
  BLI_filename_make_safe(filename);
  BLI_make_file_string(blendfile_path, newname, sfile->params->dir, filename);

  if (!STREQ(orgname, newname)) {
    if (!BLI_exists(newname)) {
      errno = 0;
      if ((BLI_rename(orgname, newname) != 0) || !BLI_exists(newname)) {
        WM_reportf(RPT_ERROR, "Could not rename: %s", errno ? strerror(errno) : "unknown error");
        WM_report_banner_show();
      }
      else {
        /* If rename is successful, scroll to newly renamed entry. */
        BLI_strncpy(sfile->params->renamefile, filename, sizeof(sfile->params->renamefile));
        sfile->params->rename_flag = FILE_PARAMS_RENAME_POSTSCROLL_PENDING;

        if (sfile->smoothscroll_timer != NULL) {
          WM_event_remove_timer(CTX_wm_manager(C), CTX_wm_window(C), sfile->smoothscroll_timer);
        }
        sfile->smoothscroll_timer = WM_event_add_timer(wm, CTX_wm_window(C), TIMER1, 1.0 / 1000.0);
        sfile->scroll_offset = 0;
      }

      /* to make sure we show what is on disk */
      ED_fileselect_clear(wm, sa, sfile);
    }

    ED_region_tag_redraw(ar);
  }
}

static void draw_background(FileLayout *layout, View2D *v2d)
{
  const int item_height = layout->tile_h + (2 * layout->tile_border_y);
  int i;
  int sy;

  uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
  immUniformThemeColorShade(TH_BACK, -7);

  /* alternating flat shade background */
  for (i = 2; (i <= layout->rows + 1); i += 2) {
    sy = (int)v2d->cur.ymax - layout->offset_top - i * item_height - layout->tile_border_y;

    /* Offsett pattern slightly to add scroll effect. */
    sy += round_fl_to_int(item_height * (v2d->tot.ymax - v2d->cur.ymax) / item_height);

    immRectf(pos,
             v2d->cur.xmin,
             (float)sy,
             v2d->cur.xmax,
             (float)(sy + layout->tile_h + 2 * layout->tile_border_y));
  }

  immUnbindProgram();
}

static void draw_dividers(FileLayout *layout, View2D *v2d)
{
  /* vertical column dividers */

  const int step = (layout->tile_w + 2 * layout->tile_border_x);

  unsigned int vertex_len = 0;
  int sx = (int)v2d->tot.xmin;
  while (sx < v2d->cur.xmax) {
    sx += step;
    vertex_len += 4; /* vertex_count = 2 points per line * 2 lines per divider */
  }

  if (vertex_len > 0) {
    int v1[2], v2[2];
    unsigned char col_hi[3], col_lo[3];

    UI_GetThemeColorShade3ubv(TH_BACK, 30, col_hi);
    UI_GetThemeColorShade3ubv(TH_BACK, -30, col_lo);

    v1[1] = v2d->cur.ymax - layout->tile_border_y;
    v2[1] = v2d->cur.ymin;

    GPUVertFormat *format = immVertexFormat();
    uint pos = GPU_vertformat_attr_add(format, "pos", GPU_COMP_I32, 2, GPU_FETCH_INT_TO_FLOAT);
    uint color = GPU_vertformat_attr_add(
        format, "color", GPU_COMP_U8, 3, GPU_FETCH_INT_TO_FLOAT_UNIT);

    immBindBuiltinProgram(GPU_SHADER_2D_FLAT_COLOR);
    immBegin(GPU_PRIM_LINES, vertex_len);

    sx = (int)v2d->tot.xmin;
    while (sx < v2d->cur.xmax) {
      sx += step;

      v1[0] = v2[0] = sx;
      immAttrSkip(color);
      immVertex2iv(pos, v1);
      immAttr3ubv(color, col_lo);
      immVertex2iv(pos, v2);

      v1[0] = v2[0] = sx + 1;
      immAttrSkip(color);
      immVertex2iv(pos, v1);
      immAttr3ubv(color, col_hi);
      immVertex2iv(pos, v2);
    }

    immEnd();
    immUnbindProgram();
  }
}

static void draw_columnheader_background(const FileLayout *layout, const View2D *v2d)
{
  uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

  immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
  immUniformThemeColorShade(TH_BACK, 11);

  immRectf(pos,
           v2d->cur.xmin,
           v2d->cur.ymax - layout->attribute_column_header_h,
           v2d->cur.xmax,
           v2d->cur.ymax);

  immUnbindProgram();
}

static void draw_columnheader_columns(const FileSelectParams *params,
                                      FileLayout *layout,
                                      const View2D *v2d,
                                      const uchar text_col[4])
{
  const float divider_pad = 0.2 * layout->attribute_column_header_h;
  int sx = v2d->cur.xmin, sy = v2d->cur.ymax;

  for (FileAttributeColumnType column_type = 0; column_type < ATTRIBUTE_COLUMN_MAX;
       column_type++) {
    if (!file_attribute_column_type_enabled(params, column_type)) {
      continue;
    }
    const FileAttributeColumn *column = &layout->attribute_columns[column_type];

    /* Active sort type triangle */
    if (params->sort == column->sort_type) {
      float tri_color[4];

      rgba_uchar_to_float(tri_color, text_col);
      UI_draw_icon_tri(sx + column->width - (0.3f * U.widget_unit) -
                           ATTRIBUTE_COLUMN_PADDING / 2.0f,
                       sy + (0.1f * U.widget_unit) - (layout->attribute_column_header_h / 2),
                       (params->flag & FILE_SORT_INVERT) ? 't' : 'v',
                       tri_color);
    }

    file_draw_string(sx + ATTRIBUTE_COLUMN_PADDING,
                     sy - layout->tile_border_y,
                     IFACE_(column->name),
                     column->width - 2 * ATTRIBUTE_COLUMN_PADDING,
                     layout->attribute_column_header_h - layout->tile_border_y,
                     UI_STYLE_TEXT_LEFT,
                     text_col);

    /* Separator line */
    if (column_type != COLUMN_NAME) {
      uint pos = GPU_vertformat_attr_add(
          immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);

      immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
      immUniformThemeColorShade(TH_BACK, -10);
      immBegin(GPU_PRIM_LINES, 2);
      immVertex2f(pos, sx - 1, sy - divider_pad);
      immVertex2f(pos, sx - 1, sy - layout->attribute_column_header_h + divider_pad);
      immEnd();
      immUnbindProgram();
    }

    sx += column->width;
  }

  /* Vertical separator lines line */
  {
    uint pos = GPU_vertformat_attr_add(immVertexFormat(), "pos", GPU_COMP_F32, 2, GPU_FETCH_FLOAT);
    immBindBuiltinProgram(GPU_SHADER_2D_UNIFORM_COLOR);
    immUniformThemeColorShade(TH_BACK, -10);
    immBegin(GPU_PRIM_LINES, 4);
    immVertex2f(pos, v2d->cur.xmin, sy);
    immVertex2f(pos, v2d->cur.xmax, sy);
    immVertex2f(pos, v2d->cur.xmin, sy - layout->attribute_column_header_h);
    immVertex2f(pos, v2d->cur.xmax, sy - layout->attribute_column_header_h);
    immEnd();
    immUnbindProgram();
  }
}

/**
 * Updates the stat string stored in file->entry if necessary.
 */
static const char *filelist_get_details_column_string(FileAttributeColumnType column,
                                                      const FileDirEntry *file,
                                                      const bool small_size,
                                                      const bool update_stat_strings)
{
  switch (column) {
    case COLUMN_DATETIME:
      if (!(file->typeflag & FILE_TYPE_BLENDERLIB) && !FILENAME_IS_CURRPAR(file->relpath)) {
        if ((file->entry->datetime_str[0] == '\0') || update_stat_strings) {
          char date[FILELIST_DIRENTRY_DATE_LEN], time[FILELIST_DIRENTRY_TIME_LEN];
          bool is_today, is_yesterday;

          BLI_filelist_entry_datetime_to_string(
              NULL, file->entry->time, small_size, time, date, &is_today, &is_yesterday);

          if (is_today || is_yesterday) {
            BLI_strncpy(date, is_today ? N_("Today") : N_("Yesterday"), sizeof(date));
          }
          BLI_snprintf(
              file->entry->datetime_str, sizeof(file->entry->datetime_str), "%s %s", date, time);
        }

        return file->entry->datetime_str;
      }
      break;
    case COLUMN_SIZE:
      if ((file->typeflag & (FILE_TYPE_BLENDER | FILE_TYPE_BLENDER_BACKUP)) ||
          !(file->typeflag & (FILE_TYPE_DIR | FILE_TYPE_BLENDERLIB))) {
        if ((file->entry->size_str[0] == '\0') || update_stat_strings) {
          BLI_filelist_entry_size_to_string(
              NULL, file->entry->size, small_size, file->entry->size_str);
        }

        return file->entry->size_str;
      }
      break;
    default:
      break;
  }

  return NULL;
}

static void draw_details_columns(const FileSelectParams *params,
                                 const FileLayout *layout,
                                 const FileDirEntry *file,
                                 const int pos_x,
                                 const int pos_y,
                                 const uchar text_col[4])
{
  const bool small_size = SMALL_SIZE_CHECK(params->thumbnail_size);
  const bool update_stat_strings = small_size != SMALL_SIZE_CHECK(layout->curr_size);
  int sx = pos_x - layout->tile_border_x - (UI_UNIT_X * 0.1f), sy = pos_y;

  for (FileAttributeColumnType column_type = 0; column_type < ATTRIBUTE_COLUMN_MAX;
       column_type++) {
    const FileAttributeColumn *column = &layout->attribute_columns[column_type];

    /* Name column is not a detail column (should already be drawn), always skip here. */
    if (column_type == COLUMN_NAME) {
      sx += column->width;
      continue;
    }
    if (!file_attribute_column_type_enabled(params, column_type)) {
      continue;
    }

    const char *str = filelist_get_details_column_string(
        column_type, file, small_size, update_stat_strings);

    if (str) {
      file_draw_string(sx + ATTRIBUTE_COLUMN_PADDING,
                       sy - layout->tile_border_y,
                       IFACE_(str),
                       column->width - 2 * ATTRIBUTE_COLUMN_PADDING,
                       layout->tile_h,
                       column->text_align,
                       text_col);
    }

    sx += column->width;
  }
}

void file_draw_list(const bContext *C, ARegion *ar)
{
  SpaceFile *sfile = CTX_wm_space_file(C);
  FileSelectParams *params = ED_fileselect_get_params(sfile);
  FileLayout *layout = ED_fileselect_get_layout(sfile, ar);
  View2D *v2d = &ar->v2d;
  struct FileList *files = sfile->files;
  struct FileDirEntry *file;
  const char *root = filelist_dir(files);
  ImBuf *imb;
  uiBlock *block = UI_block_begin(C, ar, __func__, UI_EMBOSS);
  int numfiles;
  int numfiles_layout;
  int sx, sy;
  int offset;
  int textwidth, textheight;
  int i;
  bool is_icon;
  eFontStyle_Align align;
  bool do_drag;
  unsigned char text_col[4];
  const bool draw_columnheader = (params->display == FILE_VERTICALDISPLAY);
  const float thumb_icon_aspect = MIN2(64.0f / (float)(params->thumbnail_size), 1.0f);

  numfiles = filelist_files_ensure(files);

  if (params->display != FILE_IMGDISPLAY) {
    draw_background(layout, v2d);
    draw_dividers(layout, v2d);
  }

  offset = ED_fileselect_layout_offset(layout, (int)ar->v2d.cur.xmin, (int)-ar->v2d.cur.ymax);
  if (offset < 0) {
    offset = 0;
  }

  numfiles_layout = ED_fileselect_layout_numfiles(layout, ar);

  /* adjust, so the next row is already drawn when scrolling */
  if (layout->flag & FILE_LAYOUT_HOR) {
    numfiles_layout += layout->rows;
  }
  else {
    numfiles_layout += layout->flow_columns;
  }

  filelist_file_cache_slidingwindow_set(files, numfiles_layout);

  textwidth = (FILE_IMGDISPLAY == params->display) ?
                  layout->tile_w :
                  round_fl_to_int(layout->attribute_columns[COLUMN_NAME].width);
  textheight = (int)(layout->textheight * 3.0 / 2.0 + 0.5);

  align = (FILE_IMGDISPLAY == params->display) ? UI_STYLE_TEXT_CENTER : UI_STYLE_TEXT_LEFT;

  if (numfiles > 0) {
    const bool success = filelist_file_cache_block(
        files, min_ii(offset + (numfiles_layout / 2), numfiles - 1));
    BLI_assert(success);
    UNUSED_VARS_NDEBUG(success);

    filelist_cache_previews_update(files);

    /* Handle preview timer here,
     * since it's filelist_file_cache_block() and filelist_cache_previews_update()
     * which controls previews task. */
    {
      const bool previews_running = filelist_cache_previews_running(files);
      //          printf("%s: preview task: %d\n", __func__, previews_running);
      if (previews_running && !sfile->previews_timer) {
        sfile->previews_timer = WM_event_add_timer_notifier(
            CTX_wm_manager(C), CTX_wm_window(C), NC_SPACE | ND_SPACE_FILE_PREVIEW, 0.01);
      }
      if (!previews_running && sfile->previews_timer) {
        /* Preview is not running, no need to keep generating update events! */
        //              printf("%s: Inactive preview task, sleeping!\n", __func__);
        WM_event_remove_timer_notifier(CTX_wm_manager(C), CTX_wm_window(C), sfile->previews_timer);
        sfile->previews_timer = NULL;
      }
    }
  }

  BLF_batch_draw_begin();

  UI_GetThemeColor4ubv(TH_TEXT, text_col);

  for (i = offset; (i < numfiles) && (i < offset + numfiles_layout); i++) {
    unsigned int file_selflag;
    char path[FILE_MAX_LIBEXTRA];
    int padx = 0.1f * UI_UNIT_X;
    int icon_ofs = 0;

    ED_fileselect_layout_tilepos(layout, i, &sx, &sy);
    sx += (int)(v2d->tot.xmin + padx);
    sy = (int)(v2d->tot.ymax - sy);

    file = filelist_file(files, i);
    file_selflag = filelist_entry_select_get(sfile->files, file, CHECK_ALL);

    BLI_join_dirfile(path, sizeof(path), root, file->relpath);

    if (!(file_selflag & FILE_SEL_EDITING)) {
      if ((params->highlight_file == i) || (file_selflag & FILE_SEL_HIGHLIGHTED) ||
          (file_selflag & FILE_SEL_SELECTED)) {
        int colorid = (file_selflag & FILE_SEL_SELECTED) ? TH_HILITE : TH_BACK;
        int shade = (params->highlight_file == i) || (file_selflag & FILE_SEL_HIGHLIGHTED) ? 35 :
                                                                                             0;
        const short width = ELEM(params->display, FILE_VERTICALDISPLAY, FILE_HORIZONTALDISPLAY) ?
                                layout->tile_w - (2 * padx) :
                                layout->tile_w;

        BLI_assert(i == 0 || !FILENAME_IS_CURRPAR(file->relpath));

        draw_tile(
            sx, sy - 1, width, sfile->layout->tile_h + layout->tile_border_y, colorid, shade);
      }
    }
    UI_draw_roundbox_corner_set(UI_CNR_NONE);

    /* don't drag parent or refresh items */
    do_drag = !(FILENAME_IS_CURRPAR(file->relpath));

    if (FILE_IMGDISPLAY == params->display) {
      const int icon = filelist_geticon(files, i, false);
      is_icon = 0;
      imb = filelist_getimage(files, i);
      if (!imb) {
        imb = filelist_geticon_image(files, i);
        is_icon = 1;
      }

      file_draw_preview(block,
                        path,
                        sx,
                        sy,
                        thumb_icon_aspect,
                        imb,
                        icon,
                        layout,
                        is_icon,
                        file->typeflag,
                        do_drag);
    }
    else {
      file_draw_icon(block,
                     path,
                     sx,
                     sy - layout->tile_border_y,
                     filelist_geticon(files, i, true),
                     ICON_DEFAULT_WIDTH_SCALE,
                     ICON_DEFAULT_HEIGHT_SCALE,
                     do_drag);
      icon_ofs += ICON_DEFAULT_WIDTH_SCALE + 0.2f * UI_UNIT_X;
    }

    if (file_selflag & FILE_SEL_EDITING) {
      uiBut *but;
      const short width = (params->display == FILE_IMGDISPLAY) ?
                              textwidth :
                              layout->attribute_columns[COLUMN_NAME].width -
                                  ATTRIBUTE_COLUMN_PADDING;

      but = uiDefBut(block,
                     UI_BTYPE_TEXT,
                     1,
                     "",
                     sx + icon_ofs,
                     sy - layout->tile_h - 0.15f * UI_UNIT_X,
                     width - icon_ofs,
                     textheight,
                     sfile->params->renamefile,
                     1.0f,
                     (float)sizeof(sfile->params->renamefile),
                     0,
                     0,
                     "");
      UI_but_func_rename_set(but, renamebutton_cb, file);
      UI_but_flag_enable(but, UI_BUT_NO_UTF8); /* allow non utf8 names */
      UI_but_flag_disable(but, UI_BUT_UNDO);
      if (false == UI_but_active_only(C, ar, block, but)) {
        file_selflag = filelist_entry_select_set(
            sfile->files, file, FILE_SEL_REMOVE, FILE_SEL_EDITING, CHECK_ALL);
      }
    }
    else {
      const int txpos = (params->display == FILE_IMGDISPLAY) ? sx : sx + 1 + icon_ofs;
      const int typos = (params->display == FILE_IMGDISPLAY) ?
                            sy - layout->tile_h + layout->textheight :
                            sy - layout->tile_border_y;
      const int twidth = (params->display == FILE_IMGDISPLAY) ?
                             textwidth :
                             textwidth - 1 - icon_ofs - padx - layout->tile_border_x;
      file_draw_string(txpos, typos, file->name, (float)twidth, textheight, align, text_col);
    }

    if (params->display != FILE_IMGDISPLAY) {
      draw_details_columns(params, layout, file, sx, sy, text_col);
    }
  }

  BLF_batch_draw_end();

  UI_block_end(C, block);
  UI_block_draw(C, block);

  /* Draw last, on top of file list. */
  if (draw_columnheader) {
    draw_columnheader_background(layout, v2d);
    draw_columnheader_columns(params, layout, v2d, text_col);
  }

  layout->curr_size = params->thumbnail_size;
}
