import {useEffect, useLayoutEffect, useRef, useState} from "react";

import type {CommLogEntry} from "../bridge/views";
import {formatPosition, positionTone} from "../domain/format";

type Props = {
  entries: readonly CommLogEntry[];
  // What to call this cockpit. The callsign if the console has heard one.
  ownCallsign?: string;
};

// How close to the bottom still counts as "at the bottom". A couple of lines of
// slack, so a stray wheel notch does not stop the log following the radio.
const STICK_SLACK_PX = 48;

function CommEntry({entry, ownCallsign}: {entry: CommLogEntry; ownCallsign?: string}) {
  const fromPilot = entry.kind !== "atc";
  // An overheard pilot transmission is another aircraft on the frequency, not
  // this one. Labelled as this cockpit it reads as words the pilot just said
  // themselves -- which is the one reading that makes the party line useless.
  const speaker = fromPilot
    ? entry.overheard
      ? entry.spokenBy || "Aircraft"
      : ownCallsign || "You"
    : formatPosition(entry.spokenBy);

  const className = [
    "bubble-row",
    fromPilot ? "from-pilot" : "from-atc",
    entry.overheard ? "is-overheard" : "",
  ]
    .filter(Boolean)
    .join(" ");

  return (
    <li className={className}>
      <span className={`bubble-speaker tone-${fromPilot ? "pilot" : positionTone(entry.spokenBy)}`}>
        {speaker}
        {/* An overheard copy is another aircraft's clearance. Rendered like an
            addressed one, a routing bug reads as normal operation -- a
            clearance appearing in two cockpits looks fine (phase 22 M7). */}
        {entry.overheard ? (
          <span className="bubble-tag">
            {entry.addressedTo ? `to ${entry.addressedTo}` : "overheard"}
          </span>
        ) : null}
      </span>
      <div className="bubble">{entry.text || ""}</div>
    </li>
  );
}

export function CommLog({entries, ownCallsign}: Props) {
  const listRef = useRef<HTMLUListElement | null>(null);
  // Whether the log should keep following the radio. True until the pilot
  // scrolls up to read something, and true again the moment they come back
  // down -- the behaviour every messaging app has, and the reason a long log
  // does not yank itself out from under someone reading it.
  const stickRef = useRef(true);
  const [pendingBelow, setPendingBelow] = useState(0);

  function atBottom(list: HTMLUListElement): boolean {
    return list.scrollHeight - list.scrollTop - list.clientHeight <= STICK_SLACK_PX;
  }

  // Layout effect, not effect: scrolling after paint is a visible jump.
  useLayoutEffect(() => {
    const list = listRef.current;
    if (!list) {
      return;
    }
    if (stickRef.current) {
      list.scrollTop = list.scrollHeight;
      setPendingBelow(0);
    } else {
      setPendingBelow((count) => count + 1);
    }
  }, [entries]);

  useEffect(() => {
    const list = listRef.current;
    if (!list) {
      return;
    }
    function onScroll(): void {
      if (!list) {
        return;
      }
      stickRef.current = atBottom(list);
      if (stickRef.current) {
        setPendingBelow(0);
      }
    }
    list.addEventListener("scroll", onScroll, {passive: true});
    return () => list.removeEventListener("scroll", onScroll);
  }, []);

  function jumpToLatest(): void {
    const list = listRef.current;
    if (!list) {
      return;
    }
    stickRef.current = true;
    list.scrollTop = list.scrollHeight;
    setPendingBelow(0);
  }

  return (
    <div className="comm-wrap">
      <ul className="comm-log" ref={listRef} aria-live="polite" aria-label="Radio log">
        {entries.map((entry, index) => (
          <CommEntry
            key={`${entry.at ?? ""}-${entry.generation ?? ""}-${index}`}
            entry={entry}
            ownCallsign={ownCallsign}
          />
        ))}
      </ul>

      {/* Reading back through the log must not mean missing the call that just
          came in. */}
      {pendingBelow > 0 ? (
        <button type="button" className="jump-latest" onClick={jumpToLatest}>
          {pendingBelow === 1 ? "1 new transmission" : `${pendingBelow} new transmissions`} ↓
        </button>
      ) : null}
    </div>
  );
}
