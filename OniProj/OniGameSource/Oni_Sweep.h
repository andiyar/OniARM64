// ======================================================================
// Oni_Sweep.h
//
// Core of the level sweep harness (issue #103): the output tap, modal
// suppression and deterministic seeding that every phase depends on.
//
// The harness runs one level per process with drawing bypassed, spawns
// content, and records findings as NDJSON. Two things make an unattended
// run possible at all, and both live here:
//
//   * every UUrPrintWarning is intercepted and written as a finding rather
//     than raised as a blocking AUrMessageBox — one dialog would hang the
//     whole sweep forever;
//   * both RNG streams are reset to a fixed seed at the start of every
//     phase, so two runs of the same cell produce the same findings.
//
// Phases themselves are not here yet.
// ======================================================================
#pragma once
#ifndef ONI_SWEEP_H
#define ONI_SWEEP_H

#include "BFW.h"
#include "Oni_Sweep_Report.h"

/* Settle periods, in game ticks. Constants, never wall-clock. */
#define ONcSweep_SettleCharacter	60
#define ONcSweep_SettleParticle		600
#define ONcSweep_SettleAI			3000
#define ONcSweep_SettleScript		60

/* Fixed seed for both RNG streams at the start of every phase. */
#define ONcSweep_RandomSeed			0x4F4E4931

/* Phase a record carries before any phase has set its own context. */
#define ONcSweep_PhaseInit			"init"

extern UUtBool	ONgSweep_Active;

/*
	Open the report file, latch the renderer/level identity every record
	carries, and register the warning tap. Until this succeeds the rest of
	the module is inert: ONrSweep_Record no-ops and warnings behave exactly
	as they do in normal play.
*/
UUtError ONrSweep_Begin(const char *inOutputPath, const char *inRenderer, UUtUns16 inLevel);

/* Unregister the tap and close the report. Safe to call when not active. */
void ONrSweep_End(void);

/*
	Write one finding. inPhase / inSubject may be NULL, in which case the
	values last passed to ONrSweep_SetContext are used. Safe to call before
	ONrSweep_Begin and after ONrSweep_End — it no-ops rather than touching a
	closed handle.
*/
void ONrSweep_Record(const char *inPhase, const char *inSubject,
					 ONtSweepSeverity inSeverity, const char *inMessage);

/*
	Set the phase / subject that NULL-argument records inherit.

	READ THIS BEFORE WRITING A PHASE — console records are unclassified.

	In the OniSweep binary the console tap turns every COrConsole_Print into a
	record at ONcSweepSeverity_Warn. That is 404 live call sites, and most of
	them are routine chatter, command echoes and progress messages, not
	findings. Nothing here distinguishes "AI error: no path to target" from
	"loading level 4".

	This is deliberate, not an oversight. Severity is a claim about how bad a
	thing is, and inventing a taxonomy before anyone has seen the real corpus
	would bake a guess into fifteen levels of committed baseline. It is also
	survivable as it stands: sweep_diff gates on identity against the baseline,
	so chatter enters the baseline once and then only *changes* to it gate. New
	console output failing the gate is correct behaviour.

	What it costs you: the first baseline for each level will be large and
	mostly noise, and reading a fresh report means skimming past it. When the
	corpus exists (Task 13), classify then — from real messages, and
	retroactively over the reports already collected.

	Phase and subject are the only handles triage has. Set them tightly around
	anything that prints, so console records land attributed rather than in
	whatever context happened to be current.
*/
void ONrSweep_SetContext(const char *inPhase, const char *inSubject);

/* Reset both RNG streams to ONcSweep_RandomSeed. Call at the top of a phase. */
void ONrSweep_SeedRandom(void);

/*
	Advance inTicks of game time with no input and no drawing.

	Ticks are heartbeats actually executed, not calls to ONrGameState_Update —
	those are not the same thing, and the difference decides whether a settle
	settles at all. See the comment on the implementation.
*/
void ONrSweep_Tick(UUtUns32 inTicks);

/* Phase driver — filled in by the per-phase tasks. */
void ONrSweep_RunAllPhases(UUtUns16 inLevel);

/* One-time module setup, called during engine init. */
UUtError ONrSweep_Initialize(void);

#endif /* ONI_SWEEP_H */
