"""Check Nintendo face button normalization for SDL3 gamepads.

Run with python3 Misc/stress/test_gamepad_labels.py (requires cc and SDL3 via pkg-config).
Compiles in_sdl.c's IN_UseNintendoButtonLabels and IN_FaceButtonKey with test doubles,
then checks the Switch layout fallback against SDL's own label table.
"""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "Quake/in_sdl.c").read_text()
start = source.index("static qboolean IN_UseNintendoButtonLabels(void)")
end = source.index("\n/*\n================\nIN_KeyForControllerButton", start)
helpers = source[start:end]

DOUBLES = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef int qboolean;
#define true 1
#define false 0

typedef enum
{
    SDL_GAMEPAD_BUTTON_INVALID = -1,
    SDL_GAMEPAD_BUTTON_SOUTH,
    SDL_GAMEPAD_BUTTON_EAST,
    SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK
} SDL_GamepadButton;

typedef enum
{
    SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN,
    SDL_GAMEPAD_BUTTON_LABEL_A,
    SDL_GAMEPAD_BUTTON_LABEL_B,
    SDL_GAMEPAD_BUTTON_LABEL_X,
    SDL_GAMEPAD_BUTTON_LABEL_Y,
    SDL_GAMEPAD_BUTTON_LABEL_CROSS,
    SDL_GAMEPAD_BUTTON_LABEL_CIRCLE,
    SDL_GAMEPAD_BUTTON_LABEL_SQUARE,
    SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE
} SDL_GamepadButtonLabel;

typedef struct SDL_Gamepad SDL_Gamepad;
typedef enum { GAMEPAD_NONE, GAMEPAD_XBOX, GAMEPAD_PLAYSTATION, GAMEPAD_NINTENDO } gamepadtype_t;
enum { K_ABUTTON = 253, K_BBUTTON, K_XBUTTON, K_YBUTTON };

static gamepadtype_t joy_active_type;
static SDL_Gamepad *joy_active_controller;
static SDL_GamepadButtonLabel labels[4];
static const char *environment_value;
static int failures;

static SDL_GamepadButtonLabel SDL_GetGamepadButtonLabel(SDL_Gamepad *gamepad, SDL_GamepadButton button)
{
    if (!gamepad)
    {
        printf("FAIL: label queried without a gamepad\n");
        failures++;
    }
    return button >= SDL_GAMEPAD_BUTTON_SOUTH && button <= SDL_GAMEPAD_BUTTON_NORTH
        ? labels[button] : SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
}

static const char *SDL_getenv(const char *name)
{
    if (strcmp(name, "SDL_GAMECONTROLLER_USE_BUTTON_LABELS"))
    {
        printf("FAIL: unexpected environment read %s\n", name);
        failures++;
    }
    return environment_value;
}

#define SDL_strcasecmp strcasecmp
'''

CHECKS = r'''
static void set_labels(SDL_GamepadButtonLabel south, SDL_GamepadButtonLabel east,
    SDL_GamepadButtonLabel west, SDL_GamepadButtonLabel north)
{
    labels[SDL_GAMEPAD_BUTTON_SOUTH] = south;
    labels[SDL_GAMEPAD_BUTTON_EAST] = east;
    labels[SDL_GAMEPAD_BUTTON_WEST] = west;
    labels[SDL_GAMEPAD_BUTTON_NORTH] = north;
}

static void expect_keys(int line, int south, int east, int west, int north)
{
    const int got[4] = {
        IN_FaceButtonKey(SDL_GAMEPAD_BUTTON_SOUTH), IN_FaceButtonKey(SDL_GAMEPAD_BUTTON_EAST),
        IN_FaceButtonKey(SDL_GAMEPAD_BUTTON_WEST), IN_FaceButtonKey(SDL_GAMEPAD_BUTTON_NORTH)
    };
    const int want[4] = { south, east, west, north };

    if (memcmp(got, want, sizeof(got)))
    {
        printf("FAIL line %d: got S=%d E=%d W=%d N=%d, want S=%d E=%d W=%d N=%d\n",
            line, got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
        failures++;
    }
}
#define EXPECT_KEYS(s, e, w, n) expect_keys(__LINE__, s, e, w, n)
#define POSITIONAL() EXPECT_KEYS(K_ABUTTON, K_BBUTTON, K_XBUTTON, K_YBUTTON)
#define SWITCH_LAYOUT() EXPECT_KEYS(K_BBUTTON, K_ABUTTON, K_YBUTTON, K_XBUTTON)

int main(void)
{
    static const char *opt_out[] = { "0", "false", "FALSE", "0x1" };
    static const char *opt_in[] = { "", "1", "true", "yes" };
    size_t i;

    joy_active_controller = (SDL_Gamepad *)&failures;

    /* Xbox and PlayStation pads stay positional whatever their labels say. */
    joy_active_type = GAMEPAD_XBOX;
    set_labels(SDL_GAMEPAD_BUTTON_LABEL_B, SDL_GAMEPAD_BUTTON_LABEL_A, SDL_GAMEPAD_BUTTON_LABEL_Y, SDL_GAMEPAD_BUTTON_LABEL_X);
    POSITIONAL();
    joy_active_type = GAMEPAD_PLAYSTATION;
    set_labels(SDL_GAMEPAD_BUTTON_LABEL_CROSS, SDL_GAMEPAD_BUTTON_LABEL_CIRCLE, SDL_GAMEPAD_BUTTON_LABEL_SQUARE, SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE);
    POSITIONAL();

    /* Switch Pro and Joy-Con labels: A is east, B south, X north, Y west. */
    joy_active_type = GAMEPAD_NINTENDO;
    set_labels(SDL_GAMEPAD_BUTTON_LABEL_B, SDL_GAMEPAD_BUTTON_LABEL_A, SDL_GAMEPAD_BUTTON_LABEL_Y, SDL_GAMEPAD_BUTTON_LABEL_X);
    SWITCH_LAYOUT();

    /* Unknown or non-letter labels on a Nintendo pad use the Switch layout. */
    set_labels(SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN, SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN, SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN, SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN);
    SWITCH_LAYOUT();
    set_labels(SDL_GAMEPAD_BUTTON_LABEL_CROSS, SDL_GAMEPAD_BUTTON_LABEL_CIRCLE, SDL_GAMEPAD_BUTTON_LABEL_SQUARE, SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE);
    SWITCH_LAYOUT();

    /* A remapped Nintendo pad follows whatever labels SDL reports. */
    set_labels(SDL_GAMEPAD_BUTTON_LABEL_A, SDL_GAMEPAD_BUTTON_LABEL_B, SDL_GAMEPAD_BUTTON_LABEL_X, SDL_GAMEPAD_BUTTON_LABEL_Y);
    POSITIONAL();
    set_labels(SDL_GAMEPAD_BUTTON_LABEL_A, SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN, SDL_GAMEPAD_BUTTON_LABEL_Y, SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN);
    EXPECT_KEYS(K_ABUTTON, K_ABUTTON, K_YBUTTON, K_XBUTTON);

    /* SDL_GAMECONTROLLER_USE_BUTTON_LABELS=0 keeps Nintendo pads positional. */
    set_labels(SDL_GAMEPAD_BUTTON_LABEL_B, SDL_GAMEPAD_BUTTON_LABEL_A, SDL_GAMEPAD_BUTTON_LABEL_Y, SDL_GAMEPAD_BUTTON_LABEL_X);
    for (i = 0; i < sizeof(opt_out) / sizeof(opt_out[0]); i++)
    {
        environment_value = opt_out[i];
        POSITIONAL();
    }
    for (i = 0; i < sizeof(opt_in) / sizeof(opt_in[0]); i++)
    {
        environment_value = opt_in[i];
        SWITCH_LAYOUT();
    }
    environment_value = NULL;

    /* Without an active pad there are no labels to read. */
    joy_active_controller = NULL;
    POSITIONAL();

    /* Only the four face buttons are translated here. */
    joy_active_controller = (SDL_Gamepad *)&failures;
    if (IN_FaceButtonKey(SDL_GAMEPAD_BUTTON_BACK) != 0 || IN_FaceButtonKey(SDL_GAMEPAD_BUTTON_INVALID) != 0)
    {
        printf("FAIL: a non-face button produced a face key\n");
        failures++;
    }

    if (failures)
        return 1;
    printf("face buttons: PASS (Xbox, PlayStation, Switch labels, fallback, remap, opt-out, no pad)\n");
    return 0;
}
'''

SDL_TABLE = r'''
#include <SDL3/SDL.h>
#include <stdio.h>

int main(void)
{
    const SDL_GamepadType types[] = {
        SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR, SDL_GAMEPAD_TYPE_XBOXONE
    };
    const SDL_GamepadButton buttons[] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH
    };
    for (int t = 0; t < 3; t++)
    {
        for (int b = 0; b < 4; b++)
            printf("%d ", (int)SDL_GetGamepadButtonLabelForType(types[t], buttons[b]));
        printf("\n");
    }
    return 0;
}
'''


def build_and_run(path, name, text, flags):
    source_path = path / f"{name}.c"
    binary = path / name
    source_path.write_text(text)
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    str(source_path), "-o", str(binary), *flags], check=True)
    return subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout


def main():
    with tempfile.TemporaryDirectory(prefix="qssm-gamepad-labels-") as tmp:
        path = Path(tmp)
        print(build_and_run(path, "labels", DOUBLES + helpers + CHECKS, []), end="")

        flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl3"], text=True))
        rows = [line.split() for line in build_and_run(path, "table", SDL_TABLE, flags).splitlines()]
        a, b, x, y = "1", "2", "3", "4"   # SDL_GAMEPAD_BUTTON_LABEL_A..Y
        switch = [b, a, y, x]             # south, east, west, north
        assert rows[0] == switch, f"Switch Pro labels {rows[0]} differ from the fallback {switch}"
        assert rows[1] == switch, f"Joy-Con pair labels {rows[1]} differ from the fallback {switch}"
        assert rows[2] == [a, b, x, y], f"Xbox labels {rows[2]} are not positional"
        print("SDL label table: Switch Pro and Joy-Con pair match the fallback; Xbox is positional")


if __name__ == "__main__":
    main()
