import {createContext, useContext, useEffect, useMemo, useRef, useState} from "react";
import type {ReactNode} from "react";

import {ConsoleBridge} from "../bridge/ConsoleBridge";
import {ACTIONS} from "../bridge/actions";
import {errorMessage, waitForSkyscriptHost} from "../bridge/skyscript";
import {EVENTS} from "../bridge/types";
import {ConsoleStore} from "../store/ConsoleStore";

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
