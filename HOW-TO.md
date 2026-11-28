# How to use zoal-atc

This guide is for pilots, not developers. It covers getting the plugin, setting
it up, and flying a flight with it. If you want the technical details, see
[INSTALL.md](INSTALL.md) and [DEVELOPMENT.md](DEVELOPMENT.md).

zoal-atc gives X-Plane 12 voice air traffic control. You talk to ATC with your
push-to-talk key, and controllers answer over your COM radios.

---

## 1. Download

Get the newest version from the
**[latest release page](https://github.com/x-z7a/zoal-atc/releases/latest)**.

Under **Assets**, download the zip for your computer:

| Your computer | File to download |
| --- | --- |
| Mac (Apple Silicon) | `zoal-atc-macos-arm64.zip` |
| Windows | `zoal-atc-windows-x64.zip` |
| Linux | `zoal-atc-linux-x64.zip` |

The download is large (around 600 MB unzipped). Most of that is the speech
models, which run on your own computer.

## 2. Install

The full steps are in [INSTALL.md](INSTALL.md#install). In short:

1. Unzip the download. You get a folder called `zoal-atc`.
2. Move that folder into `<your X-Plane folder>/Resources/plugins/`.
   If you have an older version there, **delete it first**. Don't merge the
   two folders.
3. Start X-Plane. Go to **Settings → Keyboard** (or **Joystick**), search for
   `zoal-atc`, and bind **"zoal-atc: Push-to-Talk"** to a key or a button.
   You hold it to talk and let go when you are done.
4. You should see the zoal-atc **HUD** in the top right corner of the screen.
   The full **panel** opens from **Plugins → zoal-atc → Toggle Panel**, or
   from **Open panel** in the HUD menu. Drag the HUD's bar to put it anywhere
   on the screen. In the panel, **Settings → Appearance → HUD** chooses
   **Fixed** (the whole HUD, always) or **Minimized** (only its bar, opening
   when you point at it or a controller calls you), and **Move back to the
   corner** puts it back where it started.

You need **X-Plane 12.4.4 or newer**. On a Mac, allow X-Plane to use the
microphone when asked. You can also turn this on later under System Settings →
Privacy & Security → Microphone.

### Updating

zoal-atc works with the **Skunkcrafts Updater**. Once you have installed a
release zip, the updater finds the `skunkcrafts_updater.cfg` in the `zoal-atc`
folder and keeps the plugin up to date for you. The voice and speech models
are not part of these updates, and you don't need to do anything about them:
each time X-Plane starts, the plugin checks its models and, if one is missing
or older than the plugin expects, downloads it in the background while X-Plane
carries on loading. Until it arrives, ATC can't hear you (speech model) or
can't speak to you (voices); everything else works. Progress is written to
`Log.txt` on lines starting `[zoal-atc] models:`.

On a Mac, the plugin is signed and notarized by Apple, so X-Plane loads it
without a security warning.

## 3. First-time setup and requirements

### What you need

- **A SimBrief account (required).** ATC learns your flight from your SimBrief
  flight plan: your route, cruise altitude, departure and destination. Without
  one, ATC has nothing to clear you on. A free account at
  [simbrief.com](https://www.simbrief.com/) is enough.
- **An IFR flight plan.** Only IFR flights are supported for now. VFR is not
  supported yet.
- **Up-to-date navigation data (recommended).** We recommend using the latest
  AIRAC cycle, both in SimBrief and in X-Plane (for example through
  Navigraph). ATC assigns current departure and arrival procedures and
  runways. If your sim or your aircraft's FMS has old data, a procedure ATC
  gives you may be missing or different.

### Connect your SimBrief account

1. Open the panel (**Plugins → zoal-atc → Toggle Panel**).
2. Go to **Settings → Flight plan**.
3. Enter your **SimBrief Pilot ID**. This is a number, and you can find it on
   SimBrief under *Account Settings*. It is not your username.
4. Click **Import now**. Your filed plan should show under *Imported plan*.

You only need to enter the Pilot ID once. The plugin remembers it.

### Settings you can ignore

The panel also has a **Connection** section (console address and access
token), and there is a config file in X-Plane's `Output/preferences` folder.
**Most pilots never need to change these.** Out of the box the plugin connects
to our hosted ATC server. Those settings are only for people who run their own
server (see [section 9](#9-running-the-server-yourself)).

The only other setting you might want is the **theme** (Graphite, Daylight or
Night) under **Settings → Appearance**.

If the sim stutters each time you let go of push-to-talk (on a Mac), set
**Settings → Speech recognition → Runs on** to **CPU**. If ATC often mishears
you and your computer has memory to spare, try a larger **Model** there; it is
downloaded once and used from the next start. See
[Speech recognition](INSTALL.md#speech-recognition).

## 4. Starting a flight

The callsign ATC uses is **taken from your SimBrief flight plan**. If you file
as `DAL123`, ATC calls you "Delta one two three". To change your callsign,
change it in SimBrief and file again.

### 4.1 If you file before starting X-Plane (easiest)

1. File your IFR flight plan on SimBrief and generate the OFP.
2. Start X-Plane and load your aircraft at the departure airport.
3. Look at the **HUD in the top right**. Check that it shows **your callsign**
   and that the console is connected. If it does, ATC has your plan and you
   are ready to call.

If the callsign is missing or wrong, open the HUD menu and click **Reload
plan**.

### 4.2 If X-Plane is already running

1. File your new flight plan on SimBrief.
2. In the **HUD menu**, click **New flight**, then click **Confirm**. This tells
   ATC you are on a new flight and clears the old radio log.
3. Click **Reload plan** to fetch the plan you just filed.
4. Check that the HUD shows your new callsign.

When you load a new aircraft or move to a different airport, the plugin
usually starts a new flight and reloads your plan on its own. If the HUD still
shows your old flight, do the steps above.

## 5. The basic flow of a flight

zoal-atc works like real-world IFR. You call each controller in turn, and each
one hands you to the next.

1. **Get your IFR clearance.** Tune your COM radio and call:
   - **Clearance Delivery**, if the airport has one.
   - **Ground**, if there is no delivery frequency.
   - **Center**, if you are at an uncontrolled airport with no tower. Center
     gives you the clearance.

   Example: *"Boston Clearance, Delta one two three, IFR to Atlanta,
   with information Alpha."*
2. **Read back the clearance** the way a real pilot would. The controller
   tells you if something was wrong.
3. **Follow the controllers.** When a controller says *"contact Tower one
   one niner point five"*, read it back, tune the new frequency, and check in.
   You will go through Ground, Tower, Departure, Center, Approach, Tower and
   Ground again, depending on the airport.
4. **Use normal radio procedure.** Wait for the frequency to be quiet, keep
   your calls short, start with who you are calling and who you are, and read
   back headings, altitudes, frequencies and clearances.

Tips:

- Speak clearly at a normal pace. Hold the key a moment before you start
  talking, and let go only after you have finished.
- You hear a controller on the COM radio tuned to that controller's
  frequency, so that radio's volume knob sets how loud they are.
- What you said and what ATC answered also show in the HUD and in the panel's
  radio log. If the plugin heard you wrong, the log shows it.
- You can also **type** a call in the panel's composer instead of speaking.

## 6. Which country's procedures?

ATC is modelled mainly on **US (FAA) and Canadian (NAV CANADA)** procedures and
phraseology. Flights in the US and Canada sound and work most like the real
thing.

You can fly anywhere. We make a best effort at local phraseology in other
countries. However, the procedures underneath are still mostly FAA/NAV CANADA
style, so expect some differences from how ATC really works there. This is a
hobby project, not a real ATC trainer. Please don't use it as a reference for
real-world flying.

## 7. Controller voices and accents

Controller voices are made on your own computer by a speech model called
Kokoro. Nothing is sent to the cloud for this. Here is how it works:

- **Accent by country.** The ATC server picks a voice that fits the country of
  the airport you are talking to. The plugin includes **74 voices**:
  - American and British English voices.
  - English spoken with Spanish, French, Hindi (Indian), Italian, Japanese,
    Portuguese (Brazilian) and Mandarin Chinese accents.
  - Extra voices blended from two speakers, for accents with only a few
    natural voices.
- **One voice per controller.** Each controller position keeps its own voice,
  so Ground, Tower and Departure sound like different people. A controller
  doesn't change voice partway through your flight.
- **Sounds like a radio.** Voices are filtered to sound like a VHF radio.
  Each voice is tuned to a normal controller's speaking speed.
- **Says aviation words properly.** Words like "niner", "ILS", "RNAV" and
  "VOR" are taught to the voice so they come out correctly.
- **British pronunciation where it fits.** British and Indian voices use
  British pronunciation.

The voices are built once, the first time you start the plugin. Nothing is
downloaded for this, but the `zoal-atc` plugin folder must be writable.

### Reply language

In the panel's **Settings**, **Reply language** picks the language controllers
answer you in: English, Español or Français. Changing it reconnects to the ATC
server.

- **Only replies change.** Push-to-talk and typed calls are still **English
  only**: speak and type to ATC in English, whatever language they answer in.
- **Every controller.** Delivery, Ground, Tower, Departure, Approach, Center
  and Oceanic all answer in the language you pick. Airport, fix and position
  names are said as published.
- **Voices to match.** A reply in Spanish or French is read by a Spanish or
  French speaker. **My voice** then lists only that language's speakers.
- German is not available yet: the speech model has no German voice.

## 8. Getting help

- **GitHub:** report bugs or ask questions at
  [github.com/x-z7a/zoal-atc/issues](https://github.com/x-z7a/zoal-atc/issues).
- [Discord](https://discord.gg/wWbTuueRdC)

GitHub and Discord are the best places to reach us. I also check the forums
and threads where I've posted about zoal-atc, but less often.

A good bug report includes:

- What you said and what ATC answered. The panel's radio log shows both.
- The departure and arrival airports, and your SimBrief route.
- The lines starting with `[zoal-atc]` from X-Plane's `Log.txt`, which is in
  your X-Plane folder.
- Which FAA/NavCan (or your local authority) doc says what ATC did was wrong. 

For common problems (no replies, push-to-talk does nothing, ATC won't clear
you), see [Troubleshooting](INSTALL.md#troubleshooting) first.

## 9. Running the server yourself

By default, zoal-atc uses our hosted ATC server, so you don't need to run
anything.

If you would rather run the ATC server on your own hardware, and you are
comfortable with **Docker, Redis and Ollama**, contact us directly on GitHub or
Discord and we will give you the server binary. Plan to run it on a different
computer from the one running X-Plane, because both need a lot of power.

The server is **not open source yet**. We expect another big rewrite to add
VFR features, so we're holding off for now. If you really want to see the
code, get in touch.
