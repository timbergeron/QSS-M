/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// pong.c -- paused-game Pong mini-game

#include "quakedef.h"

// Positive values enable Pong and set its speed. The menu stores the last
// enabled speed as a negative value while disabled so it survives restarts.
cvar_t cl_pong = {"cl_pong", "2", CVAR_ARCHIVE};

extern Uint64 maptime;

/*
=================
Pong -- woods #pong
=================

A simple Pong mini-game that runs whenever the game is paused (#pong).
Controlled by the cl_pong cvar, it features resolution-independent scaling,
an adjustable speed multiplier, basic AI paddle logic, and standard
Quake engine rendering and input handling for a retro diversion.
*/

extern cvar_t gl_load24bit;
extern qpic_t* sb_nums[2][11];
extern qboolean windowhasfocus;

#define PONG_BASE_WIDTH          1920.0f
#define PONG_BASE_HEIGHT         1080.0f
#define PONG_DEFAULT_MULTIPLIER  2.0f
#define PONG_MAX_BALL_SPEED      900.0f
#define PONG_AI_OFFSET_TIME      0.25
#define PONG_MAX_SCORE           99
#define PONG_PADDLE_WIDTH        15.0f
#define PONG_PADDLE_HEIGHT       80.0f
#define PONG_BALL_SIZE           10.0f

static inline float Pong_GetScale(void)
{
	float sx = (float)glwidth / PONG_BASE_WIDTH;
	float sy = (float)glheight / PONG_BASE_HEIGHT;
	return q_max(q_min(sx, sy), 0.001f);
}
static inline float Pong_Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

typedef struct { float x, y, w, h, speed; } paddle_t;
typedef struct {
	float x, y, size, dx, dy, speed;
	qmodel_t* model;
	struct gltexture_s* texture;
	mspriteframe_t* frame;
} ball_t;

typedef struct {
	paddle_t player;
	paddle_t ai;
	ball_t ball;
	int screen_width;
	int screen_height;
	int player_score;
	int ai_score;
	Uint64 map_start_time;
	qpic_t* pause_pic;
	double last_update_time;
	double player_paddle_flash_time;
	float ai_offset;
	double ai_last_offset_time;
	float last_multiplier;
	double sprite_retry_time;
	qboolean initialized;
	qboolean user_frozen;
	qboolean was_frozen;
	qboolean preview_only_active;
	qboolean ball_was_right;
} pong_state_t;

static pong_state_t pong;

qboolean Pong_Enabled(void)
{
	return cl_pong.value > 0.0f;
}

static void Pong_CvarChanged(cvar_t* var)
{
	if (var->value > 0.0f)
	{
		pong.last_multiplier = var->value;
	}
	else
	{
		if (var->value < 0.0f)
			pong.last_multiplier = -var->value;
		pong.was_frozen = true;
		pong.last_update_time = realtime;
	}
}

void Pong_ToggleEnabled(void)
{
	if (Pong_Enabled())
		Cvar_SetValueQuick(&cl_pong, -cl_pong.value);
	else if (cl_pong.value < 0.0f)
		Cvar_SetValueQuick(&cl_pong, -cl_pong.value);
	else
		Cvar_SetValueQuick(&cl_pong,
			pong.last_multiplier > 0.0f ? pong.last_multiplier : PONG_DEFAULT_MULTIPLIER);
}

void Pong_ToggleFreeze(void)
{
	pong.user_frozen = !pong.user_frozen;
	if (pong.user_frozen)
		pong.was_frozen = true;
}

// Return the pause-banner rectangle in Pong's virtual coordinates.
static qboolean Pong_GetPauseRect(float sc,
	float* x, float* y, float* w, float* h)
{
	if (!pong.pause_pic)                       /* texture not in memory   */
		return false;

	// Match the canvas used by SCR_DrawPause or SCR_DrawPause2.
	const float menuscale =
		cl.match_pause_time ? scr_menuscale.value - 1.f   // CANVAS_MENU2
		: scr_menuscale.value;        // CANVAS_MENU
	const float maxscale = q_min((float)glwidth / 320.f,
		(float)glheight / 200.f);
	const float msc = CLAMP(1.0f, menuscale, maxscale);
	float pic_w = pong.pause_pic->width;
	float pic_h = pong.pause_pic->height;

	// The stock LMP is 128x24 even when its cached dimensions disagree.
	if (!gl_load24bit.value)
		pic_w = 128;

	// Correct replacement textures carrying a square 24x24 header.
	if (pic_w == pic_h && pic_h > 0)
		pic_w = pic_h * (128.0f / 24.0f);

	// Calculate the pixel-space rectangle used by the menu canvas.
	float px = ((320 - pic_w) * 0.5f) * msc +
		(glwidth - 320.f * msc) * 0.5f;
	float py = ((240 - 48 - pic_h) * 0.5f) * msc +
		(glheight - 200.f * msc) * 0.5f;
	float pw = pic_w * msc;
	float ph = pic_h * msc;

	// Convert back to Pong's 1920x1080 virtual space.
	*x = px / sc;  *y = py / sc;
	*w = pw / sc;  *h = ph / sc;
	return true;
}


void Pong_Init(void)
{
	Cvar_RegisterVariable(&cl_pong);
	Cvar_SetCallback(&cl_pong, Pong_CvarChanged);
	Pong_CvarChanged(&cl_pong);

	memset(&pong.ball, 0, sizeof(pong.ball));

	// Cache pause pic on init
	if (!pong.pause_pic)
		pong.pause_pic = Draw_CachePic("gfx/pause.lmp");
}

static float Pong_MouseYInFramebuffer(int y)
{
	float framebuffer_y = (float)y;

#if defined(USE_SDL2)
	SDL_Window* window = (SDL_Window*)VID_GetWindow();
	int window_width, window_height;

	if (window)
	{
		SDL_GetWindowSize(window, &window_width, &window_height);
		if (window_height > 0)
			framebuffer_y *= (float)vid.height / window_height;
	}
#endif

	return framebuffer_y;
}

static void Pong_Reset(qboolean new_session)
{
	const float sc = Pong_GetScale();
	pong.screen_width = vid.width;  pong.screen_height = vid.height;

	pong.player.w = pong.ai.w = PONG_PADDLE_WIDTH;
	pong.player.h = pong.ai.h = PONG_PADDLE_HEIGHT;
	pong.ball.size = PONG_BALL_SIZE;

	if (new_session) {
		int mx, my; SDL_GetMouseState(&mx, &my);
		pong.player.y = Pong_Clamp(Pong_MouseYInFramebuffer(my) / sc - pong.player.h * 0.5f,
			0.0f, (pong.screen_height / sc) - pong.player.h);
		pong.player_score = pong.ai_score = 0;
		pong.ai_offset = 0.0f;
		pong.ai_last_offset_time = 0.0f;
		pong.ball_was_right = true;
		pong.user_frozen = false;
		pong.was_frozen = false;
	}
	else {
		pong.player.y = Pong_Clamp(pong.player.y, 0.0f,
			(pong.screen_height / sc) - pong.player.h);
	}
	pong.player.x = (pong.screen_width / sc) - PONG_PADDLE_WIDTH - 20.0f;

	pong.ai.x = 20.0f;
	pong.ai.y = ((pong.screen_height / sc) - PONG_PADDLE_HEIGHT) * 0.5f;
	pong.ai.speed = 300.0f;
	pong.ai_offset = 0.0f;
	pong.ball_was_right = true;

	float prx, pry, prw, prh;
	if (!Pong_GetPauseRect(sc, &prx, &pry, &prw, &prh))
	    return;                   /* nothing to bounce off - bail */

	pong.ball.x = (pong.screen_width / sc) * 0.5f;
	pong.ball.y = Pong_Clamp(pry - pong.ball.size * 8.0f,
		pong.ball.size * 4.0f, (pong.screen_height / sc) * 0.25f);
	float ang = ((rand() % 60) - 30) * (M_PI / 180.f);
	float dir = (rand() % 2) ? 1.f : -1.f;
	pong.ball.dx = dir * cosf(ang); pong.ball.dy = sinf(ang);
	float m = sqrtf(pong.ball.dx * pong.ball.dx + pong.ball.dy * pong.ball.dy);
	pong.ball.dx /= m; pong.ball.dy /= m;
	pong.ball.speed = q_min(300.0f * cl_pong.value, PONG_MAX_BALL_SPEED);

	pong.initialized = true;
	pong.last_update_time = realtime;
}

static inline void Pong_HandlePaddleCollision(paddle_t* P)
{
	if (pong.ball.x + pong.ball.size * 0.5f > P->x && pong.ball.x - pong.ball.size * 0.5f < P->x + P->w &&
		pong.ball.y + pong.ball.size * 0.5f > P->y && pong.ball.y - pong.ball.size * 0.5f < P->y + P->h)
	{
		pong.ball.x = (pong.ball.dx > 0 ? P->x - pong.ball.size * 0.5f : P->x + P->w + pong.ball.size * 0.5f);
		pong.ball.dx = -pong.ball.dx;
		S_LocalSound("buttons/switch21.wav");
		float rel = (P->y + P->h * 0.5f) - pong.ball.y;
		pong.ball.dy = -0.75f * (rel / (P->h * 0.5f));
		if (fabsf(pong.ball.dy) < 0.1f) pong.ball.dy = (pong.ball.dy > 0 ? 0.1f : -0.1f);
		float m = sqrtf(pong.ball.dx * pong.ball.dx + pong.ball.dy * pong.ball.dy);
		if (m != 0) { // Avoid division by zero
			pong.ball.dx /= m;
			pong.ball.dy /= m;
		}
		pong.ball.speed = q_min(pong.ball.speed * 1.05f, PONG_MAX_BALL_SPEED);
	}
}

static void Pong_HandlePauseCollision(float x, float y, float w, float h)
{
	float distances[4];
	int side;

	if (pong.ball.x + pong.ball.size * 0.5f <= x ||
		pong.ball.x - pong.ball.size * 0.5f >= x + w ||
		pong.ball.y + pong.ball.size * 0.5f <= y ||
		pong.ball.y - pong.ball.size * 0.5f >= y + h)
		return;

	distances[0] = pong.ball.x - x;
	distances[1] = (x + w) - pong.ball.x;
	distances[2] = pong.ball.y - y;
	distances[3] = (y + h) - pong.ball.y;
	side = 0;
	for (int i = 1; i < 4; ++i)
		if (distances[i] < distances[side])
			side = i;

	switch (side)
	{
	case 0: pong.ball.x = x - pong.ball.size * 0.5f; pong.ball.dx = -fabsf(pong.ball.dx); break;
	case 1: pong.ball.x = x + w + pong.ball.size * 0.5f; pong.ball.dx = fabsf(pong.ball.dx); break;
	case 2: pong.ball.y = y - pong.ball.size * 0.5f; pong.ball.dy = -fabsf(pong.ball.dy); break;
	case 3: pong.ball.y = y + h + pong.ball.size * 0.5f; pong.ball.dy = fabsf(pong.ball.dy); break;
	}
}

static void Pong_MoveBall(float dt, float sc, float sh)
{
	float pause_x, pause_y, pause_w, pause_h;
	const qboolean has_pause_rect = Pong_GetPauseRect(sc,
		&pause_x, &pause_y, &pause_w, &pause_h);
	const float max_step_distance = q_max(pong.ball.size * 0.5f, 1.0f);
	const int steps = q_max(1, (int)ceilf(pong.ball.speed * dt / max_step_distance));
	const float step_time = dt / steps;

	for (int step = 0; step < steps; ++step)
	{
		pong.ball.x += pong.ball.dx * pong.ball.speed * step_time;
		pong.ball.y += pong.ball.dy * pong.ball.speed * step_time;

		if (pong.ball.y - pong.ball.size * 0.5f < 0.0f)
		{
			pong.ball.y = pong.ball.size * 0.5f;
			pong.ball.dy = -pong.ball.dy;
		}
		if (pong.ball.y + pong.ball.size * 0.5f > sh)
		{
			pong.ball.y = sh - pong.ball.size * 0.5f;
			pong.ball.dy = -pong.ball.dy;
		}

		if (has_pause_rect)
			Pong_HandlePauseCollision(pause_x, pause_y, pause_w, pause_h);

		if (pong.ball.dx > 0.0f)
			Pong_HandlePaddleCollision(&pong.player);
		else
			Pong_HandlePaddleCollision(&pong.ai);
	}
}

void Pong_Update(void)
{
	qboolean pong_preview = M_LivePreview_UsePong ();
	qboolean preview_only = (pong_preview && !cl.paused && !cl.match_pause_time);

	if (!preview_only && pong.preview_only_active)
	{
		pong.initialized = false;
		pong.user_frozen = false;
		pong.was_frozen = false;
		pong.preview_only_active = false;
	}
	else if (preview_only)
	{
		pong.preview_only_active = true;
	}

	if (!Pong_Enabled() || (!cl.paused && !cl.match_pause_time && !pong_preview) || cls.demoplayback)
	{
		pong.was_frozen = true;
		pong.last_update_time = realtime;
		return;
	}

	if (pong.initialized && (vid.width != pong.screen_width || vid.height != pong.screen_height)) // Check if the window was resized while paused
	{
		Pong_Reset(false); // Re-initialize with new dimensions
	}

	if (maptime != pong.map_start_time) // Check if the map has changed since the last time Pong was active
	{
		pong.initialized = false;      // Force re-initialization
		pong.ball.model = NULL;      // Clear potentially stale pointers
		pong.ball.frame = NULL;
		pong.ball.texture = NULL;
		pong.sprite_retry_time = 0.0;
		pong.user_frozen = false;
		pong.was_frozen = false;
		pong.map_start_time = maptime; // Update our stored maptime
	}

	qboolean frozen = (pong.user_frozen ||
		(!pong_preview && (key_dest != key_game || !windowhasfocus)));

	if (frozen) {
		pong.was_frozen = true;
		pong.last_update_time = realtime;
		return;
	}

	if (!pong.initialized)
	{
		Pong_Reset(true);
		if (!pong.initialized)
			return;
	}

	if (pong.was_frozen)
	{
		pong.last_update_time = realtime;
		pong.was_frozen = false;
	}

	const float sc = Pong_GetScale();
	const float sw = pong.screen_width / sc, sh = pong.screen_height / sc;

	// Calculate dt based on last *active* update time
	double elapsed = realtime - pong.last_update_time;
	float dt;
	// Update pong.last_update_time *only when* logic runs
	pong.last_update_time = realtime;

	// Handle potential time issues (e.g., after regaining focus if dt wasn't clamped)
	if (elapsed < 0.0) elapsed = 0.0; // Prevent issues if time goes backwards slightly
	// Clamp large delta time - might still happen if focus is lost/regained rapidly
	// between the frozen check and here, though unlikely. A clamp is safe.
	if (elapsed > 0.1) elapsed = 0.1;
	dt = (float)elapsed;

	Pong_MoveBall(dt, sc, sh);

	/* AI movement -------------------------------------------------------- */
	qboolean ball_left = pong.ball.x < sw * 0.5f;
	if (pong.ball.dx < 0 && (pong.ball_was_right ||
		realtime - pong.ai_last_offset_time > PONG_AI_OFFSET_TIME)) {
		float diff = pong.ai_score - pong.player_score;
		float t = Pong_Clamp(fabsf(diff) / 5.0f, 0.0f, 1.0f);
		float eff = (diff <= 0) ? (0.6f + 0.15f * t) : (0.6f - 0.35f * t);
		float random_fraction = (rand() / (float)RAND_MAX) * 2.0f - 1.0f; // Random float between -1 and +1
		float max_offset = pong.ai.h * 0.5f;
		pong.ai_offset = (1.0f - eff) * random_fraction * max_offset;
		pong.ai_last_offset_time = realtime;
	}
	pong.ball_was_right = !ball_left;

	float ai_tgt = pong.ball.y - pong.ai.h * 0.5f + pong.ai_offset;
	if (pong.ball.dx < 0) { // Only move the AI paddle if the ball is approaching it
		if (pong.ai.y < ai_tgt) { pong.ai.y += pong.ai.speed * dt;if (pong.ai.y > ai_tgt)pong.ai.y = ai_tgt; }
		else if (pong.ai.y > ai_tgt) { pong.ai.y -= pong.ai.speed * dt;if (pong.ai.y < ai_tgt)pong.ai.y = ai_tgt; }
	}
	pong.ai.y = Pong_Clamp(pong.ai.y, 0.0f, sh - pong.ai.h);
	// Player paddle position is updated by Pong_MouseMove

	/* scoring ------------------------------------------------------------ */
	if (pong.ball.x < 0)
	{
		if (!preview_only)
		{
			pong.player_score = q_min(pong.player_score + 1, PONG_MAX_SCORE);
			S_LocalSound("zombie/z_miss.wav");
		}
		Pong_Reset(false);
	}
	else if (pong.ball.x > sw)
	{
		if (!preview_only)
		{
			pong.ai_score = q_min(pong.ai_score + 1, PONG_MAX_SCORE);
			S_LocalSound("zombie/z_miss.wav");
		}
		pong.player_paddle_flash_time = realtime + 0.1;
		Pong_Reset(false);
	}
}

static void Quad(float x, float y, float w, float h)
{
	glBegin(GL_QUADS);
	glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
	glEnd();
}

static void Pong_ResolveBallSprite(void)
{
	msprite_t* sprite;

	if (pong.ball.model && !pong.ball.model->needload &&
		pong.ball.model->type == mod_sprite && pong.ball.frame &&
		pong.ball.frame->gltexture)
	{
		pong.ball.texture = pong.ball.frame->gltexture;
		return;
	}

	if (realtime < pong.sprite_retry_time)
	{
		pong.ball.texture = NULL;
		return;
	}
	pong.sprite_retry_time = realtime + 1.0;

	pong.ball.model = Mod_ForName("progs/s_light.spr", false);
	pong.ball.frame = NULL;
	pong.ball.texture = NULL;

	if (!pong.ball.model || pong.ball.model->type != mod_sprite)
		return;

	sprite = (msprite_t*)pong.ball.model->cache.data;
	if (!sprite || sprite->numframes < 1 || sprite->frames[0].type != SPR_SINGLE)
		return;

	pong.ball.frame = sprite->frames[0].frameptr;
	if (pong.ball.frame)
	{
		pong.ball.texture = pong.ball.frame->gltexture;
		if (pong.ball.texture)
			pong.sprite_retry_time = 0.0;
	}
}

void Pong_Draw(void)
{
	qboolean pong_preview = M_LivePreview_UsePong ();

	// Skip drawing during demo playback
	if (cls.demoplayback) return;

	// Only draw if enabled and game is paused, or while the menu previews Pong.
	if (!Pong_Enabled() || (!cl.paused && !cl.match_pause_time && !pong_preview)) return;

	if (!Sbar_EnsurePics ()) // sb_nums load lazily
		return;

	if (!pong.pause_pic)
		pong.pause_pic = Draw_CachePic("gfx/pause.lmp");

	if (pong_preview && !cl.paused && !cl.match_pause_time)
	{
		GL_SetCanvas(CANVAS_MENU);
		Draw_Pic((320 - (int)pong.pause_pic->width) / 2,
			(240 - 48 - (int)pong.pause_pic->height) / 2,
			pong.pause_pic);
	}

	GL_SetCanvas(CANVAS_DEFAULT);

	if (!pong.initialized) return;
	Pong_ResolveBallSprite();

	const float sc = Pong_GetScale();

	/* scores ----------------------------------------------------------- */
	float prx, pry, prw, prh;
	if (!Pong_GetPauseRect(sc, &prx, &pry, &prw, &prh))
	    return;        /* still loading */

	int psx = prx * sc, psy = pry * sc;      /* back to pixels for drawing */
	int pw  = prw * sc, ph  = prh * sc;

	glEnable(GL_TEXTURE_2D); glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Set standard blend func
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); // Use modulate for scores/pics
	glColor4f(1, 1, 1, 0.9f);

	float num_sc = sc * 1.2f;
	int sy = psy + (ph - 24 * num_sc) / 2, pad = 20 * sc,
		x = psx - pad - (pong.ai_score >= 10 ? 2 : 1) * 24 * num_sc;

	if (pong.ai_score >= 10) { Draw_ScaledPicAlpha(x, sy, sb_nums[0][pong.ai_score / 10], num_sc, 1);x += 24 * num_sc; }
	Draw_ScaledPicAlpha(x, sy, sb_nums[0][pong.ai_score % 10], num_sc, 1);

	x = psx + pw + pad;
	if (pong.player_score >= 10) { Draw_ScaledPicAlpha(x, sy, sb_nums[0][pong.player_score / 10], num_sc, 1);x += 24 * num_sc; }
	Draw_ScaledPicAlpha(x, sy, sb_nums[0][pong.player_score % 10], num_sc, 1);
	glDisable(GL_TEXTURE_2D); // Disable texture for paddles (solid color quads)

	/* paddles ----------------------------------------------------------- */
	float pwid = PONG_PADDLE_WIDTH * sc, phgt = PONG_PADDLE_HEIGHT * sc;
	glColor4f(0, 0, 0, 0.9f); // Black outline
	Quad(pong.player.x * sc - sc, pong.player.y * sc - sc, pwid + 2 * sc, phgt + 2 * sc);
	Quad(pong.ai.x * sc - sc, pong.ai.y * sc - sc, pwid + 2 * sc, phgt + 2 * sc);

	// Determine player paddle color (flash or default)
	byte paddle_rgb[3];
	float paddle_alpha = 0.9f; // Default alpha
	static byte white_rgb[3] = { 255, 255, 255 };
	byte* default_col = (cl.viewentity > 0 && (unsigned)(cl.viewentity - 1) < (unsigned)cl.maxclients) ?
		CL_PLColours_ToRGB(&cl.scores[cl.viewentity - 1].pants) :
		white_rgb;
	paddle_rgb[0] = default_col[0];
	paddle_rgb[1] = default_col[1];
	paddle_rgb[2] = default_col[2];

	// Draw the base player paddle
	glColor4f(paddle_rgb[0] / 255.f, paddle_rgb[1] / 255.f, paddle_rgb[2] / 255.f, paddle_alpha);
	Quad(pong.player.x * sc, pong.player.y * sc, pwid, phgt);

	// Overlay damage color if flashing
	if (cl_damagehue.value != 0 && realtime < pong.player_paddle_flash_time)
	{
		// Use damage hue color
		plcolour_t damage_color = CL_PLColours_Parse(cl_damagehuecolor.string);
		paddle_rgb[0] = damage_color.rgb[0];
		paddle_rgb[1] = damage_color.rgb[1];
		paddle_rgb[2] = damage_color.rgb[2];
		float flash_alpha = 0.7f; // Opacity for the flash overlay

		// Draw the flash overlay
		glColor4f(paddle_rgb[0] / 255.f, paddle_rgb[1] / 255.f, paddle_rgb[2] / 255.f, flash_alpha);
		Quad(pong.player.x * sc, pong.player.y * sc, pwid, phgt);
	}
	// No 'else' needed, base paddle is already drawn

	glColor4f(0.5f, 0.5f, 0.5f, 0.9f); // AI paddle color
	Quad(pong.ai.x * sc, pong.ai.y * sc, pwid, phgt);

	/* ball ---------------------------------------------------------------- */
	float bs = pong.ball.size * sc * 1.5f;
	GL_DisableMultitexture();
	GL_ClearBindings();
	glColor4f(1, 1, 1, 0.9f);

	if (pong.ball.texture && pong.ball.frame)
	{
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_ALPHA_TEST);
		glAlphaFunc(GL_GREATER, 0.1f);
		GL_Bind(pong.ball.texture);

		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex2f(pong.ball.x * sc - bs, pong.ball.y * sc + bs);
		glTexCoord2f(pong.ball.frame->smax, 0); glVertex2f(pong.ball.x * sc + bs, pong.ball.y * sc + bs);
		glTexCoord2f(pong.ball.frame->smax, pong.ball.frame->tmax); glVertex2f(pong.ball.x * sc + bs, pong.ball.y * sc - bs);
		glTexCoord2f(0, pong.ball.frame->tmax); glVertex2f(pong.ball.x * sc - bs, pong.ball.y * sc - bs);
		glEnd();
	}
	else
	{
		glDisable(GL_TEXTURE_2D);
		Quad(pong.ball.x * sc - bs, pong.ball.y * sc - bs, bs * 2.0f, bs * 2.0f);
	}

	/* ---------- restore classic console GL state -------------------- */
	glEnable(GL_ALPHA_TEST);                                   /* hard mask   */
	glAlphaFunc(GL_GREATER, 0.666f);                            /* typical cut-off */
	glDisable(GL_BLEND);                                        /* opaque glyphs   */
	glEnable(GL_TEXTURE_2D);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); /* ignore colour   */
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);                          /* white           */
	GL_ClearBindings();                                         /* unbind sprites  */
}

void Pong_MouseMove(int x, int y)
{
	float sc;

	if (!Pong_Enabled() || cls.demoplayback ||
		(!cl.paused && !cl.match_pause_time) || !pong.initialized ||
		key_dest != key_game || !windowhasfocus) return;
	Q_UNUSED(x);
	sc = Pong_GetScale();
	pong.player.y = Pong_Clamp(Pong_MouseYInFramebuffer(y) / sc - pong.player.h * 0.5f,
		0.0f, (pong.screen_height / sc) - pong.player.h);
}
