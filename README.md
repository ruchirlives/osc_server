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
