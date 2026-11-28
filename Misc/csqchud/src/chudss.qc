/*
	chudss.qc -- tiny server progs for compare.py's mission-pack scenarios.
	Spawns the player and, every frame, forces the status-bar state from
	cvars, so the engine's bar and the sample HUD see identical Hipnotic or
	Rogue item bits without needing those games' progs. items2 carries bits
	23-31 exactly (QSS-M sends items | items2 << 23).
*/
.float items2;

void() main = {};
void() SetNewParms = {};
void() SetChangeParms = {};
void() ClientKill = {};
void() ClientConnect = {};
void() ClientDisconnect = {};
void() PlayerPostThink = {};
void() StartFrame = {};
void() info_player_start = {};

void() worldspawn =
{
	precache_model("progs/player.mdl");
};

void() chud_apply =
{
	self.items = cvar("chud_ss_items");
	self.items2 = cvar("chud_ss_items2");
	self.weapon = cvar("chud_ss_weapon");
	self.currentammo = cvar("chud_ss_ammo");
	self.ammo_shells = cvar("chud_ss_shells");
	self.ammo_nails = cvar("chud_ss_nails");
	self.ammo_rockets = cvar("chud_ss_rockets");
	self.ammo_cells = cvar("chud_ss_cells");
	self.armorvalue = cvar("chud_ss_armor");
	self.health = 100;
};

void() PlayerPreThink = { chud_apply(); };

void() PutClientInServer =
{
	local entity spot = find(world, classname, "info_player_start");
	self.classname = "player";
	self.solid = SOLID_SLIDEBOX;
	self.movetype = MOVETYPE_WALK;
	self.flags = FL_CLIENT;
	self.takedamage = DAMAGE_NO;
	setmodel(self, "progs/player.mdl");
	setsize(self, '-16 -16 -24', '16 16 32');
	setorigin(self, spot.origin + '0 0 1');
	self.view_ofs = '0 0 22';
	self.angles = spot.angles;
	self.fixangle = TRUE;
	chud_apply();
};
