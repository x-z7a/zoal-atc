// What the panel currently knows, held outside React.
//
// The bridge is a stream: it delivers an event and forgets it. That was fine
// when the page was one screen built once, but a tabbed panel mounts views
// later than the events they need. facility_snapshot, for instance, is only
// published when an aircraft connects or disconnects -- so a Debug tab opened
// mid-flight would show an empty bay until somebody else took off.
//
// So the store keeps the last payload of each event and hands it to whoever
// asks, whenever they ask. The plugin already does exactly this on its side,
// replaying its cache when the page announces readiness (gui_bridge.cpp,
// replay_locked); this is the same idea one layer up.
//
// It holds no React. Snapshots are replaced rather than mutated so that
// useSyncExternalStore can compare them by identity: a slice that did not
// change keeps its reference, and the components reading it do not re-render.

import type {ConsoleBridge, Unsubscribe} from "../bridge/ConsoleBridge";
import {EVENTS, type ConsoleStatus} from "../bridge/types";
import type {CommLogEntry} from "../bridge/views";

// The log is capped in the page as well as in the console. A panel left open
// for a long flight must not grow without bound inside the sim's process.
export const COMM_LOG_LIMIT = 100;

const DISCONNECTED: ConsoleStatus = {
  connected: false,
  subscribed: false,
  pending: 0,
  droppedEvents: 0,
};

export class ConsoleStore {
  private readonly events = new Map<string, unknown>();
  private readonly listeners = new Set<() => void>();
  private comm: readonly CommLogEntry[] = [];
  // Lines pushed while a resync is in flight. The console read its record at
  // some moment between the request and the answer, so a line pushed in that
  // window may or may not be in what comes back; it is kept here and added to
  // the answer unless the answer already has it.
  private arrivedDuringSync: CommLogEntry[] | null = null;
  private status: ConsoleStatus = DISCONNECTED;

  subscribe = (listener: () => void): Unsubscribe => {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  };

  // getEvent returns the same reference until that event arrives again, which
  // is what lets a reader bail out of re-rendering.
  getEvent = (event: string): unknown => this.events.get(event);

  getCommLog = (): readonly CommLogEntry[] => this.comm;

  getStatus = (): ConsoleStatus => this.status;

  setEvent(event: string, payload: unknown): void {
    if (event === EVENTS.commLog) {
      this.appendComm(payload as CommLogEntry | null);
      return;
    }
    this.events.set(event, payload);
    this.notify();
  }

  setStatus(status: ConsoleStatus): void {
    this.status = status;
    this.notify();
  }

  // attach wires a bridge's events into the store and returns the function that
  // unwires them.
  attach(bridge: ConsoleBridge): Unsubscribe {
    const stops: Unsubscribe[] = [];

    for (const event of Object.values(EVENTS)) {
      stops.push(bridge.on(event, (payload) => this.setEvent(event, payload)));
    }
    stops.push(bridge.onStatus((status) => this.setStatus(status)));

    return () => {
      for (const stop of stops) {
        stop();
      }
    };
  }

  // clearComm empties the chat window. "New flight" is a different journey
  // under the same flight ID, and the console clears its record of the last
  // one; the window only ever appends what is pushed to it, so it has to be
  // told to start again too.
  clearComm(): void {
    if (this.comm.length === 0) {
      return;
    }
    this.comm = [];
    this.notify();
  }

  // beginCommSync marks the moment the panel asked the console for its whole
  // record of the conversation.
  //
  // The window only hears what is pushed to it while it is open. Close it and
  // the plugin stops delivering events and keeps just the newest of each name,
  // so a pilot who talked to three controllers with the panel shut came back to
  // one line. The console has kept every line all along (it is the radio
  // record), so the page asks for it rather than trusting what it was shown.
  beginCommSync(): void {
    this.arrivedDuringSync = [];
  }

  // finishCommSync replaces the window with the console's record, plus any line
  // pushed since the request that the record does not already hold.
  finishCommSync(entries: readonly CommLogEntry[]): void {
    const since = this.arrivedDuringSync ?? [];
    this.arrivedDuringSync = null;
    const next: CommLogEntry[] = [];
    for (const entry of [...entries, ...since]) {
      if (entry && !holds(next, entry)) {
        next.push(entry);
      }
    }
    this.comm = next.length > COMM_LOG_LIMIT ? next.slice(next.length - COMM_LOG_LIMIT) : next;
    this.notify();
  }

  // abandonCommSync leaves the window as it was: a resync that failed has
  // nothing better to offer than what the page already shows.
  abandonCommSync(): void {
    this.arrivedDuringSync = null;
  }

  private appendComm(entry: CommLogEntry | null): void {
    if (!entry) {
      return;
    }
    this.arrivedDuringSync?.push(entry);
    if (holds(this.comm, entry)) {
      // The plugin replays the newest line every time the window is shown, and
      // a line the window already has is not a second transmission.
      return;
    }
    const next = [...this.comm, entry];
    this.comm = next.length > COMM_LOG_LIMIT ? next.slice(next.length - COMM_LOG_LIMIT) : next;
    this.notify();
  }

  private notify(): void {
    for (const listener of [...this.listeners]) {
      listener();
    }
  }
}

// holds reports a line the log already has. Only a line stamped with when it
// was said can be recognised again: two unstamped lines with the same words may
// genuinely be two transmissions.
function holds(log: readonly CommLogEntry[], entry: CommLogEntry): boolean {
  if (!entry.at) {
    return false;
  }
  return log.some(
    (held) => held.at === entry.at && held.kind === entry.kind && held.text === entry.text,
  );
}
