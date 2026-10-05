import {createContext, useContext, useEffect, useMemo, useRef, useState} from "react";
import type {ReactNode} from "react";

import {ConsoleBridge} from "../bridge/ConsoleBridge";
import {ACTIONS} from "../bridge/actions";
import {errorMessage, waitForSkyscriptHost} from "../bridge/skyscript";
import {EVENTS} from "../bridge/types";
import type {CommLogEntry} from "../bridge/views";
import {COMM_LOG_LIMIT, ConsoleStore} from "../store/ConsoleStore";

// How the panel came up, which is the first thing to know when it is showing
// nothing. Opened in an ordinary browser there is no host and never will be;
// inside X-Plane the host arrives a moment after the page does.
export type BootState = "waiting-for-host" | "no-host" | "starting" | "ready" | "host-error";

const SKYSCRIPT_HOST_WAIT_MS = 5000;

type ConsoleContextValue = {
  store: ConsoleStore;
  bridge: ConsoleBridge | undefined;
  boot: BootState;
  bootDetail: string;
};

const ConsoleContext = createContext<ConsoleContextValue | undefined>(undefined);

export function useConsole(): ConsoleContextValue {
  const value = useContext(ConsoleContext);
  if (!value) {
    throw new Error("useConsole must be used inside a ConsoleProvider");
  }
  return value;
}

type Props = {
  children: ReactNode;
  // Injected by tests. Left out, the provider waits for the real Skyscript host
  // and owns the bridge it builds -- including disposing it.
  bridge?: ConsoleBridge;
  hostTimeoutMs?: number;
};

export function ConsoleProvider({children, bridge: injected, hostTimeoutMs}: Props) {
  const storeRef = useRef<ConsoleStore>(undefined as unknown as ConsoleStore);
  if (!storeRef.current) {
    storeRef.current = new ConsoleStore();
  }
  const store = storeRef.current;

  const [bridge, setBridge] = useState<ConsoleBridge | undefined>(injected);
  const [boot, setBoot] = useState<BootState>(injected ? "starting" : "waiting-for-host");
  const [bootDetail, setBootDetail] = useState("");

  useEffect(() => {
    let cancelled = false;
    let detach = () => {};
    // Only a bridge this effect created is ours to dispose. An injected one
    // belongs to whoever passed it in.
    let created: ConsoleBridge | undefined;
    let unwatch = () => {};
    let syncing = false;

    // resyncComm asks the console for the whole conversation and puts it in
    // the chat window. The window only hears lines pushed while it is open, and
    // the console has been keeping every one of them regardless.
    async function resyncComm(active: ConsoleBridge): Promise<void> {
      if (syncing || cancelled) {
        return;
      }
      syncing = true;
      store.beginCommSync();
      try {
        const entries = await active.request(ACTIONS.commLog, {limit: COMM_LOG_LIMIT});
        if (cancelled) {
          return;
        }
        if (Array.isArray(entries)) {
          store.finishCommSync(entries as CommLogEntry[]);
        } else {
          store.abandonCommSync();
        }
      } catch {
        // A console that cannot answer leaves the window showing what it has,
        // and the next reconnect or reopen asks again.
        store.abandonCommSync();
      } finally {
        syncing = false;
      }
    }

    // watchForMissedLines resyncs when the panel may have missed something:
    // the console link coming back, or the plugin reporting that events were
    // replaced before anybody saw them -- which is what a closed window is.
    function watchForMissedLines(active: ConsoleBridge): () => void {
      let previous = store.getStatus();
      return store.subscribe(() => {
        const status = store.getStatus();
        if (status === previous) {
          return;
        }
        const linked = status.connected && status.subscribed;
        const wasLinked = previous.connected && previous.subscribed;
        const missed = status.droppedEvents > previous.droppedEvents;
        previous = status;
        if (linked && (!wasLinked || missed)) {
          void resyncComm(active);
        }
      });
    }

    async function boot(): Promise<void> {
      let active = injected;
      if (!active) {
        const host = await waitForSkyscriptHost(hostTimeoutMs ?? SKYSCRIPT_HOST_WAIT_MS);
        if (cancelled) {
          return;
        }
        active = new ConsoleBridge(host);
        created = active;
      }

      if (!active.available) {
        setBoot("no-host");
        return;
      }

      detach = store.attach(active);
      setBridge(active);
      setBoot("starting");

      try {
        // start() subscribes before it announces readiness, because the plugin
        // replays its cached snapshot inside the ready handler.
        await active.start();
        if (cancelled) {
          return;
        }
        setBoot("ready");

        // A page that has just loaded holds none of the conversation so far.
        unwatch = watchForMissedLines(active);
        void resyncComm(active);

        // The flight plan is not published on a schedule -- it changes when
        // somebody imports one -- so the panel asks once on the way up.
        const plan = await active.request(ACTIONS.flightPlan);
        if (!cancelled) {
          store.setEvent(EVENTS.flightPlan, plan);
        }
      } catch (error) {
        if (!cancelled) {
          setBoot((previous) => (previous === "ready" ? previous : "host-error"));
          setBootDetail(errorMessage(error));
        }
      }
    }

    void boot();

    return () => {
      cancelled = true;
      unwatch();
      detach();
      created?.dispose();
    };
  }, [injected, hostTimeoutMs, store]);

  const value = useMemo(
    () => ({store, bridge, boot, bootDetail}),
    [store, bridge, boot, bootDetail],
  );

  return <ConsoleContext.Provider value={value}>{children}</ConsoleContext.Provider>;
}
