import {useCallback, useEffect, useState} from "react";

import {ACTIONS} from "../bridge/actions";
import {EVENTS} from "../bridge/types";
import type {
  ATCReplyView,
  BayView,
  DebugRecordView,
  SessionView,
  TranscriptView,
} from "../bridge/views";
import {BayTable} from "../components/BayTable";
import {DebugTail} from "../components/DebugTail";
import {JsonBlock} from "../components/JsonBlock";
import {describeStatus} from "../domain/status";
import {useConsole} from "../state/ConsoleProvider";
import {useConsoleAction, useConsoleEvent, useConsoleStatus} from "../state/hooks";

type Props = {
  pluginVersion: string;
  skyscript: string;
};

// Why the panel is doing what it is doing.
//
// Everything here was either buried on the single old screen or already
// arriving and thrown away: the console publishes transcript and atc_reply
// events that the pre-React panel never handled, and a full row per aircraft on
// every facility_snapshot where only the count was read.
export function DebugTab({pluginVersion, skyscript}: Props) {
  const {boot, bootDetail} = useConsole();
  const status = useConsoleStatus();
  const session = useConsoleEvent<SessionView>(EVENTS.flightSnapshot);
  const bay = useConsoleEvent<BayView>(EVENTS.facilitySnapshot);
  const transcript = useConsoleEvent<TranscriptView>(EVENTS.transcript);
  const reply = useConsoleEvent<ATCReplyView>(EVENTS.atcReply);
  const {run} = useConsoleAction();

  const [tail, setTail] = useState<readonly DebugRecordView[]>([]);
  const [category, setCategory] = useState("");
  const [loadingTail, setLoadingTail] = useState(false);

  // Pulled rather than pushed, and only while this tab is mounted. The log is
  // verbose and nobody wants it streaming into the sim's frame budget behind a
  // tab they are not looking at.
  const loadTail = useCallback(
    (next: string) => {
      if (boot !== "ready") {
        return;
      }
      setLoadingTail(true);
      void run(ACTIONS.debugTail, {category: next, limit: 200})
        .then((payload) => setTail((payload as DebugRecordView[] | null) ?? []))
        .catch(() => {})
        .finally(() => setLoadingTail(false));
    },
    [boot, run],
  );

  useEffect(() => {
    loadTail(category);
  }, [category, loadTail]);

  return (
    <>
      <section className="feed" aria-label="Panel health">
        <h2 className="section-title">Panel</h2>
        <dl className="kv">
          <dt>Plugin</dt>
          <dd>{pluginVersion}</dd>
          <dt>Skyscript</dt>
          <dd>{skyscript}</dd>
          <dt>Boot</dt>
          <dd>{bootDetail ? `${boot} (${bootDetail})` : boot}</dd>
          <dt>Console</dt>
          <dd>{describeStatus(status)}</dd>
          <dt>Requests in flight</dt>
          <dd>{status.pending}</dd>
          {/* Events the plugin dropped because the panel was hidden or behind.
              A climbing number is the panel falling behind the console. */}
          <dt>Updates dropped</dt>
          <dd>{status.droppedEvents}</dd>
        </dl>
      </section>

      <section className="feed" aria-label="Facility bay">
        <h2 className="section-title">Facility</h2>
        <BayTable rows={bay?.flights ?? []} />
      </section>

      <section className="feed" aria-label="Last turn">
        <h2 className="section-title">Last turn</h2>
        <dl className="kv">
          <dt>Heard</dt>
          <dd>{transcript?.text || "—"}</dd>
          <dt>Replied</dt>
          <dd>{reply?.text || "—"}</dd>
          <dt>Category</dt>
          <dd>{reply?.category || "—"}</dd>
          {/* A reply that was generated but not delivered has not been heard,
              and the console does not advance past what the pilot heard. */}
          <dt>Delivered</dt>
          <dd>{reply ? (reply.sent ? "yes" : reply.error || "no") : "—"}</dd>
        </dl>
      </section>

      {/* Typing a transmission is no longer a debug affordance: it is the
          composer at the bottom of the radio on Home, where a chat's input
          belongs. Nothing here duplicates it. */}

      <section className="feed" aria-label="Debug log">
        <h2 className="section-title">Log</h2>
        <DebugTail
          records={tail}
          category={category}
          loading={loadingTail}
          onCategory={setCategory}
          onRefresh={() => loadTail(category)}
        />
      </section>

      <section className="feed" aria-label="Session state vector">
        <h2 className="section-title">Session</h2>
        {/* The 12 state-vector dimensions, raw. lifecycleOverrides being
            non-zero means the rest of this flight is running on a hand-forced
            state vector, which is worth knowing when reading anything else. */}
        <JsonBlock value={session} empty="Waiting for the console's first snapshot." />
      </section>
    </>
  );
}
