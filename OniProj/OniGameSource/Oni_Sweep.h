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

/* Set the phase / subject that NULL-argument records inherit. */
void ONrSweep_SetContext(const char *inPhase, const char *inSubject);

/* Reset both RNG streams to ONcSweep_RandomSeed. Call at the top of a phase. */
void ONrSweep_SeedRandom(void);

/* Advance the simulation inTicks times with no input and no drawing. */
void ONrSweep_Tick(UUtUns32 inTicks);

/* Phase driver — filled in by the per-phase tasks. */
void ONrSweep_RunAllPhases(UUtUns16 inLevel);

/* One-time module setup, called during engine init. */
UUtError ONrSweep_Initialize(void);

#endif /* ONI_SWEEP_H */
