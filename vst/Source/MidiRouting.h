#pragma once
// ============================================================================
//  MIDI message -> engine (shared by the plugin and the test tools).
//  Notes, sustain (CC64, continuous), sostenuto (CC66), soft (CC67), pitch
//  bend, all sound off / all notes off / reset controllers. Every channel.
// ============================================================================
#include <juce_audio_basics/juce_audio_basics.h>
#include "PianoEngine.h"

namespace resonique
{
inline void routeMidi (const juce::MidiMessage& m, PianoEngine& engine, double bendRangeSemitones)
{
    if (m.isNoteOn())            engine.noteOn (m.getNoteNumber(), m.getVelocity());
    else if (m.isNoteOff())      engine.noteOff (m.getNoteNumber());
    else if (m.isController())   engine.controller (m.getControllerNumber(), m.getControllerValue());
    else if (m.isPitchWheel())   engine.pitchBend ((m.getPitchWheelValue() - 8192) / 8192.0 * bendRangeSemitones);
    else if (m.isAllSoundOff())  engine.panic();
    else if (m.isAllNotesOff() || m.isResetAllControllers()) engine.allNotesOff();
}
} // namespace resonique
