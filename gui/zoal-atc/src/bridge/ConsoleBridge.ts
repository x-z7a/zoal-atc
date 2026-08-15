import {currentSkyscriptHost, errorMessage, panelWarn} from "./skyscript";
import {
  EVENT_CHANNEL,
  READY_CHANNEL,
  REQUEST_CHANNEL,
  type ConsoleEnvelope,
  type ConsoleStatus,
  type RequestAck,
  type SkyscriptHost,
} from "./types";

export type EventHandler = (payload: unknown) => void;
export type StatusHandler = (status: ConsoleStatus) => void;
export type Unsubscribe = () => void;

type PendingRequest = {
  resolve: (payload: unknown) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
};

// An answer that arrived before anything was waiting for it.
type EarlyAnswer = {
  ok: boolean;
  payload: unknown;
  error: string;
  timer: ReturnType<typeof setTimeout>;
};

// Longer than the plugin's own 10s request timeout (gui_bridge.hpp) on purpose.
// The plugin's timeout produces a specific error the pilot can act on, and it
// should win in the normal case. This one is the backstop for the case the
// plugin cannot report: an ack that arrived but whose response never will,
// because the socket went away or the page reloaded underneath it.
const REQUEST_TIMEOUT_MS = 15000;

// The receipt is the host's return value, and nothing underneath it guarantees
// one. Without this bound a host that accepts a post and never answers leaves
// the promise pending for the life of the page, and every control the caller
// disabled while it waited stays disabled — which is how a mistyped token can
// take away the field that fixes it.
const ACK_TIMEOUT_MS = 5000;

// How long an answer waits for the request it belongs to. The plugin queues a
// local action's response *before* it returns the receipt (gui_bridge.cpp), so
// the answer can reach the page while `request` is still awaiting the ack.
// Dropping it there is silent and unrecoverable: the caller then waits out the
// full request timeout for something that already arrived.
const EARLY_ANSWER_TTL_MS = 30000;

// Rejects if `promise` has not settled in `ms`. Used where the far side is the
// Skyscript host itself, which has no timeout of its own to fall back on.
function withTimeout<T>(promise: Promise<T>, ms: number, message: string): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(message)), ms);
    promise.then(
      (value) => {
        clearTimeout(timer);
        resolve(value);
      },
      (error: unknown) => {
        clearTimeout(timer);
        reject(error instanceof Error ? error : new Error(String(error)));
      },
    );
  });
}

export class ConsoleBridge {
  private readonly host: SkyscriptHost | undefined;
  private readonly pending = new Map<number, PendingRequest>();
  private readonly earlyAnswers = new Map<number, EarlyAnswer>();
  private readonly eventHandlers = new Map<string, Set<EventHandler>>();
  private readonly statusHandlers = new Set<StatusHandler>();
  private started = false;
  private disposed = false;

  constructor(host: SkyscriptHost | undefined = currentSkyscriptHost()) {
    this.host = host;
  }

  // available reports whether a Skyscript host is present at all. Opened in an
  // ordinary browser there is none, and the panel says so rather than
  // pretending to be connected.
  get available(): boolean {
    return this.host !== undefined;
  }

  // start subscribes to console.event *before* announcing readiness. The plugin
  // replays its cached snapshot as part of handling console.ready, so a
  // listener registered afterwards would miss the first frame of everything.
  async start(): Promise<void> {
    if (this.started || this.disposed) {
      return;
    }
    if (!this.host) {
      panelWarn("bridge start skipped", {reason: "no Skyscript host"});
      return;
    }
    this.started = true;
    this.host.onMessage(EVENT_CHANNEL, (payload) => {
      this.dispatch(payload as ConsoleEnvelope);
    });
    try {
      await this.host.postMessage(READY_CHANNEL, {});
    } catch (error) {
      panelWarn("ready post failed", {channel: READY_CHANNEL, error: errorMessage(error)});
      throw error;
    }
  }

  // on registers a listener and returns the function that removes it.
  //
  // Several listeners per event is the whole point: with the panel split into
  // components, Home and Debug both want telemetry. This used to be one handler
  // per event name in a Map, so the second registration silently replaced the
  // first and one of the two views simply stopped updating.
  on(event: string, handler: EventHandler): Unsubscribe {
    let handlers = this.eventHandlers.get(event);
    if (!handlers) {
      handlers = new Set();
      this.eventHandlers.set(event, handlers);
    }
    handlers.add(handler);

    return () => {
      const current = this.eventHandlers.get(event);
      if (!current) {
        return;
      }
      current.delete(handler);
      if (current.size === 0) {
        this.eventHandlers.delete(event);
      }
    };
  }

  onStatus(handler: StatusHandler): Unsubscribe {
    this.statusHandlers.add(handler);
    return () => {
      this.statusHandlers.delete(handler);
    };
  }

  // request asks the console for something and resolves with its payload. It
  // rejects when the console refuses, when the link is down, or when nothing
  // answers - the panel is never left holding a promise that will not settle.
  async request(action: string, payload?: unknown): Promise<unknown> {
    if (!this.host) {
      throw new Error("no Skyscript host");
    }
    if (this.disposed) {
      throw new Error("panel is shutting down");
    }
    // Responses arrive on the event channel, which start() subscribes to. A
    // request made before then would be accepted and then wait for an answer
    // nothing is listening for, so say so now rather than time out in fifteen
    // seconds looking like a dead console. start() registers the listener
    // synchronously before it awaits, so a request made while it is still in
    // flight is fine.
    if (!this.started) {
      throw new Error("bridge not started");
    }

    const ack = (await withTimeout(
      this.host.postMessage(REQUEST_CHANNEL, {action, payload: payload ?? null}),
      ACK_TIMEOUT_MS,
      "the panel host did not acknowledge the request",
    )) as RequestAck | null;

    if (!ack || !ack.accepted) {
      panelWarn("request refused", {action, ack});
      throw new Error(ack?.error || "request refused");
    }

    // dispose() settles what is in `pending`, and until the receipt arrives
    // this request is not in it yet. Re-checking here is what keeps a request
    // made just before shutdown from outliving the page that made it.
    if (this.disposed) {
      throw new Error("panel is shutting down");
    }

    // The answer may already be here: the plugin queues a local action's
    // response before it hands back the receipt we just awaited.
    const early = this.earlyAnswers.get(ack.request_id);
    if (early) {
      this.earlyAnswers.delete(ack.request_id);
      clearTimeout(early.timer);
      if (!early.ok) {
        throw new Error(early.error || "request failed");
      }
      return early.payload;
    }

    return new Promise<unknown>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(ack.request_id);
        reject(new Error("no answer from the console"));
      }, REQUEST_TIMEOUT_MS);

      this.pending.set(ack.request_id, {resolve, reject, timer});
    });
  }

  // dispose settles everything still waiting and stops dispatching.
  //
  // Skyscript's onMessage has no counterpart, so the host callback registered
  // in start() outlives us; the disposed flag is what keeps a late envelope
  // from reaching handlers whose components are gone.
  dispose(): void {
    this.disposed = true;
    for (const [, waiting] of this.pending) {
      clearTimeout(waiting.timer);
      waiting.reject(new Error("panel is shutting down"));
    }
    this.pending.clear();
    for (const [, held] of this.earlyAnswers) {
      clearTimeout(held.timer);
    }
    this.earlyAnswers.clear();
    this.eventHandlers.clear();
    this.statusHandlers.clear();
  }

  // Parks an answer nothing is waiting for yet, on a timer so a page that
  // never claims it does not accumulate them.
  private holdEarlyAnswer(
    requestId: number,
    answer: {ok: boolean; payload: unknown; error: string},
  ): void {
    const existing = this.earlyAnswers.get(requestId);
    if (existing) {
      clearTimeout(existing.timer);
    }
    const timer = setTimeout(() => {
      this.earlyAnswers.delete(requestId);
    }, EARLY_ANSWER_TTL_MS);
    this.earlyAnswers.set(requestId, {...answer, timer});
  }

  private settle(requestId: number): PendingRequest | undefined {
    const waiting = this.pending.get(requestId);
    if (!waiting) {
      return undefined;
    }
    this.pending.delete(requestId);
    clearTimeout(waiting.timer);
    return waiting;
  }

  private dispatch(envelope: ConsoleEnvelope | null): void {
    if (this.disposed) {
      return;
    }
    if (!envelope || typeof envelope !== "object") {
      panelWarn("ignored non-object console envelope", {envelope});
      return;
    }

    switch (envelope.kind) {
      case "response": {
        const waiting = this.settle(envelope.request_id);
        if (!waiting) {
          // Either the answer overtook its own receipt — hold it, `request`
          // will claim it in a moment — or it belongs to a request made before
          // a reload, in which case the TTL discards it.
          this.holdEarlyAnswer(envelope.request_id, {
            ok: envelope.ok,
            payload: envelope.payload,
            error: envelope.error,
          });
          return;
        }
        if (envelope.ok) {
          waiting.resolve(envelope.payload);
        } else {
          waiting.reject(new Error(envelope.error || "request failed"));
        }
        return;
      }
      case "event": {
        this.emit(this.eventHandlers.get(envelope.event), envelope.payload);
        return;
      }
      case "status": {
        this.emit(this.statusHandlers, {
          connected: envelope.connected,
          subscribed: envelope.subscribed,
          pending: envelope.pending,
          droppedEvents: envelope.dropped_events,
        });
        return;
      }
    }
  }

  // emit copies before iterating, because a handler is allowed to unsubscribe
  // itself, and isolates throws, because one broken listener must not stop the
  // rest of the panel from hearing the radio.
  private emit<T>(handlers: Set<(value: T) => void> | undefined, value: T): void {
    if (!handlers || handlers.size === 0) {
      return;
    }
    for (const handler of [...handlers]) {
      try {
        handler(value);
      } catch (error) {
        panelWarn("listener threw", {error: errorMessage(error)});
      }
    }
  }
}
