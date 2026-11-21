# Licences and credits

**Resonique** (this repository): no licence file has been added yet, so the usual default applies: the author
keeps all rights to the plugin's own code, UI, presets and documentation. Add a `LICENSE` file here to change that.

The binaries are built with and contain:

- **JUCE 8.0.17** (juce.com): the plugin framework, the VST3 wrapper, the Standalone host and the convolution
  engine used for the Impulse Response reverb type. JUCE 8 is available under its own licence terms (the JUCE End
  User Licence Agreement, with an AGPLv3 option for open-source projects); the terms that apply to a distribution of
  this plugin are JUCE's.
- **VST3 SDK** (Steinberg Media Technologies GmbH): through JUCE's VST3 wrapper. VST is a trademark of Steinberg
  Media Technologies GmbH.
- **WebView2** (Microsoft): the plugin's UI runs in the system's WebView2 runtime, which stays Microsoft's.

Everything in the sound engine is this repository's own implementation of published acoustics and signal-processing
techniques: the stiff-string partial series (Fletcher & Rossing), the Hunt–Crossley / Hertz-type felt contact law
(Chaigne & Askenfelt, Stulov), modal synthesis with rotation (coupled-form) resonators, the bridge coupling of unison
strings (Weinreich), reduced-order modal soundboards, the Railsback stretch curve, historical temperaments (public
cent tables), the Scala `.scl` format (Manuel Op de Coul's public specification), and a Householder feedback delay
network reverb. No Pianoteq code, assets, or trademarks are used; Pianoteq is a trademark of Modartt S.A.S.

No recorded samples, licensed impulse responses or third-party DSP libraries are included. Users can load their own
impulse-response files; those stay subject to their own licences.
