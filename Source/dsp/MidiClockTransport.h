#pragma once

/** MIDI-clock tracking for the sequencer's external clock. Audio-thread only;
    shared by the native processor and the WebAssembly CLAP core, so no JUCE
    types appear here, only plain numbers.

    Two responsibilities:

    1. Sync = Midi Clock: Start (0xFA) resets the 24-PPQ tick measure, so the
       engine re-anchors its step grid on the next tick; Stop (0xFC) drops the
       run line. The tempo the clock actually runs at is read off the tick
       spacing with a light exponential average - a stepwise knob value would
       zipper every following step.

    2. Start mode = Note Trigger: a note-on while the run line is down starts
       the clock - at the BPM knob's tempo when no clock ticks flow (Manual,
       or a Midi Clock source that is only started by notes), at the measured
       clock tempo the moment ticks arrive. The host's transport is left
       untouched in Sync = Host: the run line there belongs to the host.

    Sample bookkeeping is the caller's: every block, scan first (event sample
    positions are relative to the block's own start), then advance() once. */
class MidiClockTransport
{
public:
    void prepare() { *this = MidiClockTransport(); }

    /** The run line's meaning changes when the sync or start mode changes, so
        a stale trigger must not survive a switch. */
    void beforeBlock (int syncIndex, int startModeIndex)
    {
        if (syncIndex != lastSyncIndex || startModeIndex != lastStartIndex)
        {
            lastSyncIndex = syncIndex;
            lastStartIndex = startModeIndex;
            triggered = false;
        }
    }

    /** One block's worth of samples, called after the block's events were fed. */
    void advance (int numSamples, double sampleRateHz)
    {
        if (triggered && triggerBpm > 1.0)
            triggerBeat += static_cast<double> (numSamples) / (60.0 / triggerBpm * sampleRateHz);
        processedSamples += numSamples;
    }

    // ── MIDI clock status ─────────────────────────────────────────────────
    void start() { clockRunning = true; tickCount = 0; lastTickSample = -1; tickBpm = 0.0; }
    void cont()  { clockRunning = true; }
    /** A Stop drops the run line and any stale trigger with it: whatever
        started the run, the outside world said stop. */
    void halt()  { clockRunning = false; tickCount = 0; lastTickSample = -1; tickBpm = 0.0; triggered = false; }

    void tick (int sampleInBlock)
    {
        const long long sample = processedSamples + sampleInBlock;
        if (lastTickSample >= 0 && sample > lastTickSample)
        {
            // 24 ticks per beat: the spacing reads the tempo straight out.
            const double fresh = 60.0 * sampleRateHz
                / static_cast<double> (sample - lastTickSample) / 24.0;
            if (fresh > 20.0 && fresh < 400.0)
                tickBpm = tickBpm > 0.0 ? 0.6 * tickBpm + 0.4 * fresh : fresh;
        }
        lastTickSample = sample;
        ++tickCount;
    }

    // ── Note Trigger start mode ───────────────────────────────────────────
    void noteOn (bool triggerAllowed, bool runLineUp, double knobBpm)
    {
        if (triggerAllowed && ! runLineUp)
        {
            triggered = true;
            triggerBeat = 0.0;
            triggerBpm = knobBpm;
        }
    }

    void setSampleRate (double rateHz) { sampleRateHz = rateHz > 1.0 ? rateHz : 48000.0; }

    /** The run line as this block will see it, before the block's own note scan
        decides anything: a Note Trigger firing mid-buffer must not re-arm on
        the same notes that raised it. */
    bool clockLineUp (int syncIndex, bool manualPlaying) const
    {
        if (syncIndex == 1)
            return manualPlaying || triggered;
        if (syncIndex == 2)
            return clockRunning || triggered;
        return false;
    }

    // ── Per-block read-out, for the transport's own branches ──────────────

    /** Sync = Manual: the PLAY switch rules, the trigger only ever starts what
        the switch left down. beats carries the synthetic grid (which restarts
        the pattern at step 1) for a triggered run, and -1 for the switch's. */
    void evaluateManual (bool manualPlaying, double knobBpm,
                         bool& running, double& bpm, double& beats)
    {
        bpm = knobBpm;
        if (manualPlaying)
        {
            triggered = false;
            running = true;
            beats = -1.0;
        }
        else if (triggered)
        {
            triggerBpm = knobBpm;   // the knob keeps steering a triggered free run
            running = true;
            beats = triggerBeat;
        }
        else
            running = false;
    }

    /** Sync = Midi Clock: the status line or a note decides, the tempo is the
        measure's while it has seen ticks and the knob's while it has not.
        beats is the tick measure, so every start lands on step 1. */
    void evaluateClock (double knobBpm, bool& running, double& bpm, double& beats)
    {
        bpm = tickBpm > 0.0 ? tickBpm : knobBpm;
        running = clockRunning || triggered;
        beats = static_cast<double> (tickCount) / 24.0;
        if (! running)
            beats = -1.0;
    }

    /** The clock's own status line dropped: a stale trigger should not keep it
        running (a Stop arrives before or after a trigger indiscriminately). */
    void dropTrigger() { triggered = false; }

private:
    double sampleRateHz = 48000.0;
    long long processedSamples = 0;

    bool clockRunning = false;
    long long tickCount = 0;
    long long lastTickSample = -1;
    double tickBpm = 0.0;

    bool triggered = false;
    double triggerBeat = 0.0, triggerBpm = 130.0;
    int lastSyncIndex = -1, lastStartIndex = -1;
};
