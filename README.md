# OSCDawServer
VST3 plugin host which communicates via OSC with a VST3 Plugin client that can be dropped into another DAW.
Several DAWs can use the same Server this way.

Copyright (c) 2024 Ruchir Shah

OSCDawServer is released under the terms of the GNU Affero General Public License v3.0. You are free to use, modify, and redistribute it as long as you comply with the AGPL, including sharing source changes. See [LICENSE](LICENSE) for the full text.
The VST3 Plugin client is also available at [OSC_Client](https://github.com/ruchirlives/OSC_Client)

## Installation
Grab the latest release of the server .exe and the VST plugin .vst3: [Releases](https://github.com/ruchirlives/OSC_Server/releases)
Put the VST3 plugin in your VST3 plugins folder.
Run the .exe

Load the VST3 plugin in your VST DAW application.
The VST3 Server and plugin use PORTS 8000 and 9000 to communicate using OSC protocol.

## OSC API

OSCDawServer listens for OSC messages on UDP port 8000 and, by default, sends any replies toward `239.255.0.1:9000`. Payloads are routed by instrument tags whenever possible, so every tag you configure in the UI can be used to target one or more instruments from OSC.

### `/orchestra/set_tempo`

- Payload: single numeric argument (int, float or string) that represents the target BPM.
- Updates the host and UI tempo via `PluginManager::setBpm` and `MainComponent::setBpm`.

### `/orchestra`

The first string of every `/orchestra` message picks one of the supported commands, and the arguments that follow are interpreted as described below. Any trailing string arguments are treated as instrument tags and are used to resolve the matching `InstrumentInfo` entries.

- `add_instrument <instrumentName> <pluginInstanceId> <midiChannel> <tag>...`  
  Creates or updates an orchestra entry with the given name, plugin instance ID and 1‑based MIDI channel, plus all supplied tags.
- `get_recorded`  
  Copies the recorded buffer to the MIDI manager (`MidiManager::getRecorded`).
- `select_by_tag <tag>`  
  Selects the first row in the orchestra table whose tag list contains `<tag>`.
- `open_instrument <tag>`  
  Opens the plugin window for the first instrument that advertises `<tag>`.
- `save_project` / `restore_project`  
  Persists or reloads the three state files under `Documents/OSCDawServer/project*`.
- `restore_from_file`  
  Forces the UI to restore the orchestra from the last project file without appending.
- `request_tags`  
  Instructs the host to resend the last tag list (see `/selected/tags` below).

### `/midi/message`

The `/midi/message` listener reads a command name as the first string followed by command‑specific arguments. All **tag arguments** that follow the listed parameters are used to select instruments before the host injects MIDI or performs the request.

- `note_on <note> <velocity> <timestamp> <tag>...`  
  Sends Note On events as soon as the supplied timestamp (string/int/float seconds or milliseconds) allows.
- `note_off <note> <timestamp> <tag>...`  
  Stops notes for the tagged instruments.
- `controller <controllerNumber> <controllerValue> <timestamp> <tag>...`  
  Routes CC messages.
- `controller_ramp <controllerNumber> <startValue> <endValue> <durationSeconds> <timestamp> <tag>...`  
  Schedules a CC ramp that interpolates between `startValue` and `endValue` over `durationSeconds`, beginning at the provided timestamp.
- `channel_aftertouch <value> <timestamp> <tag>...`
- `poly_aftertouch <note> <value> <timestamp> <tag>...`
- `pitchbend <value> <timestamp> <tag>...`
- `program_change <programNumber> <timestamp> <tag>...`
- `save_plugin_data <filePath> <filename> <tag>`  
  Persists plugin settings for the first instrument that matches `<tag>` via `PluginManager::savePluginData`.
- `request_dawServerData <tag>`  
  Triggers the host to reply with the instrument metadata described below.
- `sync_request <timestamp>`  
  Captures the provided timestamp to align the local clock, then resets playback.
- `stop_request`  
  Resets timestamps/playback without extra payload.

### Responses

- `/selected/tags <tag>...`  
  Sent in reply to `request_tags`; contains the last tag list that was dispatched to the client.
- `/dawServerData <tag> <midiChannel> <pluginInstanceId> <pluginName> <instrumentName> <uniqueId>`  
  Emitted when `/midi/message` receives `request_dawServerData` so that an OSC client can learn the details of a tagged instrument.

## Operating the OSCDawServer
1. On first open, Press `Scan` to scan for VST files which might take some time.
2. Highlight the first entry or `Add New Instrument`.
3. `Select a Plugin` menu to place a plugin in the highlighted entry. Then press `Update`. You can click `Open Plugin` to make sure it is loaded.
4. Make sure your MIDI controller is selected in the drop down menu with MIDI input names. You can now preview the highlighted instrument
5. Right click on Tags to select new Tags which you can match in the VST3 Client Plugin to send to this instrument.
6. Right click on the other columns of the entry for various other features and options.

## Compiling

1. Install JUCE (https://juce.com/get-juce)
2. Clone or download this repository
3. Open `OSCDAWServer.jucer` in the Projucer (part of JUCE)
4. Click "Save Project" to generate platform-specific build files (e.g. `.sln` for Visual Studio)
5. Open the generated `.sln` file in Visual Studio and build the project
