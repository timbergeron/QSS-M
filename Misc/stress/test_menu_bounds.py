"""Exercise production menu bounds and pointer hit-testing without a window."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MENU = (ROOT / "Quake/menu.c").read_text()
DRAW = (ROOT / "Quake/gl_draw.c").read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index("\n}", start) + len("\n}")
    return source[start:end] + "\n"


layout_macros = re.findall(
    r"^#define (?:M_LIST_COLS\b|M_EXTRA_ROWS\b|M_BOTTOM\(|MS_[XY]\(|"
    r"MENU_SEARCH_(?:QUERY_CHARS|FIELD_[A-Z]+)\b|TALL_OPTIONS_SEARCH_Y\b|"
    r"AUDIO_(?:LIST_[A-Z]+|PANEL_[XW]|SCROLL_X)\b|AUDIO_Y\(|"
    r"MODMENU_MAP_(?:FIRST_Y|VIEW_ROWS|SEARCH_BOX_Y)\b).*$",
    MENU,
    re.MULTILINE,
)
types_start = MENU.index("typedef struct\n", MENU.index("/* Listbox */"))
types_end = MENU.index("void M_List_CheckIntegrity", types_start)
source = r'''
#include "quakedef.h"
#include <assert.h>

int glwidth, glheight, m_mousex, m_mousey;
cvar_t scr_menuscale;
static int m_left, m_top, m_width, m_height;
'''
source += "\n".join(layout_macros) + "\n" + MENU[types_start:types_end]
source += function(DRAW, "void Draw_GetMenuCanvas (menucanvas_t *mc)")
for signature in (
    "static void M_UpdateBounds (void)",
    "void M_List_AutoScroll(",
    "static void M_List_SetViewsize (",
    "qboolean M_List_GetScrollbar(",
    "qboolean M_List_UseScrollbar(",
    "static qboolean M_List_MouseInScrollbar(",
    "static qboolean M_List_MouseInColumns(",
    "static int M_List_RowAt(",
    "static qboolean M_List_SelectClickedRow(",
    "static qboolean M_TextField_MouseInRow(",
    "static qboolean M_MenuSearch_MouseInField(",
    "static int M_TallOptions_VisibleRows(",
    "static int M_TallOptions_Scroll(",
    "static int M_TallOptions_MouseItem(",
):
    source += function(MENU, signature)

source += r'''
static qboolean selectable(int row) { return row != 3; }

static void set_bounds(int width, int height, float scale)
{
    glwidth = width;
    glheight = height;
    scr_menuscale.value = scale;
    M_UpdateBounds();
}

int main(void)
{
    menulist_t list = { .cursor = 20, .numitems = 25, .viewsize = 24,
        .isactive_fn = selectable };
    set_bounds(320, 200, 1);
    assert(m_left == 0 && m_top == 0 && m_width == 320 && m_height == 200);
    assert(AUDIO_LIST_ROWS == 16 && AUDIO_LIST_COLS == 18 && AUDIO_PANEL_W == 128);
    assert(MODMENU_MAP_VIEW_ROWS == 13);

    /* Shrinking keeps the selected row visible. Growing clamps back to the
       first page. Both operations preserve the selected item. */
    M_List_SetViewsize(&list, 16);
    assert(list.cursor == 20 && list.scroll == 5);
    M_List_SetViewsize(&list, 30);
    assert(list.cursor == 20 && list.scroll == 0);

    set_bounds(1920, 1080, 1.5f);
    int cols = M_LIST_COLS;
    list.viewsize = 16;
    assert(cols > 36 && m_height > 200);
    assert(AUDIO_LIST_ROWS > 16 && AUDIO_LIST_COLS > 18 && AUDIO_PANEL_W > 128);
    assert(AUDIO_SCROLL_X < AUDIO_PANEL_X);
    assert(MODMENU_MAP_VIEW_ROWS > 13);
    assert(MODMENU_MAP_FIRST_Y + MODMENU_MAP_VIEW_ROWS * 8 < MODMENU_MAP_SEARCH_BOX_Y);
    assert(M_List_MouseInScrollbar(&list, (cols - 1) * 8, 0, cols));
    assert(!M_List_MouseInScrollbar(&list, cols * 8, 0, cols));
    assert(!M_List_MouseInScrollbar(&list, (cols - 1) * 8, -1, cols));
    assert(!M_List_MouseInScrollbar(&list, (cols - 1) * 8, list.viewsize * 8, cols));
    assert(M_List_UseScrollbar(&list, 1000) && list.scroll == 9);
    assert(M_List_UseScrollbar(&list, -1000) && list.scroll == 0);

    assert(!M_List_SelectClickedRow(&list, -9, 0, cols));
    assert(!M_List_SelectClickedRow(&list, cols * 8, 0, cols));
    assert(!M_List_SelectClickedRow(&list, 0, -1, cols));
    assert(!M_List_SelectClickedRow(&list, 0, 3 * 8, cols));
    assert(list.cursor == 20);
    assert(M_List_SelectClickedRow(&list, 0, 4 * 8, cols) && list.cursor == 4);
    list.numitems = 5;
    assert(!M_List_SelectClickedRow(&list, 0, 5 * 8, cols));
    assert(!M_List_MouseInScrollbar(&list, (cols - 1) * 8, 0, cols));

    m_mousex = MENU_SEARCH_FIELD_X + 8;
    m_mousey = MENU_SEARCH_FIELD_Y;
    assert(M_MenuSearch_MouseInField());
    m_mousex = MENU_SEARCH_FIELD_X + (MENU_SEARCH_FIELD_COLS + 2) * 8;
    assert(!M_MenuSearch_MouseInField());
    assert(M_TallOptions_VisibleRows(12, 23, true) == 23);

    set_bounds(320, 200, 1);
    int rows = M_TallOptions_VisibleRows(12, 23, true);
    int scroll = M_TallOptions_Scroll(0, 22, 23, rows);
    assert(rows < 23 && scroll + rows == 23);
    assert(M_TallOptions_MouseItem(12 + (rows - 1) * 8, 12, scroll, 23, true) == 22);
    assert(M_TallOptions_MouseItem(TALL_OPTIONS_SEARCH_Y, 12, scroll, 23, true) == -1);
    assert(M_TallOptions_MouseItem(192, 12, scroll, 23, false) == 22);
    puts("PASS: menu resize, list clicks and scrollbar bounds, search geometry");
}
'''

with tempfile.TemporaryDirectory(prefix="qssm-menu-bounds-") as tmp:
    test = Path(tmp) / "test.c"
    exe = test.with_suffix("")
    test.write_text(source)
    flags = subprocess.check_output(["pkg-config", "--cflags", "sdl3"], text=True)
    subprocess.run(
        shlex.split(os.environ.get("CC", "cc")) + shlex.split(flags) +
        ["-std=gnu11", "-I", str(ROOT / "Quake"), str(test), "-lm", "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
