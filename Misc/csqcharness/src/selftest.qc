/*
	selftest.qc -- assertion core shared by the CSQC and MenuQC halves.

	Every check prints one machine-readable line:

		SELFTEST PASS <name>
		SELFTEST FAIL <name> got=<x> want=<y>

	and the run ends with

		SELFTEST DONE <module> pass=<n> fail=<n>

	run_harness.py parses those. Keep the format stable.
*/

.float st_flagfield;	// shared scratch field for the find* checks

float st_pass, st_fail;

void(string name, float ok) st_ok =
{
	if (ok)
	{
		st_pass = st_pass + 1;
		print("SELFTEST PASS ", name, "\n");
	}
	else
	{
		st_fail = st_fail + 1;
		print("SELFTEST FAIL ", name, "\n");
	}
};

void(string name, float got, float want) st_eqf =
{
	if (got == want)
	{
		st_pass = st_pass + 1;
		print("SELFTEST PASS ", name, "\n");
	}
	else
	{
		st_fail = st_fail + 1;
		print("SELFTEST FAIL ", name, " got=", ftos(got), " want=", ftos(want), "\n");
	}
};

void(string name, string got, string want) st_eqs =
{
	if (got == want)
	{
		st_pass = st_pass + 1;
		print("SELFTEST PASS ", name, "\n");
	}
	else
	{
		st_fail = st_fail + 1;
		print("SELFTEST FAIL ", name, " got=\"", got, "\" want=\"", want, "\"\n");
	}
};

//======================================================================
// Return poisoning.
//
// A negative-path check that expects 0 (or '0 0 0', or "") can pass by accident
// when OFS_RETURN already held that value. Call a poison helper immediately
// before the call under test so the return globals hold something distinctive;
// a handler that leaves them alone, or clears only part of a vector, then fails.
//======================================================================

float() st_poisonf =
{
	return 12345;
};

vector() st_poisonv =
{
	return '123 456 789';
};

string() st_poisons =
{
	return "poison";
};

void(string name, vector got, vector want) st_eqv =
{
	if (got == want)
	{
		st_pass = st_pass + 1;
		print("SELFTEST PASS ", name, "\n");
	}
	else
	{
		st_fail = st_fail + 1;
		print("SELFTEST FAIL ", name, " got=", vtos(got), " want=", vtos(want), "\n");
	}
};

void(string module) st_report =
{
	print("SELFTEST DONE ", module, " pass=", ftos(st_pass), " fail=", ftos(st_fail), "\n");
};

//======================================================================
// checks valid in both modules
//======================================================================

// 20d62460 -- getgamedirinfo used to walk off the end of the mod list and
// dereference NULL. Any index past the end must return the null string.
void() st_getgamedirinfo =
{
	st_poisons();
	st_eqs("getgamedirinfo/out-of-range", getgamedirinfo(99999, 0), "");
	st_poisons();
	st_eqs("getgamedirinfo/negative",     getgamedirinfo(-5, 0), "");
	// index 0 is whatever the first mod is; only assert it did not crash
	getgamedirinfo(0, 0);
	st_ok("getgamedirinfo/first-survives", TRUE);
};

// 20d62460 -- bindmap is the 3rd argument and modifier the 4th, so a two- or
// three-argument call used to read an absent parameter. A three-arg call in
// particular picked up garbage as the modifier and returned without binding.
void() st_setkeybind =
{
	local float k = stringtokeynum("F11");
	setkeybind(k, "impulse 251");
	st_eqs("setkeybind/2arg", getkeybind(k), "impulse 251");
	setkeybind(k, "impulse 252", 0);
	st_eqs("setkeybind/3arg", getkeybind(k), "impulse 252");
	setkeybind(k, "", 0, 0);
	st_eqs("setkeybind/4arg-clears", getkeybind(k), "");
};

// 66180b68 / dd50a274 -- FTE's menuqc numbers are bound alongside (or now
// ahead of) the QSS ones. Both slots must reach the same builtin.
void() st_numbering_shared =
{
	st_eqs("chr2str/canonical", chr2str(65), "A");
	st_eqf("stringtokeynum/canonical", stringtokeynum("ESCAPE"), 27);
	st_ok("gettime/canonical", gettime(0) > 0);
	// own bit again, and assert a positive find rather than an absence
	local entity f = spawn();
	f.st_flagfield = 128;
	st_ok("findflags/finds-tagged-entity",
		findflags(__NULL__, st_flagfield, 128) == f);
};

// serverkeyfloat used to ignore its assumevalue entirely, and serverkey handed
// back an empty tempstring rather than the null string for a missing key.
void() st_serverkeyfloat =
{
	st_eqf("serverkeyfloat/missing-default",
		serverkeyfloat("hq_key_does_not_exist", 37), 37);
	st_poisonf();
	st_eqf("serverkeyfloat/missing-zero",
		serverkeyfloat("hq_key_does_not_exist"), 0);
	st_ok("serverkey/missing-is-null-string",
		serverkey("hq_key_does_not_exist") == "");
};
