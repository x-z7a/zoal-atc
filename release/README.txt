zoal-atc plugin release

This archive contains the X-Plane plugin binary, the in-sim panel, and the
models it hears and speaks with: whisper for push-to-talk, and Kokoro for
the controllers, each position in a voice of its own on the COM radio.

1. Drop this zoal-atc folder into X-Plane Resources/plugins.
2. Open the panel with Plugins > zoal-atc > Toggle Panel.
3. Bind the X-Plane command zoal_atc/ptt to your push-to-talk input.

The plugin talks to the hosted zoal-atc console at wss://atc.zoal.app/plugin.
That is the default and needs no setup. To use a console you run yourself,
set url in <X-Plane>/Output/preferences/zoal_atc.cfg, which the plugin
writes on first run.
