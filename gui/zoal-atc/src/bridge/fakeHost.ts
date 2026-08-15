// An in-memory stand-in for window.skyscript.
//
// Everything the panel does goes through this one object, so a fake of it is
// enough to test the whole page without X-Plane, without the plugin and without
// a console. Kept beside the real thing rather than in a test folder because it
// is part of the bridge's contract: if the host shape changes, both move.

import type {ConsoleEnvelope, RequestAck, SkyscriptHost} from "./types";
import {EVENT_CHANNEL, READY_CHANNEL, REQUEST_CHANNEL} from "./types";

export type RecordedPost = {
  channel: string;
  payload: unknown;
};

export type RecordedRequest = {
  action: string;
  payload: unknown;
  requestId: number;
};

export class FakeSkyscriptHost implements SkyscriptHost {
  readonly posts: RecordedPost[] = [];
  readonly requests: RecordedRequest[] = [];
  readonly listeners = new Map<string, Array<(payload: unknown) => void>>();

  // Set to refuse the next request, the way the plugin does when the console is
  // disconnected or too many requests are in flight.
  refusal: string | undefined;
  // Set to make console.ready reject, the way a dead host does.
  readyError: Error | undefined;
  // Replayed to the event listener the moment it announces readiness, which is
  // what the plugin does with its cached snapshot.
  replayOnReady: ConsoleEnvelope[] = [];
  // Answers a request *before* handing back the receipt, which is the order the
  // plugin answers a local action in: GuiBridge::submit queues the response on
  // the js channel and only then returns the ack (gui_bridge.cpp). Modelled here
  // because that ordering is a real hazard for the panel, not a test artifact —
  // an answer that arrives before the receipt has nothing waiting for it yet.
  answerBeforeAck: ((action: string) => unknown | undefined) | undefined;

  private nextRequestId = 1;

  async postMessage(channel: string, payload: unknown): Promise<unknown> {
    this.posts.push({channel, payload});

    if (channel === READY_CHANNEL) {
      if (this.readyError) {
        throw this.readyError;
      }
      for (const envelope of this.replayOnReady) {
        this.emit(envelope);
      }
      return {ok: true, replayed: this.replayOnReady.length};
    }

    if (channel !== REQUEST_CHANNEL) {
      return null;
    }

    const request = payload as {action: string; payload: unknown};
    if (this.refusal !== undefined) {
      const error = this.refusal;
      this.refusal = undefined;
      return {accepted: false, request_id: 0, error} satisfies RequestAck;
    }

    const requestId = this.nextRequestId++;
    this.requests.push({action: request.action, payload: request.payload, requestId});

    if (this.answerBeforeAck) {
      const answer = this.answerBeforeAck(request.action);
      if (answer !== undefined) {
        this.respondTo(requestId, answer);
      }
    }

    return {accepted: true, request_id: requestId, error: ""} satisfies RequestAck;
  }

  onMessage(channel: string, callback: (payload: unknown) => void): void {
    const existing = this.listeners.get(channel) ?? [];
    existing.push(callback);
    this.listeners.set(channel, existing);
  }

  // --- test drivers --------------------------------------------------------

  emit(envelope: ConsoleEnvelope): void {
    for (const listener of this.listeners.get(EVENT_CHANNEL) ?? []) {
      listener(envelope);
    }
  }

  emitEvent(event: string, payload: unknown): void {
    this.emit({kind: "event", event, payload});
  }

  emitStatus(status: {
    connected: boolean;
    subscribed: boolean;
    pending?: number;
    droppedEvents?: number;
  }): void {
    this.emit({
      kind: "status",
      connected: status.connected,
      subscribed: status.subscribed,
      pending: status.pending ?? 0,
      dropped_events: status.droppedEvents ?? 0,
    });
  }

  respondTo(requestId: number, payload: unknown): void {
    this.emit({kind: "response", request_id: requestId, ok: true, payload, error: ""});
  }

  failRequest(requestId: number, error: string): void {
    this.emit({kind: "response", request_id: requestId, ok: false, payload: null, error});
  }

  // The id of the most recent accepted request, so a test can answer it without
  // tracking ids itself.
  get lastRequestId(): number {
    const last = this.requests[this.requests.length - 1];
    return last ? last.requestId : 0;
  }

  // The order channels were first posted on. start() must subscribe before it
  // announces readiness, and this is how a test sees that it did.
  get postedChannels(): string[] {
    return this.posts.map((post) => post.channel);
  }
}
